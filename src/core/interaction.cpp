#include "core/interaction.h"

#include <cstdlib>

namespace cataclysmx {
bool ValidInteraction(const InteractionRequest& request) {
    if (!ValidPosition(request.position) || request.kind > InteractionKind::Unlock) { return false; }
    if (request.kind == InteractionKind::Object) {
        return request.object != 0 && request.terrain.value == 0 && request.action >= ObjectAction::Start && request.action <= ObjectAction::Disable;
    }
    return request.object == 0 && request.terrain.value != 0 && request.action == ObjectAction::Start;
}
void QueryInteractions(const World& world, ActorHandle handle, TileCoord position,
    Vector<InteractionChoice>& output) {
    const auto* actor = world.GetActor(handle);
    if (!actor || actor->parked || !ValidPosition(position) || !world.CanPlayerSee(handle, position)) { return; }
    const auto a = actor->position;
    if (a.y != position.y || eastl::max(std::abs(a.x - position.x), std::abs(a.z - position.z)) > 1) { return; }
    const auto* tile = world.GetTile(position);
    const auto* source = world.GetTile(a);
    if (!tile || !source || !world.IsActive(position)) { return; }
    const auto& terrain = world.GetContent().GetTerrain(tile->terrain);
    if (terrain.opensTo.value || terrain.closesTo.value || terrain.locksTo.value || terrain.unlocksTo.value) {
        output.push_back({{position, 0, tile->terrain, InteractionKind::Open}, terrain.name, "Open",
            terrain.unlocksTo.value ? ObjectUseReason::Locked
                : terrain.opensTo.value ? ObjectUseReason::None : ObjectUseReason::AlreadyOpen});
        output.push_back({{position, 0, tile->terrain, InteractionKind::Close}, terrain.name, "Close",
            !terrain.closesTo.value ? ObjectUseReason::AlreadyClosed
                : world.ActorAt(position).value ? ObjectUseReason::Occupied : ObjectUseReason::None});
    }
    if (terrain.locksTo.value || terrain.unlocksTo.value) {
        output.push_back({{position, 0, tile->terrain, InteractionKind::Lock}, terrain.name, "Lock",
            terrain.unlocksTo.value ? ObjectUseReason::AlreadyLocked
                : terrain.closesTo.value ? ObjectUseReason::MustClose : ObjectUseReason::None});
        output.push_back({{position, 0, tile->terrain, InteractionKind::Unlock}, terrain.name, "Unlock",
            terrain.unlocksTo.value ? ObjectUseReason::None : ObjectUseReason::AlreadyUnlocked});
    }
    const auto objectHandle = world.ObjectAt(position);
    const auto* object = world.GetObject(objectHandle);
    if (!object) { return; }
    const String name = object->type.value ? world.GetContent().GetObject(object->type).name : String("Object");
    Vector<ObjectActionInfo> actions;
    world.QueryObjectActions(handle, objectHandle, actions);
    for (const auto& action : actions) {
        output.push_back({{position, object->id, {}, InteractionKind::Object, action.action}, name,
            ObjectActionLabel(action.action), action.reason});
    }
}
}
