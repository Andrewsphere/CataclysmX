#include "sdl/app.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SDL gameplay dispatch preserves axial directions and action kinds", "[sdl][input]") {
    using namespace cataclysmx;
    for (auto action : {InputAction::West, InputAction::East, InputAction::North,
        InputAction::South, InputAction::MoveDown, InputAction::MoveUp}) {
        const auto move = sdl::GameplayCommand(action, false);
        const auto interact = sdl::GameplayCommand(action, true);
        REQUIRE(move); REQUIRE(interact);
        CHECK(ValidCommandShape(*move)); CHECK(ValidCommandShape(*interact));
        CHECK(move->kind == CommandKind::move);
        CHECK(interact->kind == CommandKind::interact);
        CHECK(move->dx == interact->dx); CHECK(move->dy == interact->dy); CHECK(move->dz == interact->dz);
    }
    CHECK(sdl::GameplayCommand(InputAction::West, false)->dx == -1);
    CHECK(sdl::GameplayCommand(InputAction::East, false)->dx == 1);
    CHECK(sdl::GameplayCommand(InputAction::North, false)->dz == -1);
    CHECK(sdl::GameplayCommand(InputAction::South, false)->dz == 1);
    CHECK(sdl::GameplayCommand(InputAction::MoveDown, false)->dy == -1);
    CHECK(sdl::GameplayCommand(InputAction::MoveUp, false)->dy == 1);
    CHECK(sdl::GameplayCommand(InputAction::Wait, false)->kind == CommandKind::wait);
    CHECK(sdl::GameplayCommand(InputAction::PickUp, false)->kind == CommandKind::pick_up);
    CHECK(sdl::GameplayCommand(InputAction::Drop, false)->kind == CommandKind::drop);
    CHECK_FALSE(sdl::GameplayCommand(InputAction::Help, false));
    CHECK_FALSE(sdl::GameplayCommand(InputAction::Cancel, true));
}
