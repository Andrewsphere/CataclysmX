#include "core/lighting.h"
#include "core/perception.h"
#include "core/world.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

// Local u is the outward wall normal (exterior is negative); v runs along it.
TileCoord Position(int rotation, int u, int v, int y = 0) {
    switch (rotation) {
    case 0: return {u, v, y};
    case 1: return {-v, u, y};
    case 2: return {-u, -v, y};
    default: return {v, -u, y};
    }
}

void PrepareFacade(World& world, int rotation) {
    REQUIRE(world.RequestChunks({{0, 0}, 1}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    // Close the entire working area first, then explicitly open every exterior
    // sky column. The room is enclosed on all four sides and has a solid roof,
    // so the sky flood cannot find a path into it.
    for (int v = -12; v <= 12; ++v) {
        for (int u = -12; u <= 12; ++u) {
            REQUIRE(world.SetTile(Position(rotation, u, v), {TestTerrain("floor"), 0}));
            REQUIRE(world.SetTile(Position(rotation, u, v, 1), {TestTerrain("floor"), 0}));
            if (u < 0) {
                for (int y = 1; y <= world_max_y; ++y) {
                    REQUIRE(world.SetTile(Position(rotation, u, v, y), {TestTerrain("air"), 0}));
                }
            }
        }
    }
    for (int v = -10; v <= 10; ++v) {
        REQUIRE(world.SetTile(Position(rotation, 0, v), {TestTerrain("wall"), 0}));
        REQUIRE(world.SetTile(Position(rotation, 10, v), {TestTerrain("wall"), 0}));
    }
    for (int u = 1; u < 10; ++u) {
        for (int v : {-10, 10}) {
            REQUIRE(world.SetTile(Position(rotation, u, v), {TestTerrain("wall"), 0}));
        }
    }
}

TEST_CASE("Daylit continuous facades retain exposed faces at shallow and diagonal angles",
    "[lighting][surface][perception]") {
    for (int rotation = 0; rotation < 4; ++rotation) {
        CAPTURE(rotation);
        World world(TestContent());
        PrepareFacade(world, rotation);
        world.SetSkyLight(192);
        VisionProfile vision;
        vision.range = 16;
        REQUIRE(world.SetVisionProfile(vision));
        const auto wall = Position(rotation, 0, 0);
        REQUIRE(world.GetLightLevel(Position(rotation, -1, 0)) == 192);
        REQUIRE(world.GetLightLevel(Position(rotation, 1, 0)) == 0);
        CHECK(world.GetLightLevel(wall) == 0);
        CHECK(TerrainLightLevel(world, Position(rotation, 1, 0), wall) == 0);

        // Both signs along the facade, a normal view, and both exact ties.
        const int views[][2] = {{-2, -8}, {-2, 8}, {-8, 0}, {-6, -6}, {-6, 6}};
        for (const auto& view : views) {
            const auto observer = Position(rotation, view[0], view[1]);
            CAPTURE(view[0], view[1]);
            REQUIRE(LineVisible(world, observer, wall));
            CHECK(TerrainLightLevel(world, observer, wall) == 192);
            CHECK(CanSeeTile(world, observer, wall, vision));
            VisibilityMask mask;
            PerceptionStats stats;
            BuildVisibilityMask(world, observer, vision.range, mask, stats);
            CHECK(mask.Contains(wall));
            const auto player = world.CreateActor(observer, true);
            REQUIRE(player);
            CHECK(world.CanPlayerSee(*player, wall));
            REQUIRE(world.DestroyActor(*player));
        }

        // Lighting alone must not bypass planar occlusion.
        const auto observer = Position(rotation, -8, 0);
        REQUIRE(world.SetTile(Position(rotation, -4, 0), {TestTerrain("wall"), 0}));
        CHECK(TerrainLightLevel(world, observer, wall) == 192);
        CHECK_FALSE(LineVisible(world, observer, wall));
        CHECK_FALSE(CanSeeTile(world, observer, wall, vision));
        const auto player = world.CreateActor(observer, true);
        REQUIRE(player);
        CHECK_FALSE(world.CanPlayerSee(*player, wall));
    }
}

TEST_CASE("An interior lamp cannot lend light to an obliquely viewed exterior face",
    "[lighting][surface][perception]") {
    for (int rotation = 0; rotation < 4; ++rotation) {
        CAPTURE(rotation);
        World world(TestContent());
        PrepareFacade(world, rotation);
        const auto wall = Position(rotation, 0, 0);
        const auto lamp = world.CreateObject(Position(rotation, 1, 0), true);
        REQUIRE(lamp);
        REQUIRE(world.SetObjectLight(*lamp, {255, 1}));
        REQUIRE(world.GetLightLevel(wall) > 0); // The opposite endpoint is lit.
        CHECK(TerrainLightLevel(world, Position(rotation, 2, 0), wall) == 255);
        REQUIRE(world.GetLightLevel(Position(rotation, -1, 0)) == 0);
        for (int along : {-8, 8}) {
            CAPTURE(along);
            const auto observer = Position(rotation, -2, along);
            REQUIRE(LineVisible(world, observer, wall));
            CHECK(TerrainLightLevel(world, observer, wall) == 0);
            CHECK_FALSE(CanSeeTile(world, observer, wall));
        }
    }
}

TEST_CASE("Daylight through a missing facade reveals the interior beyond dim sight",
    "[lighting][surface][perception]") {
    World world(TestContent());
    PrepareFacade(world, 0);
    world.SetSkyLight(192);
    VisionProfile vision;
    vision.range = 16;
    REQUIRE(world.SetVisionProfile(vision));
    const TileCoord observer{-8,0,0};
    // At reach one only the missing facade itself receives spill light.
    const TileCoord interior{skySpillDistance >= 2 ? 1 : 0,0,0};
    const auto player = world.CreateActor(observer, true);
    REQUIRE(player);
    CHECK(world.GetLightLevel(interior) == 0);
    CHECK_FALSE(world.CanPlayerSee(*player, interior));

    for (int z = -9; z <= 9; ++z) {
        REQUIRE(world.SetTile({0,z,0}, {TestTerrain("floor"),0}));
    }
    for (int x = 0; x <= 8; ++x) {
        CAPTURE(x);
        const int distance = x+1; // The nearest direct-sky column is x = -1.
        const int expected = distance <= skySpillDistance
            ? 128*(skySpillDistance+1-distance)/skySpillDistance : 0;
        CHECK(world.GetLightLevel({x,0,0}) == expected);
    }
    CHECK(CanSeeTile(world, observer, interior, vision));
    CHECK(world.CanPlayerSee(*player, interior));
    // Find a dim cell in the room when this reach produces one. Very short
    // reaches can transition straight from ordinary light to darkness.
    for (int x = 1; x <= 8; ++x) {
        const int distance = x+1;
        const int level = distance <= skySpillDistance
            ? 128*(skySpillDistance+1-distance)/skySpillDistance : 0;
        if (level >= 16 && level < 64) {
            const TileCoord dim{x,0,0};
            CHECK(ClassifyLight(world.GetLightLevel(dim)) == LightBand::Dim);
            CHECK_FALSE(CanSeeTile(world, observer, dim, vision));
            CHECK(CanSeeTile(world, {x-1,0,0}, dim, vision));
            break;
        }
    }

    for (int z = -9; z <= 9; ++z) {
        REQUIRE(world.SetTile({0,z,0}, {TestTerrain("wall"),0}));
    }
    CHECK(world.GetLightLevel(interior) == 0);
    CHECK_FALSE(world.CanPlayerSee(*player, interior));
}
}
