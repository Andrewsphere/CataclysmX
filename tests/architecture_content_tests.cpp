#include "content/content.h"
#include "core/world.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Architectural windows admit sight independently of movement", "[content][architecture][windows]") {
    Diagnostics diagnostics;
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(content);
    World world(*content);
    REQUIRE(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    const auto player = world.CreateActor({2, 2}, true);
    REQUIRE(player);
    REQUIRE(world.SetTile({3, 2}, {content->FindTerrain("window"), 0}));
    CHECK(world.CanPlayerSee(*player, {4, 2}));
    CHECK_FALSE(world.TryActorStep(*player, {1, 0}));
    CHECK(world.GetActor(*player)->position == TileCoord{2, 2});
    REQUIRE(world.SetTile({3, 2}, {content->FindTerrain("window_curtained"), 0}));
    CHECK_FALSE(world.CanPlayerSee(*player, {4, 2}));
    CHECK_FALSE(world.TryActorStep(*player, {1, 0}));
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("Architectural materials provide distinct supported floor and opaque wall roles", "[content][architecture]") {
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(content);
    for (const char* name : {"wall_wood", "wall_brick", "wall_concrete", "wall_metal", "wall_plaster"}) {
        const auto* terrain = content->TryGetTerrain(content->FindTerrain(name));
        REQUIRE(terrain);
        CHECK_FALSE(terrain->passable);
        CHECK(terrain->blocksSight);
        CHECK_FALSE(terrain->openSpace);
    }
    for (const char* name : {"floor_wood", "floor_tile", "floor_concrete"}) {
        const auto* terrain = content->TryGetTerrain(content->FindTerrain(name));
        REQUIRE(terrain);
        CHECK(terrain->passable);
        CHECK_FALSE(terrain->blocksSight);
        CHECK_FALSE(terrain->openSpace);
        CHECK_FALSE(terrain->stairs);
    }
}
}
