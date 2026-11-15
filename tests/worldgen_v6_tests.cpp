#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/algorithm.h>
#include <EASTL/utility.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

String Site(const char* id, int width, int depth, const char* metadata = "",
            PatternPoint entrance = {0, 0}) {
    String text = "{type:'building_pattern',id:'";
    text += id;
    text += "',rows:[";
    for (int z = 0; z < depth; ++z) {
        if (z) { text += ','; }
        text += '\'';
        for (int x = 0; x < width; ++x) { text += '.'; }
        text += '\'';
    }
    char buffer[100];
    std::snprintf(buffer, sizeof(buffer), "],palette:{'.':'floor'},entrance:{x:%d,z:%d}", entrance.x, entrance.z);
    text += buffer;
    text += metadata;
    text += '}';
    return text;
}

ContentCatalog Catalog(const String& patterns, bool trees = false) {
    String text = R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
        appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
        appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,
        open_space:true,looks_like:'floor'},
      {type:'terrain',id:'grass',name:'Grass',movement:{passable:true},blocks_sight:false,looks_like:'floor'},
      {type:'terrain',id:'road',name:'Road',movement:{passable:true},blocks_sight:false,looks_like:'floor'},
      {type:'terrain',id:'pavement',name:'Pavement',movement:{passable:true},blocks_sight:false,looks_like:'floor'}
    )";
    if (trees) {
        text += R"(,{type:'terrain',id:'tree',name:'Tree',movement:{passable:false},
            blocks_sight:true,looks_like:'wall'})";
    }
    if (!patterns.empty()) { text += ','; text += patterns; }
    text += "]}";
    const ContentSource source{"v6.json5", View(text)};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

bool Overlap(TileRect a, TileRect b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.z < b.max.z && b.min.z < a.max.z;
}
bool On(RoadSegment road, TileCoord p) {
    const int radius = road.width / 2;
    return p.x >= eastl::min(road.first.x, road.last.x) - radius
        && p.x <= eastl::max(road.first.x, road.last.x) + radius
        && p.z >= eastl::min(road.first.z, road.last.z) - radius
        && p.z <= eastl::max(road.first.z, road.last.z) + radius;
}
Tile At(const ContentCatalog& content, const RegionPlan& plan, TileCoord p) {
    return GenerateChunk(content, plan.seed, plan, ChunkOf(p))[static_cast<std::size_t>(TileIndex(p))];
}
TileCoord TowardStreet(Frontage direction) {
    switch (direction) {
    case Frontage::North: return {0, -1};
    case Frontage::East: return {1, 0};
    case Frontage::South: return {0, 1};
    case Frontage::West: return {-1, 0};
    }
    std::abort();
}

// Flood only declared road/driveway and alley tiles, so grass cannot hide a
// disconnected rear lane or driveway. This oracle never calls IsRoad.
void CheckNetwork(const RegionPlan& plan) {
    Vector<std::uint8_t> network(region_size * region_size, 0);
    const auto index = [&](TileCoord p) {
        return static_cast<std::size_t>((p.z - plan.bounds.min.z) * region_size + p.x - plan.bounds.min.x);
    };
    for (int z = plan.bounds.min.z; z < plan.bounds.max.z; ++z) {
        for (int x = plan.bounds.min.x; x < plan.bounds.max.x; ++x) {
            const TileCoord p{x, z};
            bool paved = false;
            for (const auto road : plan.roads) { paved |= On(road, p); }
            for (const auto& building : plan.buildings) {
                paved |= !Contains(building.bounds, p) && On(building.driveway, p);
            }
            for (const auto alley : plan.serviceAlleys) { paved |= Contains(alley, p); }
            network[index(p)] = paved ? 1 : 0;
        }
    }
    Vector<TileCoord> queue{plan.townCenter};
    REQUIRE(network[index(plan.townCenter)] == 1);
    network[index(plan.townCenter)] = 2;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto p = queue[head];
        for (const TileCoord next : {TileCoord{p.x - 1, p.z}, {p.x + 1, p.z}, {p.x, p.z - 1}, {p.x, p.z + 1}}) {
            if (!Contains(plan.bounds, next) || network[index(next)] != 1) { continue; }
            network[index(next)] = 2;
            queue.push_back(next);
        }
    }
    for (const auto state : network) { REQUIRE(state != 1); }
}

TEST_CASE("V6 profiles pack bounded four-way sites and connected shared access", "[worldgen][v6][roads]") {
    const auto content = Catalog(Site("home", 7, 5, "", {3, 0}) + ','
        + Site("wide", 17, 9, "", {8, 0}) + ','
        + Site("hospital", 40, 23, ",site:'landmark'", {20, 0}) + ','
        + Site("park", 7, 5, ",site:'park'", {3, 0}) + ','
        + Site("square", 9, 7, ",site:'square'", {4, 0}));
    bool profiles[4]{};
    bool directions[4]{};
    bool sawAlleys = false;
    bool sawWide = false;
    Vector<std::size_t> counts;
    for (std::uint64_t seed = 0; seed < 48; ++seed) {
        const auto plan = GenerateRegionPlan(content, seed, {-2, 1}, 6);
        CAPTURE(seed, static_cast<int>(plan.townProfile));
        REQUIRE(plan == GenerateRegionPlan(content, seed, plan.region, 6));
        REQUIRE(plan.hasTown);
        REQUIRE(plan.townProfile != TownProfile::LegacyGrid);
        profiles[static_cast<unsigned>(plan.townProfile)] = true;
        REQUIRE(plan.roads.Size() >= 14);
        REQUIRE(plan.roads.Size() <= 16);
        REQUIRE(plan.parcels.size() == plan.buildings.size());
        counts.push_back(plan.parcels.size());
        int hospital = 0;
        int park = 0;
        int square = 0;
        for (std::size_t i = 0; i < plan.parcels.size(); ++i) {
            const auto& parcel = plan.parcels[i];
            const auto& building = plan.buildings[i];
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            REQUIRE(parcel.owner.parcel == i);
            CHECK(building.owner == parcel.owner);
            directions[static_cast<unsigned>(parcel.frontage)] = true;
            CHECK(Contains(plan.bounds, parcel.bounds.min));
            CHECK(Contains(plan.bounds, {parcel.bounds.max.x - 1, parcel.bounds.max.z - 1}));
            CHECK(Contains(parcel.bounds, building.bounds.min));
            CHECK(Contains(parcel.bounds, {building.bounds.max.x - 1, building.bounds.max.z - 1}));
            const bool horizontal = parcel.frontage == Frontage::North || parcel.frontage == Frontage::South;
            CHECK((horizontal ? parcel.bounds.max.x - parcel.bounds.min.x : parcel.bounds.max.z - parcel.bounds.min.z)
                == (horizontal ? building.bounds.max.x - building.bounds.min.x : building.bounds.max.z - building.bounds.min.z) + 2);
            const auto step = TowardStreet(parcel.frontage);
            const TileCoord outside{building.entrance.x + step.x, building.entrance.z + step.z};
            CHECK_FALSE(Contains(building.bounds, outside));
            CHECK(Contains(parcel.bounds, outside));
            CHECK(IsRoad(plan, outside));
            CHECK(building.driveway.last == (horizontal ? TileCoord{building.entrance.x, parcel.streetZ}
                : TileCoord{parcel.streetX, building.entrance.z}));
            CHECK((horizontal ? parcel.streetX : parcel.streetZ) == 0);
            const int rear = parcel.frontage == Frontage::North ? parcel.bounds.max.z - building.bounds.max.z
                : parcel.frontage == Frontage::South ? building.bounds.min.z - parcel.bounds.min.z
                : parcel.frontage == Frontage::West ? parcel.bounds.max.x - building.bounds.max.x
                : building.bounds.min.x - parcel.bounds.min.x;
            CHECK(rear >= 1);
            CHECK(rear <= 8);
            if (pattern.id == "hospital") { ++hospital; CHECK(rear >= 3); }
            park += pattern.site == BuildingSite::Park;
            square += pattern.site == BuildingSite::Square;
            sawWide |= pattern.id == "wide";
            for (std::size_t j = 0; j < i; ++j) { REQUIRE_FALSE(Overlap(parcel.bounds, plan.parcels[j].bounds)); }
            for (const auto road : plan.roads) {
                const TileRect footprint{{eastl::min(road.first.x, road.last.x) - 1, eastl::min(road.first.z, road.last.z) - 1},
                    {eastl::max(road.first.x, road.last.x) + 2, eastl::max(road.first.z, road.last.z) + 2}};
                REQUIRE_FALSE(Overlap(parcel.bounds, footprint));
            }
        }
        CHECK(hospital == 1);
        CHECK(park >= 1);
        CHECK(square == 1);
        sawAlleys |= !plan.serviceAlleys.empty();
        for (const auto alley : plan.serviceAlleys) {
            REQUIRE(Contains(plan.bounds, alley.min));
            REQUIRE(Contains(plan.bounds, {alley.max.x - 1, alley.max.z - 1}));
            for (const auto& parcel : plan.parcels) { REQUIRE_FALSE(Overlap(alley, parcel.bounds)); }
        }
        if (seed < 6) { CheckNetwork(plan); }
    }
    for (unsigned i = 1; i < 4; ++i) { CHECK(profiles[i]); }
    for (const auto direction : directions) { CHECK(direction); }
    CHECK(sawAlleys);
    CHECK(sawWide);
    bool varied = false;
    for (const auto count : counts) { varied |= count != counts.front(); }
    CHECK(varied);
}

TEST_CASE("V6 family prevalence is invariant under equal-fit variant multiplicity", "[worldgen][v6][families]") {
    const auto a = Site("a", 7, 5, ",family:'house',family_weight:7", {3, 0});
    const auto b = Site("b", 7, 5, ",family:'house',family_weight:7,weight:1000000", {3, 0});
    // A singleton with the same ID bytes as the named family remains separate.
    const auto singleton = Site("house", 7, 5, ",weight:7", {3, 0});
    const auto baseline = Catalog(a + ',' + singleton);
    const auto expanded = Catalog(a + ',' + b + ',' + singleton);
    const auto reordered = Catalog(singleton + ',' + b + ',' + a);
    int named = 0;
    int single = 0;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const auto before = GenerateRegionPlan(baseline, seed, {1, -1}, 6);
        const auto after = GenerateRegionPlan(expanded, seed, {1, -1}, 6);
        const auto reverse = GenerateRegionPlan(reordered, seed, {1, -1}, 6);
        CHECK(after.roads == before.roads);
        REQUIRE(after.buildings.size() == before.buildings.size());
        REQUIRE(reverse.buildings.size() == after.buildings.size());
        for (std::size_t i = 0; i < before.buildings.size(); ++i) {
            const auto& oldPattern = baseline.GetBuildingPattern(before.buildings[i].pattern);
            const auto& newPattern = expanded.GetBuildingPattern(after.buildings[i].pattern);
            CHECK(oldPattern.family == newPattern.family);
            CHECK(after.buildings[i].bounds == before.buildings[i].bounds);
            auto comparable = reverse.buildings[i];
            CHECK(reordered.GetBuildingPattern(comparable.pattern).id == newPattern.id);
            comparable.pattern = after.buildings[i].pattern;
            CHECK(comparable == after.buildings[i]);
            named += !newPattern.family.empty();
            single += newPattern.family.empty();
        }
        CHECK(reverse.parcels == after.parcels);
        CHECK(reverse.serviceAlleys == after.serviceAlleys);
    }
    CHECK(named > 0);
    CHECK(single > 0);
    CHECK(named < single * 2);
    CHECK(single < named * 2);
}

TEST_CASE("V6 rotates each authored entrance side toward all four frontage directions", "[worldgen][v6][rotation]") {
    for (const PatternPoint entry : {PatternPoint{3, 0}, {6, 2}, {3, 4}, {0, 2}}) {
        const auto content = Catalog(Site("asymmetric", 7, 5, "", entry));
        bool directions[4]{};
        for (std::uint64_t seed = 0; seed < 8; ++seed) {
            const auto plan = GenerateRegionPlan(content, seed, {1, 1}, 6);
            for (const auto& building : plan.buildings) {
                const auto& parcel = plan.parcels[building.owner.parcel];
                directions[static_cast<unsigned>(parcel.frontage)] = true;
                int x = entry.x;
                int z = entry.z;
                int width = 7;
                int height = 5;
                for (unsigned turn = 0; turn < building.quarterTurns; ++turn) {
                    const int oldX = x;
                    x = height - 1 - z;
                    z = oldX;
                    eastl::swap(width, height);
                }
                CHECK(building.entrance == TileCoord{building.bounds.min.x + x, building.bounds.min.z + z});
                CHECK(building.bounds.max.x - building.bounds.min.x == width);
                CHECK(building.bounds.max.z - building.bounds.min.z == height);
                const auto step = TowardStreet(parcel.frontage);
                const TileCoord front{building.entrance.x + step.x, building.entrance.z + step.z};
                CHECK_FALSE(Contains(building.bounds, front));
                CHECK(IsRoad(plan, front));
            }
        }
        for (const bool direction : directions) { CHECK(direction); }
    }
}

TEST_CASE("V6 no-fit empty and tiny catalogs terminate without phantom sites", "[worldgen][v6][bounds]") {
    const auto empty = Catalog({});
    const auto huge = Catalog(Site("huge", 64, 64));
    const auto tiny = Catalog(Site("tiny", 1, 1));
    const auto mixed = Catalog(Site("huge", 64, 64, ",family:'home',weight:1000000") + ','
        + Site("tiny", 1, 1, ",family:'home'"));
    for (std::uint64_t seed = 0; seed < 12; ++seed) {
        for (const auto* content : {&empty, &huge}) {
            const auto plan = GenerateRegionPlan(*content, seed, {0, 0}, 6);
            CHECK(plan.hasTown);
            CHECK(plan.parcels.empty());
            CHECK(plan.buildings.empty());
            CHECK(plan.serviceAlleys.empty());
            CHECK(plan.parcels.capacity() == 0);
            CHECK(plan.buildings.capacity() == 0);
        }
        for (const auto* content : {&tiny, &mixed}) {
            const auto plan = GenerateRegionPlan(*content, seed, {0, 0}, 6);
            REQUIRE(plan.parcels.size() > 24);
            for (std::size_t i = 0; i < plan.parcels.size(); ++i) {
                CHECK(content->GetBuildingPattern(plan.buildings[i].pattern).id == "tiny");
                CHECK(Contains(plan.bounds, plan.parcels[i].bounds.min));
                for (std::size_t j = 0; j < i; ++j) { REQUIRE_FALSE(Overlap(plan.parcels[i].bounds, plan.parcels[j].bounds)); }
            }
        }
    }
    for (const RegionCoord region : {RegionCoord{3906, 2}, {-3907, 2}, {2, 3906}, {2, -3907}, {-3907, -3907}, {3906, 3906}}) {
        const auto plan = GenerateRegionPlan(tiny, 42, region, 6);
        const auto legacy = GenerateRegionPlan(tiny, 42, region, 5);
        CHECK_FALSE(plan.hasTown);
        CHECK(plan.townProfile == TownProfile::LegacyGrid);
        CHECK(plan.roads == legacy.roads);
        CHECK(plan.boundaries == legacy.boundaries);
        CHECK(plan.parcels.empty());
        CHECK(plan.buildings.empty());
        CHECK(plan.serviceAlleys.empty());
    }
}

TEST_CASE("V6 paving is selective and materialization is independent of chunk order", "[worldgen][v6][terrain]") {
    const auto content = Catalog(Site("home", 7, 5, "", {3, 0}));
    const auto plan = GenerateRegionPlan(content, 42, {1, 1}, 6);
    REQUIRE_FALSE(plan.serviceAlleys.empty());
    for (const auto& building : plan.buildings) {
        const auto& parcel = plan.parcels[building.owner.parcel];
        auto front = parcel.bounds.min;
        auto rear = parcel.bounds.min;
        switch (parcel.frontage) {
        case Frontage::North: rear.z = parcel.bounds.max.z - 1; break;
        case Frontage::South: front.z = parcel.bounds.max.z - 1; break;
        case Frontage::West: rear.x = parcel.bounds.max.x - 1; break;
        case Frontage::East: front.x = parcel.bounds.max.x - 1; break;
        }
        CHECK(At(content, plan, front).terrain == content.FindTerrain("pavement"));
        CHECK(At(content, plan, rear).terrain == content.FindTerrain("grass"));
    }
    for (const auto alley : plan.serviceAlleys) {
        const auto expected = IsRoad(plan, alley.min) ? "road" : "pavement";
        CHECK(At(content, plan, alley.min).terrain == content.FindTerrain(expected));
    }
    const TileCoord gap{plan.townBounds.min.x, plan.townBounds.min.z};
    CHECK_FALSE(IsRoad(plan, gap));
    CHECK(ParcelAt(plan, gap) == nullptr);
    CHECK(At(content, plan, gap).terrain == content.FindTerrain("grass"));
    Vector<eastl::array<Tile, chunk_size * chunk_size>> forward;
    for (int z = 18; z < 29; ++z) {
        for (int x = 18; x < 29; ++x) { forward.push_back(GenerateChunk(content, plan.seed, plan, {x, z})); }
    }
    std::size_t i = forward.size();
    for (int z = 28; z >= 18; --z) {
        for (int x = 28; x >= 18; --x) { CHECK(GenerateChunk(content, plan.seed, plan, {x, z}) == forward[--i]); }
    }
}

TEST_CASE("Mixed-depth rear yards reach service lanes through materialized woodland", "[worldgen][v6][alleys][terrain]") {
    const auto content = Catalog(Site("shallow", 7, 3, "", {3, 0}) + ','
        + Site("deep", 7, 17, "", {3, 0}), true);
    bool directions[4]{};
    int servedParcels = 0;
    int bridgedGaps = 0;
    int clearedTrees = 0;
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
        const auto plan = GenerateRegionPlan(content, seed, {1, 1}, 6);
        auto uncleared = plan;
        uncleared.serviceAlleys.clear();
        Vector<Tile> surface(region_size * region_size);
        Vector<Tile> natural(surface.size());
        const auto index = [&](TileCoord p) {
            return static_cast<std::size_t>((p.z - plan.bounds.min.z) * region_size + p.x - plan.bounds.min.x);
        };
        int trees = 0;
        for (int z = 16; z < 32; ++z) {
            for (int x = 16; x < 32; ++x) {
                const auto tiles = GenerateChunk(content, seed, plan, {x, z});
                const auto before = GenerateChunk(content, seed, uncleared, {x, z});
                for (int dz = 0; dz < chunk_size; ++dz) {
                    for (int dx = 0; dx < chunk_size; ++dx) {
                        const TileCoord p{x * chunk_size + dx, z * chunk_size + dz};
                        const auto local = static_cast<std::size_t>(dz * chunk_size + dx);
                        surface[index(p)] = tiles[local];
                        natural[index(p)] = before[local];
                        trees += tiles[local].terrain == content.FindTerrain("tree");
                    }
                }
            }
        }
        // Keep real blocking vegetation in this regression; an all-passable
        // natural palette would hide the original disconnected rear access.
        REQUIRE(trees > 100);
        Vector<std::uint8_t> reached(surface.size(), 0);
        Vector<TileCoord> queue{plan.townCenter};
        reached[index(plan.townCenter)] = 1;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const auto p = queue[head];
            for (const TileCoord next : {TileCoord{p.x - 1, p.z}, {p.x + 1, p.z}, {p.x, p.z - 1}, {p.x, p.z + 1}}) {
                if (!Contains(plan.bounds, next) || reached[index(next)]) { continue; }
                const auto terrain = surface[index(next)].terrain;
                if (terrain != content.FindTerrain("pavement") && terrain != content.FindTerrain("road")) { continue; }
                reached[index(next)] = 1;
                queue.push_back(next);
            }
        }
        // Recover occupied frontage runs independently of alley output order.
        // Their deepest rear edge determines the optional associated lane.
        for (std::size_t first = 0; first < plan.parcels.size();) {
            const auto& start = plan.parcels[first];
            const bool horizontal = start.frontage == Frontage::North || start.frontage == Frontage::South;
            const bool positive = start.frontage == Frontage::North || start.frontage == Frontage::West;
            const auto alongMin = [&](TileRect b) { return horizontal ? b.min.x : b.min.z; };
            const auto alongMax = [&](TileRect b) { return horizontal ? b.max.x : b.max.z; };
            const auto rearEdge = [&](TileRect b) {
                return horizontal ? (positive ? b.max.z : b.min.z) : (positive ? b.max.x : b.min.x);
            };
            std::size_t last = first + 1;
            int far = rearEdge(start.bounds);
            while (last < plan.parcels.size()) {
                const auto& next = plan.parcels[last];
                if (next.frontage != start.frontage || next.streetX != start.streetX || next.streetZ != start.streetZ
                    || alongMin(next.bounds) != alongMax(plan.parcels[last - 1].bounds)) { break; }
                far = positive ? eastl::max(far, rearEdge(next.bounds)) : eastl::min(far, rearEdge(next.bounds));
                ++last;
            }
            const TileRect* lane = nullptr;
            if (last - first >= 2) {
                for (const auto& alley : plan.serviceAlleys) {
                    const int low = horizontal ? alley.min.z : alley.min.x;
                    const int high = horizontal ? alley.max.z : alley.max.x;
                    if (low == (positive ? far : far - 3) && high == (positive ? far + 3 : far)
                        && alongMin(alley) <= alongMin(start.bounds)
                        && alongMax(alley) >= alongMax(plan.parcels[last - 1].bounds)) { lane = &alley; break; }
                }
            }
            if (lane) {
                for (auto i = first; i < last; ++i) {
                    const auto& parcel = plan.parcels[i];
                    const int middle = alongMin(parcel.bounds) + (alongMax(parcel.bounds) - alongMin(parcel.bounds)) / 2;
                    const int edge = rearEdge(parcel.bounds);
                    const auto front = TowardStreet(parcel.frontage);
                    // Start on the last rear-garden tile, then walk straight
                    // through the cleared gap until entering the shared lane.
                    const int rear = positive ? edge - 1 : edge;
                    TileCoord p = horizontal ? TileCoord{middle, rear} : TileCoord{rear, middle};
                    REQUIRE(Contains(parcel.bounds, p));
                    REQUIRE(content.GetTerrain(surface[index(p)].terrain).passable);
                    int gap = 0;
                    while (!Contains(*lane, p)) {
                        p.x -= front.x;
                        p.z -= front.z;
                        REQUIRE(Contains(plan.bounds, p));
                        REQUIRE(ParcelAt(plan, p) == nullptr);
                        REQUIRE(content.GetTerrain(surface[index(p)].terrain).passable);
                        REQUIRE(reached[index(p)] != 0);
                        if (!Contains(*lane, p)) {
                            ++gap;
                            clearedTrees += natural[index(p)].terrain == content.FindTerrain("tree");
                            REQUIRE(surface[index(p)].terrain == content.FindTerrain("pavement"));
                        }
                    }
                    ++servedParcels;
                    if (gap > 0) {
                        ++bridgedGaps;
                        directions[static_cast<unsigned>(parcel.frontage)] = true;
                    }
                }
            }
            first = last;
        }
        for (const auto alley : plan.serviceAlleys) {
            for (const auto& parcel : plan.parcels) { REQUIRE_FALSE(Overlap(alley, parcel.bounds)); }
            REQUIRE(Contains(plan.bounds, alley.min));
            REQUIRE(Contains(plan.bounds, {alley.max.x - 1, alley.max.z - 1}));
        }
    }
    CHECK(servedParcels > 0);
    CHECK(bridgedGaps > 0);
    CHECK(clearedTrees > 0);
    for (const bool direction : directions) { CHECK(direction); }
}

String CompositionPatterns(const char* use, bool extraVariant = false, const char* site = "building") {
    String metadata = ",family:'a_family',family_weight:1000000,use:'";
    metadata += use;
    metadata += "',site:'";
    metadata += site;
    metadata += '\'';
    String patterns = Site("a", 5, 5, metadata.c_str(), {2, 0});
    if (extraVariant) {
        patterns += ',';
        patterns += Site("a_variant", 5, 5, (metadata + ",weight:1000000").c_str(), {2, 0});
    }
    metadata = ",family:'b_family',family_weight:1000000,site:'";
    metadata += site;
    metadata += '\'';
    patterns += ',';
    patterns += Site("b", 5, 5, metadata.c_str(), {2, 0});
    return patterns;
}

struct SlotLocation {
    std::size_t street;
    int distance;
    int reach;
    bool centralMain;
    bool sideStreet;
    bool outerEnd;
};
SlotLocation LocateSlot(const RegionPlan& plan, const ParcelPlan& parcel) {
    const bool horizontal = parcel.frontage == Frontage::North || parcel.frontage == Frontage::South;
    const int leading = (horizontal ? parcel.bounds.min.x : parcel.bounds.min.z) + 1;
    const int center = horizontal ? plan.townCenter.x : plan.townCenter.z;
    // Only the initial town streets supply frontage. Connector segments extend
    // their axes but are deliberately excluded from this independent oracle.
    for (std::size_t i = 0; i < plan.roads.Size() - 12; ++i) {
        const auto road = plan.roads[i];
        if (horizontal ? road.first.z != parcel.streetZ || road.last.z != parcel.streetZ
            : road.first.x != parcel.streetX || road.last.x != parcel.streetX) { continue; }
        const int low = horizontal ? eastl::min(road.first.x, road.last.x) : eastl::min(road.first.z, road.last.z);
        const int high = horizontal ? eastl::max(road.first.x, road.last.x) : eastl::max(road.first.z, road.last.z);
        const int reach = leading < center ? center - low : high - center;
        const int distance = std::abs(leading - center);
        REQUIRE(reach > 0);
        REQUIRE(distance <= reach);
        return {i, distance, reach, i == 0 && distance <= 24, i != 0, distance * 4 >= reach * 3};
    }
    FAIL("Parcel has no associated town street");
}

TEST_CASE("V6 ordinary family preferences follow center side streets and actual outer arms", "[worldgen][v6][composition]") {
    const auto neutral = Catalog(CompositionPatterns("mixed"));
    struct PreferenceCase { const char* use; BuildingUse value; };
    for (const auto preference : {PreferenceCase{"residential", BuildingUse::Residential},
        {"commercial", BuildingUse::Commercial}, {"industrial", BuildingUse::Industrial}}) {
        const auto preferred = Catalog(CompositionPatterns(preference.use));
        int preferredHits = 0;
        int neutralHits = 0;
        int targetSlots = 0;
        int ordinarySlots = 0;
        int boundarySlots = 0;
        int boundaryPreferred = 0;
        int boundaryNeutral = 0;
        bool directions[4]{};
        bool at24 = false;
        bool at25 = false;
        bool atOuterBoundary = false;
        bool justInsideOuter = false;
        bool branch = false;
        for (std::uint64_t seed = 0; seed < 256; ++seed) {
            const auto before = GenerateRegionPlan(neutral, seed, {1, -1}, 6);
            const auto after = GenerateRegionPlan(preferred, seed, {1, -1}, 6);
            REQUIRE(before.roads == after.roads);
            REQUIRE(before.buildings.size() == after.buildings.size());
            for (std::size_t i = 0; i < after.parcels.size(); ++i) {
                const auto location = LocateSlot(after, after.parcels[i]);
                REQUIRE(after.buildings[i].bounds == before.buildings[i].bounds);
                const bool target = preference.value == BuildingUse::Residential ? location.sideStreet
                    : preference.value == BuildingUse::Commercial ? location.centralMain : location.outerEnd;
                const bool chosen = preferred.GetBuildingPattern(after.buildings[i].pattern).id == "a";
                const bool wasChosen = neutral.GetBuildingPattern(before.buildings[i].pattern).id == "a";
                directions[static_cast<unsigned>(after.parcels[i].frontage)] = true;
                branch |= location.street >= 2;
                at24 |= location.street == 0 && location.distance == 24;
                at25 |= location.street == 0 && location.distance == 25;
                atOuterBoundary |= 4 * location.distance == 3 * location.reach;
                justInsideOuter |= 4 * location.distance < 3 * location.reach
                    && 4 * (location.distance + 1) >= 3 * location.reach;
                if ((preference.value == BuildingUse::Commercial && location.street == 0 && location.distance == 24)
                    || (preference.value == BuildingUse::Industrial && 4 * location.distance == 3 * location.reach)) {
                    ++boundarySlots;
                    boundaryPreferred += chosen;
                    boundaryNeutral += wasChosen;
                }
                if (target) {
                    ++targetSlots;
                    preferredHits += chosen;
                    neutralHits += wasChosen;
                } else {
                    ++ordinarySlots;
                    // Positive 1x fallback, with identical draws at every
                    // nonpreferred slot, not a weak aggregate-only assertion.
                    REQUIRE(chosen == wasChosen);
                }
            }
        }
        CAPTURE(preference.use, targetSlots, preferredHits, neutralHits);
        CHECK(targetSlots > 100);
        CHECK(ordinarySlots > 100);
        CHECK(preferredHits * 10 > neutralHits * 13);
        CHECK(preferredHits < targetSlots); // The neutral family stays eligible.
        CHECK(at24);
        CHECK(at25);
        CHECK(atOuterBoundary);
        CHECK(justInsideOuter);
        CHECK(branch);
        if (preference.value != BuildingUse::Residential) {
            CAPTURE(boundarySlots, boundaryPreferred, boundaryNeutral);
            CHECK(boundarySlots > 20);
            CHECK(boundaryPreferred * 10 > boundaryNeutral * 12);
        }
        for (const bool direction : directions) { CHECK(direction); }
    }
}

TEST_CASE("Preferred V6 families count once before unchanged weighted variant selection", "[worldgen][v6][composition][families]") {
    for (const char* use : {"residential", "commercial", "industrial"}) {
        const auto single = Catalog(CompositionPatterns(use));
        const auto variants = Catalog(CompositionPatterns(use, true));
        int preferredFamily = 0;
        int otherFamily = 0;
        int addedVariant = 0;
        for (std::uint64_t seed = 0; seed < 32; ++seed) {
            const auto before = GenerateRegionPlan(single, seed, {-1, 1}, 6);
            const auto after = GenerateRegionPlan(variants, seed, {-1, 1}, 6);
            REQUIRE(before.roads == after.roads);
            REQUIRE(before.buildings.size() == after.buildings.size());
            for (std::size_t i = 0; i < before.buildings.size(); ++i) {
                const auto& a = single.GetBuildingPattern(before.buildings[i].pattern);
                const auto& b = variants.GetBuildingPattern(after.buildings[i].pattern);
                REQUIRE(a.family == b.family);
                CHECK(before.buildings[i].bounds == after.buildings[i].bounds);
                preferredFamily += b.family == "a_family";
                otherFamily += b.family == "b_family";
                addedVariant += b.id == "a_variant";
            }
        }
        CHECK(preferredFamily > 0);
        CHECK(otherFamily > 0);
        CHECK(addedVariant * 10 > preferredFamily * 9);
    }
}

TEST_CASE("V6 use is neutral for civic sites and remains positive for one-use catalogs", "[worldgen][v6][composition]") {
    const auto onlyMixed = Catalog(Site("only", 5, 5, ",use:'mixed'", {2, 0}));
    for (const char* use : {"mixed", "residential", "commercial", "industrial"}) {
        String metadata = ",use:'";
        metadata += use;
        metadata += '\'';
        const auto onlyUse = Catalog(Site("only", 5, 5, metadata.c_str(), {2, 0}));
        for (std::uint64_t seed = 0; seed < 8; ++seed) {
            const auto neutral = GenerateRegionPlan(onlyMixed, seed, {1, 1}, 6);
            const auto preferred = GenerateRegionPlan(onlyUse, seed, {1, 1}, 6);
            CHECK(preferred == neutral);
            REQUIRE_FALSE(preferred.buildings.empty());
        }
        for (const char* site : {"landmark", "park", "square"}) {
            const auto neutral = Catalog(CompositionPatterns("mixed", false, site));
            const auto preferred = Catalog(CompositionPatterns(use, false, site));
            for (std::uint64_t seed = 0; seed < 8; ++seed) {
                const auto a = GenerateRegionPlan(neutral, seed, {1, 1}, 6);
                const auto b = GenerateRegionPlan(preferred, seed, {1, 1}, 6);
                REQUIRE_FALSE(a.buildings.empty());
                CHECK(a == b);
            }
        }
    }
}

TEST_CASE("All-Mixed V6 catalogs retain neutral plan output", "[worldgen][v6][composition][neutral]") {
    const auto omitted = Catalog(Site("a", 5, 5, ",family:'a_family',family_weight:1000000", {2, 0}) + ','
        + Site("b", 5, 5, ",family:'b_family',family_weight:1000000", {2, 0}) + ','
        + Site("singleton", 9, 7, ",weight:1000000", {4, 0}));
    const auto explicitMixed = Catalog(CompositionPatterns("mixed") + ','
        + Site("singleton", 9, 7, ",weight:1000000,use:'mixed'", {4, 0}));
    struct NeutralGolden { std::uint64_t seed; std::uint64_t digest; };
    for (const auto golden : {NeutralGolden{0, 0x6859d92f1f06962fULL},
        {1, 0xe725dbe74e2a83f7ULL}, {42, 0x5d52685e397bae9cULL}, {73, 0x91ee65b11e3cc16bULL}}) {
        const auto seed = golden.seed;
        const auto plan = GenerateRegionPlan(omitted, seed, {1, -1}, 6);
        CHECK(plan == GenerateRegionPlan(explicitMixed, seed, {1, -1}, 6));
        // Stable record digest freezes the neutral selection/geometry contract,
        // without catalog-local numeric IDs, object padding or std::hash.
        std::uint64_t digest = 14695981039346656037ULL;
        const auto add = [&](std::uint64_t value) { digest = (digest ^ value) * 1099511628211ULL; };
        const auto point = [&](TileCoord p) { add(static_cast<std::uint64_t>(p.x)); add(static_cast<std::uint64_t>(p.z)); };
        const auto rect = [&](TileRect b) { point(b.min); point(b.max); };
        add(static_cast<unsigned>(plan.townProfile));
        point(plan.townCenter);
        for (const auto road : plan.roads) { point(road.first); point(road.last); add(road.width); }
        add(plan.parcels.size());
        for (const auto& parcel : plan.parcels) { rect(parcel.bounds); add(static_cast<unsigned>(parcel.frontage)); }
        for (const auto& building : plan.buildings) {
            const auto& pattern = omitted.GetBuildingPattern(building.pattern);
            add(pattern.id.size());
            for (const unsigned char byte : pattern.id) { add(byte); }
            rect(building.bounds);
            point(building.entrance);
            point(building.driveway.last);
            add(building.quarterTurns);
            add(building.abandonmentSeed);
            add(building.abandoned);
        }
        add(plan.serviceAlleys.size());
        for (const auto alley : plan.serviceAlleys) { rect(alley); }
        CAPTURE(seed, digest);
        CHECK(digest == golden.digest);
    }
}

TEST_CASE("Preferred V6 singleton family totals may exceed uint32", "[worldgen][v6][composition][weights]") {
    String patterns;
    for (int i = 0; i < 1100; ++i) {
        char id[32];
        std::snprintf(id, sizeof(id), "workshop_%04d", i);
        if (i) { patterns += ','; }
        patterns += Site(id, 1, 1, ",use:'commercial',weight:1000000");
    }
    const auto content = Catalog(patterns);
    const auto plan = GenerateRegionPlan(content, 42, {1, 1}, 6);
    bool checkedCentral = false;
    for (std::size_t i = 0; i < plan.parcels.size(); ++i) {
        const auto location = LocateSlot(plan, plan.parcels[i]);
        if (location.centralMain) {
            // 1100 fitting singleton families * 4,000,000 = 4.4 billion.
            checkedCentral = true;
            CHECK(content.GetBuildingPattern(plan.buildings[i].pattern).use == BuildingUse::Commercial);
        }
    }
    CHECK(checkedCentral);
}

TEST_CASE("Legacy plans ignore family and use metadata on the identical pattern catalog", "[worldgen][v6][legacy]") {
    const auto plain = Catalog(Site("a", 7, 5) + ',' + Site("b", 9, 7));
    const auto families = Catalog(Site("a", 7, 5, ",family:'house',family_weight:100,use:'industrial'") + ','
        + Site("b", 9, 7, ",family:'house',family_weight:100,use:'industrial'"));
    for (const auto version : {2U, 3U, 4U, 5U}) {
        for (const auto seed : {0ULL, 42ULL, 73ULL}) {
            const auto a = GenerateRegionPlan(plain, seed, {-1, 1}, version);
            const auto b = GenerateRegionPlan(families, seed, {-1, 1}, version);
            CHECK(a == b);
            CHECK(a.townProfile == TownProfile::LegacyGrid);
            CHECK(a.serviceAlleys.empty());
            for (const auto& building : a.buildings) {
                const auto chunk = ChunkOf(building.entrance);
                CHECK(GenerateChunk(plain, seed, a, chunk) == GenerateChunk(families, seed, b, chunk));
            }
        }
    }
}
}
