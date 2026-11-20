#include "worldgen/worldgen.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/algorithm.h>
#include <EASTL/utility.h>
#include <cstdlib>

namespace {
using namespace cataclysmx;

bool Overlap(TileRect a, TileRect b) {
    return a.min.x < b.max.x && b.min.x < a.max.x
        && a.min.z < b.max.z && b.min.z < a.max.z;
}

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

ContentCatalog Catalog(const String& patterns) {
    String text = R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
       appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
       appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,
       open_space:true,looks_like:'floor'}
    )";
    if (!patterns.empty()) { text += ','; text += patterns; }
    text += "]}";
    const ContentSource source{"city-test.json5", View(text)};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

String Site(const char* id, const char* metadata = "") {
    String text = "{type:'building_pattern',id:'";
    text += id;
    text += "',rows:['.......','.......','.......','.......','.......'],"
            "palette:{'.':'floor'},entrance:{x:3,z:0}";
    text += metadata;
    text += '}';
    return text;
}

const SettlementDescriptor& Large(const PlanningPatch& patch) {
    for (const auto& settlement : patch.settlements) {
        if (settlement.scale == SettlementScale::Large) { return settlement; }
    }
    FAIL("Interior patch has no large settlement");
    std::abort();
}

TEST_CASE("Planning patches have stable scale shares and disjoint descriptors", "[city][worldgen][planning]") {
    REQUIRE(world_generation_version == 8);
    for (const RegionCoord origin : {RegionCoord{6, 6}, {-12, -6}, {12, -12}}) {
        const auto patch = GeneratePlanningPatch(73, origin);
        CHECK(patch.origin == origin);
        for (int z = 0; z < planning_patch_regions; ++z) {
            for (int x = 0; x < planning_patch_regions; ++x) {
                CHECK(GeneratePlanningPatch(73, {origin.x + x, origin.z + z}) == patch);
            }
        }
        REQUIRE(patch.settlements.size() == 10);
        int small = 0;
        int medium = 0;
        int large = 0;
        for (std::size_t i = 0; i < patch.settlements.size(); ++i) {
            const auto& s = patch.settlements[i];
            CHECK(s.id.region == origin);
            CHECK(Contains(s.bounds, s.center));
            const int width = s.bounds.max.x - s.bounds.min.x;
            const int height = s.bounds.max.z - s.bounds.min.z;
            const int longer = eastl::max(width, height);
            const int shorter = eastl::min(width, height);
            CHECK(shorter * 2 >= longer);
            switch (s.scale) {
            case SettlementScale::Small: ++small; CHECK(longer >= 96); CHECK(longer <= 144); break;
            case SettlementScale::Medium: ++medium; CHECK(longer >= 192); CHECK(longer <= 256); break;
            case SettlementScale::Large: ++large; CHECK(longer >= 384); CHECK(longer <= 512); break;
            }
            for (std::size_t j = 0; j < i; ++j) {
                CHECK_FALSE(s.id == patch.settlements[j].id);
                CHECK_FALSE(Overlap(s.bounds, patch.settlements[j].bounds));
            }
        }
        CHECK(small == 6);
        CHECK(medium == 3);
        CHECK(large == 1);
        for (const RegionCoord neighbor : {RegionCoord{origin.x + 6, origin.z},
                                          {origin.x, origin.z + 6}, {origin.x + 6, origin.z + 6}}) {
            const auto adjacent = GeneratePlanningPatch(73, neighbor);
            for (const auto& a : patch.settlements) {
                for (const auto& b : adjacent.settlements) { CHECK_FALSE(Overlap(a.bounds, b.bounds)); }
            }
        }
    }
}

TEST_CASE("Finite boundary patches have valid nonoverlapping settlement descriptors", "[city][worldgen][planning][coordinates]") {
    for (const RegionCoord corner : {RegionCoord{-3907, -3907}, {3906, 3906}, {-3907, 3906}, {3906, -3907}}) {
        const auto patch = GeneratePlanningPatch(91, corner);
        CHECK(patch.settlements.size() <= 10);
        for (std::size_t i = 0; i < patch.settlements.size(); ++i) {
            const auto& s = patch.settlements[i];
            CHECK(ValidPosition(s.bounds.min));
            CHECK(ValidPosition({s.bounds.max.x - 1, s.bounds.max.z - 1}));
            for (std::size_t j = 0; j < i; ++j) { CHECK_FALSE(Overlap(s.bounds, patch.settlements[j].bounds)); }
        }
    }
}

TEST_CASE("Dense city region extraction preserves canonical buildings and population", "[city][worldgen][seams]") {
    constexpr std::uint64_t seed = 42;
    const auto patch = GeneratePlanningPatch(seed, {6, 6});
    const auto& descriptor = Large(patch);
    const auto full = GenerateSettlementPlan(TestContent(), seed, descriptor);
    CHECK(full == GenerateSettlementPlan(TestContent(), seed, descriptor));
    CHECK(full.settlement == descriptor);
    // These are accepted typical site targets, not an exact seeded layout.
    CHECK(full.buildings.size() >= 120);
    CHECK(full.buildings.size() <= 200);
    const auto first = RegionOf(descriptor.bounds.min);
    const auto last = RegionOf({descriptor.bounds.max.x - 1, descriptor.bounds.max.z - 1});
    REQUIRE((first.x != last.x || first.z != last.z));
    for (int z = first.z; z <= last.z; ++z) {
        for (int x = first.x; x <= last.x; ++x) {
            const auto region = GenerateRegionPlan(TestContent(), seed, {x, z});
            CHECK(region == GenerateRegionPlan(TestContent(), seed, {x, z}));
            for (const auto& building : full.buildings) {
                if (!Overlap(building.bounds, region.bounds)) { continue; }
                CHECK(eastl::find(region.buildings.begin(), region.buildings.end(), building) != region.buildings.end());
                const auto otherRegion = RegionOf({building.bounds.max.x - 1, building.bounds.max.z - 1});
                if (RegionOf(building.bounds.min) != otherRegion
                    && region.region == RegionOf(building.bounds.min)) {
                    const auto other = GenerateRegionPlan(TestContent(), seed, otherRegion);
                    CHECK(eastl::find(other.buildings.begin(), other.buildings.end(), building) != other.buildings.end());
                    Vector<GeneratedBuildingPower> a;
                    Vector<GeneratedBuildingPower> b;
                    GenerateBuildingPower(TestContent(), region, ChunkOf(building.bounds.min), a);
                    GenerateBuildingPower(TestContent(), other,
                        ChunkOf({building.bounds.max.x - 1, building.bounds.max.z - 1}), b);
                    for (const auto& power : a) {
                        if (power.origin != building.bounds.min) { continue; }
                        CHECK(eastl::find(b.begin(), b.end(), power) != b.end());
                    }
                }
            }
            for (const auto& parcel : full.parcels) {
                if (!Overlap(parcel.bounds, region.bounds)) { continue; }
                CHECK(eastl::find(region.parcels.begin(), region.parcels.end(), parcel) != region.parcels.end());
            }
            for (const auto& monster : full.monsters) {
                if (!Contains(region.bounds, monster.position)) { continue; }
                CHECK(eastl::find(region.urbanMonsters.begin(), region.urbanMonsters.end(), monster)
                      != region.urbanMonsters.end());
            }
            for (const auto& monster : region.urbanMonsters) {
                if (monster.key.owner.region != descriptor.id.region
                    || monster.key.owner.settlement != descriptor.id.ordinal) { continue; }
                CHECK(Contains(region.bounds, monster.position));
                CHECK(eastl::find(full.monsters.begin(), full.monsters.end(), monster) != full.monsters.end());
            }
        }
    }
}

TEST_CASE("Family selection does not count equally fitting variants as extra families", "[city][worldgen][families]") {
    const auto a = Site("a", ",family:'house',family_weight:7");
    const auto b = Site("b", ",family:'house',family_weight:7,weight:1000000");
    const auto singleton = Site("other", ",weight:7");
    const auto baseline = Catalog(a + ',' + singleton);
    const auto expanded = Catalog(a + ',' + b + ',' + singleton);
    const auto reordered = Catalog(singleton + ',' + b + ',' + a);
    int named = 0;
    int other = 0;
    const auto patch = GeneratePlanningPatch(42, {6, 6});
    for (const auto& descriptor : patch.settlements) {
        if (descriptor.scale != SettlementScale::Small) { continue; }
        const auto before = GenerateSettlementPlan(baseline, 42, descriptor);
        const auto after = GenerateSettlementPlan(expanded, 42, descriptor);
        const auto reverse = GenerateSettlementPlan(reordered, 42, descriptor);
        REQUIRE(before.buildings.size() == after.buildings.size());
        REQUIRE(reverse.buildings.size() == after.buildings.size());
        for (std::size_t i = 0; i < after.buildings.size(); ++i) {
            const auto& oldPattern = baseline.GetBuildingPattern(before.buildings[i].pattern);
            const auto& newPattern = expanded.GetBuildingPattern(after.buildings[i].pattern);
            CHECK(oldPattern.family == newPattern.family);
            CHECK(before.buildings[i].bounds == after.buildings[i].bounds);
            CHECK(reordered.GetBuildingPattern(reverse.buildings[i].pattern).id == newPattern.id);
            CHECK(reverse.buildings[i].bounds == after.buildings[i].bounds);
            named += !newPattern.family.empty();
            other += newPattern.family.empty();
        }
    }
    CHECK(named > 0);
    CHECK(other > 0);
}

TEST_CASE("Terrain-only and nonfitting catalogs finish without phantom buildings", "[city][worldgen][content]") {
    const auto empty = Catalog({});
    String huge = "{type:'building_pattern',id:'huge',rows:[";
    for (int z = 0; z < 64; ++z) {
        if (z) { huge += ','; }
        huge += '\'';
        for (int x = 0; x < 64; ++x) { huge += '.'; }
        huge += '\'';
    }
    huge += "],palette:{'.':'floor'},entrance:{x:0,z:0}}";
    const auto noFit = Catalog(huge);
    const auto patch = GeneratePlanningPatch(19, {6, 6});
    for (const auto& descriptor : patch.settlements) {
        if (descriptor.scale != SettlementScale::Small) { continue; }
        for (const auto* content : {&empty, &noFit}) {
            const auto plan = GenerateSettlementPlan(*content, 19, descriptor);
            CHECK(plan.buildings.empty());
            CHECK(plan.parcels.empty());
        }
    }
}
}
