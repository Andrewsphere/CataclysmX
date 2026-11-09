#include "core/world.h"
#include "core/perception.h"
#include "core/server.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <EASTL/sort.h>

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

void Differential(World& world, TileCoord origin, int radius) {
    VisibilityMask mask;
    PerceptionStats stats;
    BuildVisibilityMask(world, origin, radius, mask, stats);
    REQUIRE(stats.geometryRebuilds == 1);
    CHECK(stats.pointLosCalls == 0);
    for (int y = world_min_y; y <= world_max_y; ++y) {
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                const TileCoord target{origin.x + x, origin.z + z, y};
                const int dy = y - origin.y;
                const bool expected = x*x + z*z + dy*dy <= radius*radius
                    && LineVisible(world, origin, target);
                INFO("origin " << origin.x << ',' << origin.z << ',' << origin.y
                    << " target " << target.x << ',' << target.z << ',' << target.y);
                REQUIRE(mask.Contains(target) == expected);
            }
        }
    }
}

TEST_CASE("Bulk integer visibility matches reference through sparse multilayer geometry",
    "[perception][bulk][3d]") {
    World world(TestContent());
    Load(world);
    std::uint32_t rng = 0x12345678;
    const TerrainId choices[] = {TestTerrain("air"), TestTerrain("floor"),
        TestTerrain("wall"), TestTerrain("stairs")};
    for (int scene = 0; scene < 4; ++scene) {
        Fill(world, 10, -3, 3, choices[0]);
        for (int i = 0; i < 180; ++i) {
            const auto next = [&]() { rng = rng * 1664525U + 1013904223U; return rng; };
            const int x = static_cast<int>((next() >> 16) % 21) - 10;
            const int z = static_cast<int>((next() >> 16) % 21) - 10;
            const int y = static_cast<int>((next() >> 16) % 7) - 3;
            REQUIRE(world.SetTile({x, z, y}, {choices[(next() >> 16) % 4], 0}));
        }
        Differential(world, {-2, -1, 0}, 8);
        Differential(world, {1, 2, 2}, 7);
    }
}

TEST_CASE("Bulk masks preserve opaque endpoints corners floors stairs bounds and residency",
    "[perception][bulk][3d]") {
    World world(TestContent());
    Load(world);
    Fill(world, 9, -2, 2, TestTerrain("air"));
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetTile({0, 1, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetTile({-1, -1, 0}, {TestTerrain("stairs"), 0}));
    REQUIRE(world.SetTile({-1, -1, 1}, {TestTerrain("stairs"), 0}));
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("floor"), 0}));
    Differential(world, {0, 0, 0}, 8);
    Differential(world, {-1, -1, 0}, 8);
    Differential(world, {-15, -15, world_max_y}, 5);
    Differential(world, {0, 0, world_min_y}, 5);
    // The starting tile's opacity is exempt, exactly as in the reference.
    Differential(world, {1, 0, 0}, 6);
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
            for (int y = -1; y <= 2; ++y) {
                for (int z = -6; z <= 6; ++z) {
                    for (int x = -6; x <= 6; ++x) {
                        const TileCoord target{x, z, y};
                        REQUIRE(world.CanPlayerSee(observer, target) == CanSeeTile(world, origin, target, vision));
                    }
                }
            }
        }
    }
    CHECK(world.GetPlayerMemories().empty());
}

TEST_CASE("Radius64 mask has exact spherical boundary and shares occlusion work",
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
    world.ResetPerceptionStats();
    for (int z = -64; z <= 64; ++z) {
        for (int x = -64; x <= 64; ++x) {
            if (x*x + z*z > 64*64) { continue; }
            REQUIRE(mask.Contains({x, z, 0}) == LineVisible(world, {0, 0, 0}, {x, z, 0}));
        }
    }
    CHECK(stats.traversedVoxels < world.GetPerceptionStats().traversedVoxels);
    std::uint32_t rng = 0x2468ACE1;
    const auto next = [&]() { rng = rng * 1664525U + 1013904223U; return rng >> 16; };
    const TerrainId terrain[] = {TestTerrain("air"), TestTerrain("wall"), TestTerrain("floor"), TestTerrain("stairs")};
    for (int i = 0; i < 100; ++i) {
        const int x = static_cast<int>(next() % 129) - 64;
        const int z = static_cast<int>(next() % 129) - 64;
        const int y = static_cast<int>(next() % 7) - 3;
        REQUIRE(world.SetTile({x, z, y}, {terrain[next() % 4], 0}));
    }
    BuildVisibilityMask(world, {0, 0, 0}, 64, mask, stats);
    for (int i = 0; i < 4096; ++i) {
        const int x = static_cast<int>(next() % 129) - 64;
        const int z = static_cast<int>(next() % 129) - 64;
        const int y = static_cast<int>(next() % 21) - 10;
        const TileCoord target{x, z, y};
        INFO("radius64 target " << x << ',' << z << ',' << y);
        REQUIRE(mask.Contains(target) == (x*x + z*z + y*y <= 64*64
            && LineVisible(world, {0, 0, 0}, target)));
    }
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
                const bool expected = y >= 0 && x*x + z*z + y*y <= 12*12;
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
    // Light-only invalidation materializes implicit slabs on demand, without
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
        std::printf("phases radius=%d geometry_filter_median_ms=%.3f memory_median_ms=%.3f optics=%llu volumes=%llu sampled=%llu memory_examined=%llu\n",
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

TEST_CASE("Chunk aligned optical volumes reuse and invalidate without changing integer visibility",
    "[perception][bulk][cache][3d]") {
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
        for (int y = -8; y <= 8; ++y) {
            for (int z = -8; z <= 8; ++z) {
                for (int x = -8; x <= 8; ++x) {
                    const TileCoord target{originX + x, z, y};
                    REQUIRE(mask.Contains(target) == (x*x + z*z + y*y <= 64 && LineVisible(world, origin, target)));
                }
            }
        }
        CHECK(work.volumeRebuilds == (originX == 8 ? 2 : 1));
    }
    REQUIRE(world.SetTile({6, 0, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({7, 0, 0}, {TestTerrain("wall"), 0}));
    optics.Rebuild(world);
    BuildVisibilityMask(world, {8, 0, 0}, 8, mask, work, &optics, &workspace);
    CHECK(work.volumeRebuilds == 3);
    CHECK(mask.Contains({6, 0, 0}) == LineVisible(world, {8, 0, 0}, {6, 0, 0}));
    BuildVisibilityMask(world, {8, 0, 0}, 8, mask, work, nullptr, &workspace);
    CHECK_FALSE(mask.Contains({6, 0, 0}));
    REQUIRE(world.SetTile({7, 0, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({6, 0, 0}, {TestTerrain("wall"), 0}));
    BuildVisibilityMask(world, {8, 0, 0}, 8, mask, work, nullptr, &workspace);
    CHECK(mask.Contains({6, 0, 0}));
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
