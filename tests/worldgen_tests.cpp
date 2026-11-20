#include "worldgen/worldgen.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace {
using namespace cataclysmx;

TEST_CASE("Region coordinates floor negative tiles and include world extremes", "[city][worldgen][coordinates]") {
    CHECK(RegionOf({0, 255}) == RegionCoord{0, 0});
    CHECK(RegionOf({256, -1}) == RegionCoord{1, -1});
    CHECK(RegionOf({-256, -257}) == RegionCoord{-1, -2});
    CHECK(RegionOf({-1, -256}) == RegionCoord{-1, -1});
    CHECK(RegionOf({-world_coordinate_limit, world_coordinate_limit}) == RegionCoord{-3907, 3906});
    CHECK(RegionOf({world_coordinate_limit, -world_coordinate_limit}) == RegionCoord{3906, -3907});
    for (int x : {-world_coordinate_limit, world_coordinate_limit}) {
        for (int z : {-world_coordinate_limit, world_coordinate_limit}) {
            const TileCoord p{x, z};
            const auto plan = GenerateRegionPlan(TestContent(), 91, RegionOf(p));
            CHECK(Contains(plan.bounds, p));
            const auto sample = SampleEnvironment(91, p);
            CHECK(sample.moisture >= 0);
            CHECK(sample.moisture <= 1000);
            CHECK(sample.temperature >= 0);
            CHECK(sample.temperature <= 1000);
            CHECK(sample.woodland >= 0);
            CHECK(sample.woodland <= 1000);
        }
    }
}

TEST_CASE("Environment is deterministic and continuous across signed region seams", "[city][worldgen][environment]") {
    bool meadow = false;
    bool woodland = false;
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
        for (int along = -1024; along <= 1024; along += 127) {
            for (int border : {0, 256}) {
                for (bool xAxis : {false, true}) {
                    const TileCoord a = xAxis ? TileCoord{border - 1, along} : TileCoord{along, border - 1};
                    const TileCoord b = xAxis ? TileCoord{border, along} : TileCoord{along, border};
                    const auto first = SampleEnvironment(seed, a);
                    const auto second = SampleEnvironment(seed, b);
                    CHECK(first == SampleEnvironment(seed, a));
                    CHECK(std::abs(first.moisture - second.moisture) <= 16);
                    CHECK(std::abs(first.temperature - second.temperature) <= 16);
                    CHECK(std::abs(first.woodland - second.woodland) <= 16);
                    for (const auto sample : {first, second}) {
                        CHECK(sample.moisture >= 0);
                        CHECK(sample.moisture <= 1000);
                        CHECK(sample.temperature >= 0);
                        CHECK(sample.temperature <= 1000);
                        CHECK(sample.woodland >= 0);
                        CHECK(sample.woodland <= 1000);
                        meadow |= sample.biome == Biome::Meadow;
                        woodland |= sample.biome == Biome::Woodland;
                    }
                }
            }
        }
    }
    CHECK(meadow);
    CHECK(woodland);
}

TEST_CASE("Worldgen validates required terrain roles", "[city][worldgen][content]") {
    Diagnostics diagnostics;
    CHECK(ValidateWorldgenContent(ContentCatalog{}, &diagnostics)
          == eastl::unexpected(ContentError::InvalidData));
    CHECK(FormatDiagnostics(diagnostics).find("floor") != String::npos);
    CHECK(ValidateWorldgenContent(TestContent()));
}

TEST_CASE("Partial edge chunks contain valid terrain and wall outside the world", "[city][worldgen][coordinates]") {
    for (int x : {-world_coordinate_limit, world_coordinate_limit}) {
        for (int z : {-world_coordinate_limit, world_coordinate_limit}) {
            const TileCoord corner{x, z};
            const auto chunk = ChunkOf(corner);
            const auto plan = GenerateRegionPlan(TestContent(), 77, RegionOf(corner));
            const auto tiles = GenerateChunk(TestContent(), 77, plan, chunk);
            int validCount = 0;
            for (int dz = 0; dz < chunk_size; ++dz) {
                for (int dx = 0; dx < chunk_size; ++dx) {
                    const TileCoord p{chunk.x * chunk_size + dx, chunk.z * chunk_size + dz};
                    const auto tile = tiles[static_cast<std::size_t>(dz * chunk_size + dx)];
                    if (ValidPosition(p)) {
                        ++validCount;
                        CHECK(Contains(plan.bounds, p));
                        CHECK(TestContent().TryGetTerrain(tile.terrain) != nullptr);
                    } else {
                        CHECK(tile == Tile{TestTerrain("wall"), 0});
                    }
                }
            }
            CHECK(validCount == (x < 0 ? 16 : 1) * (z < 0 ? 16 : 1));
        }
    }
}

TEST_CASE("Demo clearing remains floor across region and chunk seams", "[city][worldgen][clearing]") {
    for (const RegionCoord region : {RegionCoord{-1, -1}, {-1, 0}, {0, -1}, {0, 0}}) {
        const auto plan = GenerateRegionPlan(TestContent(), 42, region);
        const int minX = region.x < 0 ? -32 : 0;
        const int minZ = region.z < 0 ? -32 : 0;
        const int maxX = region.x < 0 ? 0 : 33;
        const int maxZ = region.z < 0 ? 0 : 33;
        for (int z = minZ; z < maxZ; z += chunk_size) {
            for (int x = minX; x < maxX; x += chunk_size) {
                const auto chunk = ChunkOf({x, z});
                const auto tiles = GenerateChunk(TestContent(), 42, plan, chunk);
                for (int dz = 0; dz < chunk_size && z + dz < maxZ; ++dz) {
                    for (int dx = 0; dx < chunk_size && x + dx < maxX; ++dx) {
                        const TileCoord p{x + dx, z + dz};
                        CHECK(tiles[static_cast<std::size_t>(TileIndex(p))] == Tile{TestTerrain("floor"), 0});
                        CHECK_FALSE(IsRoad(plan, p));
                        CHECK(BuildingAt(plan, p) == nullptr);
                    }
                }
            }
        }
    }
}
}
