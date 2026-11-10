#include "core/world.h"
#include "core/perception.h"
#include "core/lighting.h"
#include "core/planar_visibility.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <chrono>
#include <cstdio>

namespace {
using namespace cataclysmx;

void Prepare(World& world) {
    REQUIRE(world.RequestChunks({{0,0},0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    for (int y = 0; y <= 1; ++y) {
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = 0; x < chunk_size; ++x) {
                REQUIRE(world.SetTile({x,z,y}, {TestTerrain("floor"),0}));
            }
        }
    }
}
ObjectHandle Lamp(World& world, TileCoord position, LightEmission light) {
    auto result = world.CreateObject(position, true);
    REQUIRE(result);
    REQUIRE(world.SetObjectLight(*result, light));
    return *result;
}

TEST_CASE("Light falloff is planar circular, max combined and bounded with two-step spill", "[lighting]") {
    World world(TestContent());
    Prepare(world);
    const auto lamp = Lamp(world, {5,5,0}, {255,1});
    CHECK(world.GetLightLevel({5,5,0}) == 255);
    CHECK(world.GetLightLevel({6,5,0}) == 127);
    CHECK(world.GetLightLevel({7,5,0}) == 31);
    CHECK(world.GetLightLevel({8,5,0}) == 15);
    CHECK(world.GetLightLevel({9,5,0}) == 0);
    for (int i = 0; i < 5; ++i) { CHECK(world.GetLightLevel({9,5,0}) == 0); }
    const auto second = Lamp(world, {5,6,0}, {255,1});
    CHECK(world.GetLightLevel({5,5,0}) == 255);
    REQUIRE(world.DestroyObject(second));
    REQUIRE(world.SetObjectLight(lamp, {64,1}));
    CHECK(world.GetLightLevel({6,5,0}) == 32);
    REQUIRE(world.SetObjectLight(lamp, {64,12}));
    CHECK(world.GetLightLevel({11,5,0}) >= 34);
    CHECK(world.SetObjectLight(lamp, {64,13}) == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.SetObjectEnabled(lamp, false));
    CHECK(world.GetLightLevel({5,5,0}) == 0);
    REQUIRE(world.SetObjectEnabled(lamp, true));
    REQUIRE(world.DestroyObject(lamp));
    CHECK(world.GetLightLevel({5,5,0}) == 0);
    CHECK(world.GetLightLevel({16,5,0}) == 0);
    Lamp(world, {4,5,0}, {64,1});
    Lamp(world, {6,5,0}, {64,1});
    CHECK(world.GetLightLevel({5,5,0}) == 32); // Two equal beams do not add.
}

TEST_CASE("Inactive resident sources illuminate and unloading retires their contribution", "[lighting]") {
    World world(TestContent());
    auto request = world.RequestChunks({{0,0},0}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    const auto lamp = Lamp(world, {5,5,0}, {192,1});
    CHECK(world.GetLightLevel({5,5,0}) == 192);
    REQUIRE(world.ReleaseRequest(*request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetLightLevel({5,5,0}) == 0);
    REQUIRE(world.RequestChunks({{0,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetLightLevel({5,5,0}) == 192);
    CHECK(world.GetObject(lamp) == nullptr); // Restored source has a fresh handle.
}

TEST_CASE("Walls and doors cast shadows and opaque lit faces cannot seed spill", "[lighting]") {
    World world(TestContent());
    Prepare(world);
    Lamp(world, {4,8,0}, {255,12});
    for (int z = 0; z < chunk_size; ++z) {
        REQUIRE(world.SetTile({7,z,0}, {TestTerrain("wall"),0}));
    }
    CHECK(world.GetLightLevel({7,8,0}) > 63);
    CHECK(world.GetLightLevel({8,8,0}) == 0);
    REQUIRE(world.SetTile({7,8,0}, {TestTerrain("open_door"),0}));
    CHECK(world.GetLightLevel({10,8,0}) > 63);
    REQUIRE(world.SetTile({7,8,0}, {TestTerrain("closed_door"),0}));
    CHECK(world.GetLightLevel({10,8,0}) == 0);
    REQUIRE(world.SetTile({7,8,0}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({7,7,0}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({7,9,0}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({6,8,0}, {TestTerrain("wall"),0}));
    CHECK(world.GetLightLevel({10,8,0}) <= 63); // Pillar umbra, at most indirect.
}

TEST_CASE("Floors block vertical light while matching stairs open their face", "[lighting][perception]") {
    World world(TestContent());
    Prepare(world);
    Lamp(world, {5,5,0}, {255,6});
    CHECK_FALSE(LineVisible(world, {5,5,0}, {5,5,1}));
    CHECK(world.GetLightLevel({5,5,1}) == 0);
    REQUIRE(world.SetTile({5,5,0}, {TestTerrain("stairs"),0}));
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("stairs"),0}));
    CHECK(OpenVerticalFace(world, {5,5,0}, {5,5,1}));
    CHECK(world.GetLightLevel({5,5,1}) == 63);
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("air"),0}));
    CHECK_FALSE(LineVisible(world, {5,5,0}, {5,5,1})); // Perception never projects upward.
    CHECK(world.GetLightLevel({5,5,1}) == 63); // Transport still crosses the open face.
}

TEST_CASE("Sky enters only through open top-down faces and respects roofs", "[lighting]") {
    World world(TestContent());
    Prepare(world);
    for (int y = 1; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({5,5,y}, {TestTerrain("air"),0}));
    }
    world.SetSkyLight(192);
    CHECK(world.GetLightLevel({5,5,0}) == 192);
    REQUIRE(world.SetTile({5,5,2}, {TestTerrain("floor"),0}));
    CHECK(world.GetLightLevel({5,5,0}) < 192);
    world.SetSkyLight(0);
    CHECK(world.GetLightLevel({5,5,0}) == 0);
}

TEST_CASE("A downward column view can receive sky on a lower wall top", "[lighting][surface]") {
    World world(TestContent());
    Prepare(world);
    REQUIRE(world.SetTile({5,5,0}, {TestTerrain("wall"),0}));
    for (int y = 1; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({5,5,y}, {TestTerrain("air"),0}));
    }
    world.SetSkyLight(192);
    CHECK(TerrainLightLevel(world, {5,5,3}, {5,5,0}) == 192);
    CHECK(world.GetIncidentLightLevel({5,5,0}, 4) == 192);
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("floor"),0}));
    CHECK(TerrainLightLevel(world, {5,5,3}, {5,5,0}) == 0);
}

TEST_CASE("Resident actor sources move, park, unpark and disappear deterministically", "[lighting]") {
    World world(TestContent());
    Prepare(world);
    auto player = world.CreateActor({4,4,0}, true);
    REQUIRE(player);
    REQUIRE(world.SetActorLight(*player, {192,1}));
    CHECK(world.GetLightLevel({4,4,0}) == 192);
    REQUIRE(world.MoveActor(*player, {10,10,0}));
    CHECK(world.GetLightLevel({4,4,0}) == 0);
    CHECK(world.GetLightLevel({10,10,0}) == 192);
    REQUIRE(world.SetActorParked(*player, true));
    CHECK(world.GetLightLevel({10,10,0}) == 0);
    REQUIRE(world.SetActorParked(*player, false));
    CHECK(world.GetLightLevel({10,10,0}) == 192);
    REQUIRE(world.DestroyActor(*player));
    CHECK(world.GetLightLevel({10,10,0}) == 0);
}

TEST_CASE("Remote projected wall tops are lit independently of sides and upper air", "[lighting][surface]") {
    World world(TestContent());
    Prepare(world);
    const TileCoord observer{2,5,4};
    const TileCoord target{12,5,0};
    REQUIRE(world.SetTile(observer, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile(target, {TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({11,5,0}, {TestTerrain("wall"),0}));
    for (int y = 1; y <= 4; ++y) {
        REQUIRE(world.SetTile({12,5,y}, {TestTerrain("air"),0}));
    }
    Lamp(world, {12,5,1}, {255,1});
    CHECK(world.GetLightLevel({12,5,4}) == 0);
    CHECK_FALSE(CanSeeTile(world, observer, {12,5,4})); // Dark upper air is outside dark range.
    CHECK(LineVisible(world, observer, target));
    CHECK(TerrainLightLevel(world, observer, target) == 255);
    CHECK(CanSeeTile(world, observer, target)); // No upper-air illumination filter.
    CHECK(TerrainLightLevel(world, {2,5,0}, target) == 0); // Side remains dark.
    REQUIRE(world.SetTile({12,5,1}, {TestTerrain("floor"),0}));
    CHECK_FALSE(LineVisible(world, observer, target));
    CHECK(TerrainLightLevel(world, observer, target) == 0);
    CHECK_FALSE(CanSeeTile(world, observer, target));
}

TEST_CASE("Perception uses target light and restricts only dim targets under glare", "[lighting][perception]") {
    World world(TestContent());
    Prepare(world);
    const TileCoord observer{4,4,0};
    const TileCoord target{8,4,0};
    const auto glare = Lamp(world, observer, {255,1});
    const auto targetLamp = Lamp(world, target, {32,1});
    CHECK_FALSE(CanSeeTile(world, observer, target));
    REQUIRE(world.SetObjectEnabled(glare, false));
    CHECK(CanSeeTile(world, observer, target)); // Dark observer, dim target.
    REQUIRE(world.SetObjectEnabled(glare, true));
    REQUIRE(world.SetObjectLight(targetLamp, {192,1}));
    CHECK(CanSeeTile(world, observer, target)); // Lit target unaffected by glare.
    REQUIRE(world.SetObjectEnabled(targetLamp, false));
    CHECK_FALSE(CanSeeTile(world, observer, target));
    CHECK(CanSeeTile(world, observer, observer));
    CHECK(ValidVisionProfile({}));
    VisionProfile invalid;
    invalid.darkRange = 7;
    CHECK_FALSE(ValidVisionProfile(invalid));
    invalid = {};
    invalid.range = playerSightRange + 1;
    CHECK_FALSE(ValidVisionProfile(invalid));
}

TEST_CASE("Spill crosses negative chunk seams and residency changes refresh lookup hints", "[lighting]") {
    World world(TestContent());
    auto request = world.RequestChunks({{0,0},1}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    for (int y = 0; y <= 1; ++y) {
        for (int z = -4; z <= 3; ++z) {
            for (int x = -4; x <= 3; ++x) {
                REQUIRE(world.SetTile({x,z,y}, {TestTerrain("floor"),0}));
            }
        }
    }
    auto lamp = Lamp(world, {-1,-1,0}, {255,1});
    const auto id = world.GetObject(lamp)->id;
    CHECK(world.GetLightLevel({0,-1,0}) == 127);
    CHECK(world.GetLightLevel({1,-1,0}) == 31);
    CHECK(world.GetLightLevel({2,-1,0}) == 15);
    CHECK(world.GetLightLevel({-1,0,0}) == 127);
    CHECK(world.GetLightLevel({-1,1,0}) == 31);
    CHECK(world.GetLightLevel({-1,2,0}) == 15);
    REQUIRE(world.UpdateRequest(*request, {{0,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetLightLevel({-1,-1,0}) == 0);
    CHECK(world.GetLightLevel({0,0,0}) == 0);
    REQUIRE(world.UpdateRequest(*request, {{0,0},1}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetLightLevel({1,-1,0}) == 31);
    REQUIRE(world.DestroyObject(world.FindObject(id)));
    Lamp(world, {0,0,0}, {255,1});
    CHECK(world.GetLightLevel({-1,0,0}) == 127);
    CHECK(world.GetLightLevel({-2,0,0}) == 31);
    CHECK(world.GetLightLevel({-3,0,0}) == 15);
    CHECK(world.GetLightLevel({0,-1,0}) == 127);
    CHECK(world.GetLightLevel({0,-2,0}) == 31);
    CHECK(world.GetLightLevel({0,-3,0}) == 15);
}

TEST_CASE("Vertical spill uses open faces and emitter gravity invalidates cached light", "[lighting]") {
    World world(TestContent());
    Prepare(world);
    for (int y = 0; y <= 4; ++y) {
        REQUIRE(world.SetTile({5,5,y}, {TestTerrain(y ? "air" : "floor"),0}));
    }
    auto lamp = Lamp(world, {5,5,0}, {255,1});
    CHECK(world.GetLightLevel({5,5,1}) == 63);
    CHECK(world.GetLightLevel({5,5,2}) == 31);
    CHECK(world.GetLightLevel({5,5,3}) == 0);
    CHECK(world.GetLightLevel({5,5,4}) == 0);
    REQUIRE(world.SetTile({5,5,2}, {TestTerrain("floor"),0}));
    CHECK(world.GetLightLevel({5,5,2}) == 0); // A closed upper floor face blocks spill.
    CHECK(world.GetLightLevel({5,5,1}) == 63);
    REQUIRE(world.DestroyObject(lamp));
    auto actor = world.CreateActor({5,5,2}, true);
    REQUIRE(actor);
    REQUIRE(world.SetActorLight(*actor, {192,1}));
    CHECK(world.GetLightLevel({5,5,2}) == 192);
    REQUIRE(world.SetTile({5,5,2}, {TestTerrain("air"),0}));
    CHECK(world.GetActor(*actor)->position == TileCoord{5,5,0});
    CHECK(world.GetLightLevel({5,5,0}) == 192);
    CHECK(world.GetLightLevel({5,5,2}) == 24);
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("floor"),0}));
    CHECK(world.GetLightLevel({5,5,1}) == 0);
    CHECK(world.GetLightLevel({5,5,2}) == 0);
}

TEST_CASE("Daylit facade receives full incident sky beyond dim sight without lighting its interior", "[lighting][surface]") {
    World world(TestContent());
    Prepare(world);
    REQUIRE(world.RequestChunks({{0,0},1}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    for (int x = -15; x < 0; ++x) {
        REQUIRE(world.SetTile({x,8,0}, {TestTerrain("floor"),0}));
        REQUIRE(world.SetTile({x,8,1}, {TestTerrain("floor"),0}));
    }
    for (int z = 0; z < chunk_size; ++z) {
        REQUIRE(world.SetTile({13,z,0}, {TestTerrain("wall"),0}));
        REQUIRE(world.SetTile({15,z,0}, {TestTerrain("wall"),0}));
    }
    // Enclose the ends as well: eight-step sky spill can now reach the middle
    // of this corridor from a neighboring chunk if either end is left open.
    REQUIRE(world.SetTile({14,0,0}, {TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({14,15,0}, {TestTerrain("wall"),0}));
    for (int y = 1; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({12,8,y}, {TestTerrain("air"),0}));
    }
    world.SetSkyLight(192);
    const TileCoord observer{-15,8,0};
    const TileCoord wall{13,8,0};
    CHECK(LineVisible(world, observer, wall));
    CHECK(world.GetLightLevel(wall) == 0); // Roofed solid has no direct sky endpoint.
    CHECK(TerrainLightLevel(world, observer, wall) == 192);
    CHECK(CanSeeTile(world, observer, wall));
    CHECK(world.GetLightLevel({14,8,0}) == 0);
    CHECK(TerrainLightLevel(world, {14,8,0}, wall) == 0);
    CHECK(TerrainLightLevel(world, observer, {12,8,0}) == world.GetLightLevel({12,8,0}));
}

TEST_CASE("An opposite-side lamp cannot illuminate a wall face or relay through it", "[lighting][surface]") {
    World world(TestContent());
    Prepare(world);
    for (int z = 0; z < chunk_size; ++z) {
        REQUIRE(world.SetTile({8,z,0}, {TestTerrain("wall"),0}));
    }
    const TileCoord wall{8,8,0};
    const auto lamp = Lamp(world, {9,8,0}, {255,1});
    CHECK(world.GetLightLevel(wall) == 127); // Preserve the existing direct endpoint field.
    CHECK(TerrainLightLevel(world, {12,8,0}, wall) == 255);
    CHECK(TerrainLightLevel(world, {2,8,0}, wall) == 0);
    CHECK(world.GetLightLevel({7,8,0}) == 0);
    CHECK(world.GetLightLevel({6,8,0}) == 0);
    REQUIRE(world.SetObjectEnabled(lamp, false));
    CHECK(TerrainLightLevel(world, {12,8,0}, wall) == 0);
    REQUIRE(world.SetObjectEnabled(lamp, true));
    CHECK(TerrainLightLevel(world, {12,8,0}, wall) == 255);
    CHECK(world.GetLightLevel({7,8,0}) == 0); // Surface queries never seed propagation.
}

TEST_CASE("Surface receivers combine exposed observer-facing sides at shallow angles and corners", "[lighting][surface]") {
    World world(TestContent());
    Prepare(world);
    const TileCoord wall{8,8,0};
    REQUIRE(world.SetTile(wall, {TestTerrain("wall"),0}));
    Lamp(world, {7,8,0}, {255,1});
    CHECK(TerrainLightLevel(world, {2,7,0}, wall) == 255);
    CHECK(TerrainLightLevel(world, {7,2,0}, wall) == 255); // Non-dominant exposed X face.
    CHECK(TerrainLightLevel(world, {9,2,0}, wall) == world.GetLightLevel({8,7,0}));
    CHECK(TerrainLightLevel(world, {2,2,0}, wall) == 255); // Both accessible faces.
    REQUIRE(world.SetTile({8,7,0}, {TestTerrain("wall"),0}));
    CHECK(LineVisible(world, {2,2,0}, wall)); // Shared shadowcast permits this corner.
    CHECK(TerrainLightLevel(world, {2,2,0}, wall) == 255); // Blocked Z face cannot veto exposed X.
    CHECK(TerrainLightLevel(world, {2,8,0}, wall) == 255);
    REQUIRE(world.SetTile({5,8,0}, {TestTerrain("wall"),0}));
    CHECK_FALSE(LineVisible(world, {2,8,0}, wall)); // Caller rejects hidden terrain.
}

TEST_CASE("Opaque vertical surfaces respect roofs floors and matching stair openings", "[lighting][surface]") {
    World world(TestContent());
    Prepare(world);
    const TileCoord wall{5,5,0};
    REQUIRE(world.SetTile(wall, {TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("air"),0}));
    REQUIRE(world.SetTile({5,5,2}, {TestTerrain("air"),0}));
    Lamp(world, {5,5,1}, {255,1});
    CHECK(LineVisible(world, {5,5,2}, wall));
    CHECK(TerrainLightLevel(world, {5,5,2}, wall) == 255); // Exposed upper face.
    CHECK(TerrainLightLevel(world, {5,5,-1}, wall) == 0); // Never borrow above a roof.
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("floor"),0}));
    CHECK_FALSE(LineVisible(world, {5,5,2}, wall));
    CHECK(TerrainLightLevel(world, {5,5,2}, wall) == 0); // Closed floor above target.

    REQUIRE(world.SetTile(wall, {TestTerrain("stairs"),0}));
    CHECK(TerrainLightLevel(world, {5,5,2}, wall) == world.GetLightLevel(wall));
    CHECK(world.GetLightLevel(wall) == 0); // Unmatched stairs do not cross the floor.
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("stairs"),0}));
    CHECK(world.GetLightLevel(wall) == 63);
    CHECK(TerrainLightLevel(world, {5,5,2}, wall) == 63);

    REQUIRE(world.SetTile(wall, {TestTerrain("wall"),0}));
    for (int y = 1; y <= 3; ++y) {
        for (int z = 2; z <= 5; ++z) {
            for (int x = 2; x <= 5; ++x) {
                REQUIRE(world.SetTile({x,z,y}, {TestTerrain("air"),0}));
            }
        }
    }
    CHECK(LineVisible(world, {2,2,3}, wall));
    CHECK(TerrainLightLevel(world, {2,2,3}, wall) == 255); // Projected top face.
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("floor"),0}));
    CHECK_FALSE(LineVisible(world, {2,2,3}, wall));
    CHECK(TerrainLightLevel(world, {2,2,3}, wall) == 0); // Closed top face.
}

TEST_CASE("Surface samples are residency clipped across negative seams and world bounds", "[lighting][surface]") {
    World world(TestContent());
    const auto request = world.RequestChunks({{0,0},1}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    REQUIRE(world.SetTile({-1,5,0}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({0,5,0}, {TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({-1,5,1}, {TestTerrain("floor"),0}));
    Lamp(world, {-1,5,0}, {255,1});
    CHECK(TerrainLightLevel(world, {-1,5,0}, {0,5,0}) == 255);
    REQUIRE(world.UpdateRequest(*request, {{0,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK(TerrainLightLevel(world, {-1,5,0}, {0,5,0}) == 0);
    CHECK(TerrainLightLevel(world, {0,5,0}, {-1,5,0}) == 0);
    CHECK(TerrainLightLevel(world, {0,5,world_max_y+1}, {0,5,0}) == 0);
    CHECK(TerrainLightLevel(world, {0,5,0}, {0,5,world_min_y-1}) == 0);
    CHECK(TerrainLightLevel(world, {0,5,0}, {0,5,0}) == 0);
}

TEST_CASE("Lower walls use lit tops rather than obsolete diagonal floor ties", "[lighting][surface]") {
    for (const int y : {-1, 2}) {
        CAPTURE(y);
        World world(TestContent());
        Prepare(world);
        const TileCoord observer{4,4,y};
        const TileCoord sample{5,4,y-1};
        const TileCoord target{6,4,y-1};
        REQUIRE(world.SetTile(observer, {TestTerrain("floor"),0}));
        REQUIRE(world.SetTile({4,4,y-1}, {TestTerrain("floor"),0}));
        REQUIRE(world.SetTile(sample, {TestTerrain("floor"),0}));
        REQUIRE(world.SetTile(target, {TestTerrain("wall"),0}));
        REQUIRE(world.SetTile({5,4,y}, {TestTerrain("air"),0}));
        REQUIRE(world.SetTile({6,4,y}, {TestTerrain("air"),0}));
        Lamp(world, {6,4,y}, {192,1});
        CHECK(LineVisible(world, observer, sample)); // Downward columns have no diagonal floor tie.
        CHECK(LineVisible(world, observer, target));
        CHECK(TerrainLightLevel(world, observer, target) == 192);
        CHECK(CanSeeTile(world, observer, target));
        CHECK(TerrainLightLevel(world, {7,4,y-1}, target) < 64);
    }
}

void CheckFields(const World& world, const LightingField& incremental) {
    LightingField fresh;
    fresh.Rebuild(world);
    for (const auto coord : world.LoadedChunks()) {
        const auto actual = incremental.GetChunkLevels(coord);
        const auto expected = fresh.GetChunkLevels(coord);
        REQUIRE(actual.size() == chunk_size*chunk_size);
        REQUIRE(eastl::equal(actual.begin(), actual.end(), expected.begin()));
        for (int i = 0; i < chunk_size*chunk_size; ++i) {
            const TileCoord p{coord.x*chunk_size+i%chunk_size,coord.z*chunk_size+i/chunk_size,coord.y};
            for (int face = 0; face < 6; ++face) {
                REQUIRE(incremental.GetIncident(p, face) == fresh.GetIncident(p, face));
            }
        }
    }
}

template <typename Range>
void CheckUniqueCoords(const Range& coords) {
    for (auto it = coords.begin(); it != coords.end(); ++it) {
        REQUIRE(eastl::find(it + 1, coords.end(), *it) == coords.end());
    }
}

template <typename Left, typename Right>
void CheckCoordSets(const Left& left, const Right& right) {
    REQUIRE(left.size() == right.size());
    CheckUniqueCoords(left);
    CheckUniqueCoords(right);
    for (const auto coord : left) {
        REQUIRE(eastl::find(right.begin(), right.end(), coord) != right.end());
    }
}

TEST_CASE("Uniform sky shortcuts match independent full configured transport", "[lighting][incremental]") {
    World world(TestContent());
    Prepare(world);
    for (int y = 1; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({5,5,y}, {TestTerrain("air"),0}));
    }
    REQUIRE(world.SetTile({6,6,0}, {TestTerrain("wall"),0}));
    LightingField field;
    const auto verify = [&] {
        struct ReferenceChunk {
            ChunkCoord position;
            eastl::array<std::uint8_t,chunk_size*chunk_size> direct{};
            eastl::array<std::uint8_t,chunk_size*chunk_size> reached{};
            eastl::array<std::uint8_t,chunk_size*chunk_size> next{};
            eastl::array<std::uint8_t,chunk_size*chunk_size> level{};
        };
        Vector<ReferenceChunk> reference;
        for (const auto coord : world.LoadedChunks()) { reference.push_back({coord}); }
        const auto find = [&](TileCoord p) -> ReferenceChunk* {
            for (auto& chunk : reference) { if (chunk.position == ChunkOf(p)) { return &chunk; } }
            return nullptr;
        };
        const auto transmitting = [&](TileCoord p) {
            const auto* tile = world.GetTile(p);
            return tile && !world.GetContent().GetTerrain(tile->terrain).blocksSight;
        };
        const TileCoord offsets[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        const auto connected = [&](TileCoord p, TileCoord q) {
            return transmitting(p) && transmitting(q)
                && (p.y == q.y || OpenVerticalFace(world,p,q));
        };
        for (const auto& chunk : reference) {
            if (chunk.position.y != world_max_y) { continue; }
            for (int i = 0; i < chunk_size*chunk_size; ++i) {
                TileCoord p{chunk.position.x*chunk_size+i%chunk_size,
                    chunk.position.z*chunk_size+i/chunk_size,world_max_y};
                for (;;) {
                    auto* current = find(p);
                    if (!current || !world.GetTile(p)) { break; }
                    current->direct[TileIndex(p)] = world.GetSkyLight();
                    if (!transmitting(p)) { break; }
                    const TileCoord below{p.x,p.z,p.y-1};
                    if (!OpenVerticalFace(world,below,p)) { break; }
                    p = below;
                }
            }
        }
        for (auto& chunk : reference) {
            chunk.level = chunk.direct;
            chunk.reached = chunk.direct;
        }
        // Full synchronous world-wide reachability passes, independent of the
        // production bounded patch/FIFO. Keep direct-lit cells in every pass.
        for (int distance = 1; distance <= skySpillDistance; ++distance) {
            for (auto& chunk : reference) { chunk.next = chunk.reached; }
            for (auto& chunk : reference) {
                for (int i = 0; i < chunk_size*chunk_size; ++i) {
                    if (!chunk.reached[i]) { continue; }
                    const TileCoord p{chunk.position.x*chunk_size+i%chunk_size,
                        chunk.position.z*chunk_size+i/chunk_size,chunk.position.y};
                    for (const auto offset : offsets) {
                        const TileCoord q{p.x+offset.x,p.z+offset.z,p.y+offset.y};
                        if (!connected(p,q)) { continue; }
                        auto* next = find(q);
                        REQUIRE(next);
                        next->next[TileIndex(q)] = 1;
                    }
                }
            }
            for (auto& chunk : reference) {
                chunk.reached = chunk.next;
                for (int i = 0; i < chunk_size*chunk_size; ++i) {
                    if (chunk.reached[i]) {
                        chunk.level[i] = eastl::max(chunk.level[i],
                            static_cast<std::uint8_t>(2*world.GetSkyLight()*(skySpillDistance+1-distance)/(3*skySpillDistance)));
                    }
                }
            }
        }
        for (const auto& chunk : reference) {
            field.EnsureChunk(world, chunk.position);
            const auto actual = field.GetChunkLevels(chunk.position);
            REQUIRE(eastl::equal(actual.begin(),actual.end(),chunk.level.begin()));
            for (int i = 0; i < chunk_size*chunk_size; ++i) {
                const TileCoord p{chunk.position.x*chunk_size+i%chunk_size,
                    chunk.position.z*chunk_size+i/chunk_size,chunk.position.y};
                for (int face = 0; face < 6; ++face) {
                    const auto incident = face == 4 && !transmitting(p) ? chunk.direct[i] : 0;
                    REQUIRE(field.GetIncident(p,face) == incident);
                }
            }
        }
    };
    for (const int sky : {0,1,2,3,4,7,8,11,12,23,24,95,96,191,192,255,0}) {
        CAPTURE(sky);
        world.SetSkyLight(static_cast<std::uint8_t>(sky));
        field.Prepare(world);
        CHECK(field.GetWorkStats().outputChunks == 0);
        field.EnsureChunk(world, {0,0,0});
        CHECK(field.Get({5,5,0}) == sky);
        for (int distance = 1; distance < chunk_size-5; ++distance) {
            CHECK(field.Get({5+distance,5,0}) == (distance <= skySpillDistance
                ? 2*sky*(skySpillDistance+1-distance)/(3*skySpillDistance) : 0));
        }
        verify();
    }
    world.SetSkyLight(192);
    field.Rebuild(world);
    REQUIRE(world.SetTile({5,5,2}, {TestTerrain("floor"),0}));
    field.Rebuild(world);
    verify();
    CheckFields(world, field);
    REQUIRE(world.SetTile({5,5,2}, {TestTerrain("air"),0}));
    field.Rebuild(world);
    verify();
    CheckFields(world, field);
}

TEST_CASE("Sky follows corner path distance and open vertical faces", "[lighting]") {
    World world(TestContent());
    Prepare(world);
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            REQUIRE(world.SetTile({x,z,0}, {TestTerrain("wall"),0}));
            REQUIRE(world.SetTile({x,z,1}, {TestTerrain("wall"),0}));
        }
    }
    const TileCoord path[] = {{2,2,0},{3,2,0},{4,2,0},{5,2,0},{6,2,0},
        {7,2,0},{7,3,0},{7,4,0},{7,4,1},{8,4,1}};
    for (const auto p : path) { REQUIRE(world.SetTile(p, {TestTerrain("floor"),0})); }
    REQUIRE(world.SetTile({7,4,0}, {TestTerrain("stairs"),0}));
    REQUIRE(world.SetTile({7,4,1}, {TestTerrain("stairs"),0}));
    REQUIRE(world.SetTile({7,4,2}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({8,4,2}, {TestTerrain("floor"),0}));
    for (int y = 1; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({2,2,y}, {TestTerrain("air"),0}));
    }
    world.SetSkyLight(192);
    const auto expected = [](int distance) {
        if (distance == 0) { return 192; }
        return distance <= skySpillDistance ? 128*(skySpillDistance+1-distance)/skySpillDistance : 0;
    };
    for (int d = 0; d <= 9; ++d) {
        CAPTURE(d);
        const int level = expected(d);
        CHECK(world.GetLightLevel(path[d]) == level);
        const auto band = level < 16 ? LightBand::Dark : level < 64 ? LightBand::Dim
            : level < 192 ? LightBand::Ordinary : LightBand::Bright;
        CHECK(ClassifyLight(world.GetLightLevel(path[d])) == band);
    }
    REQUIRE(world.SetTile({7,4,1}, {TestTerrain("floor"),0}));
    CHECK(world.GetLightLevel(path[8]) == 0); // Unmatched lower stairs cannot cross a floor.
    REQUIRE(world.SetTile({7,4,1}, {TestTerrain("stairs"),0}));
    CHECK(world.GetLightLevel(path[8]) == expected(8));
    REQUIRE(world.SetTile({4,2,0}, {TestTerrain("closed_door"),0}));
    CHECK(world.GetLightLevel(path[7]) == 0);
    REQUIRE(world.SetTile({4,2,0}, {TestTerrain("open_door"),0}));
    CHECK(world.GetLightLevel(path[8]) == expected(8));
    const auto lamp = Lamp(world, path[1], {8,1});
    CHECK(world.GetLightLevel(path[1]) == expected(1)); // Local and sky contributions do not add.
    REQUIRE(world.SetObjectLight(lamp, {255,1}));
    CHECK(world.GetLightLevel(path[1]) == 255);
    REQUIRE(world.DestroyObject(lamp));
    CHECK(world.GetLightLevel(path[1]) == expected(1));
    // A directly sky-lit opaque endpoint is still never a transport seed.
    REQUIRE(world.SetTile(path[0], {TestTerrain("wall"),0}));
    CHECK(world.GetLightLevel(path[0]) == 192);
    CHECK(world.GetLightLevel(path[1]) == 0);
}

TEST_CASE("Sky reach measures the connected detour rather than displacement through a wall", "[lighting][incremental]") {
    World world(TestContent());
    Prepare(world);
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            REQUIRE(world.SetTile({x,z,0}, {TestTerrain("wall"),0}));
            REQUIRE(world.SetTile({x,z,1}, {TestTerrain("wall"),0}));
        }
    }
    const TileCoord path[] = {{2,2,0},{3,2,0},{4,2,0},{5,2,0},{6,2,0},
        {6,3,0},{6,4,0},{6,5,0},{5,5,0},{4,5,0}};
    for (const auto p : path) { REQUIRE(world.SetTile(p, {TestTerrain("floor"),0})); }
    for (int y = 1; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({2,2,y}, {TestTerrain("air"),0}));
    }
    world.SetSkyLight(192);
    const auto expected = [](int distance) {
        return distance <= skySpillDistance ? 128*(skySpillDistance+1-distance)/skySpillDistance : 0;
    };
    CHECK(world.GetLightLevel({5,5,0}) == expected(8)); // Eight hops around the corner.
    CHECK(world.GetLightLevel({4,5,0}) == expected(9)); // Nine hops, despite displacement five.
    REQUIRE(world.SetTile({4,3,0}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({4,4,0}, {TestTerrain("open_door"),0}));
    CHECK(world.GetLightLevel({4,5,0}) == expected(5)); // The new shortest path is five hops.
    REQUIRE(world.SetTile({4,4,0}, {TestTerrain("closed_door"),0}));
    CHECK(world.GetLightLevel({4,5,0}) == expected(9));
}

TEST_CASE("Configured sky reach preserves falloff and lazy dependencies across negative seams", "[lighting][incremental][lazy]") {
    World world(TestContent());
    const auto request = world.RequestChunks({{0,0},1}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    // Seal the corridor's sides and roof, including the source's negative chunk.
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = -chunk_size; x < chunk_size; ++x) {
            REQUIRE(world.SetTile({x,z,0}, {TestTerrain("wall"),0}));
            REQUIRE(world.SetTile({x,z,1}, {TestTerrain("floor"),0}));
        }
    }
    for (int x = -1; x <= skySpillDistance; ++x) {
        REQUIRE(world.SetTile({x,5,0}, {TestTerrain("floor"),0}));
    }
    for (int y = 1; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({-1,5,y}, {TestTerrain("air"),0}));
    }
    LightingField field;
    const ChunkCoord receiving{0,0,0};
    const TileCoord lastStep{skySpillDistance-1,5,0};
    const TileCoord beyond{skySpillDistance,5,0};
    // Exercise the actual transported field, including low intensities whose
    // rounded-to-zero tail shortens the internal flood. This also runs with a
    // separately configured reach to catch byte wraparound beyond step nine.
    for (const int sky : {0,1,2,3,11,12,23,24,191,192,255}) {
        CAPTURE(sky, skySpillDistance);
        world.SetSkyLight(static_cast<std::uint8_t>(sky));
        field.Prepare(world);
        field.EnsureChunk(world, receiving);
        CHECK(field.GetWorkStats().outputChunks == 1);
        CHECK(field.Get({0,5,0}) == 2*sky/3);
        CHECK(field.Get(lastStep) == 2*sky/(3*skySpillDistance));
        CHECK(field.Get(beyond) == 0);
        int previous = 2*sky/3;
        for (int x = 1; x < skySpillDistance; ++x) {
            const int level = field.Get({x,5,0});
            CHECK(level <= previous);
            previous = level;
        }
    }
    world.SetSkyLight(192);
    const int lastLevel = 128/skySpillDistance;
    field.Prepare(world);
    field.EnsureChunk(world, receiving);
    CHECK(field.GetWorkStats().outputChunks == 1);
    CHECK(field.GetWorkStats().sourcePatches == 0);
    CHECK(field.Get(lastStep) == lastLevel);
    CHECK(field.Get(beyond) == 0);
    CHECK(field.GetWorkStats().skyFirstChunks == 1);
    const auto lamp = Lamp(world, {-1,5,0}, {64,1});
    field.Prepare(world);
    field.EnsureChunk(world, receiving);
    CHECK(field.GetWorkStats().outputChunks == 1);
    CHECK(field.GetWorkStats().skyDirectChunks == 0);
    CHECK(field.GetWorkStats().skyFirstChunks == 0); // Emitter edits reuse complete sky patches.
    CHECK(field.GetWorkStats().sourcePatches == 1);
    CHECK(field.Get(lastStep) == lastLevel);
    REQUIRE(world.DestroyObject(lamp));
    field.Prepare(world);
    field.EnsureChunk(world, receiving);
    CHECK(field.GetWorkStats().skyFirstChunks == 0);
    auto revision = field.GetChunkRevision(receiving);
    // The source output was never ensured; closure must invalidate its consumer.
    REQUIRE(world.SetTile({-1,5,1}, {TestTerrain("floor"),0}));
    field.Prepare(world);
    CHECK(field.GetChunkRevision(receiving) > revision);
    CHECK(field.GetWorkStats().outputChunks == 0);
    revision = field.GetChunkRevision(receiving);
    field.Prepare(world);
    CHECK(field.GetChunkRevision(receiving) == revision);
    field.EnsureChunk(world, receiving);
    CHECK(field.Get(lastStep) == 0);
    CHECK(field.GetWorkStats().outputChunks == 1);
    REQUIRE(world.SetTile({-1,5,1}, {TestTerrain("air"),0}));
    field.Prepare(world);
    field.EnsureChunk(world, receiving);
    CHECK(field.Get(lastStep) == lastLevel);
    // Unloading the seed column retires cached direct dependencies as well.
    REQUIRE(world.UpdateRequest(*request, {{0,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    field.Prepare(world);
    field.EnsureChunk(world, receiving);
    CHECK(field.Get(lastStep) == 0);
    CHECK(field.GetWorkStats().outputChunks == 1);
    REQUIRE(world.UpdateRequest(*request, {{0,0},1}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    field.Prepare(world);
    field.EnsureChunk(world, receiving);
    CHECK(field.Get(lastStep) == lastLevel);
    for (const auto coord : world.LoadedChunks()) { field.EnsureChunk(world, coord); }
    CheckFields(world, field);
}

TEST_CASE("Chunk input and output revisions track localized changes independently", "[lighting][incremental]") {
    World world(TestContent());
    Prepare(world);
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("air"),0}));
    const ChunkCoord local{0,0,0};
    const ChunkCoord receiving{0,0,1};
    const ChunkCoord upper{0,0,world_max_y};
    const ChunkCoord absent{99,99,0};
    LightingField field;
    CHECK(field.ChangedChunks().empty());
    CHECK(field.GetChunkRevision(local) == 0);
    CHECK(world.GetLightingInputRevision(absent) == 0);
    const auto rebuild = [&] {
        Vector<std::uint64_t> old;
        for (const auto coord : world.LoadedChunks()) { old.push_back(field.GetChunkRevision(coord)); }
        field.Rebuild(world);
        const auto changed = field.ChangedChunks();
        const auto loaded = world.LoadedChunks();
        CheckUniqueCoords(changed);
        for (const auto coord : changed) {
            REQUIRE(eastl::find(loaded.begin(), loaded.end(), coord) != loaded.end());
        }
        std::size_t index = 0;
        for (const auto coord : world.LoadedChunks()) {
            const bool dirty = eastl::find(changed.begin(), changed.end(), coord) != changed.end();
            const auto revision = field.GetChunkRevision(coord);
            REQUIRE(revision != 0);
            if (dirty) { REQUIRE(revision > old[index]); }
            else { REQUIRE(revision == old[index]); }
            ++index;
        }
        REQUIRE(field.GetChunkRevision(absent) == 0);
        CheckFields(world, field);
    };
    rebuild();
    const auto input = world.GetLightingInputRevision(local);
    REQUIRE(input != 0);
    const auto upperInput = world.GetLightingInputRevision(upper);
    const auto upperOutput = field.GetChunkRevision(upper);
    const auto receivingInput = world.GetLightingInputRevision(receiving);
    const auto receivingOutput = field.GetChunkRevision(receiving);
    rebuild();
    CHECK(field.ChangedChunks().empty());
    CHECK(world.GetLightingInputRevision(local) == input);
    const auto lamp = Lamp(world, {5,5,0}, {192,1});
    CHECK(world.GetLightingInputRevision(local) > input);
    rebuild();
    CHECK(field.GetChunkRevision(upper) == upperOutput); // Moving sources do not rebuild sky.
    CHECK(world.GetLightingInputRevision(receiving) == receivingInput);
    CHECK(field.GetChunkRevision(receiving) > receivingOutput); // Received spill changed without a local input edit.
    CHECK(field.Get({5,5,1}) == 48);
    auto previous = world.GetLightingInputRevision(local);
    REQUIRE(world.SetObjectEnabled(lamp, false));
    CHECK(world.GetLightingInputRevision(local) > previous);
    rebuild();
    previous = world.GetLightingInputRevision(local);
    REQUIRE(world.DestroyObject(lamp));
    CHECK(world.GetLightingInputRevision(local) == previous); // Disabled source no longer contributes.
    rebuild();
    CHECK(field.ChangedChunks().empty());
    const auto actor = world.CreateActor({5,5,0}, true);
    REQUIRE(actor);
    REQUIRE(world.SetActorLight(*actor, {255,1}));
    rebuild();
    previous = world.GetLightingInputRevision(local);
    REQUIRE(world.MoveActor(*actor, {10,5,0}));
    CHECK(world.GetLightingInputRevision(local) > previous);
    rebuild();
    CHECK(field.GetChunkRevision(upper) == upperOutput);
    previous = world.GetLightingInputRevision(local);
    REQUIRE(world.SetActorParked(*actor, true));
    CHECK(world.GetLightingInputRevision(local) > previous);
    rebuild();
    previous = world.GetLightingInputRevision(local);
    REQUIRE(world.SetTile({8,5,0}, {TestTerrain("wall"),0}));
    CHECK(world.GetLightingInputRevision(local) > previous);
    rebuild();
    CHECK(field.GetChunkRevision(upper) > upperOutput); // Conservative vertical sky column refresh.
    CHECK(world.GetLightingInputRevision(upper) == upperInput);
}

TEST_CASE("Residency changed chunks include removed lighting and fresh-equivalent survivors", "[lighting][incremental]") {
    World world(TestContent());
    const auto request = world.RequestChunks({{0,0},1}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    REQUIRE(world.SetTile({-1,5,0}, {TestTerrain("floor"),0}));
    Lamp(world, {-1,5,0}, {255,1});
    LightingField field;
    field.Rebuild(world);
    const auto loaded = world.LoadedChunks();
    const Vector<ChunkCoord> original(loaded.begin(), loaded.end());
    const auto initialRevision = field.GetChunkRevision({0,0,0});
    REQUIRE(world.UpdateRequest(*request, {{0,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    field.Rebuild(world);
    CheckCoordSets(original, field.ChangedChunks());
    CHECK(field.GetChunkRevision({-1,0,0}) == 0);
    CHECK(world.GetLightingInputRevision({-1,0,0}) == 0);
    CHECK(field.GetChunkRevision({0,0,0}) > initialRevision);
    CheckFields(world, field);
    REQUIRE(world.UpdateRequest(*request, {{0,0},1}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    field.Rebuild(world);
    CheckCoordSets(original, field.ChangedChunks());
    CHECK(field.Get({-1,5,0}) == 255);
    CheckFields(world, field);
    REQUIRE(world.ReleaseRequest(*request));
    REQUIRE(world.ReconcileChunks());
    field.Rebuild(world);
    CheckCoordSets(original, field.ChangedChunks());
    CHECK(field.GetChunkRevision({0,0,0}) == 0);
    CheckFields(world, field);
    field.Rebuild(world);
    CHECK(field.ChangedChunks().empty());
}

TEST_CASE("Residency reconciliation ignores insertion history and publishes mixed invalidations", "[lighting][incremental][lazy]") {
    World world(TestContent());
    Prepare(world);
    REQUIRE(world.SetTile({6,5,0}, {TestTerrain("wall"),0}));
    const auto lamp = Lamp(world, {5,5,0}, {192,1});
    LightingField history;
    LightingField direct;
    // Populate one field before adding the other columns; the second field
    // encounters the same final residents together on its first Prepare.
    history.Prepare(world);
    const auto remote = world.RequestChunks({{-4,0},0}, LoadMode::resident);
    REQUIRE(remote);
    REQUIRE(world.RequestChunks({{2,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    history.Prepare(world);
    direct.Prepare(world);
    const ChunkCoord local{0,0,0};
    const ChunkCoord pending{0,0,1};
    const ChunkCoord removed{-4,0,0};
    const ChunkCoord added{4,0,0};
    history.EnsureChunk(world, local);
    direct.EnsureChunk(world, local);
    REQUIRE(history.Get({5,5,0}) == 192);
    REQUIRE(history.GetIncident({6,5,0}, 1) == 96);
    const auto historyLocal = history.GetChunkRevision(local);
    const auto directLocal = direct.GetChunkRevision(local);
    const auto historyPending = history.GetChunkRevision(pending);
    const auto directPending = direct.GetChunkRevision(pending);
    REQUIRE(historyPending != 0);
    REQUIRE(directPending != 0);
    const auto old = world.LoadedChunks();

    REQUIRE(world.UpdateRequest(*remote, {{4,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    const auto current = world.LoadedChunks();
    REQUIRE(old.size() == current.size());
    Vector<ChunkCoord> expected = old;
    for (const auto coord : current) {
        if (eastl::find(expected.begin(), expected.end(), coord) == expected.end()) { expected.push_back(coord); }
    }
    // Sky and source invalidations overlap on the surviving column while the
    // equally sized remote resident set is replaced in the same Prepare.
    world.SetSkyLight(1);
    REQUIRE(world.SetObjectEnabled(lamp, false));
    history.Prepare(world);
    direct.Prepare(world);
    CheckCoordSets(history.ChangedChunks(), expected);
    CheckCoordSets(direct.ChangedChunks(), expected);
    CheckCoordSets(history.ChangedChunks(), direct.ChangedChunks());
    for (const auto coord : old) {
        if (coord.x == -4) {
            CHECK(history.GetChunkRevision(coord) == 0);
            CHECK(direct.GetChunkRevision(coord) == 0);
        }
    }
    for (const auto coord : current) {
        CHECK(history.GetChunkRevision(coord) != 0);
        CHECK(direct.GetChunkRevision(coord) != 0);
    }
    CHECK(history.GetChunkRevision(removed) == 0);
    CHECK(history.GetChunkRevision(added) != 0);
    CHECK(history.GetChunkRevision(local) > historyLocal);
    CHECK(direct.GetChunkRevision(local) > directLocal);
    CHECK(history.GetChunkRevision(pending) > historyPending);
    CHECK(direct.GetChunkRevision(pending) > directPending);
    const auto pendingRevision = history.GetChunkRevision(pending);
    const auto retainedRevision = history.GetChunkRevision(local);
    history.Prepare(world);
    CHECK(history.ChangedChunks().empty());
    CHECK(history.GetChunkRevision(pending) == pendingRevision);
    CHECK(history.GetChunkRevision(local) == retainedRevision);
    for (const auto coord : current) {
        history.EnsureChunk(world, coord);
        direct.EnsureChunk(world, coord);
    }
    CHECK(history.GetChunkRevision(pending) == pendingRevision);
    CHECK(history.GetChunkRevision(local) == retainedRevision);
    CHECK(history.Get({5,5,0}) == 0);
    CHECK(history.GetIncident({6,5,0}, 1) == 0);
    CheckFields(world, history);
    CheckFields(world, direct);
}

TEST_CASE("Same-coordinate reload reuses lighting patches and restored emitter edits invalidate visibility", "[lighting][incremental][perception][streaming]") {
    World world(TestContent());
    const auto request = world.RequestChunks({{0,0},0}, LoadMode::active);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    for (int x = 4; x <= 12; ++x) {
        for (int y = 0; y <= 1; ++y) {
            REQUIRE(world.SetTile({x,5,y}, {TestTerrain("floor"),0}));
        }
    }
    VisionProfile vision;
    vision.range = 12;
    REQUIRE(world.SetVisionProfile(vision));
    const TileCoord target{12,5,0};
    const auto observer = world.CreateActor({4,5,0}, true);
    REQUIRE(observer);
    const auto lamp = Lamp(world, target, {192,1});
    const auto observerId = world.GetActor(*observer)->id;
    const auto lampId = world.GetObject(lamp)->id;
    world.UpdatePlayerObservations();
    REQUIRE(world.CanPlayerSee(*observer, target));
    const auto memories = world.GetPlayerMemories();
    const auto original = world.LoadedChunks();
    const auto inputRevision = world.GetLightingInputRevision(ChunkOf(target));
    LightingField field;
    field.Rebuild(world);
    const auto outputRevision = field.GetChunkRevision(ChunkOf(target));
    const ChunkCoord upper{0,0,world_max_y};
    const auto upperRevision = field.GetChunkRevision(upper);

    // Neither field observes the temporary absence. The final resident set is
    // identical, so reload must be detected through fresh local input tokens.
    REQUIRE(world.ReleaseRequest(*request));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.RequestChunks({{0,0},0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CheckCoordSets(world.LoadedChunks(), original);
    const auto restoredObserver = world.FindActor(observerId);
    const auto restoredLamp = world.FindObject(lampId);
    REQUIRE(world.GetActor(restoredObserver));
    REQUIRE(world.GetObject(restoredLamp));
    CHECK(world.GetActor(*observer) == nullptr);
    CHECK(world.GetObject(lamp) == nullptr);
    CHECK(world.GetLightingInputRevision(ChunkOf(target)) > inputRevision);
    field.Rebuild(world);
    CheckFields(world, field);
    CHECK(field.ChangedChunks().empty());
    CHECK(field.GetChunkRevision(ChunkOf(target)) == outputRevision);
    CHECK(world.CanPlayerSee(restoredObserver, target));
    CHECK(world.GetPlayerMemories() == memories);

    auto previousInput = world.GetLightingInputRevision(ChunkOf(target));
    auto previousOutput = outputRevision;
    for (const bool enabled : {false, true}) {
        CAPTURE(enabled);
        world.ResetPerceptionStats();
        REQUIRE(world.SetObjectEnabled(restoredLamp, enabled));
        const auto nextInput = world.GetLightingInputRevision(ChunkOf(target));
        CHECK(nextInput > previousInput);
        field.Rebuild(world);
        CheckFields(world, field);
        CHECK(field.Get(target) == (enabled ? 192 : 0));
        const auto nextOutput = field.GetChunkRevision(ChunkOf(target));
        CHECK(nextOutput > previousOutput);
        CHECK(field.GetChunkRevision(upper) == upperRevision);
        CHECK(world.CanPlayerSee(restoredObserver, target) == enabled);
        CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
        CHECK(world.GetPerceptionStats().filterRebuilds == 1);
        CHECK(world.GetPlayerMemories() == memories);
        previousInput = nextInput;
        previousOutput = nextOutput;
    }
}

TEST_CASE("Local light crosses elevation only through two attenuated spill steps", "[lighting]") {
    World world(TestContent());
    Prepare(world);
    for (int y = -3; y <= 3; ++y) {
        REQUIRE(world.SetTile({5,5,y}, {TestTerrain("stairs"),0}));
    }
    Lamp(world, {5,5,0}, {255,12});
    for (const int sign : {-1,1}) {
        CHECK(world.GetLightLevel({5,5,sign}) == 63);
        CHECK(world.GetLightLevel({5,5,2*sign}) == 31);
        CHECK(world.GetLightLevel({5,5,3*sign}) == 0);
        CHECK(world.GetIncidentLightLevel({5,5,sign}, 4) == 0);
        CHECK(world.GetIncidentLightLevel({5,5,sign}, 5) == 0);
    }
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("floor"),0}));
    CHECK(world.GetLightLevel({5,5,1}) == 0);
    CHECK(world.GetLightLevel({5,5,2}) == 0);
    CHECK(world.GetLightLevel({5,5,-1}) == 63);
}

TEST_CASE("Localized roof and seam edits match fresh sky and source patches", "[lighting][incremental]") {
    World world(TestContent());
    Prepare(world);
    REQUIRE(world.RequestChunks({{0,0},1}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    for (int x = 14; x <= 17; ++x) {
        for (int y = 1; y <= world_max_y; ++y) {
            REQUIRE(world.SetTile({x,5,y}, {TestTerrain("air"),0}));
        }
        REQUIRE(world.SetTile({x,5,0}, {TestTerrain("floor"),0}));
    }
    world.SetSkyLight(192);
    const auto actor = world.CreateActor({14,5,0}, true);
    REQUIRE(actor);
    REQUIRE(world.SetActorLight(*actor, {255,4}));
    Lamp(world, {-10,5,0}, {192,1}); // Distant patch remains reusable.
    LightingField incremental;
    incremental.Rebuild(world);
    REQUIRE(world.SetTile({16,5,3}, {TestTerrain("floor"),0}));
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    REQUIRE(world.MoveActor(*actor, {17,5,0}));
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    REQUIRE(world.SetTile({16,5,3}, {TestTerrain("air"),0}));
    REQUIRE(world.SetTile({15,5,0}, {TestTerrain("wall"),0}));
    incremental.Rebuild(world);
    CheckFields(world, incremental);
}

TEST_CASE("Source patches clip horizontal and vertical world limits", "[lighting][incremental]") {
    for (const int sign : {-1,1}) {
        World world(TestContent());
        const TileCoord source{sign*world_coordinate_limit,sign*world_coordinate_limit,
            sign > 0 ? world_max_y : world_min_y};
        const auto coord = ChunkOf(source);
        REQUIRE(world.RequestChunks({{coord.x,coord.z},0}, LoadMode::resident));
        REQUIRE(world.ReconcileChunks());
        world.SetSkyLight(0);
        REQUIRE(world.SetTile(source, {TestTerrain("floor"),0}));
        const auto lamp = Lamp(world, source, {255,12});
        LightingField incremental;
        incremental.Rebuild(world);
        CHECK(incremental.Get(source) == 255);
        CHECK(incremental.Get({source.x+sign,source.z,source.y}) == 0);
        CHECK(incremental.Get({source.x,source.z,source.y+sign}) == 0);
        CheckFields(world, incremental);
        REQUIRE(world.DestroyObject(lamp));
        incremental.Rebuild(world);
        CHECK(incremental.Get(source) == 0);
        CheckFields(world, incremental);
    }
}

TEST_CASE("Incremental lighting equals fresh max-combination after source and optical changes", "[lighting][incremental]") {
    World world(TestContent());
    Prepare(world);
    LightingField incremental;
    const auto first = Lamp(world, {5,5,0}, {192,4});
    const auto second = Lamp(world, {7,5,0}, {255,3});
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    REQUIRE(world.SetObjectEnabled(second, false));
    incremental.Rebuild(world);
    CheckFields(world, incremental); // Removed winner restores the remaining contributor.
    REQUIRE(world.SetObjectLight(first, {64,2}));
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    const auto actor = world.CreateActor({5,6,0}, true);
    REQUIRE(actor);
    REQUIRE(world.SetActorLight(*actor, {255,4}));
    incremental.Rebuild(world);
    REQUIRE(world.MoveActor(*actor, {10,6,0}));
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    REQUIRE(world.SetTile({8,6,0}, {TestTerrain("wall"),0}));
    incremental.Rebuild(world);
    CheckFields(world, incremental); // Geometry rebuilds the complete shadow footprint.
    world.SetSkyLight(192);
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    REQUIRE(world.SetActorParked(*actor, true));
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    REQUIRE(world.DestroyObject(first));
    incremental.Rebuild(world);
    CheckFields(world, incremental);
    CHECK(incremental.GetChunkLevels({99,99,0}).empty());
}

TEST_CASE("Planar source lighting matches bulk visibility and independently combined two-step transport", "[lighting][incremental]") {
    World world(TestContent());
    Prepare(world);
    for (int y = 1; y <= 3; ++y) {
        for (int z = 2; z <= 12; ++z) {
            for (int x = 2; x <= 12; ++x) {
                REQUIRE(world.SetTile({x,z,y}, {TestTerrain("air"),0}));
            }
        }
    }
    REQUIRE(world.SetTile({7,6,0}, {TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({7,6,1}, {TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({4,4,0}, {TestTerrain("stairs"),0}));
    REQUIRE(world.SetTile({4,4,1}, {TestTerrain("stairs"),0}));
    const TileCoord positions[] = {{4,4,0},{10,6,1}};
    const LightEmission emissions[] = {{192,6},{255,4}};
    Lamp(world, positions[0], emissions[0]);
    Lamp(world, positions[1], emissions[1]);
    Vector<std::uint8_t> visibility[2];
    for (int s = 0; s < 2; ++s) {
        const int r = emissions[s].range;
        const int side = 2*r+1;
        Vector<std::uint8_t> optics(side*side, 0);
        visibility[s].resize(side*side, 0);
        for (int z = 0; z < side; ++z) {
            for (int x = 0; x < side; ++x) {
                const auto* tile = world.GetTile({positions[s].x+x-r,positions[s].z+z-r,positions[s].y});
                if (tile) { optics[z*side+x] = world.GetContent().GetTerrain(tile->terrain).blocksSight ? 3 : 1; }
            }
        }
        BuildPlanarVisibility({optics.data(), optics.size()}, side, side, r, r, r,
            {visibility[s].data(), visibility[s].size()});
    }
    struct ReferenceChunk {
        ChunkCoord position;
        eastl::array<std::uint8_t,chunk_size*chunk_size> direct{};
        eastl::array<std::uint8_t,chunk_size*chunk_size> first{};
        eastl::array<std::uint8_t,chunk_size*chunk_size> level{};
    };
    Vector<ReferenceChunk> reference;
    for (const auto coord : world.LoadedChunks()) { reference.push_back({coord}); }
    const auto find = [&reference](TileCoord p) -> ReferenceChunk* {
        const auto coord = ChunkOf(p);
        for (auto& chunk : reference) { if (chunk.position == coord) { return &chunk; } }
        return nullptr;
    };
    const TileCoord offsets[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const auto connected = [&world](TileCoord p, TileCoord q) {
        const auto* a = world.GetTile(p);
        const auto* b = world.GetTile(q);
        return a && b && !world.GetContent().GetTerrain(a->terrain).blocksSight
            && !world.GetContent().GetTerrain(b->terrain).blocksSight
            && (p.y == q.y || OpenVerticalFace(world,p,q));
    };
    LightingField actual;
    actual.Prepare(world);
    CHECK(actual.GetWorkStats().sourcePatches == 0);
    CHECK(actual.GetWorkStats().outputChunks == 0);
    // Request arbitrary elevations in reverse order; the oracle below computes
    // combined direct/first/second transport independently of the source cache.
    for (std::size_t c = world.LoadedChunks().size(); c > 0; --c) {
        actual.EnsureChunk(world, world.LoadedChunks()[c-1]);
    }
    for (auto& chunk : reference) {
        for (int i = 0; i < chunk_size*chunk_size; ++i) {
            const TileCoord p{chunk.position.x*chunk_size+i%chunk_size,
                chunk.position.z*chunk_size+i/chunk_size,chunk.position.y};
            eastl::array<std::uint8_t,6> faces{};
            for (int s = 0; s < 2; ++s) {
                const int dx = positions[s].x-p.x;
                const int dz = positions[s].z-p.z;
                const int dy = positions[s].y-p.y;
                const int squared = dx*dx+dz*dz;
                const int r = emissions[s].range;
                if (dy || squared > r*r || !visibility[s][(r-dz)*(2*r+1)+r-dx]) { continue; }
                int distance = 0;
                while (distance*distance < squared) { ++distance; }
                const auto level = static_cast<std::uint8_t>(emissions[s].intensity*(r+1-distance)/(r+1));
                chunk.direct[i] = eastl::max(chunk.direct[i],level);
                if (world.GetContent().GetTerrain(world.GetTile(p)->terrain).blocksSight) {
                    const int largest = eastl::max(eastl::max(std::abs(dx),std::abs(dz)),std::abs(dy));
                    if (!largest) { continue; }
                    const int deltas[] = {dx,dz,dy};
                    for (int axis = 0; axis < 3; ++axis) {
                        if (std::abs(deltas[axis]) != largest) { continue; }
                        const int face = axis*2+(deltas[axis] < 0 ? 1 : 0);
                        faces[face] = eastl::max(faces[face],level);
                    }
                }
            }
            for (int d = 0; d < 6; ++d) { REQUIRE(actual.GetIncident(p,d) == faces[d]); }
            chunk.level[i] = chunk.direct[i];
        }
    }
    for (auto& chunk : reference) {
        for (int i = 0; i < chunk_size*chunk_size; ++i) {
            const TileCoord p{chunk.position.x*chunk_size+i%chunk_size,
                chunk.position.z*chunk_size+i/chunk_size,chunk.position.y};
            for (const auto offset : offsets) {
                const TileCoord q{p.x+offset.x,p.z+offset.z,p.y+offset.y};
                if (!connected(p,q)) { continue; }
                auto* next = find(q);
                REQUIRE(next);
                auto& level = next->first[TileIndex(q)];
                level = eastl::max(level,static_cast<std::uint8_t>(chunk.direct[i]/4));
            }
        }
    }
    for (auto& chunk : reference) {
        for (int i = 0; i < chunk_size*chunk_size; ++i) {
            const TileCoord p{chunk.position.x*chunk_size+i%chunk_size,
                chunk.position.z*chunk_size+i/chunk_size,chunk.position.y};
            chunk.level[i] = eastl::max(chunk.level[i],chunk.first[i]);
            for (const auto offset : offsets) {
                const TileCoord q{p.x+offset.x,p.z+offset.z,p.y+offset.y};
                if (!connected(p,q)) { continue; }
                auto* next = find(q);
                REQUIRE(next);
                auto& level = next->level[TileIndex(q)];
                level = eastl::max(level,static_cast<std::uint8_t>(chunk.first[i]/2));
            }
        }
    }
    for (const auto& chunk : reference) {
        const auto levels = actual.GetChunkLevels(chunk.position);
        REQUIRE(eastl::equal(levels.begin(),levels.end(),chunk.level.begin()));
    }
}

TEST_CASE("Prepare publishes never-computed dependencies and Ensure only finalizes requested chunks", "[lighting][incremental][lazy]") {
    World world(TestContent());
    Prepare(world);
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("air"),0}));
    const auto lamp = Lamp(world, {5,5,0}, {192,1});
    LightingField field;
    field.Prepare(world);
    const ChunkCoord local{0,0,0};
    const ChunkCoord receiving{0,0,1};
    const auto before = field.GetChunkRevision(receiving);
    REQUIRE(before != 0);
    CHECK(field.GetWorkStats().outputChunks == 0);
    CHECK(field.GetWorkStats().sourcePatches == 0);
    field.EnsureChunk(world, local);
    CHECK(field.Get({5,5,0}) == 192);
    CHECK(field.GetWorkStats().outputChunks == 1);
    CHECK(field.GetWorkStats().sourcePatches == 1);
    field.EnsureChunk(world, local);
    CHECK(field.GetWorkStats().outputChunks == 1);
    REQUIRE(world.SetObjectEnabled(lamp, false));
    field.Prepare(world);
    const auto changed = field.ChangedChunks();
    CheckUniqueCoords(changed);
    CHECK(eastl::find(changed.begin(), changed.end(), receiving) != changed.end());
    CHECK(field.GetChunkRevision(receiving) > before);
    CHECK(field.GetWorkStats().outputChunks == 0);
    const auto revision = field.GetChunkRevision(receiving);
    // A second no-op sync must retain the outstanding lazy invalidation.
    field.Prepare(world);
    CHECK(field.ChangedChunks().empty());
    CHECK(field.GetChunkRevision(receiving) == revision);
    field.EnsureChunk(world, receiving);
    CHECK(field.Get({5,5,1}) == 0);
    CHECK(field.GetWorkStats().outputChunks == 1);
    CHECK(field.GetWorkStats().sourcePatches == 0);
    CHECK(field.GetChunkRevision(receiving) == revision);
    field.EnsureChunk(world, {99,99,0});
    CHECK(field.GetChunkLevels({99,99,0}).empty());
    CHECK(field.GetWorkStats().outputChunks == 1);
}

TEST_CASE("Remote residency retains output bytes revisions and source patches across column mutations", "[lighting][incremental][lazy]") {
    World world(TestContent());
    Prepare(world);
    const auto lamp = Lamp(world, {5,5,0}, {192,1});
    LightingField field;
    field.Rebuild(world);
    const ChunkCoord local{0,0,0};
    const auto revision = field.GetChunkRevision(local);
    const auto* storage = field.GetChunkLevels(local).data();
    const auto remote = world.RequestChunks({{-4,-4},0}, LoadMode::resident);
    REQUIRE(remote);
    REQUIRE(world.ReconcileChunks());
    field.Prepare(world);
    CHECK(field.GetChunkRevision(local) == revision);
    CHECK(field.GetChunkLevels(local).data() == storage);
    CHECK(field.Get({5,5,0}) == 192);
    CHECK(field.GetWorkStats().outputChunks == 0);
    CHECK(field.GetWorkStats().sourcePatches == 0);
    // Force local recombination while retaining the source patch across the
    // insertion of a negative remote column.
    world.SetSkyLight(1);
    field.Prepare(world);
    field.EnsureChunk(world, local);
    CHECK(field.Get({5,5,0}) == 192);
    CHECK(field.GetWorkStats().sourcePatches == 0);
    for (const auto coord : world.LoadedChunks()) { field.EnsureChunk(world, coord); }
    CheckFields(world, field);
    const auto preservedRevision = field.GetChunkRevision(local);
    REQUIRE(world.ReleaseRequest(*remote));
    REQUIRE(world.ReconcileChunks());
    field.Prepare(world);
    CHECK(field.GetChunkRevision(local) == preservedRevision);
    CHECK(field.GetChunkLevels(local).data() == storage);
    CHECK(field.GetWorkStats().outputChunks == 0);
    Vector<ChunkCoord> removed;
    for (int y = world_min_y; y <= world_max_y; ++y) { removed.push_back({-4,-4,y}); }
    CheckCoordSets(field.ChangedChunks(), removed);
    REQUIRE(world.DestroyObject(lamp));
    field.Prepare(world);
    field.EnsureChunk(world, local);
    CHECK(field.Get({5,5,0}) < 192);
}

TEST_CASE("Column map growth and early erasure preserve cached lighting addresses and source identity", "[lighting][incremental][lazy]") {
    World world(TestContent());
    const auto early = world.RequestChunks({{-8,0},0}, LoadMode::resident);
    REQUIRE(early);
    REQUIRE(world.RequestChunks({{-1,-1},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    for (int y = 0; y <= 1; ++y) {
        for (int z = -8; z <= -2; ++z) {
            for (int x = -8; x <= -2; ++x) {
                REQUIRE(world.SetTile({x,z,y}, {TestTerrain("floor"),0}));
            }
        }
    }
    REQUIRE(world.SetTile({-4,-5,0}, {TestTerrain("wall"),0}));
    Lamp(world, {-5,-5,0}, {255,1});
    const ChunkCoord local{-1,-1,0};
    LightingField field;
    field.Prepare(world);
    field.EnsureChunk(world, local);
    REQUIRE(field.GetWorkStats().sourcePatches == 1);
    REQUIRE(field.Get({-5,-5,0}) == 255);
    REQUIRE(field.GetIncident({-4,-5,0}, 1) == 127);
    const auto* storage = field.GetChunkLevels(local).data();
    const auto revision = field.GetChunkRevision(local);
    // Grow well beyond the initial two columns, then erase an earlier column
    // (also exercising dense-map relocation of a surviving map entry).
    REQUIRE(world.RequestChunks({{8,8},2}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    const auto checkRetained = [&] {
        field.Prepare(world);
        CHECK(field.GetWorkStats().outputChunks == 0);
        CHECK(field.GetWorkStats().sourcePatches == 0);
        CHECK(field.GetChunkRevision(local) == revision);
        // Reacquire the borrow after Prepare before reading; only compare the
        // old address, whose public borrow lifetime has ended.
        CHECK(field.GetChunkLevels(local).data() == storage);
        field.EnsureChunk(world, local);
        CHECK(field.GetWorkStats().outputChunks == 0);
        CHECK(field.Get({-5,-5,0}) == 255);
        CHECK(field.GetIncident({-4,-5,0}, 1) == 127);
        CheckUniqueCoords(field.ChangedChunks());
    };
    checkRetained();
    REQUIRE(world.ReleaseRequest(*early));
    REQUIRE(world.ReconcileChunks());
    checkRetained();
    CHECK(field.GetChunkRevision({-8,0,0}) == 0);
    CHECK(field.GetChunkLevels({-8,0,0}).empty());
    CHECK(field.Get({-8*chunk_size,0,0}) == 0);
    CHECK(field.GetChunkLevels({-1,-1,world_min_y-1}).empty());
    CHECK(field.GetChunkLevels({-1,-1,world_max_y+1}).empty());
    CHECK(field.GetChunkRevision({-1,-1,world_min_y-1}) == 0);
    CHECK(field.Get({-5,-5,world_max_y+1}) == 0);
    CHECK(field.GetIncident({-5,-5,world_min_y-1}, 4) == 0);
    // Recombine after both residency changes. The unchanged source must reuse
    // its coordinate-based patch across column insertion and erasure.
    Lamp(world, {-6,-5,0}, {64,1}); // Separate object tile; old source remains the maximum.
    field.Prepare(world);
    field.EnsureChunk(world, local);
    CHECK(field.GetWorkStats().sourcePatches == 1); // Only the new source.
    CHECK(field.Get({-5,-5,0}) == 255);
    CHECK(field.GetIncident({-4,-5,0}, 1) == 127);
    CHECK(field.GetChunkLevels(local).data() == storage);
    for (const auto coord : world.LoadedChunks()) { field.EnsureChunk(world, coord); }
    CheckFields(world, field);
}

TEST_CASE("Uniform sky and solid lazy paths preserve direct endpoints and incident faces", "[lighting][incremental][lazy]") {
    World world(TestContent());
    Prepare(world);
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            REQUIRE(world.SetTile({x,z,world_max_y}, {TestTerrain("air"),0}));
            REQUIRE(world.SetTile({x,z,world_max_y-1}, {TestTerrain("wall"),0}));
            REQUIRE(world.SetTile({x,z,world_max_y-2}, {TestTerrain("floor"),0}));
        }
    }
    LightingField field;
    for (const int sky : {0,1,3,4,7,8,192,255}) {
        world.SetSkyLight(static_cast<std::uint8_t>(sky));
        field.Prepare(world);
        field.EnsureChunk(world, {0,0,world_max_y-2});
        CHECK(field.GetWorkStats().outputChunks == 1);
        CHECK(field.Get({5,5,world_max_y-2}) == 0); // Solid above stops sky.
        field.EnsureChunk(world, {0,0,world_max_y-1});
        field.EnsureChunk(world, {0,0,world_max_y});
        CHECK(field.GetWorkStats().outputChunks == 3);
        CHECK(field.GetWorkStats().sourcePatches == 0);
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = 0; x < chunk_size; ++x) {
                const TileCoord wall{x,z,world_max_y-1};
                CHECK(field.Get(wall) == sky);
                CHECK(field.Get({x,z,world_max_y}) == sky);
                for (int face = 0; face < 6; ++face) {
                    CHECK(field.GetIncident(wall,face) == (face == 4 ? sky : 0));
                    CHECK(field.GetIncident({x,z,world_max_y},face) == 0);
                }
            }
        }
    }
}

TEST_CASE("Lazy roof source and seam updates are independent of query order", "[lighting][incremental][lazy]") {
    World world(TestContent());
    Prepare(world);
    const auto neighbor = world.RequestChunks({{-1,0},0}, LoadMode::resident);
    REQUIRE(neighbor);
    REQUIRE(world.ReconcileChunks());
    for (int x = -2; x <= 2; ++x) {
        REQUIRE(world.SetTile({x,5,0}, {TestTerrain("floor"),0}));
        for (int y = 1; y <= world_max_y; ++y) {
            REQUIRE(world.SetTile({x,5,y}, {TestTerrain("air"),0}));
        }
    }
    world.SetSkyLight(192);
    const auto first = Lamp(world, {-1,5,0}, {255,2});
    Lamp(world, {1,5,0}, {128,2});
    LightingField forward;
    LightingField reverse;
    const auto verify = [&] {
        forward.Prepare(world);
        reverse.Prepare(world);
        CHECK(forward.GetWorkStats().outputChunks == 0);
        CHECK(reverse.GetWorkStats().outputChunks == 0);
        const auto loaded = world.LoadedChunks();
        for (const auto coord : loaded) { forward.EnsureChunk(world, coord); }
        for (std::size_t i = loaded.size(); i > 0; --i) { reverse.EnsureChunk(world, loaded[i-1]); }
        CheckFields(world, forward);
        CheckFields(world, reverse);
    };
    verify();
    REQUIRE(world.SetTile({0,5,3}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({0,5,0}, {TestTerrain("wall"),0}));
    verify();
    REQUIRE(world.SetObjectEnabled(first, false));
    verify();
    REQUIRE(world.ReleaseRequest(*neighbor));
    REQUIRE(world.ReconcileChunks());
    verify();
    REQUIRE(world.RequestChunks({{-1,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    verify();
}

// Explicit opt-in benchmark: timings are diagnostics, never pass/fail thresholds.
TEST_CASE("Moving source lighting benchmark", "[.benchmark][lighting-benchmark]") {
    for (const int radius : {2,5}) {
        World world(TestContent());
        REQUIRE(world.RequestChunks({{0,0},radius}, LoadMode::resident));
        REQUIRE(world.ReconcileChunks());
        for (int z = -16; z <= 16; ++z) {
            for (int x = -16; x <= 16; ++x) {
                REQUIRE(world.SetTile({x,z,0}, {TestTerrain("floor"),0}));
                REQUIRE(world.SetTile({x,z,1}, {TestTerrain("air"),0}));
            }
        }
        auto actor = world.CreateActor({0,0,0}, true);
        REQUIRE(actor);
        REQUIRE(world.SetActorLight(*actor, {192,12}));
        LightingField cached;
        cached.Rebuild(world);
        constexpr int samples = 20;
        eastl::array<double,samples> incrementalMs{};
        eastl::array<double,samples> freshMs{};
        for (int i = 0; i < samples; ++i) {
            REQUIRE(world.MoveActor(*actor, {(i+1)%2,0,0}));
            auto start = std::chrono::steady_clock::now();
            cached.Rebuild(world);
            incrementalMs[i] = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            LightingField fresh;
            start = std::chrono::steady_clock::now();
            fresh.Rebuild(world);
            freshMs[i] = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
        CheckFields(world, cached);
        eastl::sort(incrementalMs.begin(),incrementalMs.end());
        eastl::sort(freshMs.begin(),freshMs.end());
        std::printf("resident=%d samples=%d lighting incremental_median_ms=%.3f p95_ms=%.3f fresh_median_ms=%.3f p95_ms=%.3f\n",
            radius,samples,incrementalMs[9],incrementalMs[18],freshMs[9],freshMs[18]);
    }
}
}
