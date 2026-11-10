#include "worldgen/worldgen.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

#include <cstdlib>
#include <cstdio>
#include <initializer_list>

namespace {
using namespace cataclysmx;

struct TileImage {
    TileRect bounds;
    Vector<Tile> tiles;

    const Tile& At(TileCoord position) const {
        REQUIRE(Contains(bounds, position));
        const int width = bounds.max.x - bounds.min.x;
        return tiles[static_cast<std::size_t>((position.z - bounds.min.z) * width
                                             + position.x - bounds.min.x)];
    }
};

// Assemble only intersecting chunks, copying their owned tiles into one image.
// Reverse mode exercises whole-feature decoration without relying on a World cache.
TileImage Materialize(const ContentCatalog& content, const RegionPlan& plan,
                      TileRect bounds, bool reverse = false) {
    TileImage image{bounds, Vector<Tile>(static_cast<std::size_t>(
        (bounds.max.x - bounds.min.x) * (bounds.max.z - bounds.min.z)))};
    const auto first = ChunkOf(bounds.min);
    const auto last = ChunkOf({bounds.max.x - 1, bounds.max.z - 1, bounds.min.y});
    Vector<ChunkCoord> chunks;
    for (int z = first.z; z <= last.z; ++z) {
        for (int x = first.x; x <= last.x; ++x) { chunks.push_back({x, z, first.y}); }
    }
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto chunk = chunks[reverse ? chunks.size() - 1 - i : i];
        const auto tiles = GenerateChunk(content, plan.seed, plan, chunk);
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = 0; x < chunk_size; ++x) {
                const TileCoord p{chunk.x * chunk_size + x, chunk.z * chunk_size + z, chunk.y};
                if (!ValidPosition(p) || !Contains(bounds, p)) { continue; }
                const int width = bounds.max.x - bounds.min.x;
                image.tiles[static_cast<std::size_t>((p.z - bounds.min.z) * width
                                                    + p.x - bounds.min.x)] =
                    tiles[static_cast<std::size_t>(z * chunk_size + x)];
            }
        }
    }
    return image;
}

bool Accessible(const ContentCatalog& content, TerrainId terrain) {
    const auto& definition = content.GetTerrain(terrain);
    const auto* opened = content.TryGetTerrain(definition.opensTo);
    return definition.passable || (opened && opened->passable);
}

// A contract-level four-neighbor flood fill, shared by the road and interior tests.
Vector<std::uint8_t> Reachable(const TileImage& image, const ContentCatalog& content,
                             TileCoord start, bool roadsOnly) {
    const int width = image.bounds.max.x - image.bounds.min.x;
    Vector<std::uint8_t> seen(image.tiles.size(), 0);
    Vector<TileCoord> queue{start};
    const auto index = [&](TileCoord p) {
        return static_cast<std::size_t>((p.z - image.bounds.min.z) * width
                                       + p.x - image.bounds.min.x);
    };
    REQUIRE(Contains(image.bounds, start));
    REQUIRE(Accessible(content, image.At(start).terrain));
    seen[index(start)] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto p = queue[head];
        for (const TileCoord next : {TileCoord{p.x, p.z - 1, p.y}, {p.x + 1, p.z, p.y},
                                     {p.x, p.z + 1, p.y}, {p.x - 1, p.z, p.y}}) {
            if (!ValidPosition(next) || !Contains(image.bounds, next) || seen[index(next)]) { continue; }
            const auto terrain = image.At(next).terrain;
            if (roadsOnly ? terrain != content.FindTerrain("road") : !Accessible(content, terrain)) { continue; }
            seen[index(next)] = 1;
            queue.push_back(next);
        }
    }
    return seen;
}

// Independent authored-coordinate oracle: turn the point repeatedly clockwise.
TileCoord AuthoredPosition(const BuildingPattern& pattern, const BuildingPlan& building,
                           int x, int z) {
    int width = pattern.width;
    int height = pattern.height;
    for (int turn = 0; turn < building.quarterTurns; ++turn) {
        const int oldX = x;
        x = height - 1 - z;
        z = oldX;
        eastl::swap(width, height);
    }
    return {building.bounds.min.x + x, building.bounds.min.z + z};
}

void CheckAuthored(const ContentCatalog& content, const BuildingPlan& building,
                   const TileImage& image) {
    REQUIRE_FALSE(building.abandoned);
    const auto& pattern = content.GetBuildingPattern(building.pattern);
    CHECK(AuthoredPosition(pattern, building, pattern.entrance.x, pattern.entrance.z)
          == building.entrance);
    for (int z = 0; z < pattern.height; ++z) {
        for (int x = 0; x < pattern.width; ++x) {
            CHECK(image.At(AuthoredPosition(pattern, building, x, z))
                  == Tile{pattern.tiles[static_cast<std::size_t>(z * pattern.width + x)], 0});
        }
    }
}

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

ContentCatalog CustomContent(eastl::string_view patterns, bool vertical = true) {
    String text = R"({format_version:1,definitions:[
        {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
         appearance:{symbol:'.',foreground:'white'}},
        {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
         appearance:{symbol:'#',foreground:'white'}},
        {type:'terrain',id:'door',name:'Door',movement:{passable:false},blocks_sight:true,
         opens_to:'floor',looks_like:'wall'},
        {type:'terrain',id:'marker',name:'Marker',movement:{passable:true},blocks_sight:false,
         looks_like:'floor'},)";
    if (vertical) {
        text += R"({type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,
            open_space:true,looks_like:'floor'},
            {type:'terrain',id:'stairs',name:'Stairs',movement:{passable:true},blocks_sight:false,
            stairs:true,looks_like:'floor'},)";
    }
    text.append(patterns.data(), patterns.size());
    text += "]}";
    const ContentSource source{"worldgen-test.json5", View(text)};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

String RectanglePattern(eastl::string_view id, int width, int height,
                        PatternPoint entrance, int weight = 1) {
    char number[32];
    String text = "{type:'building_pattern',id:'";
    text.append(id.data(), id.size());
    text += "',weight:";
    std::snprintf(number, sizeof(number), "%d", weight);
    text += number;
    text += ",rows:[";
    for (int z = 0; z < height; ++z) {
        if (z != 0) { text += ','; }
        text += '\'';
        for (int x = 0; x < width; ++x) {
            text += PatternPoint{x, z} == entrance ? '+'
                : x == 0 || z == 0 || x == width - 1 || z == height - 1 ? '#'
                : x == 1 && z == 1 ? 'm' : '.';
        }
        text += '\'';
    }
    text += "],palette:{'#':'wall','.':'floor','+':'door','m':'marker'},entrance:{x:";
    std::snprintf(number, sizeof(number), "%d", entrance.x);
    text += number;
    text += ",z:";
    std::snprintf(number, sizeof(number), "%d", entrance.z);
    text += number;
    text += "}}";
    return text;
}

TEST_CASE("Region coordinates floor negative tiles and include world extremes", "[worldgen][coordinates]") {
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
            CHECK_FALSE(plan.hasTown);
            CHECK(plan.parcels.empty());
            CHECK(plan.buildings.empty());
            CHECK(ParcelAt(plan, p) == nullptr);
            CHECK(BuildingAt(plan, p) == nullptr);
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

TEST_CASE("Environment fields are bounded and continuous across signed region seams", "[worldgen][environment]") {
    bool meadow = false;
    bool woodland = false;
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
        for (int along = -1024; along <= 1024; along += 127) {
            for (int border : {0, 256}) {
                for (bool xAxis : {false, true}) {
                    const TileCoord before = xAxis ? TileCoord{border - 1, along} : TileCoord{along, border - 1};
                    const TileCoord after = xAxis ? TileCoord{border, along} : TileCoord{along, border};
                    const auto a = SampleEnvironment(seed, before);
                    const auto b = SampleEnvironment(seed, after);
                    CHECK(a == SampleEnvironment(seed, before));
                    CHECK(b == SampleEnvironment(seed, after));
                    // Adjacent tiles may differ slightly after integer rounding;
                    // a region-local reset would create a much larger jump.
                    CHECK(std::abs(a.moisture - b.moisture) <= 16);
                    CHECK(std::abs(a.temperature - b.temperature) <= 16);
                    CHECK(std::abs(a.woodland - b.woodland) <= 16);
                    for (const auto sample : {a, b}) {
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

TEST_CASE("Generation content validates passable roles and supports terrain-only fallbacks", "[worldgen][content]") {
    Diagnostics diagnostics;
    CHECK(ValidateWorldgenContent(ContentCatalog{}, &diagnostics)
          == eastl::unexpected(ContentError::InvalidData));
    CHECK(FormatDiagnostics(diagnostics).find("floor") != String::npos);
    for (const char* role : {"grass", "road", "rubble"}) {
        String definition = "{type:'terrain',id:'";
        definition += role;
        definition += "',name:'Blocked role',movement:{passable:false},blocks_sight:false,looks_like:'floor'}";
        const auto content = CustomContent(View(definition));
        diagnostics.Clear();
        CHECK(ValidateWorldgenContent(content, &diagnostics)
              == eastl::unexpected(ContentError::InvalidData));
        const auto message = FormatDiagnostics(diagnostics);
        CHECK(message.find(role) != String::npos);
        CHECK(message.find("passable") != String::npos);
        CHECK(ValidateWorldgenContent(content) == eastl::unexpected(ContentError::InvalidData));
    }
    const auto content = CustomContent({});
    REQUIRE(ValidateWorldgenContent(content));
    const auto plan = GenerateRegionPlan(content, 42, {1, 1});
    CHECK(plan.buildings.empty());
    const auto image = Materialize(content, plan, plan.bounds);
    bool floor = false;
    bool wall = false;
    for (int z = image.bounds.min.z; z < image.bounds.max.z; ++z) {
        for (int x = image.bounds.min.x; x < image.bounds.max.x; ++x) {
            const TileCoord p{x, z};
            const auto terrain = image.At(p).terrain;
            CHECK((terrain == content.FindTerrain("floor") || terrain == content.FindTerrain("wall")));
            if (IsRoad(plan, p)) { CHECK(Passable(content, terrain)); }
            floor |= terrain == content.FindTerrain("floor");
            wall |= terrain == content.FindTerrain("wall");
        }
    }
    CHECK(floor);
    CHECK(wall);
    // Legacy surface generation is still usable without an air role; the 3D
    // application boundary rejects that catalog rather than inventing sky floors.
    const auto legacy = CustomContent({}, false);
    diagnostics.Clear();
    CHECK(ValidateWorldgenContent(legacy, &diagnostics)
          == eastl::unexpected(ContentError::InvalidData));
    CHECK(FormatDiagnostics(diagnostics).find("air") != String::npos);
    const auto legacyPlan = GenerateRegionPlan(legacy, 42, {1, 1});
    const auto legacyTiles = GenerateChunk(legacy, 42, legacyPlan, {16, 16});
    CHECK(legacy.TryGetTerrain(legacyTiles[0].terrain) != nullptr);
    for (const char* role : {"air", "stairs"}) {
        String definition = "{type:'terrain',id:'";
        definition += role;
        definition += "',name:'Wrong vertical role',movement:{passable:true},blocks_sight:false,looks_like:'floor'}";
        if (eastl::string_view(role) == "stairs") {
            definition += R"(,{type:'terrain',id:'air',name:'Air',movement:{passable:true},
                blocks_sight:false,open_space:true,looks_like:'floor'})";
        }
        const auto invalid = CustomContent(View(definition), false);
        diagnostics.Clear();
        CHECK_FALSE(ValidateWorldgenContent(invalid, &diagnostics));
        CHECK(FormatDiagnostics(diagnostics).find(role) != String::npos);
    }
}

TEST_CASE("Generation version three preserves version two surface terrain and damage output", "[worldgen][determinism]") {
    REQUIRE(world_generation_version == 3);
    // Environmental seam, rotated divided house, and abandoned cross-chunk long house.
    const ChunkCoord chunks[] = {{-17, 20}, {-16, 20}, {5, 4}, {9, 7}, {10, 7}};
    const std::uint32_t expected[] = {2483476373U, 3666744973U, 2506376988U, 2959312743U, 518236207U};
    for (std::size_t i = 0; i < 5; ++i) {
        const auto chunk = chunks[i];
        const auto plan = GenerateRegionPlan(TestContent(), 42,
            RegionOf({chunk.x * chunk_size, chunk.z * chunk_size}));
        const auto tiles = GenerateChunk(TestContent(), 42, plan, chunk);
        std::uint32_t checksum = 2166136261U;
        for (const auto tile : tiles) {
            for (unsigned char ch : TestContent().GetTerrain(tile.terrain).id) {
                checksum = (checksum ^ ch) * 16777619U;
            }
            checksum *= 16777619U; // Stable string terminator, not catalog-local ID.
            checksum = (checksum ^ tile.damage) * 16777619U;
        }
        CAPTURE(chunk.x, chunk.z);
        CHECK(checksum == expected[i]);
    }
}

TEST_CASE("Vertical generation has solid depths open sky and coherent building roofs", "[worldgen][vertical]") {
    const auto plan = GenerateRegionPlan(TestContent(), 42, {0, 0});
    REQUIRE_FALSE(plan.buildings.empty());
    for (int y : {world_min_y, -1, 1, 2, world_max_y}) {
        const TileRect bounds{{0, 0, y}, {256, 256, y}};
        const auto image = Materialize(TestContent(), plan, bounds);
        const auto reverse = Materialize(TestContent(), plan, bounds, true);
        CHECK(image.tiles == reverse.tiles);
        for (int z = 0; z < 256; ++z) {
            for (int x = 0; x < 256; ++x) {
                const TileCoord p{x, z, y};
                bool roof = false;
                for (const auto& building : plan.buildings) {
                    // Independent footprint oracle, including abandoned roofs.
                    if (x >= building.bounds.min.x && x < building.bounds.max.x
                        && z >= building.bounds.min.z && z < building.bounds.max.z) { roof = true; }
                }
                const auto expected = TestTerrain(y < 0 ? "wall" : y == 1 && roof ? "floor" : "air");
                REQUIRE(image.At(p) == Tile{expected, 0});
            }
        }
    }
    for (int y : {world_min_y, 0, world_max_y}) {
        CHECK(RegionOf({-257, 256, y}) == RegionCoord{-2, 1});
        CHECK(SampleEnvironment(42, {-257, 256, y}) == SampleEnvironment(42, {-257, 256, 0}));
        const auto& building = plan.buildings.front();
        const TileCoord p{building.entrance.x, building.entrance.z, y};
        CHECK(BuildingAt(plan, p) == &building);
    }
}

TEST_CASE("Vertical terrain-only generation uses air without requiring stairs or patterns", "[worldgen][vertical][content]") {
    const auto content = CustomContent(R"({type:'terrain',id:'air',name:'Air',movement:{passable:true},
        blocks_sight:false,open_space:true,looks_like:'floor'})", false);
    REQUIRE(ValidateWorldgenContent(content));
    const auto plan = GenerateRegionPlan(content, 19, {-1, -1});
    for (int y : {-1, 1, world_max_y}) {
        const auto tiles = GenerateChunk(content, 19, plan, {-1, -1, y});
        for (const auto tile : tiles) {
            CHECK(tile == Tile{content.FindTerrain(y < 0 ? "wall" : "air"), 0});
        }
    }
    const TileCoord corner{world_coordinate_limit, world_coordinate_limit, world_max_y};
    const auto edgePlan = GenerateRegionPlan(content, 19, RegionOf(corner));
    const auto edge = GenerateChunk(content, 19, edgePlan, ChunkOf(corner));
    CHECK(edge[0] == Tile{content.FindTerrain("air"), 0});
    for (std::size_t i = 1; i < edge.size(); ++i) {
        CHECK(edge[i] == Tile{content.FindTerrain("wall"), 0});
    }
}

TEST_CASE("Canonical crossings have matching width-three materialized seams only at declared borders", "[worldgen][roads]") {
    for (const RegionCoord region : {RegionCoord{1, 2}, {-2, -3}}) {
        const auto plan = GenerateRegionPlan(TestContent(), 1234, region);
        for (bool xAxis : {false, true}) {
            const RegionCoord neighbor = xAxis ? RegionCoord{region.x + 1, region.z}
                                               : RegionCoord{region.x, region.z + 1};
            const auto other = GenerateRegionPlan(TestContent(), plan.seed, neighbor);
            const auto& crossing = plan.boundaries[xAxis ? 1 : 2];
            CHECK(crossing == other.boundaries[xAxis ? 3 : 0]);
            REQUIRE(crossing.width == 3);
            CHECK(crossing.approachLength > 0);
            const int border = xAxis ? plan.bounds.max.x : plan.bounds.max.z;
            CHECK(crossing.key.axis == (xAxis ? BorderAxis::X : BorderAxis::Z));
            CHECK(crossing.key.border == border);
            CHECK(crossing.key.alongRegion == (xAxis ? region.z : region.x));
            const TileRect left = xAxis
                ? TileRect{{border - 1, plan.bounds.min.z}, {border, plan.bounds.max.z}}
                : TileRect{{plan.bounds.min.x, border - 1}, {plan.bounds.max.x, border}};
            const TileRect right = xAxis
                ? TileRect{{border, plan.bounds.min.z}, {border + 1, plan.bounds.max.z}}
                : TileRect{{plan.bounds.min.x, border}, {plan.bounds.max.x, border + 1}};
            const auto a = Materialize(TestContent(), plan, left);
            const auto b = Materialize(TestContent(), other, right);
            int roadTiles = 0;
            for (int offset = 0; offset < region_size; ++offset) {
                const int along = (xAxis ? plan.bounds.min.z : plan.bounds.min.x) + offset;
                const TileCoord p = xAxis ? TileCoord{border - 1, along} : TileCoord{along, border - 1};
                const TileCoord q = xAxis ? TileCoord{border, along} : TileCoord{along, border};
                const int center = xAxis ? crossing.position.z : crossing.position.x;
                const bool declared = std::abs(along - center) <= 1;
                CHECK(IsRoad(plan, p) == declared);
                CHECK(IsRoad(other, q) == declared);
                CHECK((a.At(p).terrain == TestTerrain("road")) == declared);
                CHECK((b.At(q).terrain == TestTerrain("road")) == declared);
                if (declared) { ++roadTiles; }
            }
            CHECK(roadTiles == 3);
        }
    }
}

TEST_CASE("Materialized road network reaches every crossing street and frontage", "[worldgen][roads][town]") {
    const auto plan = GenerateRegionPlan(TestContent(), 73, {-2, 1});
    REQUIRE(plan.hasTown);
    REQUIRE_FALSE(plan.buildings.empty());
    const auto image = Materialize(TestContent(), plan, plan.bounds);
    const auto seen = Reachable(image, TestContent(), plan.townCenter, true);
    const auto reached = [&](TileCoord p) {
        return seen[static_cast<std::size_t>((p.z - plan.bounds.min.z) * region_size
                                            + p.x - plan.bounds.min.x)] != 0;
    };
    for (const auto& crossing : plan.boundaries) {
        auto terminal = crossing.position;
        if (terminal.x == plan.bounds.max.x) { --terminal.x; }
        if (terminal.z == plan.bounds.max.z) { --terminal.z; }
        CHECK(reached(terminal));
    }
    // Check the promised three-by-three street grid independently of IsRoad.
    for (int axis : {plan.townBounds.min.x, plan.townCenter.x, plan.townBounds.max.x}) {
        for (int z = plan.townBounds.min.z; z <= plan.townBounds.max.z; ++z) {
            CHECK(image.At({axis, z}).terrain == TestTerrain("road"));
            CHECK(reached({axis, z}));
        }
    }
    for (int axis : {plan.townBounds.min.z, plan.townCenter.z, plan.townBounds.max.z}) {
        for (int x = plan.townBounds.min.x; x <= plan.townBounds.max.x; ++x) {
            CHECK(image.At({x, axis}).terrain == TestTerrain("road"));
            CHECK(reached({x, axis}));
        }
    }
    for (const auto& parcel : plan.parcels) {
        for (const auto& road : plan.roads) {
            REQUIRE(road.width == 3);
            const int left = (road.first.x < road.last.x ? road.first.x : road.last.x) - 1;
            const int right = (road.first.x > road.last.x ? road.first.x : road.last.x) + 2;
            const int top = (road.first.z < road.last.z ? road.first.z : road.last.z) - 1;
            const int bottom = (road.first.z > road.last.z ? road.first.z : road.last.z) + 2;
            CHECK_FALSE((parcel.bounds.min.x < right && left < parcel.bounds.max.x
                         && parcel.bounds.min.z < bottom && top < parcel.bounds.max.z));
        }
    }
    for (int z = plan.bounds.min.z; z < plan.bounds.max.z; ++z) {
        for (int x = plan.bounds.min.x; x < plan.bounds.max.x; ++x) {
            const TileCoord p{x, z};
            if (IsRoad(plan, p)) {
                CHECK(image.At(p).terrain == TestTerrain("road"));
                CHECK(reached(p));
                CHECK(BuildingAt(plan, p) == nullptr);
                if (const auto* parcel = ParcelAt(plan, p)) {
                    // Parcels contain their access driveways, but no through streets.
                    bool driveway = false;
                    for (const auto& building : plan.buildings) {
                        if (building.owner != parcel->owner || p.x != building.entrance.x) { continue; }
                        driveway |= p.z >= (building.entrance.z < parcel->streetZ ? building.entrance.z : parcel->streetZ)
                            && p.z <= (building.entrance.z > parcel->streetZ ? building.entrance.z : parcel->streetZ);
                    }
                    CHECK(driveway);
                }
            }
        }
    }
    for (const auto& building : plan.buildings) {
        const auto* parcel = ParcelAt(plan, building.entrance);
        REQUIRE(parcel != nullptr);
        CHECK(parcel->owner == building.owner);
        CHECK(building.driveway.width == 1);
        CHECK(building.driveway.first == building.entrance);
        CHECK(building.driveway.last == TileCoord{building.entrance.x, parcel->streetZ});
        CHECK(Accessible(TestContent(), image.At(building.entrance).terrain));
        const int step = parcel->frontage == Frontage::North ? -1 : 1;
        CHECK(building.entrance.z == (step < 0 ? building.bounds.min.z : building.bounds.max.z - 1));
        REQUIRE((step < 0 ? parcel->streetZ < building.entrance.z : parcel->streetZ > building.entrance.z));
        for (int z = building.entrance.z + step; z != parcel->streetZ + step; z += step) {
            const TileCoord p{building.entrance.x, z};
            CHECK(IsRoad(plan, p));
            CHECK(reached(p));
        }
        CHECK(Contains(parcel->bounds, building.bounds.min));
        CHECK(Contains(parcel->bounds, {building.bounds.max.x - 1, building.bounds.max.z - 1}));
        for (int z = building.bounds.min.z; z < building.bounds.max.z; ++z) {
            for (int x = building.bounds.min.x; x < building.bounds.max.x; ++x) {
                CHECK(BuildingAt(plan, {x, z}) == &building);
                CHECK_FALSE(IsRoad(plan, {x, z}));
            }
        }
    }
    CHECK_FALSE(IsRoad(plan, {0, 0}));
    CHECK(ParcelAt(plan, {0, 0}) == nullptr);
    CHECK(BuildingAt(plan, {0, 0}) == nullptr);
}

TEST_CASE("Region plans repeat exactly while seeds change actual geometry", "[worldgen][determinism]") {
    const auto first = GenerateRegionPlan(TestContent(), 100, {2, -2});
    CHECK(first == GenerateRegionPlan(TestContent(), 100, {2, -2}));
    bool geometryChanged = false;
    for (std::uint64_t seed = 101; seed < 109; ++seed) {
        const auto other = GenerateRegionPlan(TestContent(), seed, first.region);
        geometryChanged |= first.boundaries != other.boundaries || first.buildings != other.buildings;
    }
    CHECK(geometryChanged);
}

TEST_CASE("All authored entrance sides rotate non-square templates toward both frontages", "[worldgen][buildings]") {
    for (const PatternPoint entrance : {PatternPoint{2, 0}, {4, 1}, {2, 2}, {0, 1}}) {
        CAPTURE(entrance.x, entrance.z);
        const auto content = CustomContent(View(RectanglePattern("asymmetric", 5, 3, entrance)));
        bool north = false;
        bool south = false;
        for (std::uint64_t seed = 0; seed < 64 && !(north && south); ++seed) {
            const auto plan = GenerateRegionPlan(content, seed, {1, -1});
            REQUIRE(plan.buildings.size() == plan.parcels.size());
            for (const auto& building : plan.buildings) {
                REQUIRE(building.quarterTurns < 4);
                const bool odd = building.quarterTurns % 2 != 0;
                CHECK(building.bounds.max.x - building.bounds.min.x == (odd ? 3 : 5));
                CHECK(building.bounds.max.z - building.bounds.min.z == (odd ? 5 : 3));
                const auto* parcel = ParcelAt(plan, building.entrance);
                REQUIRE(parcel != nullptr);
                CHECK(building.entrance.z == (parcel->frontage == Frontage::North
                    ? building.bounds.min.z : building.bounds.max.z - 1));
                if (building.abandoned) { continue; }
                CheckAuthored(content, building, Materialize(content, plan, building.bounds));
                north |= parcel->frontage == Frontage::North;
                south |= parcel->frontage == Frontage::South;
            }
        }
        CHECK(north);
        CHECK(south);
    }
}

TEST_CASE("Weighted selection uses fitting authored patterns and skips oversized templates", "[worldgen][buildings]") {
    const String fitting = RectanglePattern("small", 5, 3, {2, 2});
    const String oversized = RectanglePattern("oversized", 64, 64, {2, 63}, 1000000);
    const auto onlyLarge = CustomContent(View(oversized));
    const auto noFit = GenerateRegionPlan(onlyLarge, 9, {1, 1});
    REQUIRE_FALSE(noFit.parcels.empty());
    CHECK(noFit.buildings.empty());
    const auto oneFits = CustomContent(View(fitting + ',' + oversized));
    const auto plan = GenerateRegionPlan(oneFits, 9, {1, 1});
    REQUIRE(plan.buildings.size() == plan.parcels.size());
    for (const auto& building : plan.buildings) {
        CHECK(building.pattern == oneFits.FindBuildingPattern("small"));
    }
    const auto weighted = CustomContent(View(fitting + ','
        + RectanglePattern("frequent", 5, 3, {2, 2}, 9) + ',' + oversized));
    int smallCount = 0;
    int frequentCount = 0;
    // Broad deterministic sample, no required seed-to-pattern mapping or exact ratio.
    for (std::uint64_t seed = 0; seed < 128; ++seed) {
        const auto sample = GenerateRegionPlan(weighted, seed, {-1, 2});
        REQUIRE(sample.buildings.size() == sample.parcels.size());
        for (const auto& building : sample.buildings) {
            CHECK(building.pattern != weighted.FindBuildingPattern("oversized"));
            smallCount += building.pattern == weighted.FindBuildingPattern("small");
            frequentCount += building.pattern == weighted.FindBuildingPattern("frequent");
        }
    }
    CHECK(smallCount > 0);
    CHECK(frequentCount > smallCount * 3);
}

TEST_CASE("Whole-building decoration preserves doors and accessible interiors across chunk order", "[worldgen][buildings][determinism]") {
    bool checkedAbandoned = false;
    bool checkedAuthored = false;
    bool checkedLongHouse = false;
    bool sawDamage = false;
    for (std::uint64_t seed = 0; seed < 64
         && !(checkedAbandoned && checkedAuthored && checkedLongHouse && sawDamage); ++seed) {
        const auto plan = GenerateRegionPlan(TestContent(), seed, {1, 1});
        for (const auto& building : plan.buildings) {
            const auto& pattern = TestContent().GetBuildingPattern(building.pattern);
            const bool longHouse = pattern.id == "long_house";
            if (building.abandoned ? checkedAbandoned && (!longHouse || checkedLongHouse) && sawDamage
                                   : checkedAuthored) { continue; }
            const auto image = Materialize(TestContent(), plan, building.bounds);
            if (!building.abandoned) {
                CheckAuthored(TestContent(), building, image);
                checkedAuthored = true;
                continue;
            }
            CHECK(image.tiles == Materialize(TestContent(), plan, building.bounds, true).tiles);
            const auto seen = Reachable(image, TestContent(), building.entrance, false);
            int reachableInterior = 0;
            for (int z = 0; z < pattern.height; ++z) {
                for (int x = 0; x < pattern.width; ++x) {
                    const auto p = AuthoredPosition(pattern, building, x, z);
                    const auto raw = pattern.tiles[static_cast<std::size_t>(z * pattern.width + x)];
                    const auto& tile = image.At(p);
                    if (raw == TestTerrain("closed_door") || p == building.entrance) {
                        CHECK(tile == Tile{raw, 0});
                    }
                    if (tile != Tile{raw, 0}) {
                        CHECK(tile.terrain == TestTerrain("rubble"));
                        CHECK(tile.damage > 0);
                        CHECK((raw == TestTerrain("floor") || raw == TestTerrain("wall")));
                        sawDamage = true;
                    }
                    const int width = building.bounds.max.x - building.bounds.min.x;
                    const auto index = static_cast<std::size_t>((p.z - building.bounds.min.z) * width
                                                               + p.x - building.bounds.min.x);
                    if (x > 0 && x < pattern.width - 1 && z > 0 && z < pattern.height - 1 && seen[index]) {
                        ++reachableInterior;
                    }
                    if (x > 0 && x < pattern.width - 1 && z > 0 && z < pattern.height - 1
                        && Accessible(TestContent(), raw)) {
                        CHECK(seen[index] != 0);
                    }
                }
            }
            CHECK(reachableInterior > 0);
            checkedAbandoned = true;
            if (longHouse) {
                CHECK(ChunkOf(building.bounds.min) != ChunkOf(
                    {building.bounds.max.x - 1, building.bounds.max.z - 1}));
                checkedLongHouse = true;
            }
        }
    }
    CHECK(checkedAbandoned);
    CHECK(checkedAuthored);
    CHECK(checkedLongHouse);
    CHECK(sawDamage);
}

TEST_CASE("Partial edge chunks fill out-of-bounds slots with undamaged wall", "[worldgen][coordinates]") {
    for (int x : {-world_coordinate_limit, world_coordinate_limit}) {
        for (int z : {-world_coordinate_limit, world_coordinate_limit}) {
            const TileCoord corner{x, z};
            const auto chunk = ChunkOf(corner);
            const auto plan = GenerateRegionPlan(TestContent(), 77, RegionOf(corner));
            const auto tiles = GenerateChunk(TestContent(), plan.seed, plan, chunk);
            int validCount = 0;
            for (int dz = 0; dz < chunk_size; ++dz) {
                for (int dx = 0; dx < chunk_size; ++dx) {
                    const TileCoord p{chunk.x * chunk_size + dx, chunk.z * chunk_size + dz};
                    const auto& tile = tiles[static_cast<std::size_t>(dz * chunk_size + dx)];
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

TEST_CASE("Clipped regional roads connect inward and world corners intentionally have no roads",
          "[worldgen][roads][coordinates]") {
    for (const RegionCoord region : {RegionCoord{3906, 2}, {-3907, 2}, {2, 3906}, {2, -3907}}) {
        const auto plan = GenerateRegionPlan(TestContent(), 42, region);
        const auto image = Materialize(TestContent(), plan, plan.bounds);
        const bool xAxis = region.x == 3906 || region.x == -3907;
        const bool positive = region.x == 3906 || region.z == 3906;
        const std::size_t side = xAxis ? (positive ? 3 : 1) : (positive ? 0 : 2);
        auto terminal = plan.boundaries[side].position;
        if (terminal.x == plan.bounds.max.x) { --terminal.x; }
        if (terminal.z == plan.bounds.max.z) { --terminal.z; }
        const auto seen = Reachable(image, TestContent(), terminal, true);
        int count = 0;
        for (std::size_t i = 0; i < image.tiles.size(); ++i) {
            if (image.tiles[i].terrain != TestTerrain("road")) { continue; }
            CHECK(seen[i] != 0);
            ++count;
        }
        CHECK(count > 0);
        RegionCoord inward = region;
        if (xAxis) { inward.x += positive ? -1 : 1; }
        else { inward.z += positive ? -1 : 1; }
        const auto neighbor = GenerateRegionPlan(TestContent(), 42, inward);
        CHECK(plan.boundaries[side] == neighbor.boundaries[(side + 2) % 4]);
    }
    for (int x : {-3907, 3906}) {
        for (int z : {-3907, 3906}) {
            const auto plan = GenerateRegionPlan(TestContent(), 42, {x, z});
            const auto image = Materialize(TestContent(), plan, plan.bounds);
            CHECK_FALSE(plan.hasTown);
            for (const auto tile : image.tiles) { CHECK(tile.terrain != TestTerrain("road")); }
        }
    }
}

LoadRequest Request(World& world, ChunkCoord chunk) {
    const auto result = world.RequestChunks({chunk, 0}, LoadMode::active);
    REQUIRE(result.has_value());
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.CheckInvariants());
    return *result;
}

TEST_CASE("Default World generation matches public plans regardless of streaming order", "[worldgen][world][streaming]") {
    constexpr std::uint32_t seed = 31415;
    World forward(TestContent(), seed);
    World reverse(TestContent(), seed);
    const Vector<ChunkCoord> chunks{{20, 21}, {21, 21}, {-17, -1}, {-16, -1}, {0, 0},
                                  {20, 21, 1}, {-17, -1, -1}, {0, 0, world_max_y}};
    constexpr std::size_t loadedSlices = 5 * world_layer_count; // Five distinct horizontal columns.
    Vector<LoadRequest> requests;
    for (const auto chunk : chunks) { requests.push_back(Request(forward, chunk)); }
    for (std::size_t i = chunks.size(); i > 0; --i) { Request(reverse, chunks[i - 1]); }
    REQUIRE(forward.LoadedChunks().size() == loadedSlices);
    REQUIRE(reverse.LoadedChunks().size() == loadedSlices);
    for (const auto chunk : forward.LoadedChunks()) { CHECK(reverse.GetChunk(chunk)); }
    CHECK(forward.ArchivedChunkCount() == 0);
    for (const auto chunk : chunks) {
        const auto* first = forward.GetChunk(chunk);
        const auto* second = reverse.GetChunk(chunk);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        const auto plan = GenerateRegionPlan(TestContent(), seed,
            RegionOf({chunk.x * chunk_size, chunk.z * chunk_size, chunk.y}));
        CHECK(first->tiles == GenerateChunk(TestContent(), seed, plan, chunk));
        CHECK(first->tiles == second->tiles);
        CHECK(first->actors.empty());
        CHECK(first->objects.empty());
        CHECK(first->ground_items.empty());
        CHECK(second->actors.empty());
        CHECK(second->objects.empty());
        CHECK(second->ground_items.empty());
    }
    const TileCoord edited{20 * chunk_size + 3, 21 * chunk_size + 5};
    const Tile manual{TestTerrain("open_door"), 117};
    REQUIRE(forward.SetTile(edited, manual));
    const auto modified = forward.GetChunk(chunks[0])->tiles;
    for (const auto request : requests) { REQUIRE(forward.ReleaseRequest(request)); }
    REQUIRE(forward.ReconcileChunks());
    CHECK(forward.LoadedChunks().empty());
    CHECK(forward.ArchivedChunkCount() == loadedSlices);
    for (std::size_t i = chunks.size(); i > 0; --i) { Request(forward, chunks[i - 1]); }
    CHECK(forward.ArchivedChunkCount() == 0);
    CHECK(forward.LoadedChunks().size() == loadedSlices);
    CHECK(forward.GetChunk(chunks[0])->tiles == modified);
    REQUIRE(forward.GetTile(edited) != nullptr);
    CHECK(*forward.GetTile(edited) == manual);
    for (std::size_t i = 1; i < chunks.size(); ++i) {
        CHECK(forward.GetChunk(chunks[i])->tiles == reverse.GetChunk(chunks[i])->tiles);
    }
    CHECK(forward.ActiveActors().empty());
    CHECK(forward.ActiveObjects().empty());
    const auto actor = forward.CreateActor({0, 0});
    REQUIRE(actor.has_value());
    CHECK(forward.GetActor(*actor)->id == 1);
    CHECK(forward.CheckInvariants());
    CHECK(reverse.CheckInvariants());
}

TEST_CASE("The legacy demo clearing stays floor through region and chunk seams", "[worldgen][world]") {
    for (const RegionCoord region : {RegionCoord{-1, -1}, {-1, 0}, {0, -1}, {0, 0}}) {
        const auto plan = GenerateRegionPlan(TestContent(), 42, region);
        const TileRect clearing{{region.x < 0 ? -32 : 0, region.z < 0 ? -32 : 0},
                                {region.x < 0 ? 0 : 33, region.z < 0 ? 0 : 33}};
        const auto image = Materialize(TestContent(), plan, clearing);
        for (int z = clearing.min.z; z < clearing.max.z; ++z) {
            for (int x = clearing.min.x; x < clearing.max.x; ++x) {
                CHECK(image.At({x, z}) == Tile{TestTerrain("floor"), 0});
                CHECK_FALSE(IsRoad(plan, {x, z}));
                CHECK(BuildingAt(plan, {x, z}) == nullptr);
            }
        }
    }
}
}
