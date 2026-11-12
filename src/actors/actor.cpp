#include "actors/actor.h"
#include "actors/ai_controller.h"
#include <cstdlib>

namespace cataclysmx {

Actor::Actor(PersistentId id, TileCoord position, bool player)
    : id(id), position(position), player(player), combat(MakeCombatState(player)) {
    if (!player) {
        auto controller = cataclysmx::MakeAiController(AiControllerKind::Wander);
        if (!controller) { std::abort(); }
        controller_ = eastl::move(*controller);
    }
}

Actor::~Actor() = default;
Actor::Actor(Actor&&) noexcept = default;
Actor& Actor::operator=(Actor&&) noexcept = default;

}
