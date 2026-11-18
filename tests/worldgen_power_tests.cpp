#include "worldgen/worldgen.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;

TEST_CASE("Building power descriptors include selected basement and authored floors", "[worldgen][power]") {
    BuildingPattern pattern;
    pattern.layers.resize(2);
    pattern.layers[0].y = 1;
    pattern.layers[1].y = 3;
    pattern.basements.resize(2);
    pattern.basements[0].layer.y = -1;
    pattern.basements[1].layer.y = -2;
    const TileRect footprint{{-17, -9}, {-5, -1}};
    const auto firstHouse = DescribeBuildingPower(pattern, footprint, 2);
    REQUIRE(firstHouse.has_value());
    CHECK(firstHouse->origin == TileCoord{-17, -9, 0});
    CHECK(firstHouse->bounds == PowerBounds{{-17, -9, -2}, {-6, -2, 3}});
    CHECK(DescribeBuildingPower(pattern, footprint, 0)->bounds.min.y == 0);
    CHECK(DescribeBuildingPower(pattern, footprint, 1)->bounds.min.y == -1);
    pattern.site = BuildingSite::Park;
    CHECK_FALSE(DescribeBuildingPower(pattern, footprint, 0).has_value());
    pattern.site = BuildingSite::Square;
    CHECK_FALSE(DescribeBuildingPower(pattern, footprint, 0).has_value());
}

TEST_CASE("Building power descriptors are complete and stable across chunk request order", "[worldgen][power]") {
    const auto& content = TestContent();
    const auto plan = GenerateRegionPlan(content, 42, {0, 0});
    REQUIRE_FALSE(plan.buildings.empty());
    Vector<GeneratedBuildingPower> first;
    Vector<GeneratedBuildingPower> repeated;
    for (const auto& building : plan.buildings) {
        const auto expected = DescribeBuildingPower(content.GetBuildingPattern(building.pattern),
            building.bounds, building.basementVariant);
        if (!expected) { continue; }
        const auto minChunk = ChunkOf(expected->bounds.min);
        const auto maxChunk = ChunkOf(expected->bounds.max);
        GenerateBuildingPower(content, plan, maxChunk, first);
        GenerateBuildingPower(content, plan, minChunk, repeated);
        bool inFirst = false;
        bool inRepeated = false;
        for (const auto& descriptor : first) { inFirst |= descriptor == *expected; }
        for (const auto& descriptor : repeated) { inRepeated |= descriptor == *expected; }
        CHECK(inFirst);
        CHECK(inRepeated);
        GenerateBuildingPower(content, plan, maxChunk, repeated);
        CHECK(first == repeated);
    }
    // Clears previous output even when no building intersects the layer.
    first.push_back({});
    GenerateBuildingPower(content, plan, {0, 0, world_max_y}, first);
    CHECK(first.empty());
}

TEST_CASE("Generation seven preserves version six plans terrain and population", "[worldgen][power]") {
    Diagnostics diagnostics;
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded.has_value());
    const auto& content = *loaded;
    for (const RegionCoord region : {RegionCoord{0, 0}, RegionCoord{-1, -1}, RegionCoord{3906, 3906}}) {
        const auto legacy = GenerateRegionPlan(content, 42, region, 6);
        const auto current = GenerateRegionPlan(content, 42, region, 7);
        auto comparable = current;
        CHECK(current.generationVersion == 7);
        comparable.generationVersion = 6;
        CHECK(comparable == legacy);
        ChunkPlacements oldPlacements;
        ChunkPlacements newPlacements;
        for (const auto& building : legacy.buildings) {
            const auto descriptor = DescribeBuildingPower(content.GetBuildingPattern(building.pattern),
                building.bounds, building.basementVariant);
            const int minY = descriptor ? descriptor->bounds.min.y : 0;
            const int maxY = descriptor ? descriptor->bounds.max.y : 0;
            for (int y = minY; y <= maxY; ++y) {
                const auto chunk = ChunkOf({building.bounds.min.x, building.bounds.min.z, y});
                CHECK(GenerateChunk(content, 42, legacy, chunk) == GenerateChunk(content, 42, current, chunk));
                GenerateChunkPlacements(content, 42, legacy, chunk, oldPlacements);
                GenerateChunkPlacements(content, 42, current, chunk, newPlacements);
                CHECK(oldPlacements == newPlacements);
            }
        }
    }
}
}
