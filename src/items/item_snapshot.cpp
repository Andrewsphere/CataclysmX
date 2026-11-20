#include "items/item_snapshot.h"
#include "items/item_behavior.h"
#include "content/content.h"
#include "core/world_archive.h"
#include "crafting/crafting_state.h"
#include "core/item_state.h"
#include "objects/object_behavior.h"
#include "skills/skill_state.h"
#include "combat/combat_persistence.h"

#include <cstdio>
#include <cstdlib>

namespace cataclysmx {
namespace {
constexpr unsigned maxItemDepth = 64;

eastl::unexpected<StateError> Invalid(PersistentId id, const char* field,
    const char* message, Diagnostics* diagnostics) {
    if (diagnostics) {
        DiagnosticFrame frame;
        frame.message = message;
        DiagnosticLocation location;
        char path[128];
        std::snprintf(path, sizeof(path), "items[%llu].%s",
            static_cast<unsigned long long>(id), field);
        location.path = path;
        frame.location = eastl::move(location);
        diagnostics->Add(eastl::move(frame));
    }
    return eastl::unexpected(StateError::InvalidData);
}
}

ItemSnapshotContext::ItemSnapshotContext(const ContentCatalog& content, const ChunkArchive& chunk)
    : content_(content), chunk_(chunk) {}

const SavedItem* ItemSnapshotContext::FindItem(PersistentId item) const {
    for (const auto& candidate : chunk_.items) {
        if (candidate.id == item) { return &candidate; }
    }
    return nullptr;
}

const ItemDefinition& ItemSnapshotContext::Definition(PersistentId item) const {
    const auto* saved = FindItem(item);
    const auto* definition = saved ? content_.TryGetItem(saved->type) : nullptr;
    if (!definition) { std::abort(); }
    return *definition;
}

const SavedItem* ItemSnapshotContext::Installed(PersistentId parent, ItemCapabilityId mount) const {
    if (!mount.value) { return nullptr; }
    for (const auto& item : chunk_.items) {
        if (item.location == LocationKind::item && item.owner == parent && item.installation == mount) {
            return &item;
        }
    }
    return nullptr;
}

const DataObject* ItemSnapshotContext::State(PersistentId item, ItemCapabilityId capability) const {
    const auto* saved = FindItem(item);
    const auto* definition = saved ? content_.TryGetItem(saved->type) : nullptr;
    const auto* entry = definition ? TryGetItemCapability(*definition, capability) : nullptr;
    return entry && saved->state.Type() == DataType::Object ? saved->state.Find(entry->id) : nullptr;
}

eastl::optional<ItemEnergyView> ItemSnapshotContext::Energy(PersistentId item, ItemCapabilityId store) const {
    const auto* saved = FindItem(item);
    const auto* definition = saved ? content_.TryGetItem(saved->type) : nullptr;
    const auto* config = definition ? ItemEnergyStore(*definition, store) : nullptr;
    const auto* state = State(item, store);
    if (!config || !state || state->Type() != DataType::Object) { return eastl::nullopt; }
    const auto version = state->GetUnsigned("version");
    const auto energy = state->GetUnsigned("energy");
    if (!version || *version != 1 || !energy || *energy > config->capacityEnergy || state->Size() != 2) {
        return eastl::nullopt;
    }
    return ItemEnergyView{store, static_cast<std::uint32_t>(*energy), config->capacityEnergy};
}

StateResult<void> ValidateItemSnapshot(const ContentCatalog& content, const ChunkArchive& chunk,
    Diagnostics* diagnostics) {
    const ItemSnapshotContext context(content, chunk);
    Vector<PersistentId> workers;
    const auto claimWorker = [&workers](PersistentId worker) {
        if (!worker) { return true; }
        for (auto previous : workers) { if (previous == worker) { return false; } }
        workers.push_back(worker);
        return true;
    };
    const auto validDigitalTiming = [&chunk, &content](PersistentId worker, Tick remaining) {
        if (!worker) { return true; } // Retained paid setup can be unassigned.
        for (const auto& actor : chunk.actors) {
            if (actor.id != worker) { continue; }
            const auto combat = ReadCombatState(actor.combat, content);
            return combat && combat->life == ActorLifeState::Alive
                && combat->action.kind == ActorActionKind::None && actor.remainingTicks == remaining;
        }
        return true; // A worker in another chunk is checked by the global boundary.
    };
    for (const auto& actor : chunk.actors) {
        const auto learning = ReadLearningState(actor.learning, content, diagnostics);
        const auto combat = ReadCharacterCombat(actor.characterCombat, content, diagnostics);
        if (!learning) { return eastl::unexpected(learning.error()); }
        if (!combat) { return eastl::unexpected(combat.error()); }
        if (learning->study.active && !claimWorker(actor.id)) {
            return Invalid(actor.id, "work", "Worker has conflicting activities", diagnostics);
        }
        if (*combat) {
            for (const auto& manual : (*combat)->manuals) {
                if (manual.active && !claimWorker(actor.id)) {
                    return Invalid(actor.id, "work", "Worker has conflicting activities", diagnostics);
                }
            }
        }
    }
    for (const auto& object : chunk.objects) {
        const auto* definition = content.TryGetObject(object.type);
        if (!definition || !definition->packingItem.value) {
            if (object.equipment) { return Invalid(object.id, "equipment", "Unexpected station equipment", diagnostics); }
            continue;
        }
        const auto* equipment = context.FindItem(object.equipment);
        if (!equipment || equipment->type != definition->packingItem || equipment->location != LocationKind::object
            || equipment->owner != object.id || equipment->installation.value) {
            return Invalid(object.id, "equipment", "Missing or invalid original station equipment", diagnostics);
        }
        for (const auto& item : chunk.items) {
            if (item.location == LocationKind::item && item.owner == equipment->id) {
                return Invalid(object.id, "equipment", "Station equipment must be a leaf", diagnostics);
            }
        }
    }
    Vector<PersistentId> claimedStations;
    // Check every definition and local payload before exposing the archive to
    // behavior hooks, which may inspect other items in the same tree.
    for (std::size_t i = 0; i < chunk.items.size(); ++i) {
        const auto& item = chunk.items[i];
        const auto* definition = content.TryGetItem(item.type);
        if (!item.id || !definition) {
            return Invalid(item.id, "type", "Invalid item identity or definition", diagnostics);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (chunk.items[j].id == item.id) {
                return Invalid(item.id, "id", "Duplicate item identity", diagnostics);
            }
        }
        if (item.state.Type() != DataType::Object
            || item.state.Size() != definition->energyStores.size() + definition->behaviors.size()
                + (definition->unfinishedCraft ? 1 : 0)
                + (definition->digitalDevice != DigitalDeviceKind::None ? 1 : 0)) {
            return Invalid(item.id, "state", "Expected exactly the energy and behavior state entries", diagnostics);
        }
        if (definition->digitalDevice != DigitalDeviceKind::None) {
            const auto parsed = ReadItemState(content, *definition, item.state, diagnostics);
            if (!parsed) { return eastl::unexpected(parsed.error()); }
            const auto* transfer = item.state.Find("information_library")->Find("transfer");
            if (transfer) {
                // The catalog-aware reader validates identity shape and package.
                // The source/host can disappear while work is funded; runtime
                // rechecks access and aborts rather than resurrecting anything.
                const auto worker = *transfer->GetUnsigned("worker");
                const auto remaining = *transfer->GetUnsigned("remaining_ticks");
                if (remaining > UINT32_MAX || !claimWorker(worker)) {
                    return Invalid(item.id, "transfer", "Invalid transfer timing or conflicting worker", diagnostics);
                }
                if (!validDigitalTiming(worker, remaining)) {
                    return Invalid(item.id, "transfer", "Transfer timing or action differs from worker", diagnostics);
                }
            }
        }
        if (definition->unfinishedCraft) {
            const auto* payload = item.state.Find("crafting_job");
            if (!payload) { return Invalid(item.id, "state", "Missing crafting job", diagnostics); }
            auto state = ReadCraftingState(*payload);
            if (!state) { return eastl::unexpected(state.error()); }
            if (!ResolveCraftingState(content, *state)) { return Invalid(item.id, "state", "Invalid crafting recipe or work", diagnostics); }
            if (!claimWorker(state->worker)) {
                return Invalid(item.id, "state", "Worker has conflicting activities", diagnostics);
            }
            for (auto station : state->stations) {
                for (auto previous : claimedStations) {
                    if (station == previous) { return Invalid(item.id, "stations", "Station claimed by multiple jobs", diagnostics); }
                }
                claimedStations.push_back(station);
                for (const auto& object : chunk.objects) {
                    if (object.id != station) { continue; }
                    const auto* provider = content.TryGetObject(object.type);
                    const auto& traits = state->kind == CraftingKind::Craft
                        ? content.GetCraftingRecipe(state->craftingRecipe).toolTraits
                        : content.GetRecyclingRecipe(state->recyclingRecipe).toolTraits;
                    bool matches = false;
                    if (provider && object.enabled && !object.broken) {
                        for (const auto& need : traits) {
                            for (const auto& supplied : provider->toolTraits) {
                                matches |= supplied.trait == need.trait && supplied.tier >= need.tier;
                            }
                        }
                    }
                    if (!matches) { return Invalid(item.id, "stations", "Claimed station cannot provide a required trait", diagnostics); }
                }
            }
            const auto& inputs = state->kind == CraftingKind::Craft
                ? content.GetCraftingRecipe(state->craftingRecipe).inputs : content.GetRecyclingRecipe(state->recyclingRecipe).inputs;
            StaticList<ItemOutput, 9> expected;
            for (const auto& input : inputs) {
                if (!input.Retained()) { expected.PushBack({input.Item(), input.Count()}); }
            }
            if (state->kind == CraftingKind::Recycle) {
                expected.PushBack({content.GetRecyclingRecipe(state->recyclingRecipe).target, 1});
            }
            unsigned targets = 0;
            for (const auto& child : chunk.items) {
                if (child.location != LocationKind::item || child.owner != item.id) { continue; }
                if (child.installation.value) { return Invalid(item.id, "contents", "Reserved children cannot be installed", diagnostics); }
                if (state->kind == CraftingKind::Recycle && child.id == state->recyclingTarget) {
                    if (child.type != content.GetRecyclingRecipe(state->recyclingRecipe).target) {
                        return Invalid(item.id, "state.target", "Recycling target type does not match recipe", diagnostics);
                    }
                    ++targets;
                }
                bool matched = false;
                for (auto& entry : expected) {
                    if (entry.item == child.type && entry.count) { --entry.count; matched = true; break; }
                }
                if (!matched) { return Invalid(item.id, "contents", "Unexpected reserved crafting input", diagnostics); }
                for (const auto& grandchild : chunk.items) {
                    if (grandchild.location == LocationKind::item && grandchild.owner == child.id) {
                        return Invalid(item.id, "contents", "Reserved crafting inputs must be leaves", diagnostics);
                    }
                }
            }
            if (state->kind == CraftingKind::Recycle && targets != 1) {
                return Invalid(item.id, "state.target", "Recycling target must identify exactly one reserved child", diagnostics);
            }
            for (const auto& entry : expected) {
                if (entry.count) { return Invalid(item.id, "contents", "Missing reserved crafting input", diagnostics); }
            }
            if (state->worker) {
                for (std::size_t j = 0; j < i; ++j) {
                    const auto* other = chunk.items[j].state.Find("crafting_job");
                    if (other && other->GetUnsigned("worker") && *other->GetUnsigned("worker") == state->worker) {
                        return Invalid(item.id, "state", "Worker assigned to multiple jobs", diagnostics);
                    }
                }
            }
        }
        for (const auto& store : definition->energyStores) {
            const auto* state = context.State(item.id, store.capability);
            if (!state || state->Type() != DataType::Object) {
                return Invalid(item.id, "state", "Missing energy state object", diagnostics);
            }
            const auto version = state->GetUnsigned("version");
            if (!version) { return Invalid(item.id, "state", "Missing or invalid energy state version", diagnostics); }
            if (*version != 1) {
                Invalid(item.id, "state", "Unsupported energy state version", diagnostics);
                return eastl::unexpected(StateError::UnsupportedVersion);
            }
            if (!context.Energy(item.id, store.capability)) {
                return Invalid(item.id, "state", "Invalid energy state or capacity exceeded", diagnostics);
            }
        }
        for (const auto& behavior : definition->behaviors) {
            const auto* state = context.State(item.id, behavior.capability);
            if (!state || !behavior.definition) {
                return Invalid(item.id, "state", "Missing behavior state or definition", diagnostics);
            }
            const auto parsed = behavior.definition->Create(behavior.capability, state, diagnostics);
            if (!parsed) {
                Invalid(item.id, "state", "Reading behavior state", diagnostics);
                return eastl::unexpected(parsed.error());
            }
        }
    }

    Vector<std::uint64_t> actorMass(chunk.actors.size(), 0);
    for (std::size_t i = 0; i < chunk.items.size(); ++i) {
        const auto& item = chunk.items[i];
        if (item.location != LocationKind::item && item.installation.value) {
            return Invalid(item.id, "installation", "Only item children may be installed", diagnostics);
        }
        const SavedItem* root = &item;
        unsigned depth = 1;
        while (root->location == LocationKind::item) {
            const auto* parent = context.FindItem(root->owner);
            if (!parent || ++depth > maxItemDepth) {
                return Invalid(item.id, "owner", "Missing parent, ownership cycle or excessive item depth", diagnostics);
            }
            root = parent;
        }
        switch (root->location) {
        case LocationKind::ground:
            if (root->owner || !ValidPosition(root->position) || ChunkOf(root->position) != chunk.position) {
                return Invalid(item.id, "position", "Invalid ground item location", diagnostics);
            }
            break;
        case LocationKind::actor: {
            bool found = false;
            for (std::size_t j = 0; j < chunk.actors.size(); ++j) {
                if (chunk.actors[j].id != root->owner) { continue; }
                found = true;
                const auto mass = context.Definition(item.id).massGrams;
                if (mass > UINT64_MAX - actorMass[j]) {
                    return Invalid(item.id, "contents", "Actor carrying mass overflow", diagnostics);
                }
                actorMass[j] += mass;
                if (actorMass[j] > chunk.actors[j].carryCapacityGrams) {
                    return Invalid(item.id, "contents", "Actor carrying mass capacity exceeded", diagnostics);
                }
            }
            if (!found) { return Invalid(item.id, "owner", "Missing actor owner", diagnostics); }
            break;
        }
        case LocationKind::object: {
            bool found = false;
            for (const auto& object : chunk.objects) { found = found || object.id == root->owner; }
            if (!found) { return Invalid(item.id, "owner", "Missing object owner", diagnostics); }
            break;
        }
        default: return Invalid(item.id, "owner", "Invalid item location", diagnostics);
        }
        if (item.location == LocationKind::item) {
            // Archives are canonical parent-first values. Reject unordered input
            // explicitly rather than letting the disk encoder dereference null.
            bool parentFirst = false;
            for (std::size_t j = 0; j < i; ++j) { parentFirst = parentFirst || chunk.items[j].id == item.owner; }
            if (!parentFirst) { return Invalid(item.id, "owner", "Item parent must precede child", diagnostics); }
            const auto& parent = context.Definition(item.owner);
            if (item.installation.value) {
                const auto* behavior = ItemBehaviorConfig(parent, item.installation);
                if (!behavior || !behavior->AcceptsInstallation(context.Definition(item.id))) {
                    return Invalid(item.id, "installation", "Incompatible installation capability", diagnostics);
                }
                for (std::size_t j = 0; j < i; ++j) {
                    const auto& sibling = chunk.items[j];
                    if (sibling.location == LocationKind::item && sibling.owner == item.owner
                        && sibling.installation == item.installation) {
                        return Invalid(item.id, "installation", "Installation capability already occupied", diagnostics);
                    }
                }
            } else if (!ItemContainer(parent)) {
                return Invalid(item.id, "contents", "Ordinary contents require a container", diagnostics);
            }
        }
        std::uint64_t volume = 0;
        for (const auto& child : chunk.items) {
            if (child.location != LocationKind::item || child.owner != item.id || child.installation.value) { continue; }
            std::uint64_t amount = context.Definition(child.id).volumeMl;
            if (context.Definition(child.id).unfinishedCraft) {
                amount = 0;
                for (const auto& reserved : chunk.items) {
                    if (reserved.location == LocationKind::item && reserved.owner == child.id) {
                        amount += context.Definition(reserved.id).volumeMl;
                    }
                }
            }
            if (amount > UINT64_MAX - volume) { return Invalid(item.id, "contents", "Container volume overflow", diagnostics); }
            volume += amount;
        }
        const auto* container = ItemContainer(context.Definition(item.id));
        if (volume && (!container || (item.type != content.CorpseItem() && volume > container->capacityVolumeMl))) {
            return Invalid(item.id, "contents", "Container volume capacity exceeded", diagnostics);
        }
    }
    Vector<std::uint64_t> externalVolumes(chunk.items.size(), 0);
    for (std::size_t i = chunk.items.size(); i > 0;) {
        --i;
        const auto& item = chunk.items[i];
        if (!context.Definition(item.id).unfinishedCraft) {
            externalVolumes[i] = context.Definition(item.id).volumeMl;
            continue;
        }
        for (std::size_t j = i + 1; j < chunk.items.size(); ++j) {
            if (chunk.items[j].location != LocationKind::item || chunk.items[j].owner != item.id) { continue; }
            if (externalVolumes[j] > UINT64_MAX - externalVolumes[i]) {
                return Invalid(item.id, "contents", "External volume overflow", diagnostics);
            }
            externalVolumes[i] += externalVolumes[j];
        }
    }
    for (const auto& object : chunk.objects) {
        const auto* definition = content.TryGetObject(object.type);
        if (!definition || definition->behavior != ObjectBehaviorKind::SelectableMachine) { continue; }
        const auto valid = ValidateObjectBehaviorState(*definition, object.state, diagnostics);
        if (!valid) { return eastl::unexpected(valid.error()); }
        const auto* payload = object.state.Find("job");
        if (!payload) { continue; }
        const auto job = ReadMachineJobState(*payload, diagnostics);
        if (!job || !ValidateCapturedMachineJob(content, *job)) {
            return Invalid(object.id, "job", "Invalid captured machine job", diagnostics);
        }
        const auto recipe = content.FindMachineRecipe(job->recipeName);
        bool supported = false;
        for (auto candidate : definition->selectableRecipes) { supported |= candidate == recipe; }
        if (!supported || !claimWorker(job->worker)) {
            return Invalid(object.id, "job", "Unsupported recipe or conflicting worker", diagnostics);
        }
        if (!validDigitalTiming(job->worker, job->remainingSetupTicks)) {
            return Invalid(object.id, "job", "Setup timing or action differs from worker", diagnostics);
        }
        StaticList<ItemOutput, maxRecipeInputs> counts;
        for (const auto& input : job->inputs) {
            if (!counts.PushBack({content.FindItem(input.itemName), input.count})) { std::abort(); }
        }
        std::uint64_t inputVolume = 0;
        for (auto id : job->reservedInputs) {
            const auto* item = context.FindItem(id);
            if (!item || id == object.equipment || item->location != LocationKind::object
                || item->owner != object.id || item->installation.value
                || context.Definition(id).unfinishedCraft) {
                return Invalid(object.id, "job", "Reserved original is not an owned leaf input", diagnostics);
            }
            for (const auto& child : chunk.items) {
                if (child.location == LocationKind::item && child.owner == id) {
                    return Invalid(object.id, "job", "Reserved original has children", diagnostics);
                }
            }
            bool matched = false;
            for (auto& count : counts) {
                if (count.item == item->type && count.count) { --count.count; matched = true; break; }
            }
            const auto volume = context.Definition(id).volumeMl;
            if (!matched || volume > UINT64_MAX - inputVolume) {
                return Invalid(object.id, "job", "Reserved original types or volume differ from capture", diagnostics);
            }
            inputVolume += volume;
        }
        for (const auto& count : counts) {
            if (count.count) { return Invalid(object.id, "job", "Missing captured inputs", diagnostics); }
        }
        const auto outputVolume = std::uint64_t{content.GetItem(content.FindItem(job->outputName)).volumeMl}
            * job->outputCount;
        if (inputVolume > job->reservedVolumeMl || outputVolume > job->reservedVolumeMl
            || !definition->capacityVolumeMl || job->reservedVolumeMl > *definition->capacityVolumeMl) {
            return Invalid(object.id, "job", "Captured reservation cannot contain inputs or outputs", diagnostics);
        }
        auto occupied = job->reservedVolumeMl;
        for (std::size_t i = 0; i < chunk.items.size(); ++i) {
            const auto& item = chunk.items[i];
            if (item.location != LocationKind::object || item.owner != object.id || item.id == object.equipment) { continue; }
            bool reserved = false;
            for (auto id : job->reservedInputs) { reserved |= id == item.id; }
            if (reserved) { continue; }
            const auto volume = externalVolumes[i];
            if (volume > *definition->capacityVolumeMl - occupied) {
                return Invalid(object.id, "job", "Public contents exceed captured reservation allowance", diagnostics);
            }
            occupied += volume;
        }
    }
    // Only fully validated trees reach relational behavior validation.
    for (const auto& item : chunk.items) {
        for (const auto& behavior : context.Definition(item.id).behaviors) {
            const auto valid = behavior.definition->ValidateSnapshot(context, item.id, behavior.capability, diagnostics);
            if (!valid) { return eastl::unexpected(valid.error()); }
        }
    }
    return {};
}
}
