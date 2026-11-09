#include "core/world.h"
#include "core/perception.h"
#include "core/lighting.h"
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

TEST_CASE("Light falloff is spherical, max combined and bounded with two-step spill", "[lighting]") {
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
    CHECK(world.GetLightLevel({5,5,1}) > 63);
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("air"),0}));
    CHECK(LineVisible(world, {5,5,0}, {5,5,1}));
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
    CHECK(world.GetLightLevel({5,5,0}) < 64);
    world.SetSkyLight(0);
    CHECK(world.GetLightLevel({5,5,0}) == 0);
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
    CHECK(world.GetLightLevel({5,5,1}) == 127);
    CHECK(world.GetLightLevel({5,5,2}) == 31);
    CHECK(world.GetLightLevel({5,5,3}) == 15);
    CHECK(world.GetLightLevel({5,5,4}) == 0);
    REQUIRE(world.SetTile({5,5,2}, {TestTerrain("floor"),0}));
    CHECK(world.GetLightLevel({5,5,2}) == 0); // A closed upper floor face blocks spill.
    CHECK(world.GetLightLevel({5,5,1}) == 127);
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

TEST_CASE("Surface entry faces use dominant axes and conservative edge and corner ties", "[lighting][surface]") {
    World world(TestContent());
    Prepare(world);
    const TileCoord wall{8,8,0};
    REQUIRE(world.SetTile(wall, {TestTerrain("wall"),0}));
    Lamp(world, {7,8,0}, {255,1});
    CHECK(TerrainLightLevel(world, {2,7,0}, wall) == 255); // X face only.
    CHECK(TerrainLightLevel(world, {7,2,0}, wall) == world.GetLightLevel({8,7,0}));
    CHECK(TerrainLightLevel(world, {2,2,0}, wall) == 255); // Both accessible faces.
    REQUIRE(world.SetTile({8,7,0}, {TestTerrain("wall"),0}));
    CHECK_FALSE(LineVisible(world, {2,2,0}, wall));
    CHECK(TerrainLightLevel(world, {2,2,0}, wall) == 0); // No bright-face corner peek.
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
    CHECK(world.GetLightLevel(wall) == 127);
    CHECK(TerrainLightLevel(world, {5,5,2}, wall) == 127);

    REQUIRE(world.SetTile(wall, {TestTerrain("wall"),0}));
    for (int y = 1; y <= 3; ++y) {
        for (int z = 2; z <= 5; ++z) {
            for (int x = 2; x <= 5; ++x) {
                REQUIRE(world.SetTile({x,z,y}, {TestTerrain("air"),0}));
            }
        }
    }
    CHECK(LineVisible(world, {2,2,3}, wall));
    CHECK(TerrainLightLevel(world, {2,2,3}, wall) == 255); // Three accessible entry faces.
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("floor"),0}));
    CHECK_FALSE(LineVisible(world, {2,2,3}, wall));
    CHECK(TerrainLightLevel(world, {2,2,3}, wall) == 0); // Reject the full vertical corner tie.
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

TEST_CASE("Direct incident wall faces survive different sample-center floor ties", "[lighting][surface]") {
    for (const int y : {-1, 2}) {
        CAPTURE(y);
        World world(TestContent());
        Prepare(world);
        const TileCoord observer{4,4,y};
        const TileCoord sample{5,4,y-1};
        const TileCoord target{6,4,y-1};
        REQUIRE(world.SetTile(observer, {TestTerrain("floor"),0}));
        REQUIRE(world.SetTile(sample, {TestTerrain("floor"),0}));
        REQUIRE(world.SetTile(target, {TestTerrain("wall"),0}));
        REQUIRE(world.SetTile({5,4,y}, {TestTerrain("air"),0}));
        REQUIRE(world.SetTile({6,4,y}, {TestTerrain("air"),0}));
        Lamp(world, observer, {192,12});
        CHECK_FALSE(LineVisible(world, observer, sample));
        CHECK(LineVisible(world, observer, target));
        CHECK(TerrainLightLevel(world, observer, target) >= 64);
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

TEST_CASE("Packed source lighting matches point LOS and independently combined two-step transport", "[lighting][incremental]") {
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
    actual.Rebuild(world);
    for (auto& chunk : reference) {
        for (int i = 0; i < chunk_size*chunk_size; ++i) {
            const TileCoord p{chunk.position.x*chunk_size+i%chunk_size,
                chunk.position.z*chunk_size+i/chunk_size,chunk.position.y};
            eastl::array<std::uint8_t,6> faces{};
            for (int s = 0; s < 2; ++s) {
                const int dx = positions[s].x-p.x;
                const int dz = positions[s].z-p.z;
                const int dy = positions[s].y-p.y;
                const int squared = dx*dx+dz*dz+dy*dy;
                const int r = emissions[s].range;
                if (squared > r*r || !LineVisible(world,positions[s],p)) { continue; }
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
