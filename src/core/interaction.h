#pragma once

#include "core/world.h"

namespace cataclysmx {
enum class InteractionKind : std::uint8_t { Open, Close, Object, Lock, Unlock };
struct InteractionRequest {
    TileCoord position{};
    PersistentId object = 0;
    TerrainId terrain{};
    InteractionKind kind = InteractionKind::Open;
    ObjectAction action = ObjectAction::Start;
    bool operator==(const InteractionRequest&) const = default;
};
struct InteractionChoice {
    InteractionRequest request;
    String target;
    String label;
    ObjectUseReason reason = ObjectUseReason::None;
};
bool ValidInteraction(const InteractionRequest& request);
// Appends visible targets in immediate same-layer eight-neighbor reach, preserving provider action order.
void QueryInteractions(const World& world, ActorHandle actor, TileCoord position,
    Vector<InteractionChoice>& output);
}
