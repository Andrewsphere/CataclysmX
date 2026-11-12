#include "actors/ai_vision.h"
#include "core/world.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <climits>

namespace {
using namespace cataclysmx;

void Load(World& world, int radius = 1) {
    REQUIRE(world.RequestChunks({{0, 0}, radius}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
}

void Fill(World& world, TileCoord low, TileCoord high, TerrainId terrain) {
    for (int y = low.y; y <= high.y; ++y) {
        for (int z = low.z; z <= high.z; ++z) {
            for (int x = low.x; x <= high.x; ++x) {
                REQUIRE(world.SetTile({x, z, y}, {terrain, 0}));
            }
        }
    }
}

void CheckBoth(const World& world, TileCoord a, TileCoord b, bool expected) {
    INFO("segment " << a.x << ',' << a.z << ',' << a.y << " -> "
        << b.x << ',' << b.z << ',' << b.y);
    CHECK(AiLineVisible(world, a, b) == expected);
    CHECK(AiLineVisible(world, b, a) == expected);
}

TEST_CASE("AI sight checks horizontal volumes and opaque endpoints", "[ai][vision]") {
    World world(TestContent());
    Load(world);
    Fill(world, {-3, 0, 0}, {3, 0, 0}, TestTerrain("floor"));
    CheckBoth(world, {-3, 0, 0}, {3, 0, 0}, true);
    REQUIRE(world.CreateActor({0, 0, 0}));
    CheckBoth(world, {-3, 0, 0}, {3, 0, 0}, true); // Bodies are not volumes.
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("wall"), 0}));
    CheckBoth(world, {-3, 0, 0}, {3, 0, 0}, false);
    CheckBoth(world, {0, 0, 0}, {1, 0, 0}, false);
    CheckBoth(world, {1, 0, 0}, {1, 0, 0}, false);
    CheckBoth(world, {-3, 0, 0}, {-3, 0, 0}, true);
}

TEST_CASE("AI sight never acquires missing geometry", "[ai][vision]") {
    World world(TestContent());
    const TileCoord a{15, 0, 0};
    const TileCoord b{17, 0, 0};
    CheckBoth(world, a, a, false);
    // Only the endpoints' columns are resident: the middle is unavailable.
    REQUIRE(world.RequestChunks({{0, 0}, 0}, LoadMode::resident));
    REQUIRE(world.RequestChunks({{2, 0}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile(a, {TestTerrain("floor"), 0}));
    const TileCoord far{32, 0, 0};
    REQUIRE(world.SetTile(far, {TestTerrain("floor"), 0}));
    const auto revision = world.GetRevision();
    CheckBoth(world, a, b, false); // Missing endpoint.
    CheckBoth(world, a, far, false); // Missing interior.
    CHECK(world.GetTile({16, 0, 0}) == nullptr);
    CHECK(world.GetRevision() == revision);
    CHECK_FALSE(world.IsActive(a));
}

TEST_CASE("AI floors shafts and paired stairs have explicit vertical optics", "[ai][vision]") {
    World world(TestContent());
    Load(world);
    Fill(world, {0, 0, -2}, {0, 0, 2}, TestTerrain("air"));
    CheckBoth(world, {0, 0, -2}, {0, 0, 2}, true);
    REQUIRE(world.SetTile({0, 0, 0}, {TestTerrain("floor"), 0}));
    CheckBoth(world, {0, 0, -1}, {0, 0, 0}, false); // Upper tile's lower face.
    CheckBoth(world, {0, 0, 0}, {0, 0, 1}, true); // Lower tile's floor is below the ray.
    CheckBoth(world, {0, 0, -2}, {0, 0, 2}, false);
    REQUIRE(world.SetTile({0, 0, 0}, {TestTerrain("stairs"), 0}));
    CheckBoth(world, {0, 0, -1}, {0, 0, 0}, false); // A lone stair is not an aperture.
    REQUIRE(world.SetTile({0, 0, -1}, {TestTerrain("stairs"), 0}));
    CheckBoth(world, {0, 0, -1}, {0, 0, 0}, true);
    REQUIRE(world.SetTile({0, 0, 0}, {TestTerrain("floor"), 0}));
    CheckBoth(world, {0, 0, -1}, {0, 0, 0}, false); // Lower stairs alone do not open upper floor.
}

TEST_CASE("AI sees exposed balconies only when the segment misses their slab", "[ai][vision]") {
    World world(TestContent());
    Load(world);
    Fill(world, {-4, -1, -1}, {1, 1, 1}, TestTerrain("air"));
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("floor"), 0}));
    // At the layer boundary x=-1.5, outside the balcony column.
    CheckBoth(world, {-3, 0, 0}, {0, 0, 1}, true);
    CheckBoth(world, {0, 0, 0}, {0, 0, 1}, false);
    REQUIRE(world.SetTile({-2, 0, 1}, {TestTerrain("floor"), 0}));
    CheckBoth(world, {-3, 0, 0}, {0, 0, 1}, false); // Now the slab touches the segment.
}

TEST_CASE("AI supercover checks every edge and corner incident volume and floor", "[ai][vision]") {
    World world(TestContent());
    Load(world);
    Fill(world, {-2, -2, -2}, {1, 1, 1}, TestTerrain("air"));
    const TileCoord origin{-2, -2, -2};
    const TileCoord edge{-1, -1, -2};
    const TileCoord corner{-1, -1, -1};
    CheckBoth(world, origin, edge, true);
    CheckBoth(world, origin, corner, true);
    AiVisionStats cornerStats;
    CHECK(AiLineVisible(world, origin, corner, &cornerStats));
    CHECK(cornerStats.cells == 8);
    // Each of the seven non-origin cells at the first 3-axis tie matters.
    for (unsigned subset = 1; subset < 8; ++subset) {
        const TileCoord touched{-2 + ((subset & 1U) ? 1 : 0),
            -2 + ((subset & 2U) ? 1 : 0), -2 + ((subset & 4U) ? 1 : 0)};
        REQUIRE(world.SetTile(touched, {TestTerrain("wall"), 0}));
        CheckBoth(world, origin, corner, false);
        REQUIRE(world.SetTile(touched, {TestTerrain("air"), 0}));
    }
    REQUIRE(world.SetTile({-1, -2, -2}, {TestTerrain("wall"), 0}));
    CheckBoth(world, origin, edge, false);
    REQUIRE(world.SetTile({-1, -2, -2}, {TestTerrain("air"), 0}));
    // All four upper tiles own a floor face touched at the corner, including
    // the two side columns that neither endpoint occupies.
    for (int z = -2; z <= -1; ++z) {
        for (int x = -2; x <= -1; ++x) {
            REQUIRE(world.SetTile({x, z, -1}, {TestTerrain("floor"), 0}));
            CheckBoth(world, origin, corner, false);
            REQUIRE(world.SetTile({x, z, -1}, {TestTerrain("air"), 0}));
        }
    }
    // A two-axis x/y tie must check the side column's floor too.
    REQUIRE(world.SetTile({-2, -2, -1}, {TestTerrain("floor"), 0}));
    CheckBoth(world, origin, {-1, -2, -1}, false);
}

TEST_CASE("AI rational crossing order works in mixed signed octants", "[ai][vision]") {
    World world(TestContent());
    Load(world);
    Fill(world, {-4, -2, -1}, {4, 2, 1}, TestTerrain("air"));
    for (int sx : {-1, 1}) {
        for (int sz : {-1, 1}) {
            for (int sy : {-1, 1}) {
                const TileCoord target{4 * sx, 2 * sz, sy};
                const TileCoord onRay{2 * sx, sz, 0};
                const TileCoord offRay{0, 2 * sz, sy};
                CheckBoth(world, {}, target, true);
                REQUIRE(world.SetTile(offRay, {TestTerrain("wall"), 0}));
                CheckBoth(world, {}, target, true);
                REQUIRE(world.SetTile(onRay, {TestTerrain("wall"), 0}));
                CheckBoth(world, {}, target, false);
                REQUIRE(world.SetTile(onRay, {TestTerrain("air"), 0}));
                REQUIRE(world.SetTile(offRay, {TestTerrain("air"), 0}));
            }
        }
    }
}

TEST_CASE("AI range bounds and work counters are local and cumulative", "[ai][vision]") {
    World world(TestContent());
    Load(world, 2);
    Fill(world, {-16, 0, 0}, {16, 0, 0}, TestTerrain("floor"));
    AiVisionStats stats;
    CHECK(AiLineVisible(world, {-16, 0, 0}, {16, 0, 0}, &stats));
    CHECK(stats.rays == 1);
    CHECK(stats.cells == 33);
    CHECK_FALSE(AiLineVisible(world, {-16, 0, 0}, {17, 0, 0}, &stats));
    CHECK_FALSE(AiLineVisible(world, {INT_MIN, 0, 0}, {INT_MAX, 0, 0}, &stats));
    CHECK_FALSE(AiLineVisible(world, {0, 0, world_min_y - 1}, {0, 0, 0}, &stats));
    CHECK(stats.rays == 4);
    CHECK(stats.cells == 33); // Rejected before terrain reads.
    CHECK(AiLineVisible(world, {0, 0, 0}, {0, 0, 0}, &stats));
    CHECK(stats.rays == 5);
    CHECK(stats.cells == 34);
    REQUIRE(world.SetTile({-15, 0, 0}, {TestTerrain("wall"), 0}));
    stats = {};
    CHECK_FALSE(AiLineVisible(world, {-16, 0, 0}, {16, 0, 0}, &stats));
    CHECK(stats.cells == 2); // Early obstruction, no endpoint prefetch.
}

}
