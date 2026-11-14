#include "crafting/crafting_system.h"
#include "core/world.h"
#include "core/ratio.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
void Require(bool value) { if (!value) { std::abort(); } }
void SetWorkerUnavailable(CraftingJobState& state) {
    state.worker = 0;
    state.status = CraftingStatus::WorkerUnavailable;
    state.pauseRequested = false;
}
constexpr std::uint64_t workPerTickNumerator = std::uint64_t{ticks_per_turn} * ratioScale;
bool Has(const Vector<ItemHandle>& items, ItemHandle item) {
    return eastl::find(items.begin(), items.end(), item) != items.end();
}
// Crafting converts components, rather than installing a battery. Unclassified
// internal stores are class-neutral; two explicitly different battery classes
// cannot transfer directly. Charger/installation compatibility is unaffected.
bool CompatibleCraftingStores(const EnergyStoreDefinition& source, const EnergyStoreDefinition& target) {
    return source.batteryClass.empty() || target.batteryClass.empty() || source.batteryClass == target.batteryClass;
}
// Bounded same-layer flood fill. Diagonal corners cannot cut through walls.
bool Nearby(const World& world, TileCoord from, TileCoord to, unsigned radius) {
    if (from.y != to.y || std::abs(from.x - to.x) > static_cast<int>(radius)
        || std::abs(from.z - to.z) > static_cast<int>(radius)) { return false; }
    eastl::array<TileCoord, 289> queue{};
    eastl::array<bool, 289> seen{};
    const int r = static_cast<int>(radius), width = r * 2 + 1;
    const auto passable = [&world](TileCoord p) {
        const auto* tile = world.GetTile(p);
        return tile && world.IsActive(p) && Passable(world.GetContent(), tile->terrain);
    };
    std::size_t count = 1;
    queue[0] = from;
    seen[r * width + r] = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto current = queue[i];
        if (current == to) { return true; }
        for (int dz = -1; dz <= 1; ++dz) { for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) { continue; }
            const TileCoord next{current.x + dx, current.z + dz, current.y};
            const int x = next.x - from.x + r, z = next.z - from.z + r;
            if (x < 0 || z < 0 || x >= width || z >= width || seen[z * width + x]) { continue; }
            if (!passable(next) || (dx && dz && (!passable({current.x + dx, current.z, current.y})
                || !passable({current.x, current.z + dz, current.y})))) { continue; }
            seen[z * width + x] = true;
            Require(count < queue.size()); queue[count++] = next;
        } }
    }
    return false;
}
}

CraftingSystem::CraftingSystem(World& world) : world_(world) {}
CraftingSettings CraftingSystem::Settings() const { return settings_; }
WorldResult<void> CraftingSystem::SetSettings(CraftingSettings settings) {
    if (!ValidCraftingSettings(settings)) { return eastl::unexpected(WorldError::invalid_request); }
    settings_ = settings; return {};
}
CraftingJobState* CraftingSystem::State(ItemHandle handle) {
    auto* item = world_.MutableItem(handle);
    return item ? item->runtime_.crafting_.get() : nullptr;
}
const CraftingJobState* CraftingSystem::State(ItemHandle handle) const {
    const auto* item = world_.GetItem(handle);
    return item ? item->runtime_.crafting_.get() : nullptr;
}
bool CraftingSystem::Reachable(ActorHandle actorHandle, ItemHandle handle, unsigned radius) const {
    const auto* actor = world_.GetActor(actorHandle);
    const auto* item = world_.GetItem(handle);
    if (!actor || actor->parked || !world_.IsActive(actor->position) || !item || world_.IsReservedItem(handle)) { return false; }
    for (unsigned depth = 0; item->location.kind == LocationKind::item; ++depth) {
        if (depth >= 64 || item->location.installation.value) { return false; }
        item = world_.GetItem(item->location.item);
        if (!item) { return false; }
    }
    return (item->location.kind == LocationKind::actor && item->location.actor == actorHandle)
        || (item->location.kind == LocationKind::ground && Nearby(world_, actor->position, item->location.position, radius));
}
void CraftingSystem::Gather(ActorHandle handle, unsigned radius) {
    candidates_.clear(); traversal_.clear();
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return; }
    traversal_.insert(traversal_.end(), actor->inventory.begin(), actor->inventory.end());
    const auto position = actor->position;
    const int r = static_cast<int>(radius);
    for (int z = -r; z <= r; ++z) { for (int x = -r; x <= r; ++x) {
        const TileCoord tile{position.x + x, position.z + z, position.y};
        if (!Nearby(world_, position, tile, radius)) { continue; }
        for (auto item : world_.ItemsAt(tile)) { traversal_.push_back(item); }
    } }
    for (std::size_t i = 0; i < traversal_.size(); ++i) {
        const auto candidate = traversal_[i];
        const auto* item = world_.GetItem(candidate);
        if (!item || item->location.installation.value || world_.IsReservedItem(candidate)) { continue; }
        candidates_.push_back(candidate);
        if (world_.GetContent().GetItem(item->type).unfinishedCraft) { continue; }
        traversal_.insert(traversal_.end(), item->contents.begin(), item->contents.end());
    }
    eastl::sort(candidates_.begin(), candidates_.end(), [this](auto a, auto b) { return world_.GetItem(a)->id < world_.GetItem(b)->id; });
}
CraftingOutcome CraftingSystem::ActorAvailability(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    if (!actor || actor->parked || !world_.IsActive(actor->position)) { return CraftingOutcome::ActorUnavailable; }
    if (actor->actionDeadline.Remaining(world_.GetTick()) || actor->combat.action.kind != ActorActionKind::None || Busy(handle)) {
        return CraftingOutcome::ActorBusy;
    }
    return CraftingOutcome::Started;
}
bool CraftingSystem::Busy(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return false; }
    for (auto item : assigned_) { const auto* state = State(item); if (state && state->worker == actor->id) { return true; } }
    return false;
}
eastl::optional<Tick> CraftingSystem::ActorEventIn(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    if (!actor || actor->parked || !world_.IsActive(actor->position)) { return eastl::nullopt; }
    for (auto item : assigned_) {
        const auto* state = State(item);
        if (state && state->worker == actor->id && world_.ItemEligible(item)) { return state->remaining; }
    }
    return eastl::nullopt;
}
CraftingOutcome CraftingSystem::Select(const CraftingJobState& state, bool inputs, ItemHandle target,
    eastl::array<ItemHandle, 4>& tools) {
    consumed_.clear(); retained_.clear(); tools.fill({});
    const auto& content = world_.GetContent();
    const auto& entries = state.kind == CraftingKind::Craft ? content.GetCraftingRecipe(state.craftingRecipe).inputs
        : content.GetRecyclingRecipe(state.recyclingRecipe).inputs;
    const auto& traits = state.kind == CraftingKind::Craft ? content.GetCraftingRecipe(state.craftingRecipe).toolTraits
        : content.GetRecyclingRecipe(state.recyclingRecipe).toolTraits;
    if (target.value) { consumed_.push_back(target); }
    // Reserve exact retained tools before selecting consumables; selections are disjoint.
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            for (std::size_t i = 0; i < traits.Size(); ++i) {
                for (auto handle : candidates_) {
                    if (Has(consumed_, handle)) { continue; }
                    const auto* item = world_.GetItem(handle);
                    const auto& definition = content.GetItem(item->type);
                    if (definition.unfinishedCraft) { continue; }
                    bool matches = false;
                    for (const auto& trait : definition.toolTraits) {
                        matches |= trait.trait == traits[i].trait && trait.tier >= traits[i].tier;
                    }
                    if (!matches) { continue; }
                    // Do not use the last required consumable as a tool. A retained
                    // multitool is allowed to satisfy any number of trait slots.
                    unsigned needed = 0, leaves = 0;
                    if (inputs) {
                        for (const auto& entry : entries) {
                            if (!entry.Retained() && entry.Item() == item->type) { needed += entry.Count(); }
                        }
                    }
                    for (auto other : candidates_) {
                        const auto* candidate = world_.GetItem(other);
                        if (other == handle || candidate->type != item->type || !candidate->contents.empty()
                            || Has(consumed_, other) || Has(retained_, other)) { continue; }
                        bool selected = false;
                        for (std::size_t j = 0; j < i; ++j) { selected |= tools[j] == other; }
                        if (!selected) { ++leaves; }
                    }
                    if (leaves < needed) { continue; }
                    tools[i] = handle; break;
                }
                if (!tools[i].value) { return CraftingOutcome::MissingTools; }
            }
        }
        for (const auto& entry : entries) {
            if (entry.Retained() != (pass == 0) || (!entry.Retained() && !inputs)) { continue; }
            unsigned count = 0;
            for (auto handle : candidates_) {
                const auto* item = world_.GetItem(handle);
                if (item->type != entry.Item() || Has(consumed_, handle) || Has(retained_, handle)
                    || content.GetItem(item->type).unfinishedCraft || (!entry.Retained() && !item->contents.empty())) { continue; }
                bool traitTool = false;
                for (auto tool : tools) { traitTool |= tool == handle; }
                if (!entry.Retained() && traitTool) { continue; }
                (entry.Retained() ? retained_ : consumed_).push_back(handle);
                if (++count == entry.Count()) { break; }
            }
            if (count != entry.Count()) { return entry.Retained() ? CraftingOutcome::MissingTools : CraftingOutcome::MissingInputs; }
        }
    }
    return CraftingOutcome::Started;
}
CraftingOutcome CraftingSystem::Fund(ActorHandle handle, CraftingJobState& state,
    const eastl::array<ItemHandle, 4>& tools, bool commit) {
    const auto* actor = world_.GetActor(handle);
    const auto speed = actor->speed;
    // Convert the sub-fixed-point timing residue when speed changes. The durable
    // fixed-point work itself is never rounded or discarded at a slice boundary.
    const auto residue = std::uint64_t{state.timingRemainder} * speed / state.timingSpeed;
    Tick duration = state.settings.maxSliceTicks;
    const auto left = std::uint64_t{state.totalWork} * ratioScale - state.work;
    auto work = (duration * workPerTickNumerator + residue) / speed;
    if (work >= left) {
        duration = (left * speed - residue + workPerTickNumerator - 1) / workPerTickNumerator;
        duration = eastl::max<Tick>(1, duration); work = left;
    }
    charges_.clear();
    for (auto item : retained_) { charges_.push_back({item}); }
    for (auto item : tools) { if (item.value) { charges_.push_back({item}); } }
    eastl::sort(charges_.begin(), charges_.end(), [this](const auto& a, const auto& b) {
        return world_.GetItem(a.item)->id < world_.GetItem(b.item)->id;
    });
    const auto uniqueEnd = eastl::unique(charges_.begin(), charges_.end(), [](const auto& a, const auto& b) {
        return a.item == b.item;
    });
    charges_.erase(uniqueEnd, charges_.end());
    auto remainder = state.energyRemainder;
    for (auto& payment : charges_) {
        const auto& definition = world_.GetContent().GetItem(world_.GetItem(payment.item)->type);
        if (!definition.craftingEnergyPerWork) { continue; }
        const auto energy = world_.GetItemEnergy(payment.item, definition.craftingPower);
        const auto numerator = work * definition.craftingEnergyPerWork + remainder;
        const auto charge = numerator / craftingEnergyDenominator;
        if (!energy || charge > energy->energy) { return CraftingOutcome::InsufficientEnergy; }
        payment.power = definition.craftingPower;
        payment.energyAfter = energy->energy - static_cast<std::uint32_t>(charge);
        remainder = numerator % craftingEnergyDenominator;
    }
    if (!commit) { return CraftingOutcome::Started; }
    for (const auto& payment : charges_) {
        if (payment.power.value) { Require(world_.SetItemEnergy(payment.item, payment.power, payment.energyAfter).has_value()); }
    }
    state.energyRemainder = remainder;
    state.pendingWork = work; state.remaining = duration;
    state.timingRemainder = static_cast<std::uint32_t>((duration * workPerTickNumerator + residue) % speed);
    state.timingSpeed = speed; state.worker = actor->id; state.status = CraftingStatus::Working;
    auto* mutableActor = world_.MutableActor(handle);
    mutableActor->actionDeadline.Set(world_.GetTick(), duration);
    Require(mutableActor->turns != UINT64_MAX); ++mutableActor->turns;
    world_.ActorChanged(handle);
    return CraftingOutcome::Started;
}
WorldResult<CraftingResult> CraftingSystem::Start(ActorHandle actor, CraftingRecipeId id) {
    const auto* recipe = world_.GetContent().TryGetCraftingRecipe(id);
    if (!recipe) { return eastl::unexpected(WorldError::invalid_request); }
    if (!recipe->enabled) { return CraftingResult{CraftingOutcome::RecipeDisabled}; }
    CraftingJobState state;
    state.recipeName = recipe->id; state.craftingRecipe = id; state.totalWork = recipe->work;
    return Begin(actor, eastl::move(state), {});
}
WorldResult<CraftingResult> CraftingSystem::Recycle(ActorHandle actor, ItemHandle target, RecyclingRecipeId id) {
    const auto* recipe = world_.GetContent().TryGetRecyclingRecipe(id);
    const auto* item = world_.GetItem(target);
    if (!recipe) { return eastl::unexpected(WorldError::invalid_request); }
    if (!item) { return eastl::unexpected(WorldError::invalid_handle); }
    if (item->type != recipe->target || !item->contents.empty()) { return CraftingResult{CraftingOutcome::InvalidTarget}; }
    CraftingJobState state;
    state.kind = CraftingKind::Recycle; state.recipeName = recipe->id; state.recyclingRecipe = id; state.totalWork = recipe->work;
    state.recyclingTarget = item->id;
    return Begin(actor, eastl::move(state), target);
}
WorldResult<CraftingResult> CraftingSystem::Begin(ActorHandle actor, CraftingJobState state, ItemHandle target) {
    if (!world_.GetActor(actor)) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto available = ActorAvailability(actor);
    if (available != CraftingOutcome::Started) { return CraftingResult{available}; }
    state.settings = settings_;
    if (target.value && !Reachable(actor, target, state.settings.nearbyRadius)) { return CraftingResult{CraftingOutcome::OutOfReach}; }
    Gather(actor, state.settings.nearbyRadius);
    eastl::array<ItemHandle, 4> tools{};
    auto outcome = Select(state, true, target, tools);
    if (outcome != CraftingOutcome::Started) { return CraftingResult{outcome}; }
    outcome = Fund(actor, state, tools, false);
    if (outcome != CraftingOutcome::Started) { return CraftingResult{outcome}; }
    const auto wrapperType = world_.GetContent().UnfinishedCraftItem();
    const auto* container = ItemContainer(world_.GetContent().GetItem(wrapperType));
    std::uint64_t volume = 0;
    for (auto item : consumed_) { volume += world_.GetItemVolume(item); }
    if (!container || volume > container->capacityVolumeMl) { return CraftingResult{CraftingOutcome::CapacityExceeded}; }
    // No expected failure remains. World batches gravity and all behavior callbacks
    // around this transaction, so callbacks cannot invalidate selections midway.
    const auto position = world_.GetActor(actor)->position;
    const auto created = world_.CreateItem(wrapperType, ItemLocation::Ground(position));
    Require(created.has_value());
    const auto wrapper = *created;
    *State(wrapper) = eastl::move(state);
    for (auto item : consumed_) { Require(world_.TransferItem(item, ItemLocation::Inside(wrapper)).has_value()); }
    const auto carried = world_.TransferItem(wrapper, ItemLocation::Carried(actor));
    Require(carried || carried.error() == WorldError::capacity_exceeded);
    Require(Fund(actor, *State(wrapper), tools, true) == CraftingOutcome::Started);
    Attach(wrapper); world_.MarkItemDirty(wrapper);
    return CraftingResult{CraftingOutcome::Started, wrapper};
}
void CraftingSystem::Attach(ItemHandle item) {
    auto* state = State(item);
    if (!state || !state->worker || Has(assigned_, item)) { return; }
    const auto worker = world_.FindActor(state->worker);
    const auto* actor = world_.GetActor(worker);
    if (Busy(worker) || (actor && (actor->combat.action.kind != ActorActionKind::None
        || (actor->actionDeadline.Remaining(world_.GetTick()) != 0
            && actor->actionDeadline.Remaining(world_.GetTick()) != state->remaining)))) {
        SetWorkerUnavailable(*state);
        world_.MarkItemDirty(item); return;
    }
    assigned_.push_back(item);
    eastl::sort(assigned_.begin(), assigned_.end(), [this](auto a, auto b) { return world_.GetItem(a)->id < world_.GetItem(b)->id; });
}
void CraftingSystem::Detach(ItemHandle item) {
    if (auto* state = State(item); state && state->worker) {
        const auto actor = world_.FindActor(state->worker);
        if (auto* value = world_.MutableActor(actor)) { value->actionDeadline.Set(world_.GetTick(), 0); world_.ActorChanged(actor); }
    }
    eastl::erase(assigned_, item);
}
void CraftingSystem::ReleaseWorker(CraftingJobState& state) {
    const auto actor = world_.FindActor(state.worker);
    if (auto* value = world_.MutableActor(actor)) { value->actionDeadline.Set(world_.GetTick(), 0); world_.ActorChanged(actor); }
    state.worker = 0;
    state.pauseRequested = false;
}
WorldResult<CraftingResult> CraftingSystem::Resume(ActorHandle actor, ItemHandle item) {
    auto* state = State(item);
    if (!world_.GetActor(actor) || !world_.GetItem(item)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!state) { return eastl::unexpected(WorldError::invalid_request); }
    if (state->worker) { return CraftingResult{CraftingOutcome::JobBusy, item}; }
    const auto available = ActorAvailability(actor);
    if (available != CraftingOutcome::Started) { return CraftingResult{available, item}; }
    if (!Reachable(actor, item, settings_.nearbyRadius)) { return CraftingResult{CraftingOutcome::OutOfReach, item}; }
    if (state->kind == CraftingKind::Craft && !world_.GetContent().GetCraftingRecipe(state->craftingRecipe).enabled) {
        return CraftingResult{CraftingOutcome::RecipeDisabled, item};
    }
    state->settings = settings_; state->pauseRequested = false;
    if (state->remaining || state->pendingWork) {
        state->worker = world_.GetActor(actor)->id; state->status = CraftingStatus::Working;
        world_.MutableActor(actor)->actionDeadline.Set(world_.GetTick(), state->remaining);
        world_.ActorChanged(actor); Attach(item); world_.MarkItemDirty(item);
        return CraftingResult{CraftingOutcome::Resumed, item};
    }
    Gather(actor, state->settings.nearbyRadius);
    eastl::array<ItemHandle, 4> tools{};
    auto outcome = Select(*state, false, {}, tools);
    if (outcome == CraftingOutcome::Started) { outcome = Fund(actor, *state, tools, true); }
    if (outcome == CraftingOutcome::Started) { Attach(item); outcome = CraftingOutcome::Resumed; }
    else { state->status = outcome == CraftingOutcome::MissingTools ? CraftingStatus::MissingTools : CraftingStatus::InsufficientEnergy; }
    world_.MarkItemDirty(item);
    return CraftingResult{outcome, item};
}
WorldResult<CraftingResult> CraftingSystem::Pause(ActorHandle actor, ItemHandle item) {
    auto* state = State(item);
    const auto* worker = world_.GetActor(actor);
    if (!worker || !world_.GetItem(item)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!state) { return eastl::unexpected(WorldError::invalid_request); }
    if (state->worker && state->worker != worker->id) { return CraftingResult{CraftingOutcome::JobBusy, item}; }
    if (!state->worker && !Reachable(actor, item, settings_.nearbyRadius)) { return CraftingResult{CraftingOutcome::OutOfReach, item}; }
    if (!state->worker) { return CraftingResult{CraftingOutcome::Paused, item}; }
    state->pauseRequested = true; state->status = CraftingStatus::PauseRequested;
    world_.MarkItemDirty(item);
    return CraftingResult{CraftingOutcome::PauseRequested, item};
}
WorldResult<CraftingResult> CraftingSystem::Cancel(ActorHandle actor, ItemHandle item) {
    auto* state = State(item);
    const auto* worker = world_.GetActor(actor);
    if (!worker || !world_.GetItem(item)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!state) { return eastl::unexpected(WorldError::invalid_request); }
    if (state->worker && state->worker != worker->id) { return CraftingResult{CraftingOutcome::JobBusy, item}; }
    if (!Reachable(actor, item, state->settings.nearbyRadius)) { return CraftingResult{CraftingOutcome::OutOfReach, item}; }
    const auto position = worker->position;
    ReleaseWorker(*state); eastl::erase(assigned_, item);
    while (!world_.GetItem(item)->contents.empty()) {
        const auto child = world_.GetItem(item)->contents.back();
        const auto carried = world_.TransferItem(child, ItemLocation::Carried(actor));
        if (!carried) { Require(carried.error() == WorldError::capacity_exceeded); Require(world_.TransferItem(child, ItemLocation::Ground(position)).has_value()); }
    }
    Require(world_.DestroyItem(item).has_value());
    return CraftingResult{CraftingOutcome::Cancelled};
}
eastl::optional<CraftingJobView> CraftingSystem::Inspect(ItemHandle item) const {
    const auto* state = State(item);
    if (!state || !state->totalWork) { return eastl::nullopt; }
    return CraftingJobView{world_.GetItem(item)->id, item, state->kind, state->craftingRecipe, state->recyclingRecipe,
        state->status, state->worker, state->totalWork, static_cast<std::uint32_t>(state->work / ratioScale), state->remaining, state->pauseRequested};
}
void CraftingSystem::Collect(ActorHandle actor, Vector<CraftingJobView>& output) const {
    output.clear();
    // Presentation traversal is explicit caller-owned storage, not an inventory cache.
    const auto* worker = world_.GetActor(actor);
    if (!worker || worker->parked || !world_.IsActive(worker->position)) { return; }
    const auto visit = [&](auto&& self, ItemHandle item) -> void {
        if (!Reachable(actor, item, settings_.nearbyRadius)) { return; }
        if (auto view = Inspect(item)) { output.push_back(*view); return; }
        for (auto child : world_.GetItem(item)->contents) { self(self, child); }
    };
    for (auto item : worker->inventory) { visit(visit, item); }
    const int r = settings_.nearbyRadius;
    for (int z = -r; z <= r; ++z) { for (int x = -r; x <= r; ++x) {
        for (auto item : world_.ItemsAt({worker->position.x + x, worker->position.z + z, worker->position.y})) { visit(visit, item); }
    } }
    eastl::sort(output.begin(), output.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
}
void CraftingSystem::Elapse(Tick elapsed) {
    for (auto item : assigned_) {
        auto* state = State(item);
        if (!state) { continue; }
        const auto handle = world_.FindActor(state->worker);
        auto* actor = world_.MutableActor(handle);
        if (!actor || actor->parked || !world_.IsActive(actor->position)) { continue; }
        if (world_.ItemEligible(item)) { state->remaining -= eastl::min(elapsed, state->remaining); world_.MarkItemDirty(item); }
        // The job owns this actor timer until ProcessEvents. An ineligible
        // wrapper preserves the paid duration; the next boundary releases an
        // otherwise active worker rather than leaving that actor trapped busy.
        actor->actionDeadline.Set(world_.GetTick(), CheckedTickAdd(elapsed, state->remaining));
        world_.ActorChanged(handle);
    }
}
bool CraftingSystem::ProcessEvents() {
    bool changed = false;
    due_ = assigned_;
    for (auto item : due_) {
        auto* state = State(item);
        if (!state || !state->worker) { continue; }
        const auto handle = world_.FindActor(state->worker);
        const auto* actor = world_.GetActor(handle);
        if (!actor) {
            SetWorkerUnavailable(*state);
            eastl::erase(assigned_, item); world_.MarkItemDirty(item); changed = true; continue;
        }
        if (actor->parked || !world_.IsActive(actor->position)) { continue; }
        if (!world_.ItemEligible(item)) {
            ReleaseWorker(*state);
            SetWorkerUnavailable(*state);
            eastl::erase(assigned_, item); world_.MarkItemDirty(item); changed = true; continue;
        }
        if (state->remaining) {
            world_.MutableActor(handle)->actionDeadline.Set(world_.GetTick(), state->remaining);
            continue;
        }
        state->work += state->pendingWork; state->pendingWork = 0; changed = true;
        world_.MarkItemDirty(item);
        if (state->work == std::uint64_t{state->totalWork} * ratioScale) { Finish(item); continue; }
        if (state->pauseRequested || !Reachable(handle, item, state->settings.nearbyRadius)) {
            state->status = CraftingStatus::Paused; state->pauseRequested = false;
            ReleaseWorker(*state); eastl::erase(assigned_, item); continue;
        }
        Gather(handle, state->settings.nearbyRadius);
        eastl::array<ItemHandle, 4> tools{};
        auto outcome = Select(*state, false, {}, tools);
        if (outcome == CraftingOutcome::Started) { outcome = Fund(handle, *state, tools, true); }
        if (outcome != CraftingOutcome::Started) {
            state->status = outcome == CraftingOutcome::MissingTools ? CraftingStatus::MissingTools : CraftingStatus::InsufficientEnergy;
            ReleaseWorker(*state); eastl::erase(assigned_, item);
        }
    }
    return changed;
}
void CraftingSystem::Finish(ItemHandle wrapper) {
    const auto state = *State(wrapper);
    const auto actor = world_.FindActor(state.worker);
    const auto position = world_.ItemRootPosition(wrapper);
    // Funding survives loss of access, but the physical inputs never teleport.
    // Decide collection while the wrapper still exists: another actor's inventory
    // is inaccessible even if that actor is standing within sourcing radius.
    const bool collect = Reachable(actor, wrapper, state.settings.nearbyRadius);
    StaticList<ItemOutput, 8> outputs;
    if (state.kind == CraftingKind::Craft) { Require(outputs.PushBack(world_.GetContent().GetCraftingRecipe(state.craftingRecipe).result)); }
    else { outputs = world_.GetContent().GetRecyclingRecipe(state.recyclingRecipe).returns; }
    // Sort actual children by persistent identity before distributing their energy.
    Vector<ItemHandle> sources;
    if (state.kind == CraftingKind::Recycle) {
        const auto target = world_.FindItem(state.recyclingTarget);
        const auto* item = world_.GetItem(target);
        Require(item && item->location == ItemLocation::Inside(wrapper)
            && item->type == world_.GetContent().GetRecyclingRecipe(state.recyclingRecipe).target);
        sources.push_back(target);
    } else { sources = world_.GetItem(wrapper)->contents; }
    eastl::sort(sources.begin(), sources.end(), [this](auto a, auto b) { return world_.GetItem(a)->id < world_.GetItem(b)->id; });
    Vector<ItemHandle> products;
    for (const auto& output : outputs) {
        for (std::uint32_t n = 0; n < output.count; ++n) {
            const auto made = world_.CreateItem(output.item, ItemLocation::Ground(position)); Require(made.has_value());
            products.push_back(*made);
            const auto& definition = world_.GetContent().GetItem(output.item);
            for (const auto& store : definition.energyStores) { Require(world_.SetItemEnergy(*made, store.capability, 0).has_value()); }
        }
    }
    for (auto source : sources) {
        const auto& definition = world_.GetContent().GetItem(world_.GetItem(source)->type);
        for (const auto& store : definition.energyStores) {
            auto available = world_.GetItemEnergy(source, store.capability)->energy;
            for (auto product : products) {
                const auto& output = world_.GetContent().GetItem(world_.GetItem(product)->type);
                for (const auto& destination : output.energyStores) {
                    if (!CompatibleCraftingStores(store.config, destination.config)) { continue; }
                    const auto energy = *world_.GetItemEnergy(product, destination.capability);
                    const auto amount = eastl::min(available, energy.capacity - energy.energy);
                    Require(world_.SetItemEnergy(product, destination.capability, energy.energy + amount).has_value());
                    available -= amount;
                }
            }
            Require(world_.SetItemEnergy(source, store.capability, available).has_value());
        }
    }
    ReleaseWorker(*State(wrapper)); eastl::erase(assigned_, wrapper);
    Require(world_.DestroyItem(wrapper).has_value());
    for (auto product : products) {
        if (!collect) { continue; }
        const auto carried = world_.TransferItem(product, ItemLocation::Carried(actor));
        Require(carried || carried.error() == WorldError::capacity_exceeded);
    }
}
}
