#include "core/world.h"
#include "core/perception.h"
#include "core/planar_visibility.h"
#include "core/server.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <EASTL/sort.h>
#include <EASTL/algorithm.h>

namespace {
using namespace cataclysmx;

void Load(World& world, int radius = 1) {
    REQUIRE(world.RequestChunks({{0, 0}, radius}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
}

void Fill(World& world, int radius, int low, int high, TerrainId terrain) {
    for (int y = low; y <= high; ++y) {
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                REQUIRE(world.SetTile({x, z, y}, {terrain, 0}));
            }
        }
    }
}

Vector<std::uint8_t> Planar(const Vector<std::uint8_t>& optics, int width, int depth,
    int x, int z, int radius, std::uint64_t* visits = nullptr) {
    Vector<std::uint8_t> visible(optics.size(), 0);
    BuildPlanarVisibility({optics.data(), optics.size()}, width, depth, x, z, radius,
        {visible.data(), visible.size()}, visits);
    return visible;
}

TEST_CASE("Planar walls doorways missing cells and diagonal corners have explicit geometry",
    "[perception][bulk][planar]") {
    constexpr int width = 17;
    Vector<std::uint8_t> optics(width * width, 1);
    for (int z = 0; z < width; ++z) { optics[z * width + 8] = 3; }
    auto visible = Planar(optics, width, width, 4, 8, 12);
    CHECK(visible[8 * width + 8] == 1); // Opaque endpoint.
    CHECK(visible[8 * width + 9] == 0);
    optics[8 * width + 8] = 1; // Doorway opens a cone, not the entire far side.
    visible = Planar(optics, width, width, 4, 8, 12);
    CHECK(visible[8 * width + 12] == 1);
    CHECK(visible[2 * width + 12] == 0);
    optics[8 * width + 8] = 0;
    visible = Planar(optics, width, width, 4, 8, 12);
    CHECK(visible[8 * width + 8] == 0);
    CHECK(visible[8 * width + 12] == 0);
    optics.assign(width * width, 1);
    optics[8 * width + 9] = 3;
    optics[9 * width + 8] = 3;
    visible = Planar(optics, width, width, 8, 8, 8);
    CHECK(visible[9 * width + 9] == 1); // Deliberately permissive touching corners.
    CHECK(visible[8 * width + 10] == 0);
    optics[8 * width + 8] = 0;
    visible = Planar(optics, width, width, 8, 8, 8);
    for (auto cell : visible) { CHECK(cell == 0); }
}

TEST_CASE("Integer planar FOV is reciprocal and rotation invariant for floor cells",
    "[perception][bulk][symmetry]") {
    constexpr int width = 13;
    Vector<std::uint8_t> optics(width * width, 1);
    Vector<std::uint8_t> rotated(width * width, 1);
    std::uint32_t state = 0x12345678;
    for (int z = 0; z < width; ++z) {
        for (int x = 0; x < width; ++x) {
            state = state * 1664525U + 1013904223U;
            optics[z * width + x] = (state >> 28) < 4 ? 3 : 1;
            rotated[x * width + width - 1 - z] = optics[z * width + x];
        }
    }
    Vector<Vector<std::uint8_t>> views;
    for (int z = 0; z < width; ++z) {
        for (int x = 0; x < width; ++x) {
            views.push_back(Planar(optics, width, width, x, z, 8));
            const auto turned = Planar(rotated, width, width, width - 1 - z, x, 8);
            for (int tz = 0; tz < width; ++tz) {
                for (int tx = 0; tx < width; ++tx) {
                    CHECK(views.back()[tz * width + tx] == turned[tx * width + width - 1 - tz]);
                }
            }
        }
    }
    for (int a = 0; a < width * width; ++a) {
        if (optics[a] != 1) { continue; }
        for (int b = 0; b < width * width; ++b) {
            if (optics[b] == 1) { CHECK(views[a][b] == views[b][a]); }
        }
    }
}

TEST_CASE("Bulk projects only visible open columns down to the first surface",
    "[perception][bulk][projection]") {
    World world(TestContent());
    Load(world);
    Fill(world, 9, world_min_y, 2, TestTerrain("air"));
    REQUIRE(world.SetTile({2, 0, -4}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({-2, 0, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({0, 2, 0}, {TestTerrain("stairs"), 0}));
    VisibilityMask mask;
    PerceptionStats stats;
    BuildVisibilityMask(world, {0, 0, 0}, 4, mask, stats);
    CHECK(mask.Contains({2, 0, -4}));
    CHECK_FALSE(mask.Contains({2, 0, -5}));
    CHECK_FALSE(mask.Contains({-2, 0, -1}));
    CHECK_FALSE(mask.Contains({0, 2, -1})); // Stairs are a surface, not an aperture.
    CHECK(mask.Contains({4, 0, world_min_y})); // Depth does not spend sight range.
    CHECK_FALSE(mask.Contains({4, 1, -1}));
    CHECK_FALSE(mask.Contains({0, 0, 1}));
    CHECK(LineVisible(world, {0, 0, 0}, {2, 0, -4}));
    CHECK_FALSE(LineVisible(world, {0, 0, 0}, {0, 0, 1}));
    CHECK(stats.pointLosCalls == 0);
    CHECK(stats.traversedVoxels <= 1 + 4 * 4 * (4 + 2));

    OpticalField field;
    field.Rebuild(world);
    auto missing = eastl::find_if(field.chunks.begin(), field.chunks.end(), [](const OpticalChunk& chunk) {
        return chunk.position == ChunkCoord{0, 0, -2};
    });
    REQUIRE(missing != field.chunks.end());
    field.chunks.erase(missing);
    ++field.revision;
    BuildVisibilityMask(world, {0, 0, 0}, 4, mask, stats, &field);
    CHECK(mask.Contains({2, 0, -1}));
    CHECK_FALSE(mask.Contains({2, 0, -2}));
    CHECK_FALSE(mask.Contains({2, 0, -4}));
}

TEST_CASE("Local point queries agree with bulk through opaque origins corners and downward surfaces",
    "[perception][bulk][point]") {
    World world(TestContent());
    Load(world);
    Fill(world, 8, -3, -3, TestTerrain("floor"));
    Fill(world, 8, -2, 0, TestTerrain("air"));
    REQUIRE(world.SetTile({-1, -1, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetTile({0, -1, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetTile({-1, 0, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetTile({2, 1, -1}, {TestTerrain("stairs"), 0}));
    CHECK(LineVisible(world, {-1, -1, 0}, {-1, -1, 0}));
    CHECK(LineVisible(world, {-1, -1, 0}, {0, -1, 0}));
    CHECK_FALSE(LineVisible(world, {-1, -1, 0}, {1, -1, 0}));
    CHECK(LineVisible(world, {-1, -1, 0}, {0, 0, 0}));
    CHECK(LineVisible(world, {-1, -1, 0}, {0, 0, -3}));
    CHECK_FALSE(LineVisible(world, {-1, -1, 0}, {-1, -1, -1}));
    for (TileCoord origin : {TileCoord{-1, -1, 0}, TileCoord{-2, -2, 0}}) {
        VisibilityMask mask;
        PerceptionStats bulk;
        BuildVisibilityMask(world, origin, 4, mask, bulk);
        for (int z = -4; z <= 4; ++z) {
            for (int x = -4; x <= 4; ++x) {
                if (x * x + z * z > 16) { continue; }
                for (int y : {0, -1, -3, -4}) {
                    const TileCoord target{origin.x + x, origin.z + z, y};
                    INFO("point target " << target.x << ',' << target.z << ',' << y);
                    CHECK(LineVisible(world, origin, target) == mask.Contains(target));
                }
            }
        }
    }
}

TEST_CASE("Point-query work is local and never constructs bulk snapshots or projections",
    "[perception][bulk][point][cache]") {
    World world(TestContent());
    Load(world);
    Fill(world, 8, -3, -3, TestTerrain("floor"));
    Fill(world, 8, -2, 0, TestTerrain("air"));
    const auto query = [&]() {
        world.ResetPerceptionStats();
        REQUIRE(LineVisible(world, {-1, -1, 0}, {2, -1, -3}));
        const auto stats = world.GetPerceptionStats();
        CHECK(stats.pointLosCalls == 1);
        CHECK(stats.geometryRebuilds == 0);
        CHECK(stats.opticalRebuilds == 0);
        CHECK(stats.volumeRebuilds == 0);
        CHECK(stats.projectedCells == 0); // Point probes are counted in traversedVoxels.
        CHECK(stats.traversedVoxels == 49 + (1 + 4 * 3 * 5) + 4);
        return stats.traversedVoxels;
    };
    const auto localWork = query();
    REQUIRE(world.RequestChunks({{100, -100}, 2}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK(query() == localWork);
    world.ResetPerceptionStats();
    CHECK_FALSE(LineVisible(world, {0, 0, 0}, {0, 0, 1}));
    CHECK_FALSE(LineVisible(world, {0, 0, 0}, {65, 0, 0}));
    CHECK(world.GetPerceptionStats().pointLosCalls == 2);
    CHECK(world.GetPerceptionStats().traversedVoxels == 0);
}

TEST_CASE("Bulk projection work accumulates into caller statistics",
    "[perception][bulk][projection]") {
    World world(TestContent());
    Load(world);
    Fill(world, 1, -3, -3, TestTerrain("floor"));
    Fill(world, 1, -2, 0, TestTerrain("air"));
    OpticalField field;
    field.Rebuild(world);
    VisibilityWorkspace workspace;
    VisibilityMask mask;
    PerceptionStats stats;
    stats.projectedCells = 7;
    BuildVisibilityMask(world, {0, 0, 0}, 0, mask, stats, &field, &workspace);
    CHECK(workspace.projectionSamples == 3);
    CHECK(stats.projectedCells == 10);
    BuildVisibilityMask(world, {0, 0, -3}, 0, mask, stats, &field, &workspace);
    CHECK(workspace.projectionSamples == 0);
    CHECK(stats.projectedCells == 10);
}

TEST_CASE("Authoritative perception caches survive status overlays and light-only edits",
    "[perception][bulk][cache][memory]") {
    World world(TestContent());
    Load(world);
    Fill(world, 12, 0, 0, TestTerrain("floor"));
    Fill(world, 12, 1, world_max_y, TestTerrain("air"));
    const auto player = world.CreateActor({0, 0, 0}, true);
    REQUIRE(player);
    VisionProfile vision;
    vision.range = 12;
    REQUIRE(world.SetVisionProfile(vision));
    world.UpdatePlayerObservations();
    const auto id = world.GetActor(*player)->id;
    const auto initial = world.GetPlayerMemories();
    world.ResetPerceptionStats();
    const auto map = world.GetMapRevision();
    const auto revision = world.GetRevision();
    REQUIRE(world.RecordActorTurn(*player));
    CHECK(world.GetRevision() > revision);
    CHECK(world.GetMapRevision() == map);
    const auto inventoryRevision = world.GetRevision();
    auto battery = world.CreateItem(TestItem("rechargeable_battery"), ItemLocation::Carried(*player));
    REQUIRE(battery);
    const auto power = FindItemCapability(TestContent().GetItem(TestItem("rechargeable_battery")), "power");
    REQUIRE(world.SetItemEnergy(*battery, power, 1));
    CHECK(world.GetRevision() > inventoryRevision);
    CHECK(world.GetMapRevision() == map);
    REQUIRE(world.DestroyItem(*battery));
    CHECK(world.GetMapRevision() == map);
    world.UpdatePlayerObservations();
    CHECK(world.CanPlayerSee(*player, {10, 0, 0}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().observationScans == 0);
    CHECK(world.GetPlayerMemories() == initial);

    const auto npc = world.CreateActor({3, 1, 0});
    REQUIRE(npc);
    REQUIRE(world.MoveActor(*npc, {4, 1, 0}));
    world.UpdatePlayerObservations();
    CHECK(world.GetMapRevision() > map);
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().observationScans == 0);

    world.SetSkyLight(0);
    CHECK_FALSE(world.CanPlayerSee(*player, {10, 0, 0}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().lightingRebuilds == 1);
    CHECK(world.GetPlayerMemories() == initial);
    world.SetSkyLight(192);
    CHECK(world.CanPlayerSee(*player, {10, 0, 0}));
    REQUIRE(world.SetTile({10, 0, 0}, {TestTerrain("grass"), 0}));
    CHECK(world.CanPlayerSee(*player, {10, 0, 0}));
    CHECK(world.GetRememberedTile(id, {10, 0, 0}).terrain == TestTerrain("floor"));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(id, {10, 0, 0}).terrain == TestTerrain("grass"));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().memoryTilesChanged == 1);
}

TEST_CASE("Dark upper air does not hide an illuminated downward surface",
    "[perception][bulk][projection][lighting]") {
    World world(TestContent());
    Load(world);
    Fill(world, 8, -8, -8, TestTerrain("floor"));
    Fill(world, 8, -7, world_max_y, TestTerrain("air"));
    world.SetSkyLight(0);
    const auto lamp = world.CreateObject({3, 0, -8}, true);
    REQUIRE(lamp);
    REQUIRE(world.SetObjectLight(*lamp, {255, 2}));
    REQUIRE(world.GetLightLevel({3, 0, 0}) == 0);
    REQUIRE(world.GetLightLevel({3, 0, -8}) >= 64);
    CHECK(CanSeeTile(world, {0, 0, 0}, {3, 0, -8}));
    CHECK_FALSE(CanSeeTile(world, {0, 0, 0}, {3, 0, 0}));
}

TEST_CASE("Same-tick transient geometry and appearance teach memory at each boundary",
    "[perception][bulk][memory]") {
    World world(TestContent());
    Load(world);
    Fill(world, 8, 0, 0, TestTerrain("floor"));
    Fill(world, 8, 1, world_max_y, TestTerrain("air"));
    auto player = world.CreateActor({0, 0, 0}, true);
    REQUIRE(player);
    auto object = world.CreateObject({2, 0, 0});
    REQUIRE(object);
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("wall"), 0}));
    world.UpdatePlayerObservations();
    const auto id = world.GetActor(*player)->id;
    CHECK(world.GetRememberedTile(id, {2, 0, 0}).terrain.value == 0);
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetObjectAppearance(*object, ObjectKind::Chair));
    world.UpdatePlayerObservations();
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetObjectAppearance(*object, ObjectKind::Table));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(id, {2, 0, 0}).object.kind == ObjectKind::Chair);
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("floor"), 0}));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(id, {2, 0, 0}).object.kind == ObjectKind::Table);
    REQUIRE(world.DestroyObject(*object));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(id, {2, 0, 0}).object.kind == ObjectKind::None);
}

TEST_CASE("Unchanged chunk demand is cheap and administrative changes still reconcile",
    "[perception][bulk][streaming]") {
    World world(TestContent());
    auto request = world.RequestChunks({{0, 0}, 0}, LoadMode::active);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.ResetPerceptionStats();
    REQUIRE(world.UpdateRequest(*request, {{0, 0, 4}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetPerceptionStats().demandReconciliations == 0);
    auto admin = world.RequestChunks({{-2, 0}, 0}, LoadMode::resident);
    REQUIRE(admin);
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetChunk({-2, 0, 0}) != nullptr);
    CHECK(world.GetPerceptionStats().demandReconciliations == 1);
    REQUIRE(world.ReleaseRequest(*admin));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetChunk({-2, 0, 0}) == nullptr);
    CHECK(world.GetPerceptionStats().demandReconciliations == 2);
}

TEST_CASE("Cached terrain filtering agrees with point perception for independent observers",
    "[perception][bulk][cache][lighting]") {
    World world(TestContent());
    Load(world);
    Fill(world, 8, -1, 0, TestTerrain("floor"));
    Fill(world, 8, 1, 3, TestTerrain("air"));
    auto first = world.CreateActor({-3, -2, 0}, true);
    auto second = world.CreateActor({3, 2, 0}, true);
    REQUIRE(first);
    REQUIRE(second);
    auto lamp = world.CreateObject({-2, 0, 0}, true);
    REQUIRE(lamp);
    REQUIRE(world.SetObjectLight(*lamp, {255, 6}));
    REQUIRE(world.SetTile({0, 0, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("stairs"), 0}));
    REQUIRE(world.SetTile({1, 0, 1}, {TestTerrain("stairs"), 0}));
    VisionProfile vision;
    vision.range = 8;
    REQUIRE(world.SetVisionProfile(vision));
    for (std::uint8_t sky : {std::uint8_t{0}, std::uint8_t{48}, std::uint8_t{255}}) {
        world.SetSkyLight(sky);
        for (ActorHandle observer : {*first, *second}) {
            const auto origin = world.GetActor(observer)->position;
            for (int y : {-1, 0, 2}) {
                for (int z : {-6, 0, 6}) {
                    for (int x : {-6, 0, 6}) {
                        const TileCoord target{x, z, y};
                        REQUIRE(world.CanPlayerSee(observer, target) == CanSeeTile(world, origin, target, vision));
                    }
                }
            }
        }
    }
    CHECK(world.GetPlayerMemories().empty());
}

TEST_CASE("Radius64 mask has a circular boundary and bounded planar work",
    "[perception][bulk][radius64]") {
    World world(TestContent());
    Load(world, 4);
    Fill(world, 64, 0, 0, TestTerrain("floor"));
    VisibilityMask mask;
    PerceptionStats stats;
    BuildVisibilityMask(world, {0, 0, 0}, 64, mask, stats);
    CHECK(mask.Contains({64, 0, 0}));
    CHECK(mask.Contains({-64, 0, 0}));
    CHECK_FALSE(mask.Contains({64, 1, 0}));
    CHECK_FALSE(mask.Contains({0, 0, -1}));
    CHECK(stats.pointLosCalls == 0);
    for (int z = -64; z <= 64; ++z) {
        for (int x = -64; x <= 64; ++x) {
            REQUIRE(mask.Contains({x, z, 0}) == (x*x + z*z <= 64*64));
        }
    }
    CHECK(stats.traversedVoxels <= 1 + 4 * 64 * 66);
    CHECK(mask.cells.empty());
    OpticalField field;
    field.Rebuild(world);
    VisibilityWorkspace workspace;
    BuildVisibilityMask(world, {0, 0, 0}, 64, mask, stats, &field, &workspace);
    CHECK(workspace.optics.size() == static_cast<std::size_t>(workspace.width) * workspace.depth);
    CHECK(workspace.projectionSamples == 0);
    // Buffer edges are missing blockers, never out-of-bounds reads or endpoints.
    Vector<std::uint8_t> edge(9, 1);
    const auto visible = Planar(edge, 3, 3, 0, 0, 64);
    for (auto cell : visible) { CHECK(cell == 1); }
    std::uint64_t visits = 0;
    Planar(edge, 3, 3, 0, 0, 0, &visits);
    CHECK(visits == 1);
}

TEST_CASE("Negative chunk seams do not change planar projection or mask packing",
    "[perception][bulk][bounds]") {
    World world(TestContent());
    Load(world, 2);
    Fill(world, 24, -2, 1, TestTerrain("air"));
    Fill(world, 24, -3, -3, TestTerrain("floor"));
    VisibilityMask first;
    VisibilityMask second;
    PerceptionStats stats;
    BuildVisibilityMask(world, {-16, -16, 0}, 5, first, stats);
    BuildVisibilityMask(world, {0, 0, 0}, 5, second, stats);
    for (int y = -4; y <= 1; ++y) {
        for (int z = -5; z <= 5; ++z) {
            for (int x = -5; x <= 5; ++x) {
                CHECK(first.Get({x - 16, z - 16, y}) == second.Get({x, z, y}));
            }
        }
    }
    REQUIRE(first.cells.empty());
    first.MaterializeLayer(-3);
    CHECK(first.cells.size() == 121);
    CHECK(first.Contains({-16, -16, -3}));
    first.cells[first.Index({-16, -16, -3})] = 0;
    CHECK_FALSE(first.Contains({-16, -16, -3}));
    CHECK(first.Contains({-16, -16, -2}));
    BuildVisibilityMask(world, {0, 0, world_min_y}, 0, first, stats);
    CHECK(first.Contains({0, 0, world_min_y}));
    CHECK_FALSE(first.Contains({0, 0, world_min_y - 1}));
    BuildVisibilityMask(world, {0, 0, world_max_y}, 0, first, stats);
    CHECK_FALSE(first.Contains({0, 0, world_max_y + 1}));
}

TEST_CASE("World horizontal limits clip implicit circles and missing observer planes",
    "[perception][bulk][bounds]") {
    World world(TestContent());
    const TileCoord origin{world_coordinate_limit, -world_coordinate_limit, 0};
    REQUIRE(world.RequestChunks({ChunkOf(origin), 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile(origin, {TestTerrain("floor"), 0}));
    VisibilityMask mask;
    PerceptionStats stats;
    BuildVisibilityMask(world, origin, 4, mask, stats);
    CHECK(mask.Contains(origin));
    CHECK_FALSE(mask.Contains({origin.x + 1, origin.z, 0}));
    CHECK_FALSE(mask.Contains({origin.x, origin.z - 1, 0}));
    BuildVisibilityMask(world, {0, 0, 0}, 4, mask, stats);
    CHECK(mask.cells.empty());
    CHECK_FALSE(mask.Contains({0, 0, 0}));
    CHECK_FALSE(LineVisible(world, origin, {origin.x - 65, origin.z, 0}));
}

TEST_CASE("Observer parking gravity profiles and residency invalidate authoritative caches",
    "[perception][bulk][cache][gravity]") {
    World world(TestContent());
    auto request = world.RequestChunks({{0, 0}, 1}, LoadMode::active);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    Fill(world, 12, -1, 0, TestTerrain("floor"));
    Fill(world, 12, 1, world_max_y, TestTerrain("air"));
    auto player = world.CreateActor({0, 0, 0}, true);
    REQUIRE(player);
    REQUIRE(world.SetActorLight(*player, {192, 2}));
    world.UpdatePlayerObservations();
    world.ResetPerceptionStats();
    REQUIRE(world.SetTile({0, 0, 0}, {TestTerrain("air"), 0}));
    CHECK(world.GetActor(*player)->position == TileCoord{0, 0, -1});
    CHECK(world.CanPlayerSee(*player, {0, 0, -1}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 1);
    CHECK(world.GetPerceptionStats().lightingRebuilds == 1);
    REQUIRE(world.SetActorParked(*player, true));
    CHECK_FALSE(world.CanPlayerSee(*player, {0, 0, -1}));
    REQUIRE(world.SetTile({1, 0, -1}, {TestTerrain("grass"), 0}));
    const auto memory = world.GetPlayerMemories();
    world.UpdatePlayerObservations();
    CHECK(world.GetPlayerMemories() == memory);
    REQUIRE(world.SetActorParked(*player, false));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(world.GetActor(*player)->id, {1, 0, -1}).terrain == TestTerrain("grass"));
    VisionProfile invalid;
    invalid.range = 65;
    CHECK(world.SetVisionProfile(invalid) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.GetVisionProfile().range == 64);
    VisionProfile narrow;
    narrow.range = 6;
    REQUIRE(world.SetVisionProfile(narrow));
    world.ResetPerceptionStats();
    CHECK(world.CanPlayerSee(*player, {0, 0, -1}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 1);
    REQUIRE(world.UpdateRequest(*request, {{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.ResetPerceptionStats();
    CHECK_FALSE(world.CanPlayerSee(*player, {-1, 0, -1}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 1);
}

TEST_CASE("Clear slab caches use implicit masks and learn only the new footprint",
    "[perception][bulk][cache][memory]") {
    World world(TestContent());
    Load(world);
    Fill(world, 15, 0, 0, TestTerrain("floor"));
    Fill(world, 15, 1, world_max_y, TestTerrain("air"));
    OpticalField optics;
    optics.Rebuild(world);
    VisibilityMask mask;
    PerceptionStats stats;
    BuildVisibilityMask(world, {0, 0, 0}, 12, mask, stats, &optics);
    REQUIRE(mask.cells.empty());
    for (int y = world_min_y; y <= world_max_y; ++y) {
        for (int z = -13; z <= 13; ++z) {
            const auto row = mask.ClearRow(z, y);
            for (int x = -13; x <= 13; ++x) {
                const bool expected = y == 0 && x*x + z*z <= 12*12;
                CHECK(mask.Contains({x, z, y}) == expected);
                CHECK((x >= row.first && x <= row.second) == expected);
            }
        }
    }
    auto player = world.CreateActor({0, 0, 0}, true);
    REQUIRE(player);
    VisionProfile vision;
    vision.range = 12;
    REQUIRE(world.SetVisionProfile(vision));
    world.UpdatePlayerObservations();
    world.ResetPerceptionStats();
    REQUIRE(world.MoveActor(*player, {1, 0, 0}));
    world.UpdatePlayerObservations();
    const auto work = world.GetPerceptionStats();
    CHECK(work.opticalRebuilds == 0);
    CHECK(work.filterTilesSampled == 0);
    CHECK(work.memoryTilesExamined == work.memoryTilesChanged);
    CHECK(work.memoryTilesExamined > 0);
    CHECK(work.memoryTilesExamined < 300);
    // Light-only invalidation materializes implicit planes on demand, without
    // losing their geometry or confusing a dark filtered mask with clear sky.
    world.SetSkyLight(0);
    CHECK_FALSE(world.CanPlayerSee(*player, {10, 0, 0}));
    world.SetSkyLight(192);
    CHECK(world.CanPlayerSee(*player, {10, 0, 0}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 1);
}

TEST_CASE("Bulk geometry benchmark", "[.][bulk-benchmark]") {
    World world(TestContent());
    Load(world, 4);
    Fill(world, 64, 0, 0, TestTerrain("floor"));
    OpticalField optics;
    optics.Rebuild(world);
    for (int radius : {12, 64}) {
        VisibilityMask mask;
        PerceptionStats stats;
        Vector<double> samples;
        for (int i = 0; i < 9; ++i) {
            const auto start = std::chrono::steady_clock::now();
            BuildVisibilityMask(world, {0, 0, 0}, radius, mask, stats, &optics);
            samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        eastl::sort(samples.begin(), samples.end());
        std::printf("bulk radius=%d median_ms=%.3f p95_ms=%.3f traversed=%llu\n", radius,
            samples[4], samples[8],
            static_cast<unsigned long long>(stats.traversedVoxels));
    }
    auto player = world.CreateActor({0, 0, 0}, true);
    REQUIRE(player);
    for (int radius : {12, 64}) {
        VisionProfile vision;
        vision.range = radius;
        REQUIRE(world.SetVisionProfile(vision));
        world.UpdatePlayerObservations();
        world.ResetPerceptionStats();
        Vector<double> samples;
        Vector<double> filtering;
        Vector<double> learning;
        for (int i = 0; i < 9; ++i) {
            const auto start = std::chrono::steady_clock::now();
            REQUIRE(world.MoveActor(*player, {i % 2, 0, 0}));
            world.CanPlayerSee(*player, world.GetActor(*player)->position);
            const auto filtered = std::chrono::steady_clock::now();
            world.UpdatePlayerObservations();
            const auto learned = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(learned - start).count());
            filtering.push_back(std::chrono::duration<double, std::milli>(filtered - start).count());
            learning.push_back(std::chrono::duration<double, std::milli>(learned - filtered).count());
        }
        eastl::sort(samples.begin(), samples.end());
        eastl::sort(filtering.begin(), filtering.end());
        eastl::sort(learning.begin(), learning.end());
        const auto stats = world.GetPerceptionStats();
        std::printf("move_observe radius=%d median_ms=%.3f p95_ms=%.3f geometry=%llu lighting=%llu changed=%llu\n",
            radius, samples[4], samples[8], static_cast<unsigned long long>(stats.geometryRebuilds),
            static_cast<unsigned long long>(stats.lightingRebuilds), static_cast<unsigned long long>(stats.memoryTilesChanged));
        std::printf("phases radius=%d geometry_filter_median_ms=%.3f memory_median_ms=%.3f optics=%llu planar_refreshes=%llu sampled=%llu memory_examined=%llu\n",
            radius, filtering[4], learning[4], static_cast<unsigned long long>(stats.opticalRebuilds), static_cast<unsigned long long>(stats.volumeRebuilds),
            static_cast<unsigned long long>(stats.filterTilesSampled), static_cast<unsigned long long>(stats.memoryTilesExamined));
        Vector<double> refilter;
        world.ResetPerceptionStats();
        for (int i = 0; i < 9; ++i) {
            vision.glareThreshold = static_cast<std::uint8_t>(241 + i % 2);
            REQUIRE(world.SetVisionProfile(vision));
            const auto start = std::chrono::steady_clock::now();
            world.CanPlayerSee(*player, world.GetActor(*player)->position);
            refilter.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        eastl::sort(refilter.begin(), refilter.end());
        std::printf("filter_only radius=%d median_ms=%.3f p95_ms=%.3f geometry=%llu\n", radius,
            refilter[4], refilter[8], static_cast<unsigned long long>(world.GetPerceptionStats().geometryRebuilds));
    }
    for (int y = 0; y <= 2; ++y) {
        for (int z = -4; z <= 4; ++z) {
            REQUIRE(world.SetTile({6, z, y}, {TestTerrain("wall"), 0}));
        }
    }
    REQUIRE(world.SetTile({-1, -1, 0}, {TestTerrain("stairs"), 0}));
    REQUIRE(world.SetTile({-1, -1, -1}, {TestTerrain("stairs"), 0}));
    optics.Rebuild(world);
    for (int radius : {12, 64}) {
        Vector<double> samples;
        PerceptionStats stats;
        VisibilityMask mask;
        VisibilityWorkspace workspace;
        for (int i = 0; i < 9; ++i) {
            const auto start = std::chrono::steady_clock::now();
            BuildVisibilityMask(world, {0, 0, 0}, radius, mask, stats, &optics, &workspace);
            samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        eastl::sort(samples.begin(), samples.end());
        std::printf("wall_stairs_bulk radius=%d median_ms=%.3f p95_ms=%.3f traversed_per_build=%llu\n",
            radius, samples[4], samples[8], static_cast<unsigned long long>(stats.traversedVoxels / 9));
    }
}

TEST_CASE("Chunk aligned observer planes reuse and update local optical changes",
    "[perception][bulk][cache][planar]") {
    World world(TestContent());
    Load(world);
    Fill(world, 12, -1, 0, TestTerrain("floor"));
    Fill(world, 12, 1, 3, TestTerrain("air"));
    REQUIRE(world.SetTile({6, 0, 0}, {TestTerrain("wall"), 0}));
    OpticalField optics;
    optics.Rebuild(world);
    VisibilityWorkspace workspace;
    VisibilityMask mask;
    PerceptionStats work;
    for (int originX : {0, 1, 8}) {
        const TileCoord origin{originX, 0, 0};
        BuildVisibilityMask(world, origin, 8, mask, work, &optics, &workspace);
        CHECK(mask.Contains(origin));
        CHECK(mask.Contains({6, 0, 0}));
        CHECK_FALSE(mask.Contains({originX, 0, 1}));
        CHECK_FALSE(mask.Contains({originX, 0, -1}));
        CHECK(workspace.optics.size() == static_cast<std::size_t>(workspace.width) * workspace.depth);
        CHECK(work.volumeRebuilds == (originX == 8 ? 2 : 1));
    }
    REQUIRE(world.SetTile({6, 0, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({7, 0, 0}, {TestTerrain("wall"), 0}));
    const ChunkCoord changed[] = {{0, 0, 0}};
    optics.Refresh(world, {changed, 1});
    const auto opticalRevision = optics.revision;
    optics.Refresh(world, {changed, 1});
    CHECK(optics.revision == opticalRevision);
    BuildVisibilityMask(world, {8, 0, 0}, 8, mask, work, &optics, &workspace);
    CHECK(work.volumeRebuilds == 3);
    CHECK_FALSE(mask.Contains({6, 0, 0}));
    REQUIRE(world.SetTile({-12, -12, 2}, {TestTerrain("wall"), 0}));
    const ChunkCoord unrelated[] = {{-1, -1, 2}};
    optics.Refresh(world, {unrelated, 1});
    BuildVisibilityMask(world, {8, 0, 0}, 8, mask, work, &optics, &workspace);
    CHECK(work.volumeRebuilds == 3); // Other elevation did not rewrite this plane.
    BuildVisibilityMask(world, {8, 0, 0}, 8, mask, work, nullptr, &workspace);
    CHECK_FALSE(mask.Contains({6, 0, 0}));
    REQUIRE(world.SetTile({7, 0, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({6, 0, 0}, {TestTerrain("wall"), 0}));
    BuildVisibilityMask(world, {8, 0, 0}, 8, mask, work, nullptr, &workspace);
    CHECK(mask.Contains({6, 0, 0}));
}

TEST_CASE("Optical snapshot refresh inserts and removes exact residency coordinates",
    "[perception][bulk][cache][streaming]") {
    World world(TestContent());
    OpticalField field;
    const ChunkCoord changed[] = {{-1, 0, 0}, {-1, 0, 0}};
    auto request = world.RequestChunks({{-1, 0}, 0}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    field.Refresh(world, {changed, 2});
    REQUIRE(field.Find(changed[0]) != nullptr);
    const auto revision = field.revision;
    field.Refresh(world, {changed, 2});
    CHECK(field.revision == revision);
    REQUIRE(world.ReleaseRequest(*request));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.GetChunk(changed[0]) == nullptr);
    field.Refresh(world, {changed, 2});
    CHECK(field.Find(changed[0]) == nullptr);
    CHECK(field.revision == revision + 1);
}

TEST_CASE("Warm server movement and view benchmark", "[.][bulk-server-benchmark]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    for (ActorHandle actor : world.ActiveActors()) {
        if (actor != server.GetPlayer()) { REQUIRE(world.DestroyActor(actor)); }
    }
    Fill(world, 76, 0, 0, TestTerrain("floor"));
    Fill(world, 76, 1, world_max_y, TestTerrain("air"));
    REQUIRE(world.MoveActor(server.GetPlayer(), {4, 0, 0}));
    for (int radius : {12, 64}) {
        VisionProfile vision;
        vision.range = radius;
        REQUIRE(world.SetVisionProfile(vision));
        world.UpdatePlayerObservations();
        server.View(8);
        // Warm both positions so the distribution measures routine loaded work.
        REQUIRE(server.Send({CommandKind::move, 1, 0}).advanced);
        server.View(8);
        REQUIRE(server.Send({CommandKind::move, -1, 0}).advanced);
        server.View(8);
        world.ResetPerceptionStats();
        Vector<double> samples;
        for (int i = 0; i < 41; ++i) {
            const auto start = std::chrono::steady_clock::now();
            REQUIRE(server.Send({CommandKind::move, i % 2 == 0 ? 1 : -1, 0}).advanced);
            const auto view = server.View(8);
            REQUIRE(!view.cells.empty());
            samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        eastl::sort(samples.begin(), samples.end());
        const auto work = world.GetPerceptionStats();
        std::printf("server_move_view range=%d median_ms=%.3f p95_ms=%.3f p99_ms=%.3f optics=%llu geometry=%llu filter_sampled=%llu memory_examined=%llu lighting=%llu demand=%llu\n",
            radius, samples[20], samples[38], samples[40], static_cast<unsigned long long>(work.opticalRebuilds),
            static_cast<unsigned long long>(work.geometryRebuilds), static_cast<unsigned long long>(work.filterTilesSampled),
            static_cast<unsigned long long>(work.memoryTilesExamined), static_cast<unsigned long long>(work.lightingRebuilds),
            static_cast<unsigned long long>(work.demandReconciliations));
    }
}
}
