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
    const auto patch = GeneratePlanningPatch(42, {0, 0});
    REQUIRE_FALSE(patch.settlements.empty());
    const auto plan = GenerateRegionPlan(content, 42, RegionOf(patch.settlements.front().center));
    REQUIRE_FALSE(plan.buildings.empty());
    Vector<GeneratedBuildingPower> first;
    Vector<GeneratedBuildingPower> repeated;
    unsigned checked = 0;
    for (const auto& building : plan.buildings) {
        if (!Contains(plan.bounds, building.bounds.min)
            || !Contains(plan.bounds, {building.bounds.max.x - 1, building.bounds.max.z - 1})) { continue; }
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
        ++checked;
    }
    CHECK(checked > 0);
    // Clears previous output even when no building intersects the layer.
    first.push_back({});
    GenerateBuildingPower(content, plan, {plan.region.x * (region_size / chunk_size),
        plan.region.z * (region_size / chunk_size), world_max_y}, first);
    CHECK(first.empty());
}

TEST_CASE("Generation eight retains complete cross-region building and power bounds",
    "[worldgen][power][seams]") {
    Diagnostics diagnostics;
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded.has_value());
    const auto& content = *loaded;
    const auto patch = GeneratePlanningPatch(42, {0, 0});
    bool checked = false;
    // Discover a seam from canonical geometry, not hundreds of speculative region plans.
    for (const auto& settlement : patch.settlements) {
        if (settlement.scale != SettlementScale::Large) { continue; }
        const auto city = GenerateSettlementPlan(content, 42, settlement);
        for (const auto& building : city.buildings) {
            const auto firstRegion = RegionOf(building.bounds.min);
            const auto lastPosition = TileCoord{building.bounds.max.x - 1, building.bounds.max.z - 1};
            const auto lastRegion = RegionOf(lastPosition);
            if (firstRegion == lastRegion) { continue; }
            const auto descriptor = DescribeBuildingPower(content.GetBuildingPattern(building.pattern),
                building.bounds, building.basementVariant);
            if (!descriptor) { continue; }
            const auto first = GenerateRegionPlan(content, 42, firstRegion);
            const auto last = GenerateRegionPlan(content, 42, lastRegion);
            CHECK(first.generationVersion == 8);
            CHECK(last.generationVersion == world_generation_version);
            const auto* a = BuildingAt(first, building.bounds.min);
            const auto* b = BuildingAt(last, lastPosition);
            REQUIRE(a);
            REQUIRE(b);
            CHECK(*a == building);
            CHECK(*b == building);
            Vector<GeneratedBuildingPower> fromFirst, fromLast;
            for (int y = descriptor->bounds.min.y; y <= descriptor->bounds.max.y; ++y) {
                const auto firstChunk = ChunkOf({building.bounds.min.x, building.bounds.min.z, y});
                const auto lastChunk = ChunkOf({lastPosition.x, lastPosition.z, y});
                GenerateBuildingPower(content, first, firstChunk, fromFirst);
                GenerateBuildingPower(content, last, lastChunk, fromLast);
                unsigned firstMatches = 0, lastMatches = 0;
                for (const auto& power : fromFirst) { firstMatches += power == *descriptor; }
                for (const auto& power : fromLast) { lastMatches += power == *descriptor; }
                CHECK(firstMatches == 1);
                CHECK(lastMatches == 1);
                // Repeated materialization stays independent of power descriptor requests.
                const auto tiles = GenerateChunk(content, 42, first, firstChunk);
                ChunkPlacements before, after;
                GenerateChunkPlacements(content, 42, first, firstChunk, before);
                GenerateBuildingPower(content, last, lastChunk, fromLast);
                CHECK(tiles == GenerateChunk(content, 42, first, firstChunk));
                GenerateChunkPlacements(content, 42, first, firstChunk, after);
                CHECK(before == after);
            }
            checked = true;
            break;
        }
        if (checked) { break; }
    }
    REQUIRE(checked);
}
}
