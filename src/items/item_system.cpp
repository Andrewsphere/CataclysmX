#include "items/item_system.h"
#include "core/world.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
void Require(bool condition) {
    if (!condition) { std::abort(); }
}
bool ValidArguments(ItemActionArgument argument, const ItemUseRequest& request) {
    switch (argument) {
    case ItemActionArgument::None:
        return !request.otherItem.value && request.dx == 0 && request.dz == 0 && request.dy == 0;
    case ItemActionArgument::Item:
        return request.otherItem.value && request.dx == 0 && request.dz == 0 && request.dy == 0;
    case ItemActionArgument::Direction:
        return !request.otherItem.value
            && ((request.dy == 0 && request.dx == 0 && (request.dz == -1 || request.dz == 1))
                || (request.dy == 0 && request.dz == 0 && (request.dx == -1 || request.dx == 1))
                || (request.dx == 0 && request.dz == 0 && (request.dy == -1 || request.dy == 1)));
    }
    std::abort();
}
}
ItemSystem::ItemSystem(World& world) : world_(world) {}
ItemSystem::~ItemSystem() = default;
StateResult<void> ItemSystem::Initialize(Item& item, const DataObject* archive, Diagnostics* diagnostics) {
    const auto& definition = world_.GetContent().GetItem(item.type);
    if (archive) {
        auto state = ReadItemState(definition, *archive, diagnostics);
        if (!state) { return eastl::unexpected(state.error()); }
        item.runtime_ = eastl::move(*state);
    } else { item.runtime_ = MakeItemState(definition); }
    return {};
}
DataObject ItemSystem::Capture(ItemHandle handle) const {
    DataObject result;
    const Item* item = world_.GetItem(handle);
    if (item) { WriteItemState(world_.GetContent().GetItem(item->type), item->runtime_, result); }
    return result;
}
ItemBehavior* ItemSystem::Behavior(ItemHandle item, ItemCapabilityId capability) {
    Item* value = world_.MutableItem(item);
    if (value) { for (auto& behavior : value->runtime_.behaviors_) {
        if (behavior->Capability() == capability) { return behavior.get(); }
    } }
    return nullptr;
}
const ItemBehavior* ItemSystem::Behavior(ItemHandle item, ItemCapabilityId capability) const {
    const Item* value = world_.GetItem(item);
    if (value) { for (const auto& behavior : value->runtime_.behaviors_) {
        if (behavior->Capability() == capability) { return behavior.get(); }
    } }
    return nullptr;
}
Vector<ItemActionInfo> ItemSystem::QueryActions(ActorHandle actor, ItemHandle handle) const {
    Vector<ItemActionInfo> result;
    const Item* item = world_.GetItem(handle);
    if (!item) { return result; }
    const ItemReadContext context(*this, handle);
    const auto& itemDefinition = world_.GetContent().GetItem(item->type);
    for (const auto& behavior : item->runtime_.behaviors_) {
        const auto* definition = ItemBehaviorConfig(itemDefinition, behavior->Capability());
        Require(definition != nullptr);
        Vector<ItemActionInfo> emitted;
        behavior->QueryActions(context, actor, emitted);
        for (auto& action : emitted) {
            const auto* declared = definition->FindAction(action.action);
            Require(action.capability == behavior->Capability() && ValidItemActionId(action.action)
                && declared && declared->baseCost > 0 && action.argument == declared->argument
                && action.category == declared->category && action.baseCost == declared->baseCost
                && action.label == declared->label && ValidItemUseReason(action.availability, action.reason));
            result.push_back(eastl::move(action));
        }
    }
    eastl::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.capability.value < right.capability.value
            || (left.capability == right.capability && left.action < right.action);
    });
    for (std::size_t i = 1; i < result.size(); ++i) {
        Require(result[i - 1].capability != result[i].capability || result[i - 1].action != result[i].action);
    }
    Require(result.size() <= maxItemActionsPerItem);
    if (!world_.CanAccessItem(actor, handle)) {
        for (auto& action : result) { action.availability = ItemUseOutcome::Inaccessible; action.reason = {}; }
    }
    return result;
}
ItemUseResult ItemSystem::Use(ActorHandle actor, ItemHandle handle, const ItemUseRequest& request) {
    if (!world_.CanAccessItem(actor, handle)) { return {ItemUseOutcome::Inaccessible, 0, {}}; }
    if (!ValidItemActionId(request.action)) { return {}; }
    auto* behavior = Behavior(handle, request.capability);
    if (!behavior) { return {}; }
    const auto* item = world_.GetItem(handle);
    const auto* definition = ItemBehaviorConfig(world_.GetContent().GetItem(item->type), request.capability);
    if (!definition) { return {}; }
    const auto* action = definition->FindAction(request.action);
    if (!action || !ValidArguments(action->argument, request)) { return {}; }
    Require(action->baseCost > 0);
    BeginCallback();
    ItemMutationContext context(*this, handle);
    auto result = behavior->Use(context, actor, request);
    Require(ValidItemUseReason(result.outcome, result.reason));
    switch (result.outcome) {
    case ItemUseOutcome::Succeeded:
        Require(result.baseCost == action->baseCost);
        break;
    case ItemUseOutcome::InvalidAction:
    case ItemUseOutcome::Inaccessible:
    case ItemUseOutcome::Refused:
        Require(result.baseCost == 0);
        break;
    default: std::abort();
    }
    world_.MarkItemDirty(handle);
    Refresh(handle);
    EndCallback();
    return result;
}
ItemPresentation ItemSystem::Describe(ActorHandle actor, ItemHandle handle) const {
    ItemPresentation result;
    const auto* item = world_.GetItem(handle);
    if (!item) { return result; }
    const auto& definition = world_.GetContent().GetItem(item->type);
    for (const auto& store : definition.energyStores) { result.energy.push_back(*Energy(handle, store.capability)); }
    ItemReadContext context(*this, handle);
    for (const auto& behavior : item->runtime_.behaviors_) {
        auto view = behavior->Describe(context);
        const auto* config = ItemBehaviorConfig(definition, behavior->Capability());
        Require(config != nullptr && view.capability == behavior->Capability() && ValidItemText(view.status));
        const auto& schema = config->Schema();
        Require((schema.hasActivity || !view.active)
            && (schema.countdown ? view.remainingTicks <= schema.maximumDelay : view.remainingTicks == 0)
            && (view.active || view.remainingTicks == 0));
        result.activities.push_back(eastl::move(view));
    }
    result.actions = QueryActions(actor, handle);
    return result;
}
eastl::optional<ItemEnergyView> ItemSystem::Energy(ItemHandle handle, ItemCapabilityId store) const {
    const auto* item = world_.GetItem(handle);
    if (!item) { return eastl::nullopt; }
    const auto* config = ItemEnergyStore(world_.GetContent().GetItem(item->type), store);
    if (!config) { return eastl::nullopt; }
    for (const auto& value : item->runtime_.energy_) {
        if (value.capability == store) { return ItemEnergyView{store, value.energy, config->capacityEnergy}; }
    }
    return eastl::nullopt;
}
WorldResult<void> ItemSystem::SetEnergy(ItemEnergyRef store, std::uint32_t energy) {
    const auto current = Energy(store.item, store.capability);
    if (!world_.GetItem(store.item)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!current || energy > current->capacity) { return eastl::unexpected(WorldError::invalid_request); }
    if (energy == current->energy) { return {}; }
    auto* item = world_.MutableItem(store.item);
    for (auto& value : item->runtime_.energy_) { if (value.capability == store.capability) { value.energy = energy; } }
    world_.MarkItemDirty(store.item);
    Notify({ItemChangeKind::EnergyChanged, store.item, store.capability});
    return {};
}
WorldResult<void> ItemSystem::TransferEnergy(ItemEnergyRef source, ItemEnergyRef target, std::uint32_t amount) {
    const auto from = Energy(source.item, source.capability);
    const auto to = Energy(target.item, target.capability);
    if (!from || !to || (source.item == target.item && source.capability == target.capability)
        || amount > from->energy || amount > to->capacity - to->energy) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    // Both endpoints are validated before either changes; queue notifications until both commit.
    BeginCallback();
    auto a = SetEnergy(source, from->energy - amount);
    auto b = SetEnergy(target, to->energy + amount);
    if (!a || !b) { std::abort(); }
    // Fractional activity may move no whole units, but both participating roots
    // still belong to the same dirty checkpoint boundary.
    world_.MarkItemDirty(source.item);
    world_.MarkItemDirty(target.item);
    EndCallback();
    return {};
}
bool ItemSystem::Eligible(ItemHandle item) const {
    return !PendingDestruction(item) && world_.ItemEligible(item);
}
bool ItemSystem::PendingDestruction(ItemHandle handle) const {
    while (const auto* item = world_.GetItem(handle)) {
        if (eastl::find(destruction_.begin(), destruction_.end(), handle) != destruction_.end()) { return true; }
        if (item->location.kind != LocationKind::item) { break; }
        handle = item->location.item;
    }
    return false;
}
void ItemSystem::Refresh(ItemHandle handle) {
    // Never query a behavior while a mutating callback is running or a queued
    // destruction can invalidate it. Drain retires destruction before refresh.
    if (callbackDepth_ || !destruction_.empty()) {
        if (eastl::find(refresh_.begin(), refresh_.end(), handle) == refresh_.end()) { refresh_.push_back(handle); }
        return;
    }
    const auto* item = world_.GetItem(handle);
    if (!item) { return; }
    ItemReadContext context(*this, handle);
    for (const auto& behavior : item->runtime_.behaviors_) {
        const auto cap = behavior->Capability();
        auto found = eastl::find_if(scheduled_.begin(), scheduled_.end(), [handle, cap](const auto& entry) {
            return entry.item == handle && entry.capability == cap;
        });
        if (behavior->NextEventIn(context)) {
            if (found == scheduled_.end()) { scheduled_.push_back({handle, cap}); }
        } else if (found != scheduled_.end()) { scheduled_.erase(found); }
    }
    eastl::sort(scheduled_.begin(), scheduled_.end(), [this](const auto& a, const auto& b) {
        const auto left = world_.GetItem(a.item)->id;
        const auto right = world_.GetItem(b.item)->id;
        return left < right || (left == right && a.capability.value < b.capability.value);
    });
}
void ItemSystem::Attach(ItemHandle item) { Refresh(item); }
void ItemSystem::Detach(ItemHandle item) {
    eastl::erase_if(scheduled_, [item](const auto& entry) { return entry.item == item; });
    eastl::erase_if(changes_, [item](const auto& entry) { return entry.item == item; });
    eastl::erase(destruction_, item);
    eastl::erase(refresh_, item);
}
eastl::optional<Tick> ItemSystem::NextEventIn() const {
    eastl::optional<Tick> result;
    for (const auto& entry : scheduled_) {
        if (!Eligible(entry.item)) { continue; }
        const auto* behavior = Behavior(entry.item, entry.capability);
        const auto next = behavior->NextEventIn(ItemReadContext(*this, entry.item));
        if (next && (!result || *next < *result)) { result = next; }
    }
    return result;
}
void ItemSystem::Elapse(Tick elapsed) {
    if (!elapsed) { return; }
    const auto pending = scheduled_;
    for (const auto& entry : pending) {
        if (!Eligible(entry.item)) { continue; }
        auto* behavior = Behavior(entry.item, entry.capability);
        if (!behavior || !behavior->NextEventIn(ItemReadContext(*this, entry.item))) { continue; }
        BeginCallback();
        ItemMutationContext context(*this, entry.item);
        behavior->Elapse(context, elapsed);
        world_.MarkItemDirty(entry.item);
        Refresh(entry.item);
        EndCallback();
    }
}
void ItemSystem::ProcessEvents() {
    Require(callbackDepth_ == 0 && !draining_);
    Drain();
    Vector<ScheduledBehavior> processed;
    for (;;) {
        eastl::optional<ScheduledBehavior> due;
        // Refresh keeps the sparse registry ordered by persistent ID/capability.
        // Restart selection after every callback: its notifications may make an
        // earlier or previously unscheduled behavior due at this same instant.
        for (const auto& entry : scheduled_) {
            if (!Eligible(entry.item)) { continue; }
            const auto* behavior = Behavior(entry.item, entry.capability);
            Require(behavior != nullptr);
            const auto next = behavior->NextEventIn(ItemReadContext(*this, entry.item));
            if (next && *next == 0) { due = entry; break; }
        }
        if (!due) { return; }
        const ScheduledBehavior entry = *due;
        // Per-callback progress alone cannot catch A -> B -> A zero-time cycles.
        // A behavior may fire only once at this event boundary.
        Require(eastl::none_of(processed.begin(), processed.end(), [&entry](const auto& previous) {
            return previous.item == entry.item && previous.capability == entry.capability;
        }));
        processed.push_back(entry);
        auto* behavior = Behavior(entry.item, entry.capability);
        Require(behavior != nullptr);
        BeginCallback();
        ItemMutationContext context(*this, entry.item);
        behavior->ProcessEvent(context);
        world_.MarkItemDirty(entry.item);
        Refresh(entry.item);
        EndCallback();
        if (Eligible(entry.item)) {
            const auto* remaining = Behavior(entry.item, entry.capability);
            if (remaining) {
                const auto nextEvent = remaining->NextEventIn(ItemReadContext(*this, entry.item));
                Require(!nextEvent || *nextEvent > 0);
            }
        }
    }
}
std::size_t ItemSystem::ScheduledItemCount() const {
    std::size_t result = 0;
    ItemHandle previous{};
    for (const auto& entry : scheduled_) { if (entry.item != previous) { ++result; previous = entry.item; } }
    return result;
}
void ItemSystem::BeginCallback() { ++callbackDepth_; }
void ItemSystem::EndCallback() { if (!callbackDepth_) { std::abort(); } --callbackDepth_; Drain(); }
void ItemSystem::RequestDestroy(ItemHandle item) {
    if (world_.GetItem(item) && eastl::find(destruction_.begin(), destruction_.end(), item) == destruction_.end()) {
        destruction_.push_back(item);
    }
}
void ItemSystem::Notify(const ItemChange& change) { changes_.push_back(change); Drain(); }
void ItemSystem::Drain() {
    if (callbackDepth_ || draining_) { return; }
    draining_ = true;
    while (!destruction_.empty() || !changes_.empty() || !refresh_.empty()) {
        if (!destruction_.empty()) {
            const ItemHandle handle = destruction_.front();
            destruction_.erase(destruction_.begin());
            if (world_.GetItem(handle)) { auto result = world_.DestroyItem(handle); if (!result) { std::abort(); } }
            continue;
        }
        if (changes_.empty()) {
            const auto handle = refresh_.front();
            refresh_.erase(refresh_.begin());
            Refresh(handle);
            continue;
        }
        const auto change = changes_.front();
        changes_.erase(changes_.begin());
        // Capture recipients, then resolve each again after every callback/destruction.
        Vector<ScheduledBehavior> recipients;
        ItemHandle handle = change.item;
        while (const auto* item = world_.GetItem(handle)) {
            for (const auto& behavior : item->runtime_.behaviors_) { recipients.push_back({handle, behavior->Capability()}); }
            if (item->location.kind != LocationKind::item) { break; }
            handle = item->location.item;
        }
        for (const auto& entry : recipients) {
            // A callback's requested destruction is completed before any later callback.
            while (!destruction_.empty()) {
                const auto doomed = destruction_.front(); destruction_.erase(destruction_.begin());
                if (world_.GetItem(doomed)) { auto result = world_.DestroyItem(doomed); if (!result) { std::abort(); } }
            }
            auto* behavior = Behavior(entry.item, entry.capability);
            if (!behavior) { continue; }
            BeginCallback();
            ItemMutationContext context(*this, entry.item);
            behavior->OnChange(context, change);
            world_.MarkItemDirty(entry.item);
            Refresh(entry.item);
            EndCallback();
        }
    }
    draining_ = false;
}
bool ItemSystem::CheckInvariants() const {
    if (callbackDepth_ || draining_ || !changes_.empty() || !destruction_.empty() || !refresh_.empty()) { return false; }
    PersistentId previousId = 0;
    std::uint16_t previousCap = 0;
    for (const auto& entry : scheduled_) {
        const auto* item = world_.GetItem(entry.item);
        const auto* behavior = Behavior(entry.item, entry.capability);
        if (!item || !behavior || item->id < previousId
            || (item->id == previousId && entry.capability.value <= previousCap)
            || !behavior->NextEventIn(ItemReadContext(*this, entry.item))) { return false; }
        previousId = item->id; previousCap = entry.capability.value;
    }
    // Full enumeration is reserved for invariant checks, never the tick path.
    for (auto position : world_.LoadedChunks()) {
        const auto* chunk = world_.GetChunk(position);
        Vector<ItemHandle> pending = chunk->ground_items;
        for (auto actor : chunk->actors) { for (auto item : world_.GetActor(actor)->inventory) { pending.push_back(item); } }
        for (auto object : chunk->objects) { for (auto item : world_.GetObject(object)->contents) { pending.push_back(item); } }
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto handle = pending[i];
            const auto* item = world_.GetItem(handle);
            for (auto child : item->contents) { pending.push_back(child); }
            for (const auto& behavior : item->runtime_.behaviors_) {
                const bool active = behavior->NextEventIn(ItemReadContext(*this, handle)).has_value();
                const auto count = eastl::count_if(scheduled_.begin(), scheduled_.end(), [&](const auto& entry) {
                    return entry.item == handle && entry.capability == behavior->Capability();
                });
                if (count != (active ? 1 : 0)) { return false; }
            }
        }
    }
    return true;
}
}
