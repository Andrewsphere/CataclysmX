#include "digital/information_system.h"
#include "core/item_state.h"
#include "core/world.h"
#include "power/power_system.h"
#include "skills/learning_system.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
bool Carried(const World& world, ActorHandle actor, ItemHandle handle) {
    for (auto item = world.GetItem(handle); item; item = world.GetItem(handle)) {
        if (item->location.installation.value || world.IsReservedItem(handle)) { return false; }
        if (item->location.kind == LocationKind::actor) { return item->location.actor == actor; }
        if (item->location.kind != LocationKind::item) { return false; }
        handle = item->location.item;
    }
    return false;
}
bool LocalPower(const World& world, ItemHandle handle) {
    const auto* item = world.GetItem(handle);
    if (!item) { return false; }
    const auto& definition = world.GetContent().GetItem(item->type);
    const auto capability = definition.readingPower;
    if (!capability.value) { return false; }
    const auto energy = world.GetItemEnergy(handle, capability);
    return energy && energy->energy > 0;
}
bool Legacy(const ItemDefinition& definition) {
    return definition.digitalDevice == DigitalDeviceKind::None
        && definition.readingPower.value && !definition.books.empty();
}
InformationEndpoint Endpoint(const World& world, ItemHandle handle) {
    const auto* item = world.GetItem(handle);
    if (!item) { return {}; }
    if (item->location.kind == LocationKind::object) {
        const auto* object = world.GetObject(item->location.object);
        if (object && object->equipment == handle) { return {0, object->id}; }
    }
    return {item->id, 0};
}
bool Available(const World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    return actor && actor->combat.life == ActorLifeState::Alive && !actor->parked
        && world.IsActive(actor->position);
}
}

InformationSystem::InformationSystem(World& world) : world_(world) {}

ItemHandle InformationSystem::Resolve(InformationEndpoint endpoint) const {
    if (bool(endpoint.item) == bool(endpoint.object)) { return {}; }
    if (endpoint.item) { return world_.FindItem(endpoint.item); }
    const auto* object = world_.GetObject(world_.FindObject(endpoint.object));
    return object ? object->equipment : ItemHandle{};
}
InformationOutcome InformationSystem::CheckEndpoint(ActorHandle actor, InformationEndpoint endpoint, bool writable) const {
    const auto handle = Resolve(endpoint);
    const auto* item = world_.GetItem(handle);
    if (!item) { return InformationOutcome::InvalidDevice; }
    const auto& definition = world_.GetContent().GetItem(item->type);
    if (definition.digitalDevice == DigitalDeviceKind::None && (writable || !Legacy(definition))) {
        return InformationOutcome::InvalidDevice;
    }
    if (endpoint.item) {
        if (world_.IsReservedItem(handle)) { return InformationOutcome::InvalidDevice; }
        if (!Carried(world_, actor, handle)) { return InformationOutcome::OutOfReach; }
    } else {
        const auto* object = world_.GetObject(world_.FindObject(endpoint.object));
        const auto* worker = world_.GetActor(actor);
        if (!object || object->equipment != handle || item->location != ItemLocation::Stored(world_.FindObject(endpoint.object))
            || definition.digitalDevice != DigitalDeviceKind::Controller
            || !object->enabled || object->broken) { return InformationOutcome::InvalidDevice; }
        if (!worker || !world_.IsActive(object->position) || worker->position.y != object->position.y
            || std::abs(worker->position.x - object->position.x) > 1
            || std::abs(worker->position.z - object->position.z) > 1) { return InformationOutcome::OutOfReach; }
    }
    return InformationOutcome::Copied;
}
bool InformationSystem::Powered(InformationEndpoint endpoint) const {
    const auto handle = Resolve(endpoint);
    const auto* item = world_.GetItem(handle);
    if (!item) { return false; }
    const auto kind = world_.GetContent().GetItem(item->type).digitalDevice;
    if (kind == DigitalDeviceKind::Storage) { return true; }
    if (endpoint.object && kind == DigitalDeviceKind::Controller) {
        const auto* object = world_.GetObject(world_.FindObject(endpoint.object));
        if (!object) { return false; }
        const PowerDemand demand{object->position, 1};
        return world_.Power().CanSupply({&demand, 1}) == PowerOutcome::Supplied;
    }
    return LocalPower(world_, handle);
}
bool InformationSystem::HasPackage(InformationEndpoint source, InformationId package) const {
    const auto* definition = world_.GetContent().TryGetInformation(package);
    const auto handle = Resolve(source);
    const auto* item = world_.GetItem(handle);
    if (!definition || !item) { return false; }
    if (const auto* library = world_.GetInformationLibrary(handle)) {
        if (eastl::find(library->packages.begin(), library->packages.end(), package) != library->packages.end()) { return true; }
    }
    const auto& carrier = world_.GetContent().GetItem(item->type);
    if (!Legacy(carrier) || definition->books.empty() || !definition->designs.empty() || definition->software) { return false; }
    for (auto book : definition->books) {
        if (eastl::find(carrier.books.begin(), carrier.books.end(), book) == carrier.books.end()) { return false; }
    }
    return true;
}
void InformationSystem::CollectPackages(InformationEndpoint source, Vector<InformationId>& output) const {
    output.clear();
    for (std::size_t i = 1; i <= world_.GetContent().InformationCount(); ++i) {
        const InformationId package{static_cast<std::uint32_t>(i)};
        if (HasPackage(source, package)) { output.push_back(package); }
    }
}
InformationOutcome InformationSystem::CheckTransfer(ActorHandle actor, InformationEndpoint source,
    InformationEndpoint destination, InformationId package, ItemHandle host) const {
    const auto from = CheckEndpoint(actor, source, false);
    if (from != InformationOutcome::Copied) { return from; }
    const auto to = CheckEndpoint(actor, destination, true);
    if (to != InformationOutcome::Copied) { return to; }
    if (!HasPackage(source, package)) { return InformationOutcome::MissingInformation; }
    if (!Powered(source) || !Powered(destination)) { return InformationOutcome::NoPower; }
    const auto sourceKind = world_.GetContent().GetItem(world_.GetItem(Resolve(source))->type).digitalDevice;
    const auto destinationKind = world_.GetContent().GetItem(world_.GetItem(Resolve(destination))->type).digitalDevice;
    if (sourceKind == DigitalDeviceKind::Storage && destinationKind == DigitalDeviceKind::Storage) {
        const auto* item = world_.GetItem(host);
        if (!item || !Carried(world_, actor, host)
            || world_.GetContent().GetItem(item->type).digitalDevice != DigitalDeviceKind::Reader) {
            return InformationOutcome::MissingHost;
        }
        if (!LocalPower(world_, host)) { return InformationOutcome::NoPower; }
    } else if (host != ItemHandle{}) {
        const auto* item = world_.GetItem(host);
        if (!item || !Carried(world_, actor, host)
            || world_.GetContent().GetItem(item->type).digitalDevice != DigitalDeviceKind::Reader) {
            return InformationOutcome::MissingHost;
        }
        if (!LocalPower(world_, host)) { return InformationOutcome::NoPower; }
    }
    return InformationOutcome::Copied;
}
WorldResult<InformationResult> InformationSystem::Copy(ActorHandle actor, InformationEndpoint source,
    InformationEndpoint destination, InformationId package, ItemHandle host) {
    if (!Available(world_, actor)) { return InformationResult{InformationOutcome::ActorUnavailable}; }
    const auto from = CheckEndpoint(actor, source, false);
    if (from != InformationOutcome::Copied) { return InformationResult{from}; }
    const auto to = CheckEndpoint(actor, destination, true);
    if (to != InformationOutcome::Copied) { return InformationResult{to}; }
    if (!HasPackage(source, package)) { return InformationResult{InformationOutcome::MissingInformation}; }
    if (HasPackage(destination, package)) { return InformationResult{InformationOutcome::AlreadyPresent}; }
    const auto checked = CheckTransfer(actor, source, destination, package, host);
    if (checked != InformationOutcome::Copied) { return InformationResult{checked}; }
    const auto handle = Resolve(destination);
    auto* library = world_.MutableInformationLibrary(handle);
    if (!library) { return InformationResult{InformationOutcome::InvalidDevice}; }
    if (library->transfer) { return InformationResult{InformationOutcome::Busy}; }
    const auto* worker = world_.GetActor(actor);
    if (world_.Learning().Busy(actor) || world_.DigitalBusy(actor)
        || worker->actionDeadline.Remaining(world_.GetTick())
        || worker->combat.action.kind != ActorActionKind::None) { return InformationResult{InformationOutcome::ActorBusy}; }
    const auto duration = ActionTicks(100, worker->speed);
    const auto workerId = worker->id;
    // Schedule before installing transfer ownership: readiness checks include us.
    if (!world_.ScheduleActorAction(actor, 100)) { return InformationResult{InformationOutcome::ActorBusy}; }
    InformationTransferState transfer;
    transfer.worker = workerId; transfer.sourceItem = source.item; transfer.sourceObject = source.object;
    transfer.hostItem = host == ItemHandle{} ? 0 : world_.GetItem(host)->id;
    transfer.packageName = world_.GetContent().GetInformation(package).id;
    transfer.remainingTicks = duration;
    world_.MutableInformationLibrary(handle)->transfer = eastl::move(transfer);
    Attach(handle); world_.MarkItemDirty(handle);
    return InformationResult{InformationOutcome::Started};
}
WorldResult<InformationResult> InformationSystem::Remove(ActorHandle actor, InformationEndpoint destination, InformationId package) {
    if (!Available(world_, actor)) { return InformationResult{InformationOutcome::ActorUnavailable}; }
    const auto checked = CheckEndpoint(actor, destination, true);
    if (checked != InformationOutcome::Copied) { return InformationResult{checked}; }
    if (!Powered(destination)) { return InformationResult{InformationOutcome::NoPower}; }
    if (!HasPackage(destination, package)) { return InformationResult{InformationOutcome::MissingInformation}; }
    auto* library = world_.MutableInformationLibrary(Resolve(destination));
    if (!library) { return InformationResult{InformationOutcome::InvalidDevice}; }
    if (world_.DigitalBusy(actor) || world_.Learning().Busy(actor)
        || !world_.ScheduleActorAction(actor, 100)) { return InformationResult{InformationOutcome::ActorBusy}; }
    library = world_.MutableInformationLibrary(Resolve(destination));
    eastl::erase(library->packages, package);
    world_.MarkItemDirty(Resolve(destination));
    return InformationResult{InformationOutcome::Removed};
}
WorldResult<InformationResult> InformationSystem::Cancel(ActorHandle actor) {
    const auto* worker = world_.GetActor(actor);
    if (!worker) { return InformationResult{InformationOutcome::ActorUnavailable}; }
    for (std::size_t i = 0; i < active_.size();) {
        const auto handle = active_[i];
        auto* library = world_.MutableInformationLibrary(handle);
        if (library && library->transfer && library->transfer->worker == worker->id) {
            library->transfer.reset(); world_.MarkItemDirty(handle); Detach(handle);
        } else { ++i; }
    }
    return InformationResult{InformationOutcome::Cancelled};
}
void InformationSystem::Attach(ItemHandle item) {
    const auto* library = world_.GetInformationLibrary(item);
    if (library && library->transfer && eastl::find(active_.begin(), active_.end(), item) == active_.end()) { active_.push_back(item); }
}
void InformationSystem::Detach(ItemHandle item) { eastl::erase(active_, item); }
bool InformationSystem::Busy(ActorHandle actor) const {
    const auto* worker = world_.GetActor(actor);
    if (!worker) { return false; }
    for (auto handle : active_) {
        const auto* library = world_.GetInformationLibrary(handle);
        if (library && library->transfer && library->transfer->worker == worker->id) { return true; }
    }
    return false;
}
eastl::optional<Tick> InformationSystem::ActorEventIn(ActorHandle actor) const {
    if (!Available(world_, actor)) { return eastl::nullopt; }
    const auto id = world_.GetActor(actor)->id;
    for (auto handle : active_) {
        const auto* library = world_.GetInformationLibrary(handle);
        if (library && library->transfer && library->transfer->worker == id) { return library->transfer->remainingTicks; }
    }
    return eastl::nullopt;
}
void InformationSystem::Elapse(Tick elapsed) {
    for (std::size_t i = 0; i < active_.size();) {
        const auto handle = active_[i];
        auto* library = world_.MutableInformationLibrary(handle);
        if (!library || !library->transfer) { Detach(handle); continue; }
        auto& transfer = *library->transfer;
        const auto actor = world_.FindActor(transfer.worker);
        const auto package = world_.GetContent().FindInformation(transfer.packageName);
        if (!Available(world_, actor) || (transfer.hostItem && world_.FindItem(transfer.hostItem) == ItemHandle{})
            || CheckTransfer(actor, {transfer.sourceItem, transfer.sourceObject},
            Endpoint(world_, handle), package, world_.FindItem(transfer.hostItem)) != InformationOutcome::Copied) {
            library->transfer.reset(); world_.MarkItemDirty(handle); Detach(handle); continue;
        }
        transfer.remainingTicks -= eastl::min(elapsed, transfer.remainingTicks);
        world_.MarkItemDirty(handle); ++i;
    }
}
bool InformationSystem::ProcessEvents() {
    due_.clear();
    bool changed = false;
    for (std::size_t i = 0; i < active_.size();) {
        const auto handle = active_[i];
        auto* library = world_.MutableInformationLibrary(handle);
        if (!library || !library->transfer) { Detach(handle); changed = true; continue; }
        const auto& transfer = *library->transfer;
        const auto actor = world_.FindActor(transfer.worker);
        const auto package = world_.GetContent().FindInformation(transfer.packageName);
        if (!Available(world_, actor) || (transfer.hostItem && world_.FindItem(transfer.hostItem) == ItemHandle{})
            || CheckTransfer(actor, {transfer.sourceItem, transfer.sourceObject},
                Endpoint(world_, handle), package, world_.FindItem(transfer.hostItem)) != InformationOutcome::Copied) {
            library->transfer.reset(); world_.MarkItemDirty(handle); Detach(handle); changed = true; continue;
        }
        if (!transfer.remainingTicks) { due_.push_back(handle); }
        ++i;
    }
    eastl::sort(due_.begin(), due_.end(), [this](auto a, auto b) { return world_.GetItem(a)->id < world_.GetItem(b)->id; });
    for (auto handle : due_) {
        auto* library = world_.MutableInformationLibrary(handle);
        if (!library || !library->transfer) { continue; }
        const auto& transfer = *library->transfer;
        const auto actor = world_.FindActor(transfer.worker);
        const auto package = world_.GetContent().FindInformation(transfer.packageName);
        if (Available(world_, actor) && (!transfer.hostItem || world_.FindItem(transfer.hostItem) != ItemHandle{})
            && CheckTransfer(actor, {transfer.sourceItem, transfer.sourceObject},
            Endpoint(world_, handle), package, world_.FindItem(transfer.hostItem)) == InformationOutcome::Copied
            && !HasPackage(Endpoint(world_, handle), package)) {
            const auto position = eastl::lower_bound(library->packages.begin(), library->packages.end(),
                package, [](InformationId a, InformationId b) { return a.value < b.value; });
            library->packages.insert(position, package);
        }
        library->transfer.reset(); world_.MarkItemDirty(handle); Detach(handle);
    }
    return changed || !due_.empty();
}
bool InformationSystem::HasBook(ItemHandle source, BookId book, eastl::span<const ItemHandle> accessible) const {
    const auto* item = world_.GetItem(source);
    if (!item || world_.IsReservedItem(source) || item->location.installation.value) { return false; }
    const auto& definition = world_.GetContent().GetItem(item->type);
    if (eastl::find(definition.books.begin(), definition.books.end(), book) != definition.books.end()
        && (!definition.readingPower.value || LocalPower(world_, source))) { return true; }
    const auto* library = world_.GetInformationLibrary(source);
    if (!library) { return false; }
    bool readable = definition.digitalDevice == DigitalDeviceKind::Reader && LocalPower(world_, source);
    if (definition.digitalDevice == DigitalDeviceKind::Storage) {
        for (auto handle : accessible) {
            const auto* reader = world_.GetItem(handle);
            if (reader && !world_.IsReservedItem(handle) && !reader->location.installation.value
                && world_.GetContent().GetItem(reader->type).digitalDevice == DigitalDeviceKind::Reader
                && LocalPower(world_, handle)) { readable = true; break; }
        }
    }
    if (!readable) { return false; }
    for (auto package : library->packages) {
        const auto& books = world_.GetContent().GetInformation(package).books;
        if (eastl::find(books.begin(), books.end(), book) != books.end()) { return true; }
    }
    return false;
}
void InformationSystem::Collect(ActorHandle actor, Vector<InformationDeviceView>& output) const {
    output.clear(); items_.clear();
    if (!Available(world_, actor)) { return; }
    const auto* worker = world_.GetActor(actor);
    items_.insert(items_.end(), worker->inventory.begin(), worker->inventory.end());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto handle = items_[i];
        if (!Carried(world_, actor, handle)) { continue; }
        const auto* item = world_.GetItem(handle);
        items_.insert(items_.end(), item->contents.begin(), item->contents.end());
    }
    const auto position = worker->position;
    for (int z = -1; z <= 1; ++z) { for (int x = -1; x <= 1; ++x) {
        const auto* object = world_.GetObject(world_.ObjectAt({position.x + x, position.z + z, position.y}));
        if (object && object->equipment != ItemHandle{}) { items_.push_back(object->equipment); }
    } }
    for (auto handle : items_) {
        const auto endpoint = Endpoint(world_, handle);
        if (CheckEndpoint(actor, endpoint, false) != InformationOutcome::Copied) { continue; }
        const auto* item = world_.GetItem(handle);
        const auto& definition = world_.GetContent().GetItem(item->type);
        InformationDeviceView view;
        view.endpoint = endpoint; view.type = item->type; view.powered = Powered(endpoint);
        view.writable = definition.digitalDevice != DigitalDeviceKind::None;
        view.reader = definition.digitalDevice == DigitalDeviceKind::Reader || Legacy(definition);
        if (const auto* library = world_.GetInformationLibrary(handle); library && library->transfer) {
            view.transferring = true; view.remainingTicks = library->transfer->remainingTicks;
        }
        CollectPackages(endpoint, view.packages); output.push_back(eastl::move(view));
    }
}
const char* InformationOutcomeText(InformationOutcome outcome) {
    switch (outcome) {
    case InformationOutcome::Started: return "Copy started.";
    case InformationOutcome::Copied: return "Information copied.";
    case InformationOutcome::Removed: return "Information removed.";
    case InformationOutcome::Cancelled: return "Copy cancelled.";
    case InformationOutcome::AlreadyPresent: return "Information already present.";
    case InformationOutcome::ActorUnavailable: return "Actor unavailable.";
    case InformationOutcome::ActorBusy: return "Actor busy.";
    case InformationOutcome::OutOfReach: return "Device out of reach.";
    case InformationOutcome::InvalidDevice: return "Invalid information device.";
    case InformationOutcome::MissingHost: return "A powered reader is required.";
    case InformationOutcome::NoPower: return "Device has no power.";
    case InformationOutcome::MissingInformation: return "Information not present.";
    case InformationOutcome::Busy: return "Device busy.";
    }
    std::abort();
}
}
