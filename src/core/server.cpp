#include <cstdlib>

#include "core/server.h"
#include "digital/machine_system.h"
#include "digital/information_system.h"
#include "power/power_system.h"
#include "core/first_house_scenario.h"
#include "actors/movement.h"

#include "core/simulation.h"
#include "core/static_list.h"
#include "core/perception.h"
#include "core/ratio.h"
#include "core/lighting.h"
#include "combat/combat_system.h"
#include "skills/learning_system.h"
#include "objects/object_behavior.h"
#include "worldgen/worldgen.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

constexpr std::size_t maxOnlinePlayers = 4;

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

void CollectEnemyHealth(const World& world, CombatFaction observer, WorldView& view) {
    static_assert((2 * playerSightRange + 1) * (2 * playerSightRange + 1) <= UINT16_MAX);
    static_assert(ratioScale <= UINT16_MAX);
    for (std::size_t i = 0; i < view.cells.size(); ++i) {
        const auto& cell = view.cells[i];
        if (!cell.actor || cell.player || cell.overlayVisibility != Visibility::Visible) { continue; }
        const auto* enemy = world.GetActor(world.FindActor(cell.actor));
        if (!enemy || enemy->combat.life != ActorLifeState::Alive || !Hostile(observer, enemy->combat.faction)
            || !BodyAlive(enemy->combat.body)) { continue; }

        const auto& body = enemy->combat.body;
        EnemyHealthView health{static_cast<std::uint16_t>(i), body.kind};
        bool injured = false;
        for (std::size_t p = 0; p < body_part_count; ++p) {
            const auto part = static_cast<BodyPart>(p);
            if (!HasPart(body.kind, part)) { continue; }
            const auto maximum = EffectiveMax(body, part);
            const auto hp = body.parts[p].broken ? 0 : body.parts[p].hp;
            health.partRatios[p] = static_cast<std::uint16_t>(std::uint64_t{hp} * ratioScale / maximum);
            injured |= hp < maximum;
        }
        if (injured) { view.enemyHealth.push_back(health); }
    }
}

template<class T>
T Take(WorldResult<T> result) {
    Require(result.has_value());
    return *result;
}

bool Cardinal(int dx, int dz, int dy) {
    return (dy == 0 && dz == 0 && (dx == -1 || dx == 1))
        || (dy == 0 && dx == 0 && (dz == -1 || dz == 1))
        || (dx == 0 && dz == 0 && (dy == -1 || dy == 1));
}

bool MovementDirection(int dx, int dz, int dy) {
    return Cardinal(dx, dz, dy) || (dy == 0 && (dx == -1 || dx == 1) && (dz == -1 || dz == 1));
}

ChunkCoord BubbleCenter(TileCoord position) {
    const ChunkCoord chunk = ChunkOf(position);
    return {chunk.x, chunk.z, 0};
}

bool BubbleFits(TileCoord position, int radius) {
    if (!ValidPosition(position)) {
        return false;
    }
    const ChunkCoord center = ChunkOf(position);
    constexpr int minimum = -world_coordinate_limit / chunk_size;
    constexpr int maximum = world_coordinate_limit / chunk_size;
    return center.x >= minimum + radius && center.x <= maximum - radius
        && center.z >= minimum + radius && center.z <= maximum - radius;
}

bool ResidencyFits(TileCoord position, int active, int resident,
    eastl::optional<CircleResidency> circle) {
    if (!BubbleFits(position, active)) { return false; }
    if (!circle) { return BubbleFits(position, resident); }
    const int radius = circle->radiusTiles;
    return radius >= 0 && radius <= maxCircleResidencyRadius
        && position.x >= -world_coordinate_limit + radius
        && position.x <= world_coordinate_limit - radius
        && position.z >= -world_coordinate_limit + radius
        && position.z <= world_coordinate_limit - radius;
}

LoadRequest RequestResident(World& world, TileCoord position, int resident,
    eastl::optional<CircleResidency> circle) {
    return circle ? Take(world.RequestCircle({position, circle->radiusTiles}, LoadMode::resident, LoadReason::Player))
        : Take(world.RequestChunks({BubbleCenter(position), resident}, LoadMode::resident, LoadReason::Player));
}

void UpdateResident(World& world, LoadRequest request, TileCoord position, int resident,
    eastl::optional<CircleResidency> circle) {
    if (circle) {
        Require(world.UpdateCircleRequest(request, {position, circle->radiusTiles}, LoadMode::resident).has_value());
    } else {
        Require(world.UpdateRequest(request, {BubbleCenter(position), resident}, LoadMode::resident).has_value());
    }
}

bool FootprintLoaded(const World& world, TileCoord position, int active, int resident,
    eastl::optional<CircleResidency> circle) {
    const auto center = BubbleCenter(position);
    const int extent = circle ? (circle->radiusTiles + chunk_size - 1) / chunk_size : resident;
    const int radius = eastl::max(active, extent);
    for (int z = center.z - radius; z <= center.z + radius; ++z) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
            const bool activeColumn = std::abs(x - center.x) <= active && std::abs(z - center.z) <= active;
            bool residentColumn = true;
            if (circle) {
                const int dx = position.x - eastl::clamp(position.x, x * chunk_size, (x + 1) * chunk_size - 1);
                const int dz = position.z - eastl::clamp(position.z, z * chunk_size, (z + 1) * chunk_size - 1);
                residentColumn = dx * dx + dz * dz <= circle->radiusTiles * circle->radiusTiles;
            } else {
                residentColumn = std::abs(x - center.x) <= resident && std::abs(z - center.z) <= resident;
            }
            // World demand always materializes full-height columns.
            if ((activeColumn || residentColumn) && !world.GetChunk({x, z, 0})) { return false; }
        }
    }
    return true;
}

void ReleaseStaging(World& world, Vector<LoadRequest>& staging) {
    for (LoadRequest request : staging) { Require(world.ReleaseRequest(request).has_value()); }
    staging.clear();
}

SaveResult<void> StageBubble(World& world, TileCoord position, int active, int resident,
    eastl::optional<CircleResidency> circle, Vector<LoadRequest>& staging) {
    if (FootprintLoaded(world, position, active, resident, circle)) { return {}; }
    staging.push_back(RequestResident(world, position, resident, circle));
    if (circle) {
        staging.push_back(Take(world.RequestChunks({BubbleCenter(position), active}, LoadMode::resident, LoadReason::Player)));
    }
    const auto result = world.ReconcileChunks();
    if (!result) { ReleaseStaging(world, staging); }
    return result;
}

template<class Items>
ItemHandle SelectItem(const World& world, const Items& items, PersistentId id) {
    ItemHandle selected{};
    PersistentId smallest = eastl::numeric_limits<PersistentId>::max();
    for (ItemHandle handle : items) {
        const Item* item = world.GetItem(handle);
        Require(item != nullptr);
        if (id != 0 && item->id == id) {
            return handle;
        }
        if (id == 0 && item->id < smallest) {
            selected = handle;
            smallest = item->id;
        }
    }
    return selected;
}

Outcome TransferFailure(WorldError error) {
    switch (error) {
    case WorldError::capacity_exceeded: return Outcome::capacity_exceeded;
    case WorldError::chunk_unavailable: return Outcome::unavailable;
    case WorldError::not_container:
    case WorldError::incompatible_item:
    case WorldError::containment_cycle: return Outcome::invalid_container;
    default: return Outcome::invalid_command;
    }
}

void AppendItemTreeDescription(const World& world, ActorHandle actor, ItemHandle handle,
    Vector<InventoryEntry>& result) {
    if (world.IsReservedItem(handle)) { return; }
    const Item* item = world.GetItem(handle);
    Require(item != nullptr);
    const ItemDefinition& definition = world.GetContent().GetItem(item->type);
    InventoryEntry entry{item->id, item->type, static_cast<unsigned int>(item->contents.size())};
    entry.storage = world.GetItemStorage(handle);
    entry.wielded = world.GetActor(actor)->wieldedItem == handle;
    const auto& equipment = world.GetActor(actor)->equipment;
    entry.worn = eastl::find(equipment.worn.begin(), equipment.worn.end(), handle) != equipment.worn.end();
    entry.heldHands = static_cast<HandMask>((equipment.hands[0].item == handle ? 1 : 0)
        | (equipment.hands[1].item == handle ? 2 : 0));
    entry.stackable = !entry.worn && entry.heldHands == HandMask::None && !item->location.installation.value && item->contents.empty() && !definition.unfinishedCraft
        && definition.capabilities.empty() && definition.digitalDevice == DigitalDeviceKind::None;
    if (item->location.kind == LocationKind::item) {
        entry.parent = world.GetItem(item->location.item)->id;
        entry.installation = item->location.installation;
    }
    entry.massGrams = world.GetItemMass(handle);
    const auto volume = world.GetItemVolume(handle);
    Require(volume <= UINT32_MAX); // Wrapper inputs are bounded by their uint32 container capacity.
    entry.volumeMl = static_cast<std::uint32_t>(volume);
    if (definition.unfinishedCraft) { entry.contained_items = 0; }
    if (const auto* container = ItemContainer(definition); container && !definition.unfinishedCraft) {
        entry.capacityVolumeMl = container->capacityVolumeMl;
        entry.usedVolumeMl = world.GetContainerUsedVolume(handle);
    }
    auto presentation = world.DescribeItem(actor, handle);
    entry.energy = eastl::move(presentation.energy);
    entry.activities = eastl::move(presentation.activities);
    entry.actions = eastl::move(presentation.actions);
    result.push_back(eastl::move(entry));
    // World bounds containment depth; traverse borrows without a handle work list.
    for (ItemHandle child : item->contents) { AppendItemTreeDescription(world, actor, child, result); }
}

template<class Items>
Vector<InventoryEntry> DescribeItems(const World& world, ActorHandle actor, const Items& items) {
    Vector<InventoryEntry> result;
    for (ItemHandle handle : items) { AppendItemTreeDescription(world, actor, handle, result); }
    eastl::sort(result.begin(), result.end(),
        [](const InventoryEntry& a, const InventoryEntry& b) { return a.id < b.id; });
    return result;
}

Outcome MovementFailure(WorldError error) {
    switch (error) {
    case WorldError::blocked: return Outcome::blocked;
    case WorldError::occupied: return Outcome::occupied;
    case WorldError::invalid_position:
    case WorldError::chunk_unavailable: return Outcome::unavailable;
    default: std::abort();
    }
}

bool Ready(const World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    return actor && world.IsActorAlive(handle) && !actor->parked && !actor->actionDeadline.suspended
        && actor->combat.action.kind == ActorActionKind::None && world.GetActorRemainingTicks(handle) == 0;
}

bool ActiveWorkInterrupt(const World& world, ActorHandle actor, const Command& command) {
    if (!world.IsActorAlive(actor)) { return false; }
    if (command.kind == CommandKind::cancel_information) { return world.Information().Busy(actor); }
    if (command.kind == CommandKind::machine_action
        && (command.machineAction == MachineAction::Stop || command.machineAction == MachineAction::Cancel)) {
        Vector<MachineView> machines;
        world.Machines().Collect(actor, machines);
        const auto* worker = world.GetActor(actor);
        for (const auto& machine : machines) {
            if (machine.object == command.target && machine.setup && machine.worker == worker->id) { return true; }
        }
    }
    if (command.kind == CommandKind::pause_art_study) {
        const auto* worker = world.GetActor(actor);
        return worker->characterCombat && eastl::any_of(worker->characterCombat->manuals.begin(),
            worker->characterCombat->manuals.end(), [](const ManualAttemptProgress& attempt) { return attempt.active; });
    }
    if (command.kind == CommandKind::pause_study) {
        const auto* worker = world.GetActor(actor);
        const auto remaining = world.Learning().ActorEventIn(actor);
        return worker && world.Learning().Busy(actor)
            && worker->combat.action.kind == ActorActionKind::None && !worker->actionDeadline.suspended
            && remaining && *remaining != 0 && world.GetActorRemainingTicks(actor) == *remaining;
    }
    if (command.kind != CommandKind::pause_craft && command.kind != CommandKind::cancel_craft) { return false; }
    const auto* worker = world.GetActor(actor);
    const auto job = world.InspectCrafting(world.FindItem(command.item));
    return worker && job && job->worker == worker->id
        && (job->status == CraftingStatus::Working || job->status == CraftingStatus::PauseRequested)
        && worker->combat.action.kind == ActorActionKind::None
        && !worker->actionDeadline.suspended
        && job->fundedTicksRemaining != 0
        && world.GetActorRemainingTicks(actor) == job->fundedTicksRemaining;
}

CommandResult CraftingCommand(World& world, ActorHandle actor, const Command& command) {
    auto refused = [&world](CraftingOutcome outcome) {
        CommandResult result{Outcome::crafting_refused, false, world.GetTick()};
        result.craftingOutcome = outcome;
        return result;
    };
    const auto item = world.FindItem(command.item);
    if (command.kind != CommandKind::craft && (!world.GetItem(item) || world.IsReservedItem(item))) {
        return refused(CraftingOutcome::InvalidTarget);
    }
    const auto used = [&]() -> WorldResult<CraftingResult> {
        switch (command.kind) {
        case CommandKind::craft:
            if (!world.GetContent().TryGetCraftingRecipe(command.recipe)) {
                return CraftingResult{CraftingOutcome::InvalidTarget};
            }
            return world.StartCrafting(actor, command.recipe);
        case CommandKind::recycle: {
            const auto recipe = world.GetContent().FindRecyclingFor(world.GetItem(item)->type);
            if (!recipe.value) { return CraftingResult{CraftingOutcome::InvalidTarget}; }
            return world.StartRecycling(actor, item, recipe);
        }
        case CommandKind::resume_craft: return world.ResumeCrafting(actor, item);
        case CommandKind::pause_craft: return world.PauseCrafting(actor, item);
        case CommandKind::cancel_craft: return world.CancelCrafting(actor, item);
        default: std::abort();
        }
    }();
    if (!used) { return refused(CraftingOutcome::InvalidTarget); }
    CommandResult result = refused(used->outcome);
    switch (used->outcome) {
    case CraftingOutcome::Started:
        result.outcome = command.kind == CommandKind::recycle ? Outcome::recycling_started : Outcome::craft_started;
        break;
    case CraftingOutcome::Resumed: result.outcome = Outcome::craft_resumed; break;
    case CraftingOutcome::PauseRequested:
    case CraftingOutcome::Paused: result.outcome = Outcome::craft_paused; break;
    case CraftingOutcome::Cancelled: result.outcome = Outcome::craft_cancelled; break;
    default: return result;
    }
    result.advanced = used->outcome == CraftingOutcome::Started || used->outcome == CraftingOutcome::Resumed;
    if (result.advanced) {
        result.cost = world.GetActorRemainingTicks(actor);
        Require(world.SetActorExertion(actor, ExertionKind::Working).has_value());
    }
    world.UpdatePlayerObservations();
    return result;
}

Outcome CombatOutcome(AttackOutcome outcome) {
    switch (outcome) {
    case AttackOutcome::Unavailable: return Outcome::unavailable;
    case AttackOutcome::NotReady: return Outcome::not_ready;
    case AttackOutcome::NoTarget: return Outcome::no_target;
    case AttackOutcome::OutOfReach: return Outcome::no_target;
    case AttackOutcome::NoMove: return Outcome::no_combat_move;
    case AttackOutcome::Windup: return Outcome::attack_windup;
    case AttackOutcome::Miss: return Outcome::attack_missed;
    case AttackOutcome::Parried: return Outcome::attack_parried;
    case AttackOutcome::Blocked: return Outcome::attack_blocked;
    case AttackOutcome::Hit: return Outcome::attacked;
    case AttackOutcome::Defeated: return Outcome::target_defeated;
    }
    std::abort();
}

}

bool ValidCommandShape(const Command& command) {
    // Every unused field is canonical, including historical command fields.
    // Transport and local submissions share this admission policy; catalog,
    // ownership and ability-grant authority are checked at execution.
    const auto kind = command.kind;
    const bool beginMachine = kind == CommandKind::begin_machine;
    const bool machine = beginMachine || kind == CommandKind::machine_action;
    const bool copy = kind == CommandKind::copy_information;
    const bool remove = kind == CommandKind::remove_information;
    const auto endpoint = [](InformationEndpoint value) { return (value.item != 0) != (value.object != 0); };
    if ((!beginMachine && (command.machineRecipe.value || command.quantity))
        || (kind != CommandKind::machine_action && command.machineAction != MachineAction::Stop)
        || (!copy && command.informationSource != InformationEndpoint{})
        || (!copy && !remove && (command.informationDestination != InformationEndpoint{} || command.information.value))
        || (!copy && command.informationHost)) { return false; }
    const bool forge = kind == CommandKind::load_forge || kind == CommandKind::ignite_forge
        || kind == CommandKind::extinguish_forge || kind == CommandKind::discard_forge_fuel;
    if (kind != CommandKind::wire_grid && command.powerBounds != PowerBounds{}) { return false; }
    if (kind != CommandKind::place_power_object && kind != CommandKind::place_station && command.position != TileCoord{}) { return false; }
    const bool direction = kind == CommandKind::move || kind == CommandKind::interact
        || kind == CommandKind::grab || kind == CommandKind::use_item;
    const bool item = kind == CommandKind::pick_up || kind == CommandKind::drop
        || kind == CommandKind::use_item || kind == CommandKind::store_item
        || kind == CommandKind::wield_item || kind == CommandKind::transfer_item
        || kind == CommandKind::wire_grid || kind == CommandKind::place_power_object || kind == CommandKind::place_station || kind == CommandKind::load_forge
        || (kind >= CommandKind::recycle && kind <= CommandKind::cancel_craft);
    if ((!direction && (command.dx || command.dz || command.dy))
        || (!item && command.item)
        || (kind != CommandKind::wait && command.waitCost != ticks_per_turn)
        || (kind != CommandKind::use_item && (command.capability.value || !command.itemAction.empty()))
        || (kind != CommandKind::use_item && kind != CommandKind::store_item && kind != CommandKind::place_power_object && command.otherItem)
        || (kind != CommandKind::attack && kind != CommandKind::pack_station && !forge && !machine && command.target)
        || (kind != CommandKind::combat_style && command.style != CombatStyle::Balanced)
        || (kind != CommandKind::interact_action && command.interaction != InteractionRequest{})
        || (kind != CommandKind::craft && command.recipe.value)
        || (kind != CommandKind::transfer_item && (command.source != ItemStorage{} || command.destination != ItemStorage{}))
        || (kind != CommandKind::study_book && kind != CommandKind::study_art && command.book.value)
        || (kind != CommandKind::equipment && command.equipment != EquipmentRequest{})
        || (kind != CommandKind::select_combat_style && command.combatStyle.value)
        || (kind != CommandKind::use_ability && command.ability != AbilityRequest{})
        || (kind != CommandKind::study_art && command.art.value)) { return false; }
    if (command.kind >= CommandKind::equipment && command.kind <= CommandKind::new_character) {
        if (command.dx || command.dz || command.dy || command.item || command.waitCost != ticks_per_turn
            || command.capability.value || !command.itemAction.empty() || command.otherItem || command.target
            || command.style != CombatStyle::Balanced || command.interaction != InteractionRequest{}
            || command.recipe.value || command.source != ItemStorage{} || command.destination != ItemStorage{}) { return false; }
        if (command.kind != CommandKind::equipment && command.equipment != EquipmentRequest{}) { return false; }
        if (command.kind != CommandKind::select_combat_style && command.combatStyle.value) { return false; }
        if (command.kind != CommandKind::use_ability && command.ability != AbilityRequest{}) { return false; }
        if (command.kind != CommandKind::study_art && (command.book.value || command.art.value)) { return false; }
        switch (command.kind) {
        case CommandKind::equipment: {
            const auto& request = command.equipment;
            if (request.operation > EquipmentOperation::Remove || request.hand > Hand::Right || request.grip > GripKind::TwoHanded) { return false; }
            switch (request.operation) {
            case EquipmentOperation::Hold: return request.item != 0;
            case EquipmentOperation::Release: return !request.item && request.grip == GripKind::OneHanded;
            case EquipmentOperation::Regrip: return !request.item;
            case EquipmentOperation::Wear:
            case EquipmentOperation::Remove: return request.item && request.hand == Hand::Left && request.grip == GripKind::OneHanded;
            }
            return false;
        }
        case CommandKind::select_combat_style: return command.combatStyle.value != 0;
        case CommandKind::use_ability: {
            const auto& request = command.ability;
            if (!request.ability.value || request.source.kind > AbilityGrantSourceKind::Innate
                || (request.source.kind == AbilityGrantSourceKind::Art ? !request.source.art.value : request.source.art.value != 0)) { return false; }
            const auto& target = request.target;
            if (target.direction > CardinalDirection::West) { return false; }
            switch (target.kind) {
            case AbilityTargetKind::Self: return !target.actor && target.tile == TileCoord{} && target.direction == CardinalDirection::North;
            case AbilityTargetKind::Actor: return target.actor && target.tile == TileCoord{} && target.direction == CardinalDirection::North;
            case AbilityTargetKind::Tile: return !target.actor && ValidPosition(target.tile) && target.direction == CardinalDirection::North;
            case AbilityTargetKind::CardinalDirection: return !target.actor && target.tile == TileCoord{};
            }
            return false;
        }
        case CommandKind::study_art: return command.book.value && command.art.value;
        case CommandKind::pause_art_study:
        case CommandKind::new_character: return true;
        default: return false;
        }
    }
    if (command.equipment != EquipmentRequest{} || command.combatStyle.value
        || command.ability != AbilityRequest{} || command.art.value) { return false; }
    if (command.kind == CommandKind::study_book || command.kind == CommandKind::pause_study) {
        return command.dx == 0 && command.dz == 0 && command.dy == 0 && command.item == 0
            && command.waitCost == ticks_per_turn && command.capability.value == 0
            && command.itemAction.empty() && command.otherItem == 0 && command.target == 0
            && command.style == CombatStyle::Balanced && command.interaction == InteractionRequest{}
            && command.recipe.value == 0 && command.source == ItemStorage{} && command.destination == ItemStorage{}
            && (command.kind == CommandKind::study_book ? command.book.value != 0 : command.book.value == 0);
    }
    if (command.book.value != 0) { return false; }
    if (beginMachine) { return command.target && command.machineRecipe.value && command.quantity && command.quantity <= 255; }
    if (kind == CommandKind::machine_action) { return command.target && command.machineAction <= MachineAction::Collect; }
    if (copy) { return endpoint(command.informationSource) && endpoint(command.informationDestination)
        && command.informationSource != command.informationDestination && command.information.value; }
    if (remove) { return endpoint(command.informationDestination) && command.information.value; }
    if (kind == CommandKind::cancel_information) { return true; }
    if (kind == CommandKind::wire_grid) {
        const auto bounds = command.powerBounds;
        return command.item != 0 && ValidPowerBounds(bounds) && bounds.min.y == bounds.max.y
            && std::int64_t{bounds.max.x} - bounds.min.x < maxPowerWiringExtent
            && std::int64_t{bounds.max.z} - bounds.min.z < maxPowerWiringExtent;
    }
    if (kind == CommandKind::place_power_object) {
        return command.item != 0 && command.item != command.otherItem && ValidPosition(command.position);
    }
    if (kind == CommandKind::place_station) { return command.item != 0 && ValidPosition(command.position); }
    if (kind == CommandKind::pack_station) { return command.target != 0; }
    if (forge) { return command.target != 0 && (kind != CommandKind::load_forge || command.item != 0); }
    if (command.kind == CommandKind::wield_item || command.kind == CommandKind::transfer_item) {
        return command.dx == 0 && command.dz == 0 && command.dy == 0 && command.waitCost == ticks_per_turn
            && command.capability.value == 0 && command.itemAction.empty() && command.otherItem == 0
            && command.target == 0 && command.style == CombatStyle::Balanced && command.recipe.value == 0
            && command.interaction == InteractionRequest{}
            && (command.kind == CommandKind::wield_item
                ? command.source == ItemStorage{} && command.destination == ItemStorage{}
                : command.item != 0 && ValidItemStorage(command.source) && ValidItemStorage(command.destination));
    }
    // Historical semantic checks follow the common canonical-field checks.
    if (command.source != ItemStorage{} || command.destination != ItemStorage{}) { return false; }
    if (command.style > CombatStyle::Defensive) { return false; }
    if (command.kind >= CommandKind::craft && command.kind <= CommandKind::cancel_craft) {
        return command.dx == 0 && command.dz == 0 && command.dy == 0
            && command.waitCost == ticks_per_turn && command.capability.value == 0
            && command.itemAction.empty() && command.otherItem == 0 && command.target == 0
            && command.style == CombatStyle::Balanced && command.interaction == InteractionRequest{}
            && (command.kind == CommandKind::craft
                ? command.recipe.value != 0 && command.item == 0
                : command.recipe.value == 0 && command.item != 0);
    }
    if (command.recipe.value != 0) { return false; }
    switch (command.kind) {
    case CommandKind::grab:
        return command.dy == 0 && MovementDirection(command.dx, command.dz, 0);
    case CommandKind::release_grab:
        return command.dx == 0 && command.dz == 0 && command.dy == 0;
    case CommandKind::interact_action: return ValidInteraction(command.interaction);
    case CommandKind::attack:
    case CommandKind::combat_style:
        return command.dx == 0 && command.dz == 0 && command.dy == 0;
    case CommandKind::wait: return command.waitCost != 0;
    case CommandKind::move: return MovementDirection(command.dx, command.dz, command.dy);
    case CommandKind::interact: return command.dy == 0 && command.dx >= -1 && command.dx <= 1 && command.dz >= -1 && command.dz <= 1;
    case CommandKind::pick_up:
    case CommandKind::drop: return true;
    case CommandKind::store_item:
        return command.item != 0 && command.item != command.otherItem;
    case CommandKind::use_item:
        if (command.item == 0 || command.capability.value == 0
            || command.capability.value > maxItemCapabilities
            || !ValidItemActionId(command.itemAction) || command.otherItem == command.item) { return false; }
        // Admission checks the generic argument envelope. The selected behavior's
        // declaration is checked against current authoritative state at execution.
        if (command.otherItem != 0) { return command.dx == 0 && command.dz == 0 && command.dy == 0; }
        return (command.dx == 0 && command.dz == 0 && command.dy == 0)
            || Cardinal(command.dx, command.dz, command.dy);
    }
    return false;
}

const char* OutcomeMessage(Outcome outcome) {
    switch (outcome) {
    case Outcome::equipment_changed: return "You adjust your equipment.";
    case Outcome::ability_used: return "You use the ability.";
    case Outcome::combat_refused: return "You cannot perform that combat action.";
    case Outcome::art_study_started: return "You start studying the combat art.";
    case Outcome::art_study_paused: return "You pause studying the combat art.";
    case Outcome::character_dead: return "Your character is dead.";
    case Outcome::character_created: return "You begin with a fresh character.";
    case Outcome::power_changed: return "Power installation completed.";
    case Outcome::power_refused: return "Power installation refused.";
    case Outcome::station_changed: return "Workshop equipment changed.";
    case Outcome::station_refused: return "Workshop action refused.";
    case Outcome::machine_changed: return "Machine changed.";
    case Outcome::machine_refused: return "Machine action refused.";
    case Outcome::information_changed: return "Information action completed.";
    case Outcome::information_refused: return "Information action refused.";
    case Outcome::study_started: return "You start studying.";
    case Outcome::study_paused: return "You pause studying after the funded work finishes.";
    case Outcome::study_refused: return "You cannot study that book.";
    case Outcome::wielded: return "You wield the item.";
    case Outcome::unwielded: return "You put away your weapon.";
    case Outcome::transferred: return "You move the item.";
    case Outcome::inventory_refused: return "You cannot move that item.";
    case Outcome::waited: return "You wait.";
    case Outcome::moved: return "You move.";
    case Outcome::picked_up: return "You pick up the item.";
    case Outcome::dropped: return "You drop the item.";
    case Outcome::opened: return "You open the door.";
    case Outcome::closed: return "You close the door.";
    case Outcome::locked: return "You lock the door.";
    case Outcome::unlocked: return "You unlock the door.";
    case Outcome::toggled: return "You toggle the object.";
    case Outcome::blocked: return "The way is blocked.";
    case Outcome::occupied: return "The tile is occupied.";
    case Outcome::nothing_here: return "There is nothing here.";
    case Outcome::invalid_command: return "Invalid command.";
    case Outcome::not_carried: return "You are not carrying that item.";
    case Outcome::unavailable: return "That location is unavailable.";
    case Outcome::not_ready: return "You are still busy.";
    case Outcome::queued: return "You queue your next action.";
    case Outcome::stale_input: return "Stale input.";
    case Outcome::duplicate_input: return "Duplicate input.";
    case Outcome::used_item: return "You use the item.";
    case Outcome::stored_item: return "You move the item.";
    case Outcome::item_use_failed: return "The item cannot be used.";
    case Outcome::capacity_exceeded: return "Not enough carrying or storage capacity.";
    case Outcome::invalid_container: return "That item cannot be stored there.";
    case Outcome::attacked: return "Your attack hits.";
    case Outcome::attack_missed: return "Your attack misses.";
    case Outcome::attack_parried: return "Your attack is parried.";
    case Outcome::attack_blocked: return "Protection stops your attack.";
    case Outcome::attack_windup: return "You prepare a heavy strike.";
    case Outcome::target_defeated: return "You defeat your target.";
    case Outcome::no_target: return "No perceived hostile is within melee reach.";
    case Outcome::no_combat_move: return "You cannot perform an attack with your usable limbs.";
    case Outcome::combat_style_changed: return "You change your combat style.";
    case Outcome::craft_started: return "You start crafting.";
    case Outcome::recycling_started: return "You start recycling.";
    case Outcome::craft_resumed: return "You resume crafting.";
    case Outcome::craft_paused: return "You pause crafting after the funded work finishes.";
    case Outcome::craft_cancelled: return "You cancel crafting.";
    case Outcome::crafting_refused: return "You cannot perform that crafting action.";
    case Outcome::object_moved: return "You move the furniture.";
    case Outcome::grabbed: return "You take hold of the furniture.";
    case Outcome::released: return "You let go of the furniture.";
    case Outcome::nothing_to_grab: return "There is nothing movable to grab there.";
    case Outcome::grab_lost: return "You lose your grip; the object is no longer beside you.";
    case Outcome::grab_blocked: return "The furniture cannot move that way; something is in the way.";
    }
    std::abort();
}

String CommandResultMessage(const CommandResult& result) {
    if (result.outcome == Outcome::machine_changed || result.outcome == Outcome::machine_refused) {
        return MachineOutcomeText(result.machineOutcome);
    }
    if (result.outcome == Outcome::information_changed || result.outcome == Outcome::information_refused) {
        return InformationOutcomeText(result.informationOutcome);
    }
    if (result.outcome == Outcome::station_changed || result.outcome == Outcome::station_refused) {
        return StationActionMessage(result.stationOutcome);
    }
    if (result.outcome == Outcome::power_changed || result.outcome == Outcome::power_refused) {
        return PowerActionMessage(result.powerOutcome);
    }
    if (result.outcome == Outcome::study_refused) {
        switch (result.studyOutcome) {
        case StudyOutcome::ActorUnavailable: return "You cannot study here now.";
        case StudyOutcome::ActorBusy: return "You are still busy.";
        case StudyOutcome::MissingReference: return "You need an accessible, powered copy of that book.";
        case StudyOutcome::AlreadyUsed: return "You have already studied that book.";
        case StudyOutcome::OutsideTeachingRange: return "That book cannot teach you at your current skill levels.";
        default: return OutcomeMessage(result.outcome);
        }
    }
    if (result.outcome == Outcome::inventory_refused) { return InventoryOutcomeMessage(result.inventoryOutcome); }
    if (!result.objectName.empty()) {
        if (result.outcome == Outcome::grabbed) { return String("You take hold of the ") + result.objectName + "."; }
        if (result.outcome == Outcome::released) { return String("You let go of the ") + result.objectName + "."; }
        if (result.outcome == Outcome::grab_blocked) { return String("You cannot move the ") + result.objectName + " that way; something is in the way."; }
    }
    if (result.outcome == Outcome::crafting_refused) {
        switch (result.craftingOutcome) {
        case CraftingOutcome::ActorUnavailable: return "You cannot craft here now.";
        case CraftingOutcome::ActorBusy: return "You are still busy.";
        case CraftingOutcome::JobBusy: return "Another worker is assigned to that job.";
        case CraftingOutcome::OutOfReach: return "That crafting target is out of reach.";
        case CraftingOutcome::RecipeDisabled: return "That recipe is disabled.";
        case CraftingOutcome::InvalidTarget: return "That is not a valid crafting target or recipe.";
        case CraftingOutcome::MissingInputs: return "You lack the required crafting inputs.";
        case CraftingOutcome::MissingTools: return "You lack the required crafting tools.";
        case CraftingOutcome::InsufficientEnergy: return "Your crafting tools lack energy.";
        case CraftingOutcome::CapacityExceeded: return "There is not enough capacity for crafting.";
        case CraftingOutcome::MissingSkills: return "You lack the required skill levels.";
        case CraftingOutcome::MissingRecipeReference: return "You need an accessible reference for that recipe.";
        case CraftingOutcome::StationBusy: return "A required workshop station is in use.";
        case CraftingOutcome::ColdForge: return "A required forge is cold.";
        case CraftingOutcome::InsufficientFuel: return "The forge has insufficient fuel.";
        default: return OutcomeMessage(result.outcome);
        }
    }
    if (result.outcome != Outcome::item_use_failed) { return OutcomeMessage(result.outcome); }
    return result.itemReason.message.fallback.empty() ? ItemUseMessage(result.itemOutcome)
        : result.itemReason.message.fallback.c_str();
}

ContentResult<void> ValidateServerContent(const ContentCatalog& content, Diagnostics* diagnostics) {
    for (const char* id : {"floor", "wall", "closed_door"}) {
        if (content.FindTerrain(id).value == 0) {
            if (diagnostics) {
                String message = "Missing terrain required by the demo: ";
                message += id;
                diagnostics->Add(eastl::string_view(message.data(), message.size()));
            }
            return eastl::unexpected(ContentError::InvalidData);
        }
    }
    if (!content.GetTerrain(content.FindTerrain("floor")).passable) {
        if (diagnostics) { diagnostics->Add("Demo terrain 'floor' must be passable"); }
        return eastl::unexpected(ContentError::InvalidData);
    }
    for (const char* id : {"rock", "backpack", "bandage"}) {
        if (content.FindItem(id).value == 0) {
            if (diagnostics) {
                String message = "Missing item required by the demo: "; message += id;
                diagnostics->Add({message.data(), message.size()});
            }
            return eastl::unexpected(ContentError::InvalidData);
        }
    }
    const auto* backpack = ItemContainer(content.GetItem(content.FindItem("backpack")));
    if (!backpack || backpack->capacityVolumeMl < content.GetItem(content.FindItem("bandage")).volumeMl) {
        if (diagnostics) { diagnostics->Add("Demo backpack must hold the bandage"); }
        return eastl::unexpected(ContentError::InvalidData);
    }
    return ValidateWorldgenContent(content, diagnostics);
}

Server::Server(const ContentCatalog& content, std::uint32_t seed)
    : Server(content, seed, NewGameScenario::Demo) {}

int DefaultStartSafetyRadius(NewGameScenario scenario) {
    return scenario == NewGameScenario::Town ? 16 : 0;
}

Server::Server(const ContentCatalog& content, std::uint32_t seed, NewGameScenario scenario,
    eastl::optional<int> startSafetyRadius)
    : world_(content, seed), rng_state_(seed) {
    Require(ValidateServerContent(content).has_value());
    Require(scenario == NewGameScenario::Demo || scenario == NewGameScenario::FirstHouse
        || scenario == NewGameScenario::Town || scenario == NewGameScenario::ZombieShowcase);
    GenerationSettings generation;
    generation.population = scenario == NewGameScenario::Town ? PopulationMode::Urban : PopulationMode::Authored;
    generation.safetyRadius = startSafetyRadius.value_or(DefaultStartSafetyRadius(scenario));
    Require(generation.safetyRadius >= 0 && generation.safetyRadius <= maxStartSafetyRadius);
    if (scenario == NewGameScenario::ZombieShowcase) {
        Vector<MonsterTypeId> exhibits;
        for (std::uint32_t value = 1; value <= content.MonsterCount(); ++value) {
            if (content.GetMonster({value}).faction == CombatFaction::Undead) { exhibits.push_back({value}); }
        }
        eastl::sort(exhibits.begin(), exhibits.end(), [&content](MonsterTypeId left, MonsterTypeId right) {
            return content.GetMonster(left).id < content.GetMonster(right).id;
        });
        int side = 1;
        while (static_cast<std::size_t>(side * side) < exhibits.size()) { ++side; }
        const int edge = side * 3 / 2 + 2;
        const TileCoord playerPosition{0, -edge + 1};
        generation.start = playerPosition;
        world_.ConfigureGeneration(generation);
        active_radius_ = eastl::max(active_radius_, edge / chunk_size + 1);
        resident_radius_ = eastl::max(resident_radius_, active_radius_);
        active_request_ = Take(world_.RequestChunks({{0, 0}, active_radius_}, LoadMode::active, LoadReason::Player));
        resident_request_ = RequestResident(world_, playerPosition, resident_radius_, circleResidency_);
        Require(world_.ReconcileChunks().has_value());
        for (int z = -edge; z <= edge; ++z) {
            for (int x = -edge; x <= edge; ++x) {
                const bool border = x == -edge || x == edge || z == -edge || z == edge;
                Require(world_.SetTile({x, z}, {content.FindTerrain(border ? "wall" : "floor"), 0}).has_value());
            }
        }
        player_ = Take(world_.CreateActor(playerPosition, true));
        players_.push_back({world_.GetActor(player_)->id, "Host", 0, playerPosition, PlayerConnectionState::Active});
        const auto backpack = Take(world_.CreateItem(content.FindItem("backpack"), ItemLocation::Carried(player_)));
        Take(world_.CreateItem(content.FindItem("bandage"), ItemLocation::Inside(backpack)));
        for (std::size_t index = 0; index < exhibits.size(); ++index) {
            const TileCoord position{static_cast<int>(index % side) * 3 - (side - 1) * 3 / 2,
                static_cast<int>(index / side) * 3 - (side - 1) * 3 / 2};
            const auto actor = Take(world_.CreateMonster(exhibits[index], position));
            Require(world_.SetActorController(actor, AiControllerKind::None).has_value());
        }
        world_.UpdatePlayerObservations();
        SynchronizePacing();
        return;
    }
    if (scenario != NewGameScenario::Demo) {
        const auto start = scenario == NewGameScenario::FirstHouse
            ? FindFirstHouseStart(content, seed) : FindTownStart(content, seed);
        Require(start.has_value());
        generation.start = start->playerPosition;
        world_.ConfigureGeneration(generation);
        active_request_ = Take(world_.RequestChunks({BubbleCenter(start->playerPosition), active_radius_}, LoadMode::active, LoadReason::Player));
        resident_request_ = RequestResident(world_, start->playerPosition, resident_radius_, circleResidency_);
        Require(world_.ReconcileChunks().has_value());
        player_ = Take(world_.CreateActor(start->playerPosition, true));
        players_.push_back({world_.GetActor(player_)->id, "Host", 0, start->playerPosition, PlayerConnectionState::Active});
        const auto backpack = Take(world_.CreateItem(content.FindItem("backpack"), ItemLocation::Carried(player_)));
        Take(world_.CreateItem(content.FindItem("bandage"), ItemLocation::Inside(backpack)));
        world_.UpdatePlayerObservations();
        SynchronizePacing();
        return;
    }
    world_.ConfigureGeneration(generation);
    active_request_ = Take(world_.RequestChunks({{0, 0}, active_radius_}, LoadMode::active, LoadReason::Player));
    resident_request_ = RequestResident(world_, {0, 0}, resident_radius_, circleResidency_);
    Require(world_.ReconcileChunks().has_value());
    constexpr TileCoord floors[] = {{0, 0}, {1, 0}, {2, 0}, {5, 5}};
    for (TileCoord position : floors) {
        Require(world_.SetTile(position, {content.FindTerrain("floor"), 0}).has_value());
    }
    const auto demoDoor = content.FindTerrain("lockable_closed_door");
    Require(world_.SetTile({3, 0}, {demoDoor.value ? demoDoor : content.FindTerrain("closed_door"), 0}).has_value());
    Require(world_.SetTile({3, 1}, {content.FindTerrain("wall"), 0}).has_value());
    player_ = Take(world_.CreateActor({0, 0}, true));
    players_.push_back({world_.GetActor(player_)->id, "Host", 0, {0, 0}, PlayerConnectionState::Active});
    const ItemHandle backpack = Take(world_.CreateItem(content.FindItem("backpack"), ItemLocation::Ground({0, 0})));
    Take(world_.CreateItem(content.FindItem("bandage"), ItemLocation::Inside(backpack)));
    Take(world_.CreateItem(content.FindItem("rock"), ItemLocation::Ground({1, 0})));
    Take(world_.CreateObject({2, 0}, false));
    const auto enemy = Take(world_.CreateActor({5, 5}));
    auto enemyCombat = MakeCombatState(false);
    enemyCombat.faction = CombatFaction::Undead;
    Require(world_.Combat().Configure(enemy, enemyCombat).has_value());
    if (content.FindAiProfile("sighted_zombie").value) {
        Require(world_.SetActorAiProfile(enemy, "sighted_zombie").has_value());
    } else {
        Require(world_.SetActorController(enemy, AiControllerKind::Sighted).has_value());
    }
    // Optional demo equipment follows the original entities, preserving their IDs.
    for (const char* id : {"field_charger", "rechargeable_battery", "door_controller", "utility_controller"}) {
        if (const auto type = content.FindItem(id); type.value != 0) {
            Take(world_.CreateItem(type, ItemLocation::Ground({0, 1})));
        }
    }
    const TerrainId air = content.FindTerrain("air");
    const TerrainId stairs = content.FindTerrain("stairs");
    if (air.value != 0 && stairs.value != 0 && content.GetTerrain(air).openSpace
        && content.GetTerrain(stairs).stairs && content.GetTerrain(stairs).passable) {
        // West of the original demo: stairs connect a basement, ground, and a
        // raised platform. Its hole drops through ground air into the basement.
        // Terrain edits allocate no entity IDs; the extra rock follows all of
        // the original equipment so replay identities remain stable.
        for (int z = 0; z <= 2; ++z) {
            for (int x = -5; x <= -3; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    Require(world_.SetTile({x, z, y}, {content.FindTerrain("floor"), 0}).has_value());
                }
            }
        }
        for (int y = -1; y <= 1; ++y) {
            Require(world_.SetTile({-3, 0, y}, {stairs, 0}).has_value());
        }
        Require(world_.SetTile({-5, 2, -1}, {content.FindTerrain("wall"), 0}).has_value());
        Require(world_.SetTile({-4, 1, 0}, {air, 0}).has_value());
        Require(world_.SetTile({-4, 1, 1}, {air, 0}).has_value());
        Take(world_.CreateItem(content.FindItem("rock"), ItemLocation::Ground({-4, 1, -1})));
        const auto lamp = Take(world_.CreateObject({-5, 0, -1}, true));
        Require(world_.SetObjectAppearance(lamp, ObjectKind::Lamp).has_value());
        Require(world_.SetObjectLight(lamp, {240, 4}).has_value());
        const auto chair = Take(world_.CreateObject({-5, 1, -1}));
        Require(world_.SetObjectAppearance(chair, ObjectKind::Chair, ObjectFacing::East).has_value());
        const auto table = Take(world_.CreateObject({-3, 2, -1}));
        Require(world_.SetObjectAppearance(table, ObjectKind::Table).has_value());
    }
    // Append optional visual test items after all existing demo entities.
    const char* bulbNames[] = {"blinking_lightbulb", "toggle_lightbulb", "single_blink_lightbulb"};
    constexpr TileCoord bulbPositions[] = {{1, 1}, {2, 1}, {1, 2}};
    for (unsigned int index = 0; index < 3; ++index) {
        if (const auto type = content.FindItem(bulbNames[index]); type.value != 0) {
            Require(world_.SetTile(bulbPositions[index], {content.FindTerrain("floor"), 0}).has_value());
            Take(world_.CreateItem(type, ItemLocation::Ground(bulbPositions[index])));
        }
    }
    world_.UpdatePlayerObservations();
    SynchronizePacing();
}

CommandResult Server::Send(Command command) {
    if (!ValidCommandShape(command)) { return {Outcome::invalid_command, false, GetTick()}; }
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    const PlayerSlot* host = FindPlayer(GetHostPlayerId());
    Require(host != nullptr);
    legacySequence_ = eastl::max(legacySequence_, host->lastSequence);
    Require(legacySequence_ != eastl::numeric_limits<std::uint64_t>::max());
    bool otherActive = false;
    for (const PlayerSlot& slot : players_) {
        if (slot.id != GetHostPlayerId() && slot.state == PlayerConnectionState::Active) {
            otherActive = true;
        }
    }
    if (!Ready(world_, player_) || otherActive) {
        // Keep the latest intent, not a chain of key-repeat-generated actions.
        // Destination, item ownership and cost are checked when it becomes due.
        return Submit(GetHostPlayerId(), {++legacySequence_, FindPlayer(GetHostPlayerId())->opportunity, command});
    }
    const CommandResult admission = Submit(GetHostPlayerId(),
        {++legacySequence_, FindPlayer(GetHostPlayerId())->opportunity, command});
    if (admission.outcome != Outcome::queued) { return admission; }
    const auto barrier = cataclysmx::AdvanceUntilInput(world_, rng_state_, GetTick());
    Require(barrier.stop == SimulationStop::InputRequired);
    SessionUpdate resolved;
    Require(ResolveRound(resolved, {}));
    for (const PlayerCommandResult& consumed : resolved.commandResults) {
        if (consumed.player != GetHostPlayerId()) { continue; }
        CommandResult result = consumed.result;
        if (result.advanced) {
            const SessionUpdate update = Update(0);
            result.tick = update.tick;
            result.storageError = update.storageError;
        }
        return result;
    }
    return admission;
}

CommandResult Server::Execute(ActorHandle handle, PlayerSlot& slot, Command command) {
    const Actor* actor = world_.GetActor(handle);
    Require(actor != nullptr);
    const TileCoord position = actor->position;
    const auto failed = [this](Outcome outcome) { return CommandResult{outcome, false, GetTick()}; };
    if (!world_.IsActorAlive(handle)) { return failed(Outcome::character_dead); }
    if (!Ready(world_, handle) && !ActiveWorkInterrupt(world_, handle, command)) { return failed(Outcome::not_ready); }
    if (command.kind == CommandKind::select_combat_style || command.kind == CommandKind::combat_style) {
        const auto style = command.kind == CommandKind::select_combat_style ? command.combatStyle
            : world_.GetContent().FindCombatStyle(command.style == CombatStyle::Aggressive ? "basic_aggressive"
                : command.style == CombatStyle::Defensive ? "basic_defensive" : "basic_balanced");
        const auto changed = world_.Combat().ChangeStyle(handle, style);
        auto result = failed(changed.committed ? Outcome::combat_style_changed : Outcome::combat_refused);
        result.advanced = changed.committed; result.cost = changed.cost; result.combatReason = changed.reason;
        return result;
    }
    if (command.kind == CommandKind::use_ability) {
        const auto used = world_.Combat().UseAbility(handle, command.ability, rng_state_);
        world_.FinalizeActorDeaths();
        auto result = failed(used.committed ? Outcome::ability_used : Outcome::combat_refused);
        result.advanced = used.committed; result.cost = used.cost; result.combatReason = used.reason;
        if (used.committed) { world_.UpdatePlayerObservations(); }
        return result;
    }
    if (command.kind == CommandKind::study_art) {
        const auto used = world_.Combat().StudyArt(handle, {command.book, command.art});
        auto result = failed(used.committed ? Outcome::art_study_started : Outcome::combat_refused);
        result.advanced = used.committed; result.cost = used.committed ? world_.GetActorRemainingTicks(handle) : 0;
        result.combatReason = used.reason;
        if (used.committed) { Require(world_.SetActorExertion(handle, ExertionKind::Working).has_value()); }
        return result;
    }
    if (command.kind == CommandKind::pause_art_study) {
        return failed(world_.Combat().PauseArtStudy(handle) ? Outcome::art_study_paused : Outcome::combat_refused);
    }
    if (command.kind == CommandKind::study_book || command.kind == CommandKind::pause_study) {
        const auto used = command.kind == CommandKind::study_book
            ? world_.Learning().Study(handle, command.book) : world_.Learning().PauseStudy(handle);
        auto result = failed(Outcome::study_refused);
        if (!used) { return result; }
        result.studyOutcome = used->outcome;
        if (used->outcome == StudyOutcome::Started) {
            Require(world_.SetActorExertion(handle, ExertionKind::Working).has_value());
            result.outcome = Outcome::study_started;
            result.advanced = true;
            result.cost = world_.GetActorRemainingTicks(handle);
        } else if (used->outcome == StudyOutcome::PauseRequested || used->outcome == StudyOutcome::Paused) {
            result.outcome = Outcome::study_paused;
        }
        return result;
    }
    if (command.kind >= CommandKind::craft && command.kind <= CommandKind::cancel_craft) {
        return CraftingCommand(world_, handle, command);
    }
    if (command.kind == CommandKind::begin_machine || command.kind == CommandKind::machine_action) {
        const bool setupStop = command.kind == CommandKind::machine_action
            && command.machineAction == MachineAction::Stop && ActiveWorkInterrupt(world_, handle, command);
        if (!setupStop && (world_.DigitalBusy(handle) || world_.CraftingBusy(handle) || world_.Learning().Busy(handle))) {
            auto result = failed(Outcome::machine_refused);
            result.machineOutcome = MachineOutcome::ActorBusy;
            return result;
        }
        const auto action = command.kind == CommandKind::begin_machine
            ? world_.Machines().Start(handle, world_.FindObject(command.target), command.machineRecipe, command.quantity)
            : world_.Machines().Use(handle, world_.FindObject(command.target), command.machineAction);
        auto result = failed(Outcome::machine_refused);
        if (!action) { return result; }
        result.machineOutcome = action->outcome;
        const bool success = action->outcome <= MachineOutcome::Collected;
        if (!success) { return result; }
        result.outcome = Outcome::machine_changed;
        result.advanced = action->baseCost != 0 || action->outcome == MachineOutcome::Started
            || action->outcome == MachineOutcome::Resumed;
        if (action->baseCost) {
            result.cost = ActionTicks(action->baseCost, world_.GetActor(handle)->speed);
            Require(world_.ScheduleActorAction(handle, action->baseCost).has_value());
        } else if (action->outcome == MachineOutcome::Started || action->outcome == MachineOutcome::Resumed) {
            result.cost = world_.GetActorRemainingTicks(handle);
        }
        world_.UpdatePlayerObservations();
        return result;
    }
    if (command.kind == CommandKind::copy_information || command.kind == CommandKind::remove_information
        || command.kind == CommandKind::cancel_information) {
        const auto action = command.kind == CommandKind::copy_information
            ? world_.Information().Copy(handle, command.informationSource, command.informationDestination,
                command.information, world_.FindItem(command.informationHost))
            : command.kind == CommandKind::remove_information
                ? world_.Information().Remove(handle, command.informationDestination, command.information)
                : world_.Information().Cancel(handle);
        auto result = failed(Outcome::information_refused);
        if (!action) { return result; }
        result.informationOutcome = action->outcome;
        if (action->outcome > InformationOutcome::AlreadyPresent) { return result; }
        result.outcome = Outcome::information_changed;
        result.advanced = action->baseCost != 0 || action->outcome == InformationOutcome::Started
            || action->outcome == InformationOutcome::Removed;
        if (action->baseCost) {
            result.cost = ActionTicks(action->baseCost, world_.GetActor(handle)->speed);
            Require(world_.ScheduleActorAction(handle, action->baseCost).has_value());
        } else if (action->outcome == InformationOutcome::Started || action->outcome == InformationOutcome::Removed) {
            result.cost = world_.GetActorRemainingTicks(handle);
        }
        return result;
    }
    const auto attack = [this, handle](ActorHandle target) {
        const auto result = world_.Combat().Attack(handle, target, rng_state_);
        world_.FinalizeActorDeaths();
        if (result.committed) { world_.UpdatePlayerObservations(); }
        CommandResult command{CombatOutcome(result.outcome), result.committed, GetTick()};
        command.cost = result.cost;
        command.combatReason = result.reason;
        return command;
    };
    Outcome outcome = Outcome::invalid_command;
    // Readiness alone does not imply that a work subsystem has released its
    // actor. Reject before inventory/object/map mutations; the final scheduler
    // rejects these states too. Explicit work interrupts were dispatched above.
    if (world_.DigitalBusy(handle) || world_.CraftingBusy(handle) || world_.Learning().Busy(handle)) {
        return failed(Outcome::not_ready);
    }
    StationActionOutcome stationOutcome = StationActionOutcome::Unavailable;
    std::uint32_t baseCost = command.kind == CommandKind::wait ? command.waitCost : ticks_per_turn;
    switch (command.kind) {
    case CommandKind::load_forge:
    case CommandKind::ignite_forge:
    case CommandKind::extinguish_forge:
    case CommandKind::discard_forge_fuel: {
        const ForgeAction operation = command.kind == CommandKind::load_forge ? ForgeAction::LoadFuel
            : command.kind == CommandKind::ignite_forge ? ForgeAction::Ignite
            : command.kind == CommandKind::extinguish_forge ? ForgeAction::Extinguish : ForgeAction::DiscardFuel;
        const auto action = world_.UseForge(handle, world_.FindObject(command.target), operation, world_.FindItem(command.item));
        if (!action) { return failed(Outcome::invalid_command); }
        if (action->outcome != StationActionOutcome::FuelLoaded && action->outcome != StationActionOutcome::Ignited
            && action->outcome != StationActionOutcome::Extinguished && action->outcome != StationActionOutcome::FuelDiscarded) {
            auto result = failed(Outcome::station_refused);
            result.stationOutcome = action->outcome; return result;
        }
        stationOutcome = action->outcome;
        baseCost = static_cast<std::uint32_t>(action->cost); outcome = Outcome::station_changed;
        break;
    }
    case CommandKind::wire_grid:
    case CommandKind::place_power_object: {
        const auto power = command.kind == CommandKind::wire_grid
            ? world_.WireGrid(handle, world_.FindItem(command.item), command.powerBounds)
            : world_.PlacePowerObject(handle, world_.FindItem(command.item), world_.FindItem(command.otherItem), command.position);
        if (power.outcome != PowerActionOutcome::Succeeded) {
            auto result = failed(Outcome::power_refused);
            result.powerOutcome = power.outcome;
            return result;
        }
        baseCost = power.baseCost;
        outcome = Outcome::power_changed;
        break;
    }
    case CommandKind::place_station:
    case CommandKind::pack_station: {
        const auto action = command.kind == CommandKind::place_station
            ? world_.PlaceStation(handle, world_.FindItem(command.item), command.position)
            : world_.PackStation(handle, world_.FindObject(command.target));
        if (!action) { return failed(Outcome::invalid_command); }
        if (action->outcome != StationActionOutcome::Placed && action->outcome != StationActionOutcome::Packed) {
            auto result = failed(Outcome::station_refused);
            result.stationOutcome = action->outcome; return result;
        }
        baseCost = static_cast<std::uint32_t>(action->cost); outcome = Outcome::station_changed;
        stationOutcome = action->outcome;
        break;
    }
    case CommandKind::grab: {
        const TileCoord target{position.x + command.dx, position.z + command.dz, position.y};
        const auto objectHandle = world_.ObjectAt(target);
        const auto* object = world_.GetObject(objectHandle);
        const auto* definition = object ? world_.GetContent().TryGetObject(object->type) : nullptr;
        if (!definition || !definition->movable || !world_.CanPlayerSee(handle, target)
            || !world_.IsActive(target)) { return failed(Outcome::nothing_to_grab); }
        slot.grabbedObject = objectHandle;
        slot.grabOffset = {command.dx, command.dz, 0};
        auto result = failed(Outcome::grabbed);
        result.objectName = definition->name;
        return result;
    }
    case CommandKind::release_grab: {
        auto result = failed(Outcome::released);
        if (const auto* object = world_.GetObject(slot.grabbedObject)) {
            result.objectName = world_.GetContent().GetObject(object->type).name;
        }
        slot.grabbedObject = {};
        return result;
    }
    case CommandKind::attack: {
        const auto target = command.target ? world_.FindActor(command.target) : world_.Combat().ChooseAdjacentTarget(handle, rng_state_);
        const auto* other = world_.GetActor(target);
        if (!other || !world_.CanPlayerSee(handle, other->position)) { return failed(Outcome::no_target); }
        return attack(target);
    }
    case CommandKind::equipment: {
        const auto& request = command.equipment;
        const auto item = world_.FindItem(request.item);
        const auto query = world_.QueryEquipment(handle, request);
        if (query.reason != CombatRefusal::None) {
            auto result = failed(Outcome::combat_refused); result.combatReason = query.reason; return result;
        }
        InventoryResult changed;
        switch (request.operation) {
        case EquipmentOperation::Hold: changed = world_.HoldItem(handle, item, request.hand, request.grip); break;
        case EquipmentOperation::Release: changed = world_.ReleaseHand(handle, request.hand); break;
        case EquipmentOperation::Regrip: changed = world_.RegripItem(handle, request.hand, request.grip); break;
        case EquipmentOperation::Wear: changed = world_.WearItem(handle, item); break;
        case EquipmentOperation::Remove: changed = world_.RemoveWornItem(handle, item); break;
        }
        if (changed.outcome != InventoryOutcome::Succeeded) {
            auto result = failed(Outcome::inventory_refused); result.inventoryOutcome = changed.outcome; return result;
        }
        baseCost = static_cast<std::uint32_t>(changed.baseCost); outcome = Outcome::equipment_changed;
        break;
    }
    case CommandKind::wield_item:
    case CommandKind::transfer_item: {
        const auto selected = world_.FindItem(command.item);
        const auto moved = command.kind == CommandKind::transfer_item
            ? world_.TransferInventoryItem(handle, command.item, command.source, command.destination)
            : command.item && !selected.value ? InventoryResult{InventoryOutcome::InvalidItem}
            : world_.WieldItem(handle, selected);
        if (moved.outcome != InventoryOutcome::Succeeded) {
            auto result = failed(Outcome::inventory_refused);
            result.inventoryOutcome = moved.outcome;
            return result;
        }
        baseCost = static_cast<std::uint32_t>(moved.baseCost);
        outcome = command.kind == CommandKind::transfer_item ? Outcome::transferred
            : command.item ? Outcome::wielded : Outcome::unwielded;
        break;
    }
    case CommandKind::wait:
        if (command.waitCost == 0) { return failed(Outcome::invalid_command); }
        outcome = Outcome::waited;
        break;
    case CommandKind::move: {
        if (!MovementDirection(command.dx, command.dz, command.dy)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz, position.y + command.dy};
        if (slot.grabbedObject.value) {
            const auto* object = world_.GetObject(slot.grabbedObject);
            const TileCoord expected{position.x + slot.grabOffset.x, position.z + slot.grabOffset.z, position.y};
            if (!object || object->position != expected || !world_.IsActive(expected)) {
                slot.grabbedObject = {};
                return failed(Outcome::grab_lost);
            }
            const auto& definition = world_.GetContent().GetObject(object->type);
            const auto blocked = [&] {
                auto result = failed(Outcome::grab_blocked);
                result.objectName = definition.name;
                return result;
            };
            if (command.dy || !ResidencyFits(destination, active_radius_, resident_radius_, circleResidency_)) { return blocked(); }
            Vector<LoadRequest> staging;
            const auto loaded = StageBubble(world_, destination, active_radius_, resident_radius_, circleResidency_, staging);
            if (!loaded) {
                Require(world_.ReconcileChunks().has_value());
                return {Outcome::unavailable, false, GetTick(), loaded.error()};
            }
            const auto moved = world_.TryObjectStep(handle, slot.grabbedObject, {command.dx, command.dz, 0});
            if (!moved || moved->outcome != ObjectMoveOutcome::Moved) {
                ReleaseStaging(world_, staging);
                Require(world_.ReconcileChunks().has_value());
                return blocked();
            }
            baseCost = static_cast<std::uint32_t>(moved->cost);
            CommitPlayerBubble(slot);
            ReleaseStaging(world_, staging);
            Require(world_.ReconcileChunks().has_value());
            outcome = Outcome::object_moved;
            break;
        }
        const auto occupant = world_.ActorAt(destination);
        const auto* other = world_.GetActor(occupant);
        if (other && Hostile(actor->combat.faction, other->combat.faction)) {
            if (!world_.CanPlayerSee(handle, destination)) { return failed(Outcome::occupied); }
            // Bump requests combat against this actor. It does not move or stage
            // a new residency bubble and cannot fall through into movement.
            return attack(occupant);
        }
        if (const auto* tile = world_.GetTile(destination); tile
            && world_.GetContent().GetTerrain(tile->terrain).opensTo.value != 0) {
            if (command.dy != 0 && !OpenVerticalFace(world_, position, destination)) { return failed(Outcome::blocked); }
            Tile opened = *tile;
            opened.terrain = world_.GetContent().GetTerrain(tile->terrain).opensTo;
            if (!world_.SetTile(destination, opened)) { return failed(Outcome::unavailable); }
            outcome = Outcome::opened;
            break;
        }
        baseCost = ActorStepBaseCost(world_.ActorMovementBaseCost(handle),
            {command.dx, command.dz, command.dy});
        // Validate the complete future bubble before acquiring any demand.
        if (!ResidencyFits(destination, active_radius_, resident_radius_, circleResidency_)) {
            return failed(Outcome::unavailable);
        }
        Vector<LoadRequest> staging;
        // Preserve the old bubble while staging every chunk the new requests
        // will need. Reconcile failures leave all live world state untouched.
        const auto loaded = StageBubble(world_, destination, active_radius_, resident_radius_, circleResidency_, staging);
        if (!loaded) {
            Require(world_.ReconcileChunks().has_value());
            return {Outcome::unavailable, false, GetTick(), loaded.error()};
        }
        const auto moved = world_.TryActorStep(handle, {command.dx, command.dz, command.dy});
        if (!moved) {
            if (!staging.empty()) {
                ReleaseStaging(world_, staging);
                Require(world_.ReconcileChunks().has_value());
            }
            return failed(MovementFailure(moved.error()));
        }
        // The player has entered the destination before any old demand can be
        // removed, preserving its runtime key even with zero-radius bubbles.
        // The union is resident now; changing activity and releasing temporary
        // demand below cannot require another storage read.
        CommitPlayerBubble(slot);
        ReleaseStaging(world_, staging);
        Require(world_.ReconcileChunks().has_value());
        outcome = Outcome::moved;
        break;
    }
    case CommandKind::pick_up: {
        const ItemHandle item = SelectItem(world_, world_.ItemsAt(position), command.item);
        if (item.value == 0) {
            return failed(Outcome::nothing_here);
        }
        if (const auto moved = world_.TransferItem(item, ItemLocation::Carried(handle)); !moved) {
            return failed(TransferFailure(moved.error()));
        }
        outcome = Outcome::picked_up;
        break;
    }
    case CommandKind::drop: {
        const ItemHandle item = SelectItem(world_, actor->inventory, command.item);
        if (item.value == 0) {
            return failed(Outcome::not_carried);
        }
        Require(world_.TransferItem(item, ItemLocation::Ground(position)).has_value());
        outcome = Outcome::dropped;
        break;
    }
    case CommandKind::use_item: {
        const ItemHandle item = world_.FindItem(command.item);
        const ItemUseResult used = world_.UseItem(handle, item,
            {command.capability, command.itemAction, world_.FindItem(command.otherItem), command.dx, command.dz, command.dy});
        if (used.outcome != ItemUseOutcome::Succeeded) {
            auto result = failed(Outcome::item_use_failed);
            result.itemOutcome = used.outcome;
            result.itemReason = used.reason;
            return result;
        }
        baseCost = used.baseCost;
        outcome = Outcome::used_item;
        break;
    }
    case CommandKind::store_item: {
        const ItemHandle item = world_.FindItem(command.item);
        if (!world_.CanAccessItem(handle, item)) { return failed(Outcome::not_carried); }
        if (world_.GetItem(item)->location.installation.value) { return failed(Outcome::invalid_container); }
        if (!world_.CanAccessStorage(handle, world_.GetItemStorage(item))) { return failed(Outcome::invalid_container); }
        ItemLocation destination = ItemLocation::Carried(handle);
        if (command.otherItem != 0) {
            const ItemHandle container = world_.FindItem(command.otherItem);
            if (!world_.CanAccessStorage(handle, {ItemStorageKind::Container, {}, command.otherItem})) { return failed(Outcome::invalid_container); }
            destination = ItemLocation::Inside(container);
        }
        if (world_.GetItem(item)->location == destination) { return failed(Outcome::nothing_here); }
        if (const auto moved = world_.TransferItem(item, destination); !moved) {
            return failed(TransferFailure(moved.error()));
        }
        outcome = Outcome::stored_item;
        break;
    }
    case CommandKind::interact_action: {
        Vector<InteractionChoice> choices;
        QueryInteractions(world_, handle, command.interaction.position, choices);
        const auto found = eastl::find_if(choices.begin(), choices.end(), [&](const InteractionChoice& choice) {
            return choice.request == command.interaction;
        });
        if (found == choices.end()) { return failed(Outcome::unavailable); }
        if (found->reason != ObjectUseReason::None) { return failed(Outcome::blocked); }
        if (command.interaction.kind == InteractionKind::Object) {
            const auto used = world_.UseObject(handle, world_.FindObject(command.interaction.object), command.interaction.action);
            if (used.outcome != ObjectUseOutcome::Succeeded) { return failed(Outcome::blocked); }
            baseCost = used.baseCost;
            outcome = Outcome::toggled;
        } else {
            Tile tile = *world_.GetTile(command.interaction.position);
            const auto& terrain = world_.GetContent().GetTerrain(tile.terrain);
            switch (command.interaction.kind) {
            case InteractionKind::Open: tile.terrain = terrain.opensTo; outcome = Outcome::opened; break;
            case InteractionKind::Close: tile.terrain = terrain.closesTo; outcome = Outcome::closed; break;
            case InteractionKind::Lock: tile.terrain = terrain.locksTo; outcome = Outcome::locked; break;
            case InteractionKind::Unlock: tile.terrain = terrain.unlocksTo; outcome = Outcome::unlocked; break;
            case InteractionKind::Object:
            case InteractionKind::PushObject:
            case InteractionKind::PullObject: std::abort();
            }
            if (!world_.SetTile(command.interaction.position, tile)) { return failed(Outcome::unavailable); }
        }
        break;
    }
    case CommandKind::interact: {
        if (command.dy != 0 || command.dx < -1 || command.dx > 1 || command.dz < -1 || command.dz > 1) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz, position.y + command.dy};
        const Tile* tile = world_.GetTile(destination);
        if (!tile) {
            return failed(Outcome::unavailable);
        }
        const TerrainDefinition& terrain = world_.GetContent().GetTerrain(tile->terrain);
        if (terrain.unlocksTo.value != 0) { return failed(Outcome::blocked); }
        if (terrain.opensTo.value != 0 || terrain.closesTo.value != 0) {
            const bool closing = terrain.closesTo.value != 0;
            const auto* object = world_.GetObject(world_.ObjectAt(destination));
            const auto* definition = object ? world_.GetContent().TryGetObject(object->type) : nullptr;
            if (closing && (world_.ActorAt(destination).value != 0 || (definition && definition->blocksMovement))) {
                return failed(Outcome::occupied);
            }
            Tile changed = *tile;
            changed.terrain = closing ? terrain.closesTo : terrain.opensTo;
            Require(world_.SetTile(destination, changed).has_value());
            outcome = closing ? Outcome::closed : Outcome::opened;
        } else {
            const ObjectHandle object = world_.ObjectAt(destination);
            if (object.value == 0) {
                return failed(Outcome::nothing_here);
            }
            if (const auto state = world_.DescribeObjectState(object)) {
                ObjectAction action = ObjectAction::Start;
                if (state->running) {
                    action = ObjectAction::Stop;
                } else if (!state->hasJob && !world_.GetObject(object)->contents.empty()) {
                    action = ObjectAction::Collect;
                }
                const ObjectUseResult used = world_.UseObject(handle, object, action);
                if (used.outcome != ObjectUseOutcome::Succeeded) {
                    return failed(used.reason == ObjectUseReason::NoContents
                        || used.reason == ObjectUseReason::NoJob ? Outcome::nothing_here : Outcome::blocked);
                }
                baseCost = used.baseCost;
            } else {
                const bool enabled = world_.GetObject(object)->enabled;
                Require(world_.SetObjectEnabled(object, !enabled).has_value());
            }
            outcome = Outcome::toggled;
        }
        break;
    }
    default:
        return failed(Outcome::invalid_command);
    }
    const Tick cost = ActionTicks(baseCost, world_.GetActor(handle)->speed);
    Require(world_.SetActorExertion(handle, outcome == Outcome::moved ? ExertionKind::Moving
        : command.kind == CommandKind::wait ? ExertionKind::Rest : ExertionKind::Working).has_value());
    Require(world_.ScheduleActorAction(handle, baseCost).has_value());
    // Observe the completed action for every participant, even between two
    // commands at the same tick. Snapshot/viewport cadence never grants memory.
    world_.UpdatePlayerObservations();
    CommandResult result{outcome, true, GetTick()};
    result.cost = cost;
    if (outcome == Outcome::power_changed) { result.powerOutcome = PowerActionOutcome::Succeeded; }
    if (outcome == Outcome::station_changed) {
        result.stationOutcome = stationOutcome;
    }
    return result;
}

void Server::SynchronizePacing() {
    if (!hostPlayerId_ && !players_.empty()) { hostPlayerId_ = players_.front().id; }
    StaticList<PlayerReadiness, maxOnlinePlayers> players;
    for (PlayerSlot& slot : players_) {
        if (!slot.character) { slot.character = slot.id; }
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const ActorHandle handle = PlayerActor(slot);
        const Actor* actor = world_.GetActor(handle);
        if (!world_.IsActorAlive(handle)) { continue; }
        Require(actor && actor->player && !actor->parked && world_.IsActive(actor->position));
        Require(players.PushBack({handle, slot.id, actor->speed,
            Ready(world_, handle), slot.pending.has_value()}));
    }
    pacer_.Synchronize({players.begin(), players.Size()});
}

Server::PlayerSlot* Server::FindPlayer(PersistentId id) {
    for (PlayerSlot& slot : players_) { if (slot.id == id) { return &slot; } }
    return nullptr;
}

const Server::PlayerSlot* Server::FindPlayer(PersistentId id) const {
    for (const PlayerSlot& slot : players_) { if (slot.id == id) { return &slot; } }
    return nullptr;
}

ActorHandle Server::PlayerActor(const PlayerSlot& slot) const {
    return world_.FindActor(slot.character ? slot.character : slot.id);
}

PersistentId Server::GetHostPlayerId() const { return hostPlayerId_; }
std::uint64_t Server::GetRevision() const { return revision_ + world_.GetRevision(); }
PlayerInputContext Server::GetInputContext(PersistentId id) const {
    const auto* slot = FindPlayer(id);
    return slot ? PlayerInputContext{slot->opportunity, slot->lastSequence, GetRevision()}
        : PlayerInputContext{};
}
ViewStats Server::GetViewStats() const { return viewStats_; }
void Server::ResetViewStats() const { viewStats_ = {}; }

Vector<PlayerStatus> Server::GetPlayers() const {
    Vector<PlayerStatus> result;
    for (const PlayerSlot& slot : players_) {
        const Actor* actor = world_.GetActor(PlayerActor(slot));
        const bool ready = slot.state == PlayerConnectionState::Active
            && actor && Ready(world_, PlayerActor(slot));
        result.push_back({slot.id, slot.name, slot.state, ready,
            slot.pending.has_value(), slot.opportunity,
            ready ? pacer_.GraceMicroseconds(PlayerActor(slot)) : 0,
            slot.character ? slot.character : slot.id, actor ? actor->combat.life : slot.cachedView.combat.life});
    }
    return result;
}

SessionResult<PlayerJoin> Server::JoinPlayer(eastl::string_view name,
    std::uint64_t reconnectToken, std::uint64_t newToken) {
    if (name.empty() || name.size() > 32) {
        return eastl::unexpected(SessionError::InvalidName);
    }
    for (char character : name) {
        if (character == '\0') { return eastl::unexpected(SessionError::InvalidName); }
    }
    std::size_t online = 0;
    for (const PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Parked) { ++online; }
    }
    if (online >= maxOnlinePlayers) { return eastl::unexpected(SessionError::Full); }
    if (reconnectToken != 0) {
        for (PlayerSlot& slot : players_) {
            if (slot.token != reconnectToken) { continue; }
            if (slot.state != PlayerConnectionState::Parked) {
                return eastl::unexpected(SessionError::AlreadyConnected);
            }
            const auto position = world_.FindPlayerPosition(slot.character ? slot.character : slot.id);
            if (!position) { return eastl::unexpected(SessionError::Unavailable); }
            if (!ValidPosition(*position)) {
                return eastl::unexpected(SessionError::Unavailable);
            }
            slot.position = *position;
            slot.center = BubbleCenter(*position);
            slot.residentRequest = Take(world_.RequestChunks(
                {BubbleCenter(slot.position), 0}, LoadMode::resident, LoadReason::Player));
            if (!world_.ReconcileChunks()) {
                Require(world_.ReleaseRequest(slot.residentRequest).has_value());
                slot.residentRequest = {};
                return eastl::unexpected(SessionError::StorageFailure);
            }
            const Actor* actor = world_.GetActor(PlayerActor(slot));
            if (!actor || !actor->player || !actor->parked) {
                Require(world_.ReleaseRequest(slot.residentRequest).has_value());
                slot.residentRequest = {};
                Require(world_.ReconcileChunks().has_value());
                return eastl::unexpected(SessionError::Unavailable);
            }
            slot.state = PlayerConnectionState::Joining;
            slot.name = String(name.data(), name.size());
            ++revision_;
            return PlayerJoin{slot.id, slot.token};
        }
        return eastl::unexpected(SessionError::UnknownToken);
    }
    if (newToken == 0) { return eastl::unexpected(SessionError::InvalidToken); }
    if (players_.size() >= 32) { return eastl::unexpected(SessionError::Full); }
    for (const PlayerSlot& slot : players_) {
        if (slot.token == newToken) { return eastl::unexpected(SessionError::InvalidToken); }
    }
    const TileCoord origin = world_.GetActor(player_)->position;
    TileCoord spawn{};
    bool found = false;
    for (int radius = 0; radius <= 64 && !found; ++radius) {
        for (int dz = -radius; dz <= radius && !found; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (eastl::max(std::abs(dx), std::abs(dz)) != radius) { continue; }
                const TileCoord candidate{origin.x + dx, origin.z + dz, origin.y};
                const Tile* tile = world_.GetTile(candidate);
                if (tile && Passable(world_.GetContent(), tile->terrain)
                    && world_.ActorAt(candidate).value == 0
                    && ResidencyFits(candidate, active_radius_, resident_radius_, circleResidency_)) {
                    spawn = candidate;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) { return eastl::unexpected(SessionError::Unavailable); }
    const ActorHandle actor = Take(world_.CreateActor(spawn, true));
    Require(world_.SetActorParked(actor, true).has_value());
    PlayerSlot slot;
    slot.id = world_.GetActor(actor)->id;
    slot.character = slot.id;
    slot.name = String(name.data(), name.size());
    slot.token = newToken;
    slot.position = world_.GetActor(actor)->position;
    slot.center = BubbleCenter(slot.position);
    slot.state = PlayerConnectionState::Joining;
    slot.residentRequest = Take(world_.RequestChunks({slot.center, 0}, LoadMode::resident, LoadReason::Player));
    const PlayerJoin joined{slot.id, slot.token};
    players_.push_back(eastl::move(slot));
    ++revision_;
    return joined;
}

SessionResult<void> Server::ActivatePlayer(PersistentId id) {
    PlayerSlot* slot = FindPlayer(id);
    if (!slot) { return eastl::unexpected(SessionError::InvalidPlayer); }
    if (slot->state != PlayerConnectionState::Joining) {
        return eastl::unexpected(SessionError::AlreadyConnected);
    }
    if (!ResidencyFits(slot->position, active_radius_, resident_radius_, circleResidency_)) {
        return eastl::unexpected(SessionError::Unavailable);
    }
    Require(slot->residentRequest.value != 0);
    Vector<LoadRequest> staging;
    const auto rollback = [this, &staging]() {
        ReleaseStaging(world_, staging);
        Require(world_.ReconcileChunks().has_value());
    };
    const auto staged = StageBubble(world_, slot->position, active_radius_, resident_radius_, circleResidency_, staging);
    if (!staged) {
        return eastl::unexpected(SessionError::StorageFailure);
    }
    const ActorHandle actor = PlayerActor(*slot);
    if (!world_.GetActor(actor)) {
        rollback();
        return eastl::unexpected(SessionError::Unavailable);
    }
    if (!world_.IsActorAlive(actor)) {
        slot->state = PlayerConnectionState::Active;
        slot->activeRequest = Take(world_.RequestChunks({BubbleCenter(slot->position), active_radius_}, LoadMode::active, LoadReason::Player));
        UpdateResident(world_, slot->residentRequest, slot->position, resident_radius_, circleResidency_);
        ReleaseStaging(world_, staging);
        Require(world_.ReconcileChunks().has_value());
        ++revision_; SynchronizePacing(); return {};
    }
    TileCoord spawn{};
    bool found = false;
    // At radius zero, the saved tile may be occupied while another tile in
    // the same chunk is free. Rings can also cross a chunk boundary.
    for (int radius = 0; radius <= chunk_size * (resident_radius_ + 1) && !found; ++radius) {
        for (int dz = -radius; dz <= radius && !found; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (eastl::max(std::abs(dx), std::abs(dz)) != radius) { continue; }
                const TileCoord candidate{slot->position.x + dx, slot->position.z + dz, slot->position.y};
                if (!ResidencyFits(candidate, active_radius_, resident_radius_, circleResidency_)) { continue; }
                if (!world_.GetTile(candidate)) {
                    staging.push_back(Take(world_.RequestChunks({BubbleCenter(candidate), 0}, LoadMode::resident, LoadReason::Player)));
                    if (!world_.ReconcileChunks()) {
                        rollback();
                        return eastl::unexpected(SessionError::StorageFailure);
                    }
                }
                const Tile* tile = world_.GetTile(candidate);
                if (tile && Passable(world_.GetContent(), tile->terrain)
                    && world_.ActorAt(candidate).value == 0) {
                    spawn = candidate;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        rollback();
        return eastl::unexpected(SessionError::Unavailable);
    }
    // Every chunk of the destination bubble is resident before relocating or
    // unparking the actor. Updating the permanent requests cannot need I/O.
    if (!StageBubble(world_, spawn, active_radius_, resident_radius_, circleResidency_, staging)) {
        rollback();
        return eastl::unexpected(SessionError::StorageFailure);
    }
    Require(world_.MoveActor(actor, spawn).has_value());
    Require(world_.SetActorParked(actor, false).has_value());
    slot->state = PlayerConnectionState::Active;
    slot->activeRequest = Take(world_.RequestChunks({BubbleCenter(spawn), active_radius_}, LoadMode::active, LoadReason::Player));
    CommitPlayerBubble(*slot);
    ReleaseStaging(world_, staging);
    Require(world_.ReconcileChunks().has_value());
    world_.UpdatePlayerObservations();
    ++revision_;
    SynchronizePacing();
    return {};
}

SessionResult<void> Server::DisconnectPlayer(PersistentId id) {
    PlayerSlot* slot = FindPlayer(id);
    if (!slot || id == GetHostPlayerId()) {
        return eastl::unexpected(SessionError::InvalidPlayer);
    }
    if (slot->state == PlayerConnectionState::Parked) {
        return eastl::unexpected(SessionError::Unavailable);
    }
    const ActorHandle actor = PlayerActor(*slot);
    if (const auto position = world_.FindPlayerPosition(slot->character ? slot->character : slot->id)) { slot->position = *position; }
    if (world_.GetActor(actor)) { slot->cachedView.combat = world_.Combat().Describe(actor); }
    if (actor.value != 0) { Require(world_.SetActorParked(actor, true).has_value()); }
    slot->pending.reset();
    slot->lastSequence = 0;
    Require(slot->opportunity != eastl::numeric_limits<std::uint64_t>::max());
    ++slot->opportunity;
    slot->state = PlayerConnectionState::Parked;
    slot->grabbedObject = {};
    if (slot->activeRequest.value) { Require(world_.ReleaseRequest(slot->activeRequest).has_value()); }
    if (slot->residentRequest.value) { Require(world_.ReleaseRequest(slot->residentRequest).has_value()); }
    slot->activeRequest = {};
    slot->residentRequest = {};
    Require(world_.ReconcileChunks().has_value());
    world_.UpdatePlayerObservations();
    ++revision_;
    SynchronizePacing();
    return {};
}

CommandResult Server::Submit(PersistentId id, PlayerInput input) {
    PlayerSlot* slot = FindPlayer(id);
    if (!slot || slot->state != PlayerConnectionState::Active) {
        return {Outcome::unavailable, false, GetTick()};
    }
    if (!ValidCommandShape(input.command)) { return {Outcome::invalid_command, false, GetTick()}; }
    if (input.opportunity != slot->opportunity) { return {Outcome::stale_input, false, GetTick()}; }
    if (input.sequence <= slot->lastSequence) { return {Outcome::duplicate_input, false, GetTick()}; }
    const ActorHandle actor = PlayerActor(*slot);
    if (input.command.kind == CommandKind::new_character) {
        slot->lastSequence = input.sequence;
        const auto created = CreateFreshCharacter(id);
        if (!created) { ++revision_; return {Outcome::unavailable, false, GetTick()}; }
        return {Outcome::character_created, false, GetTick()};
    }
    if (!world_.IsActorAlive(actor)) {
        slot->lastSequence = input.sequence;
        ++revision_;
        return {Outcome::character_dead, false, GetTick()};
    }
    // A shorter grace may expire while a different player still holds the
    // round. Its automatic wait is already decided, even though no action can
    // resolve until the entire cohort is satisfied.
    if (!slot->pending && Ready(world_, actor)
        && !pacer_.IsIdle() && pacer_.GraceMicroseconds(actor) == 0) {
        return {Outcome::stale_input, false, GetTick()};
    }
    slot->lastSequence = input.sequence;
    if (!Ready(world_, actor) && ActiveWorkInterrupt(world_, actor, input.command)) {
        const auto result = Execute(actor, *slot, input.command);
        if (result.outcome == Outcome::craft_paused || result.outcome == Outcome::craft_cancelled
            || result.outcome == Outcome::study_paused || result.outcome == Outcome::art_study_paused
            || result.outcome == Outcome::machine_changed || result.outcome == Outcome::information_changed) {
            slot->pending.reset();
            Require(slot->opportunity != eastl::numeric_limits<std::uint64_t>::max());
            ++slot->opportunity;
            pacer_.RestartGrace(actor);
        }
        ++revision_;
        SynchronizePacing();
        return result;
    }
    slot->pending = input;
    ++revision_;
    SynchronizePacing();
    return {Outcome::queued, false, GetTick()};
}

bool Server::ResolveRound(SessionUpdate& result, eastl::span<const ActorHandle> expired) {
    StaticList<PlayerSlot*, maxOnlinePlayers> cohort;
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* actor = world_.GetActor(PlayerActor(slot));
        Require(actor);
        if (!Ready(world_, PlayerActor(slot))) { continue; }
        bool timedOut = false;
        for (ActorHandle handle : expired) {
            if (handle == PlayerActor(slot)) { timedOut = true; break; }
        }
        if (!slot.pending && !timedOut) { return false; }
        Require(cohort.PushBack(&slot));
    }
    if (cohort.Empty()) { return false; }
    eastl::sort(cohort.begin(), cohort.end(), [](const PlayerSlot* a, const PlayerSlot* b) {
        return a->id < b->id;
    });
    for (PlayerSlot* slot : cohort) {
        if (!Ready(world_, PlayerActor(*slot))) {
            if (slot->pending) {
                const auto input = *slot->pending;
                CommandResult dead{world_.IsActorAlive(PlayerActor(*slot)) ? Outcome::not_ready : Outcome::character_dead, false, GetTick()};
                result.commandResults.push_back({slot->id, input.sequence, input.opportunity, dead});
                if (slot->id == GetHostPlayerId()) { result.commandResult = dead; }
            }
            slot->pending.reset(); slot->grabbedObject = {};
            Require(slot->opportunity != UINT64_MAX); ++slot->opportunity; ++revision_;
            continue;
        }
        bool committed = false;
        if (slot->pending) {
            const PlayerInput input = *slot->pending;
            slot->pending.reset();
            CommandResult command = Execute(PlayerActor(*slot), *slot, input.command);
            result.commandResults.push_back({slot->id, input.sequence, input.opportunity, command});
            if (slot->id == GetHostPlayerId()) { result.commandResult = command; }
            if (command.storageError) { result.storageError = command.storageError; }
            committed = command.advanced;
        } else {
            Require(Execute(PlayerActor(*slot), *slot, {}).advanced);
            ++result.skippedActions;
            committed = true;
        }
        Require(slot->opportunity != eastl::numeric_limits<std::uint64_t>::max());
        ++slot->opportunity;
        if (committed) { pacer_.RestartGrace(PlayerActor(*slot)); }
        ++revision_;
    }
    SynchronizePacing();
    CollectCombatEvents();
    return true;
}

SessionResult<PersistentId> Server::CreateFreshCharacter(PersistentId id) {
    auto* slot = FindPlayer(id);
    if (!slot || slot->state != PlayerConnectionState::Active) { return eastl::unexpected(SessionError::InvalidPlayer); }
    const auto old = PlayerActor(*slot);
    if (!world_.GetActor(old) || world_.IsActorAlive(old)) { return eastl::unexpected(SessionError::Unavailable); }
    world_.FinalizeActorDeaths();
    const auto origin = world_.GetActor(old)->position;
    for (int radius = 0; radius <= 64; ++radius) {
        for (int dz = -radius; dz <= radius; ++dz) for (int dx = -radius; dx <= radius; ++dx) {
            if (eastl::max(std::abs(dx), std::abs(dz)) != radius) { continue; }
            const TileCoord position{origin.x + dx, origin.z + dz, origin.y};
            const auto* tile = world_.GetTile(position);
            if (!tile || world_.BlocksMovement(position) || world_.GetContent().GetTerrain(tile->terrain).openSpace
                || world_.ActorAt(position).value || !world_.IsActive(position)
                || !ResidencyFits(position, active_radius_, resident_radius_, circleResidency_)) { continue; }
            const auto created = world_.CreateActor(position, true);
            if (!created) { continue; }
            slot->character = world_.GetActor(*created)->id;
            if (slot->id == GetHostPlayerId()) { player_ = *created; }
            slot->pending.reset(); slot->grabbedObject = {}; slot->cachedMapRevision = 0; slot->cachedView = {};
            Require(slot->opportunity != UINT64_MAX); ++slot->opportunity;
            CommitPlayerBubble(*slot);
            Require(world_.ReconcileChunks().has_value());
            world_.UpdatePlayerObservations();
            ++revision_; SynchronizePacing(); CollectCombatEvents();
            return slot->character;
        }
    }
    return eastl::unexpected(SessionError::Unavailable);
}

void Server::CollectCombatEvents() {
    world_.Combat().DrainEvents(drainedCombatEvents_);
    constexpr std::size_t historyLimit = 128;
    if (drainedCombatEvents_.empty()) { return; }
    for (auto& event : drainedCombatEvents_) {
        Require(nextCombatEventSerial_ != UINT64_MAX);
        event.serial = nextCombatEventSerial_++;
        if (combatEvents_.size() == historyLimit) { combatEvents_.erase(combatEvents_.begin()); }
        combatEvents_.push_back(event);
    }
    ++revision_;
}

bool Server::SetPacing(PacingSettings settings) {
    if (!pacer_.SetSettings(settings)) { return false; }
    SynchronizePacing();
    ++revision_;
    return true;
}

PacingSettings Server::GetPacing() const { return pacer_.GetSettings(); }

eastl::optional<SaveError> Server::Autosave() {
    if (!world_.HasSave() || autosaveInterval_ == 0) { return {}; }
    // Keep the public autosave setting in turns, independent of action count.
    constexpr Tick maximum = eastl::numeric_limits<Tick>::max();
    const Tick interval = autosaveInterval_ > maximum / ticks_per_turn
        ? maximum : autosaveInterval_ * ticks_per_turn;
    if (GetTick() - lastSaveTick_ < interval) { return {}; }
    const auto saved = Save();
    if (!saved) { return saved.error(); }
    return {};
}

SessionUpdate Server::Update(std::uint64_t elapsedMicroseconds) {
    world_.FinalizeActorDeaths();
    SessionUpdate result;
    std::uint64_t remaining = elapsedMicroseconds;
    constexpr Tick maximum = eastl::numeric_limits<Tick>::max();
    for (;;) {
        // Equipment intent/topology can change without player movement. Settle
        // workshop/source demand before advancing and release completed holds at
        // the next session boundary, including an otherwise idle update.
        if (const auto residency = world_.ReconcileChunks(); !residency) {
            result.storageError = residency.error();
            break;
        }
        SynchronizePacing();
        if (pacer_.IsIdle()) { break; }

        const Tick credit = pacer_.AvailableTicks();
        const Tick before = GetTick();
        const Tick limit = credit > maximum - before ? maximum : before + credit;
        const SimulationResult simulation = cataclysmx::AdvanceUntilInput(world_, rng_state_, limit);
        if (GetTick() != before) { ++revision_; }
        pacer_.ConsumeTicks(GetTick() - before);
        if (const auto residency = world_.ReconcileChunks(); !residency) {
            result.storageError = residency.error();
            break;
        }
        SynchronizePacing();
        if (ResolveRound(result, pacer_.ExpiredPlayers())) { continue; }
        if (pacer_.IsIdle()) { break; }
        if (remaining == 0 || simulation.stop == SimulationStop::NoEvents) { break; }

        std::uint64_t untilEvent = maximum;
        if (simulation.stop == SimulationStop::LimitReached) {
            Require(GetTick() != maximum && simulation.nextEventIn != 0);
            untilEvent = pacer_.MicrosecondsUntilTicks(simulation.nextEventIn);
        }
        if (const auto deadline = pacer_.NextUnexpiredDeadlineMicroseconds()) {
            if (*deadline < untilEvent) { untilEvent = *deadline; }
        }
        const std::uint64_t step = remaining < untilEvent ? remaining : untilEvent;
        Require(step != 0);
        pacer_.ElapseMicroseconds(step);
        ++revision_;
        remaining -= step;
    }
    result.tick = GetTick();
    for (auto& slot : players_) {
        if (world_.IsActorAlive(PlayerActor(slot))) { continue; }
        slot.grabbedObject = {};
        if (!slot.pending) { continue; }
        const auto input = *slot.pending;
        CommandResult dead{Outcome::character_dead, false, GetTick()};
        result.commandResults.push_back({slot.id, input.sequence, input.opportunity, dead});
        if (slot.id == GetHostPlayerId()) { result.commandResult = dead; }
        slot.pending.reset();
        Require(slot.opportunity != UINT64_MAX); ++slot.opportunity; ++revision_;
    }
    CollectCombatEvents();
    result.needsInput = Ready(world_, player_);
    if (const auto error = Autosave()) {
        if (!result.storageError) { result.storageError = error; }
    }
    return result;
}

WorldView Server::View(int radius) const {
    return ViewForPlayer(GetHostPlayerId(), radius);
}

WorldView Server::ViewForPlayer(PersistentId id, int radius) const {
    const auto* slot = FindPlayer(id);
    const auto* actor = slot ? world_.GetActor(PlayerActor(*slot)) : nullptr;
    return actor ? ViewForPlayerAtLayer(id, radius, actor->position.y) : WorldView{};
}

WorldView Server::ViewForPlayerAtLayer(PersistentId id, int radius, int layerY) const {
    if (layerY < world_min_y || layerY > world_max_y) { return {}; }
    ++viewStats_.requests;
    // Radius is a viewport extent, independent of the fixed gameplay sight range.
    // Queries use current perception and existing memories without learning.
    radius = eastl::clamp(radius, 0, playerSightRange);
    const PlayerSlot* slot = FindPlayer(id);
    if (!slot) { return {}; }
    const ActorHandle player = PlayerActor(*slot);
    const Actor* actor = world_.GetActor(player);
    if (!actor) { return {}; }
    Require(actor != nullptr);
    const auto revision = GetRevision();
    if (slot->cachedView.revision == revision && slot->cachedView.width == radius * 2 + 1 && slot->cachedView.origin.y == layerY) {
        return slot->cachedView;
    }
    ++viewStats_.constructions;
    WorldView result;
    result.player_position = actor->position;
    if (const auto* object = world_.GetObject(slot->grabbedObject); object
        && object->position == TileCoord{actor->position.x + slot->grabOffset.x,
            actor->position.z + slot->grabOffset.z, actor->position.y}) {
        result.grabbedObject = object->id;
    }
    result.origin = {actor->position.x - radius, actor->position.z - radius, layerY};
    result.width = 2 * radius + 1;
    result.height = result.width;
    result.tick = GetTick();
    result.remainingTicks = world_.GetActorRemainingTicks(player);
    result.combat = world_.Combat().Describe(player);
    result.characterId = actor->id;
    world_.Combat().CollectInfo(player, result.combatInfo);
    for (const auto& event : combatEvents_) {
        if (event.actor == actor->id || event.opponent == actor->id
            || event.strike.attacker == actor->id || event.strike.target == actor->id) {
            result.combatEvents.push_back(event);
        }
    }
    result.speed = actor->speed;
    result.pacing = pacer_.GetSettings();
    result.commandQueued = slot->pending.has_value();
    result.playerId = id;
    result.revision = revision;
    result.opportunity = slot->opportunity;
    result.lastInputSequence = slot->lastSequence;
    result.players = GetPlayers();
    const bool observing = slot->state == PlayerConnectionState::Active
        && !actor->parked && world_.IsActive(actor->position);
    if (observing) { result.playerLight = ClassifyLight(world_.GetLightLevel(actor->position)); }
    const bool reuseMap = slot->cachedMapRevision == world_.GetMapRevision()
        && slot->cachedView.origin == result.origin && slot->cachedView.width == result.width
        && slot->cachedObserving == observing;
    if (reuseMap) {
        result.cells = slot->cachedView.cells;
    } else {
        ++viewStats_.mapConstructions;
        const auto* visibility = observing ? world_.GetPlayerVisibility(player) : nullptr;
        const auto observerLight = observing ? world_.GetLightLevel(actor->position) : std::uint8_t{0};
        const auto vision = world_.GetVisionProfile();
        int overlayRanges[4];
        for (int i = 0; i < 4; ++i) {
            const int range = VisionRange(vision, static_cast<LightBand>(i), observerLight);
            overlayRanges[i] = range;
        }
        // All World accesses below are read-only. Reuse chunk/lighting borrows
        // across each horizontal run instead of resolving them for every cell.
        eastl::array<const Chunk*, world_layer_count> columnChunks{};
        eastl::array<eastl::span<const std::uint8_t>, world_layer_count> columnLight{};
        eastl::array<bool, world_layer_count> columnLoaded{};
        ChunkCoord cachedColumn{world_coordinate_limit, world_coordinate_limit, 0};
        result.cells.resize(static_cast<std::size_t>(result.width * result.height));
        for (int z = 0; z < result.height; ++z) {
            for (int x = 0; x < result.width; ++x) {
                TileCoord position{result.origin.x + x, result.origin.z + z, result.origin.y};
                const auto column = ChunkOf({position.x, position.z, 0});
                if (column != cachedColumn) {
                    columnLoaded.fill(false);
                    cachedColumn = column;
                }
                ViewCell& cell = result.cells[static_cast<std::size_t>(z * result.width + x)];
                bool liveColumnOpen = observing;
                bool knownColumnOpen = true;
                for (unsigned int depth = 0; position.y >= world_min_y; ++depth, --position.y) {
                    // Live projection can see a lit lower tile through unlit air.
                    // Its geometry never controls the independent remembered path.
                    const int layer = position.y - world_min_y;
                    const Chunk* chunk = nullptr;
                    const Tile* current = nullptr;
                    int tileIndex = 0;
                    if (liveColumnOpen && ValidPosition(position)) {
                        if (!columnLoaded[layer]) {
                            const ChunkCoord coord{column.x, column.z, position.y};
                            columnChunks[layer] = world_.GetChunk(coord);
                            columnLight[layer] = columnChunks[layer] ? world_.GetChunkLightLevels(coord)
                                : eastl::span<const std::uint8_t>{};
                            columnLoaded[layer] = true;
                        }
                        chunk = columnChunks[layer];
                        tileIndex = TileIndex(position);
                        if (chunk) { current = &chunk->tiles[tileIndex]; }
                    }
                    const auto* terrain = current ? &world_.GetContent().GetTerrain(current->terrain) : nullptr;
                    const bool visible = current && visibility && visibility->Contains(position);
                    if (visible) {
                        cell.terrainVisibility = Visibility::Visible;
                        cell.tile = *current;
                        cell.terrainDepth = depth;
                        const auto level = columnLight[layer][tileIndex];
                        const auto spaceLight = ClassifyLight(level);
                        cell.terrainLight = terrain->blocksSight
                            ? ClassifyLight(TerrainLightLevel(world_, actor->position, position)) : spaceLight;
                        const int dx = position.x - actor->position.x;
                        const int dz = position.z - actor->position.z;
                        if (cell.overlayVisibility != Visibility::Visible
                            && WithinSightRange(dx*dx + dz*dz, overlayRanges[static_cast<int>(spaceLight)],
                                position.y == actor->position.y)) {
                            const Actor* occupant = chunk->actors.empty() ? nullptr : world_.GetActor(world_.ActorAt(position));
                            const auto items = chunk->ground_items.empty() ? 0U
                                : static_cast<unsigned int>(world_.CountItemsAt(position));
                            const InteractiveObject* object = chunk->objects.empty() ? nullptr : world_.GetObject(world_.ObjectAt(position));
                            if (occupant || items != 0 || object) {
                                // Memories are a fallback, never an occluder for an
                                // actually visible lower overlay. Nearest live wins.
                                cell.actor = occupant ? occupant->id : 0;
                                cell.player = occupant && occupant->player;
                                cell.monsterType = occupant && !occupant->player ? occupant->monsterType : MonsterTypeId{};
                                cell.ground_items = items;
                                cell.object = object ? object->id : 0;
                                cell.objectAppearance = object ? DescribeObject(*object) : ObjectAppearance{};
                                cell.overlayDepth = depth;
                                cell.overlayVisibility = Visibility::Visible;
                                cell.overlayLight = spaceLight;
                            }
                        }
                        knownColumnOpen = terrain->openSpace;
                    } else if (knownColumnOpen) {
                        const auto remembered = world_.GetRememberedTile(actor->id, position);
                        if (remembered.terrain.value == 0) {
                            knownColumnOpen = false;
                        } else {
                            cell.terrainVisibility = Visibility::Remembered;
                            cell.tile = {remembered.terrain, 0};
                            cell.terrainDepth = depth;
                            cell.terrainLight = LightBand::Dark;
                            if (cell.overlayVisibility == Visibility::Unknown && remembered.object.kind != ObjectKind::None) {
                                cell.objectAppearance = remembered.object;
                                cell.overlayDepth = depth;
                                cell.overlayVisibility = Visibility::Remembered;
                                cell.overlayLight = LightBand::Dark;
                            }
                            knownColumnOpen = world_.GetContent().GetTerrain(remembered.terrain).openSpace;
                        }
                    }
                    liveColumnOpen = terrain && terrain->openSpace;
                    if (!liveColumnOpen && !knownColumnOpen) { break; }
                }
            }
        }
    }
    // Damage and healing can change while the map projection is reused.
    result.powerSettings = world_.Power().Settings();
    const bool meter = observing && world_.CanInspectPower(player);
    if (meter) {
        result.currentPower.emplace();
        world_.Power().Inspect(actor->position, *result.currentPower);
    }
    // Recompute even when reusing terrain: carrying a meter and grid geometry
    // can change independently of the terrain projection cache.
    for (std::size_t i = 0; i < result.cells.size(); ++i) {
        const TileCoord position{result.origin.x + static_cast<int>(i % result.width),
            result.origin.z + static_cast<int>(i / result.width), layerY};
        const auto grid = meter ? world_.Power().GridAt(position) : eastl::optional<PowerGridId>{};
        result.cells[i].powerGridId = grid ? grid->value : 0;
    }
    if (observing && layerY == actor->position.y) { CollectEnemyHealth(world_, actor->combat.faction, result); }
    result.inventory = DescribeItems(world_, player, actor->inventory);
    result.wieldedItem = actor->wieldedItem.value ? world_.GetItem(actor->wieldedItem)->id : 0;
    ItemStorageView carried;
    carried.accessible = observing;
    carried.reason = observing ? InventoryOutcome::Succeeded : InventoryOutcome::Inaccessible;
    carried.massGrams = world_.GetCarriedMass(player);
    carried.massLimitGrams = actor->carryCapacityGrams;
    for (const auto handle : actor->inventory) { carried.volumeMl += world_.GetItemVolume(handle); }
    result.itemStorages.push_back(eastl::move(carried));
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const TileCoord position{actor->position.x + dx, actor->position.z + dz, actor->position.y};
            ItemStorageView floor;
            floor.storage = {ItemStorageKind::Ground, position};
            floor.accessible = observing && world_.CanAccessGroundStorage(player, position);
            floor.reason = floor.accessible ? InventoryOutcome::Succeeded : InventoryOutcome::Inaccessible;
            if (floor.accessible) {
                for (const auto handle : world_.ItemsAt(position)) {
                    floor.massGrams += world_.GetItemMass(handle);
                    floor.volumeMl += world_.GetItemVolume(handle);
                    AppendItemTreeDescription(world_, player, handle, result.nearbyItems);
                }
            }
            result.itemStorages.push_back(eastl::move(floor));
            const auto objectHandle = world_.ObjectAt(position);
            const auto* object = world_.GetObject(objectHandle);
            if (!object || !observing) { continue; }
            ItemStorageView storage;
            storage.storage = {ItemStorageKind::Object, {}, object->id};
            if (!world_.CanAccessStorage(player, storage.storage)) { continue; }
            const auto& definition = world_.GetContent().GetObject(object->type);
            storage.name = definition.name;
            storage.objectPosition = object->position;
            storage.accessible = true;
            storage.reason = InventoryOutcome::Succeeded;
            storage.volumeLimitMl = *definition.capacityVolumeMl;
            storage.volumeMl = world_.GetObjectUsedVolume(objectHandle);
            for (const auto handle : object->contents) {
                if (handle == object->equipment) { continue; }
                storage.massGrams += world_.GetItemMass(handle);
                AppendItemTreeDescription(world_, player, handle, result.nearbyItems);
            }
            result.itemStorages.push_back(eastl::move(storage));
        }
    }
    eastl::sort(result.nearbyItems.begin(), result.nearbyItems.end(),
        [](const InventoryEntry& a, const InventoryEntry& b) { return a.id < b.id; });
    for (const auto* entries : {&result.inventory, &result.nearbyItems}) {
        for (const auto& entry : *entries) {
            if (!entry.capacityVolumeMl || entry.installation.value) { continue; }
            ItemStorageView storage;
            storage.storage = {ItemStorageKind::Container, {}, entry.id};
            storage.accessible = observing && world_.CanAccessStorage(player, storage.storage);
            storage.reason = storage.accessible ? InventoryOutcome::Succeeded : InventoryOutcome::Inaccessible;
            storage.name = world_.GetContent().GetItem(entry.type).name;
            storage.volumeMl = entry.usedVolumeMl;
            storage.volumeLimitMl = entry.capacityVolumeMl;
            const auto* item = world_.GetItem(world_.FindItem(entry.id));
            for (const auto child : item->contents) { storage.massGrams += world_.GetItemMass(child); }
            result.itemStorages.push_back(eastl::move(storage));
        }
    }
    Vector<CraftingJobView> jobs;
    world_.Learning().CollectView(player, result.learning);
    world_.CollectCraftingJobs(player, jobs);
    result.craftingJobs.reserve(jobs.size());
    for (const auto& job : jobs) {
        result.craftingJobs.push_back({job.id, job.kind, job.craftingRecipe, job.recyclingRecipe,
            job.status, job.worker, job.totalWork, job.completedWork,
            job.fundedTicksRemaining, job.pauseRequested, job.stations});
    }
    world_.CollectStations(player, result.stations);
    world_.Machines().Collect(player, result.machines);
    world_.Information().Collect(player, result.informationDevices);
    if (observing) { result.groundItems = DescribeItems(world_, player, world_.ItemsAt(actor->position)); }
    result.carriedMassGrams = world_.GetCarriedMass(player);
    result.carryCapacityGrams = actor->carryCapacityGrams;
    slot->cachedMapRevision = world_.GetMapRevision();
    slot->cachedObserving = observing;
    slot->cachedView = result;
    return result;
}

WorldResult<void> Server::SetBubbleRadii(int active_radius, int resident_radius,
    Diagnostics* diagnostics) {
    return SetResidency(active_radius, resident_radius, {}, diagnostics);
}

WorldResult<void> Server::SetCircleResidency(int activeRadiusChunks, CircleResidency residency,
    Diagnostics* diagnostics) {
    return SetResidency(activeRadiusChunks, eastl::max(resident_radius_, activeRadiusChunks), residency, diagnostics);
}

eastl::optional<CircleResidency> Server::GetCircleResidency() const { return circleResidency_; }
int Server::GetActiveRadius() const { return active_radius_; }
int Server::GetResidentRadius() const { return resident_radius_; }

WorldResult<void> Server::SetResidency(int active_radius, int resident_radius,
    eastl::optional<CircleResidency> circle, Diagnostics* diagnostics) {
    if (active_radius < 0 || resident_radius < active_radius || resident_radius > 8
        || (circle && (circle->radiusTiles < 0 || circle->radiusTiles > maxCircleResidencyRadius))) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    for (const PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        Require(participant);
        if (!ResidencyFits(participant->position, active_radius, resident_radius, circle)) {
            return eastl::unexpected(WorldError::invalid_region);
        }
    }
    const int oldActive = active_radius_;
    const int oldResident = resident_radius_;
    const auto oldCircle = circleResidency_;
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        const ChunkCoord center = BubbleCenter(participant->position);
        const LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
        const LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
        Require(world_.UpdateRequest(active, {center, active_radius}, LoadMode::active).has_value());
        UpdateResident(world_, resident, participant->position, resident_radius, circle);
    }
    if (!world_.ReconcileChunks(diagnostics)) {
        for (const PlayerSlot& slot : players_) {
            if (slot.state != PlayerConnectionState::Active) { continue; }
            const LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
            const LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
            Require(world_.UpdateRequest(active, {slot.center, oldActive}, LoadMode::active).has_value());
            UpdateResident(world_, resident, slot.position, oldResident, oldCircle);
        }
        return eastl::unexpected(WorldError::storage_failure);
    }
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        Require(participant);
        slot.center = BubbleCenter(participant->position);
        slot.position = participant->position;
    }
    bubbleCenter_ = FindPlayer(GetHostPlayerId())->center;
    active_radius_ = active_radius;
    resident_radius_ = resident_radius;
    circleResidency_ = circle;
    ++revision_;
    return {};
}

void Server::CommitPlayerBubble(PlayerSlot& slot) {
    const Actor* actor = world_.GetActor(PlayerActor(slot));
    Require(actor != nullptr);
    const ChunkCoord center = BubbleCenter(actor->position);
    LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
    LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
    Require(world_.UpdateRequest(active, {center, active_radius_}, LoadMode::active).has_value());
    UpdateResident(world_, resident, actor->position, resident_radius_, circleResidency_);
    const TileCoord position = actor->position;
    slot.center = center;
    slot.position = position;
    if (slot.id == GetHostPlayerId()) { bubbleCenter_ = center; }
}

Server::Server(const ContentCatalog& content, EmptyTag) : world_(content) {}

SessionState Server::Checkpoint() const {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    SessionState session{GetTick(), rng_state_, actor->id, actor->position,
        active_radius_, resident_radius_, autosaveInterval_, pacer_.GetSettings()};
    session.circleResidency = circleResidency_;
    for (const PlayerSlot& slot : players_) {
        const auto character = slot.character ? slot.character : slot.id;
        const auto position = world_.FindPlayerPosition(character);
        Require(position.has_value());
        session.players.push_back({character, *position, slot.name, slot.token, slot.id});
    }
    return session;
}

SaveResult<void> Server::CreateSave(const char* path, Diagnostics* diagnostics) {
    const auto result = world_.CreateSave(path, Checkpoint(), diagnostics);
    if (result) {
        lastSaveTick_ = GetTick();
    }
    return result;
}

SaveResult<void> Server::Save(Diagnostics* diagnostics) {
    const auto result = world_.Save(Checkpoint(), diagnostics);
    if (result) {
        lastSaveTick_ = GetTick();
    }
    return result;
}

SaveResult<eastl::unique_ptr<Server>> Server::LoadSave(const ContentCatalog& content,
    const char* path, Diagnostics* diagnostics) {
    eastl::unique_ptr<Server> server(new Server(content, EmptyTag{}));
    const auto loaded = server->world_.LoadSave(path, diagnostics);
    if (!loaded) {
        if (diagnostics) { diagnostics->Add("Loading server world"); }
        return eastl::unexpected(loaded.error());
    }
    const SessionState& session = *loaded;
    if (session.player == 0 || session.activeRadius < 0
        || session.residentRadius < session.activeRadius || session.residentRadius > 8
        || !ResidencyFits(session.playerPosition, session.activeRadius, session.residentRadius, session.circleResidency)) {
        if (diagnostics) {
            diagnostics->Add("Invalid server session");
        }
        return eastl::unexpected(SaveError::InvalidData);
    }
    server->bubbleCenter_ = BubbleCenter(session.playerPosition);
    server->active_radius_ = session.activeRadius;
    server->resident_radius_ = session.residentRadius;
    server->circleResidency_ = session.circleResidency;
    server->active_request_ = Take(server->world_.RequestChunks(
        {server->bubbleCenter_, session.activeRadius}, LoadMode::active, LoadReason::Player));
    server->resident_request_ = RequestResident(server->world_, session.playerPosition,
        session.residentRadius, session.circleResidency);
    // Materialize the whole roster together: reconciliation settles gravity,
    // and guests may support each other or the host across saved layers.
    Vector<LoadRequest> staging;
    for (const SavedPlayer& saved : session.players) {
        // Guests return parked and only need their saved column. Their future
        // active/resident footprint is validated when activation is requested.
        if (!ValidPosition(saved.position)) {
            if (diagnostics) { diagnostics->Add("Saved player position exceeds world limits"); }
            return eastl::unexpected(SaveError::InvalidData);
        }
        if (saved.actor == session.player) { continue; }
        const auto request = server->world_.RequestChunks(
            {BubbleCenter(saved.position), 0}, LoadMode::resident, LoadReason::Player);
        if (!request) { return eastl::unexpected(SaveError::InvalidData); }
        staging.push_back(*request);
    }
    const auto reconciled = server->world_.ReconcileChunks(diagnostics);
    if (!reconciled) {
        return eastl::unexpected(reconciled.error());
    }
    server->player_ = server->world_.FindActor(session.player);
    const Actor* actor = server->world_.GetActor(server->player_);
    if (!actor || !actor->player || (actor->parked && server->world_.IsActorAlive(server->player_)) || actor->position != session.playerPosition) {
        if (diagnostics) {
            diagnostics->Add("Saved player does not match server session");
        }
        return eastl::unexpected(SaveError::InvalidData);
    }
    Vector<ActorHandle> guests;
    if (session.players.empty()) {
        server->hostPlayerId_ = session.player;
        server->players_.push_back({session.player, "Host", 0, session.playerPosition,
            PlayerConnectionState::Active, {}, {}, server->bubbleCenter_});
    } else {
        for (const SavedPlayer& saved : session.players) {
            PlayerSlot slot;
            slot.id = saved.session ? saved.session : saved.actor;
            slot.character = saved.actor;
            slot.name = saved.name;
            slot.token = saved.reconnectToken;
            slot.position = saved.position;
            slot.center = BubbleCenter(saved.position);
            slot.state = saved.actor == session.player
                ? PlayerConnectionState::Active : PlayerConnectionState::Parked;
            const ActorHandle handle = server->PlayerActor(slot);
            const Actor* value = server->world_.GetActor(handle);
            if (!value || !value->player || value->position != slot.position) {
                if (diagnostics) { diagnostics->Add("Saved player does not match roster"); }
                return eastl::unexpected(SaveError::InvalidData);
            }
            if (slot.state == PlayerConnectionState::Parked) {
                guests.push_back(handle);
            }
            slot.cachedView.combat = server->world_.Combat().Describe(handle);
            if (saved.actor == session.player) { server->hostPlayerId_ = slot.id; }
            server->players_.push_back(eastl::move(slot));
        }
    }
    // Validate every saved position before removing any guest support. Parking
    // is gravity-deferred so an upper guest cannot fall before it is parked.
    Require(server->world_.ParkActors({guests.data(), guests.size()}).has_value());
    for (LoadRequest request : staging) {
        Require(server->world_.ReleaseRequest(request).has_value());
    }
    const auto released = server->world_.ReconcileChunks(diagnostics);
    if (!released) { return eastl::unexpected(released.error()); }
    for (PlayerSlot& slot : server->players_) {
        const auto position = server->world_.FindPlayerPosition(slot.character ? slot.character : slot.id);
        Require(position.has_value());
        slot.position = *position;
        slot.center = BubbleCenter(*position);
    }
    server->bubbleCenter_ = BubbleCenter(server->world_.GetActor(server->player_)->position);
    eastl::sort(server->players_.begin(), server->players_.end(),
        [](const PlayerSlot& a, const PlayerSlot& b) { return a.id < b.id; });
    server->rng_state_ = session.rngState;
    server->autosaveInterval_ = session.autosaveInterval;
    server->lastSaveTick_ = session.tick;
    Require(server->pacer_.SetSettings(session.pacing));
    server->pacer_.Reset();
    server->SynchronizePacing();
    return eastl::move(server);
}

void Server::SetAutosaveInterval(std::uint64_t turns) { autosaveInterval_ = turns; }
std::uint64_t Server::GetAutosaveInterval() const { return autosaveInterval_; }

World& Server::GetWorld() { return world_; }
const World& Server::GetWorld() const { return world_; }
ActorHandle Server::GetPlayer() const { return player_; }
std::uint64_t Server::GetTick() const { return world_.GetTick(); }

}
