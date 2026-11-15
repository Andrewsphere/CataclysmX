#include "worldgen/worldgen.h"
#include "core/first_house_scenario.h"
#include "core/ratio.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

// Non-square, wider than a chunk, with asymmetric layer markers. Each variant
// shares the surface stairs; every population tile is supported and off stairs.
String Rows(char marker, bool stairs = true) {
    String rows = "[";
    for (int z = 0; z < 7; ++z) {
        if (z != 0) { rows += ','; }
        rows += '\'';
        for (int x = 0; x < 19; ++x) {
            rows += x == 3 && z == 3 && stairs ? 's'
                : x == 1 && z == 1 ? marker : '.';
        }
        rows += '\'';
    }
    rows += ']';
    return rows;
}

String House(PatternPoint entrance, bool roof, bool extraMarker) {
    String text = "{type:'building_pattern',id:'furnished_house',rows:";
    text += Rows('m');
    text += ",palette:{'.':'floor','s':'stairs','m':'marker','w':'workshop'},entrance:{x:";
    char number[32];
    std::snprintf(number, sizeof(number), "%d", entrance.x);
    text += number;
    text += ",z:";
    std::snprintf(number, sizeof(number), "%d", entrance.z);
    text += number;
    text += "},layers:[{y:2,rows:";
    text += Rows('m', false);
    text += '}';
    if (roof) {
        text += ",{y:1,rows:";
        text += Rows('w', false);
        text += '}';
    }
    text += R"(],furniture:[{x:1,z:2,y:0,object:'cabinet',facing:'east'}],
        loot:[{x:1,z:2,y:0,pool:'nested',rolls:2,destination:'furniture'},
              {x:2,z:2,y:0,pool:'leaf',chance:0},
              {x:4,z:2,y:0,pool:'leaf',rolls:0},
              {x:16,z:2,y:0,pool:'leaf',chance:5000})";
    if (extraMarker) { text += ",{x:17,z:2,y:0,pool:'leaf'}"; }
    text += R"(],monsters:[{x:5,z:2,y:0,group:'undead'},
        {x:6,z:2,y:0,group:'undead',chance:0}],basements:[
        {id:'storage_cellar',weight:1,rows:)";
    text += Rows('m');
    text += R"(,furniture:[{x:2,z:4,y:-1,object:'cabinet',facing:'south'}],
        loot:[{x:2,z:4,y:-1,pool:'leaf',destination:'furniture'}]},
        {id:'workshop_cellar',weight:1,rows:)";
    text += Rows('w');
    text += R"(,monsters:[{x:2,z:4,y:-1,group:'undead'}]}]})";
    return text;
}

ContentCatalog Catalog(PatternPoint entrance = {9, 0}, bool roof = false,
                       bool extraMarker = false, bool extraIds = false, bool house = true,
                       bool legacy = false) {
    String text = R"({format_version:1,ai_profiles:[{id:'sighted'}],
      objects:[{id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:10000}],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
        appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
        appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,
        open_space:true,looks_like:'floor'},
      {type:'terrain',id:'stairs',name:'Stairs',movement:{passable:true},blocks_sight:false,
        stairs:true,looks_like:'floor'},
      {type:'terrain',id:'marker',name:'Marker',movement:{passable:true},blocks_sight:false,looks_like:'floor'},
      {type:'terrain',id:'workshop',name:'Workshop',movement:{passable:true},blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'apple',name:'Apple',mass_g:1,volume_ml:1},
      {type:'item',id:'bolt',name:'Bolt',mass_g:1,volume_ml:1},
      {type:'loot_pool',id:'leaf',entries:[{item:'apple',weight:1,count:{min:1,max:3}},
        {item:'bolt',weight:3,count:{min:1,max:3}}]},
      {type:'loot_pool',id:'nested',entries:[{pool:'leaf',count:{min:2,max:2}}]},
      {type:'monster',id:'shambler',name:'Shambler',ai_profile:'sighted'},
      {type:'monster',id:'walker',name:'Walker',ai_profile:'sighted'},
      {type:'monster_group',id:'undead',entries:[{monster:'shambler',weight:1},{monster:'walker',weight:3}]}
    )";
    if (extraIds) {
        text += R"(,{type:'item',id:'aaa',name:'Extra',mass_g:1,volume_ml:1},
          {type:'loot_pool',id:'a_pool',entries:[{item:'aaa'}]},
          {type:'monster',id:'a_monster',name:'Extra',ai_profile:'sighted'},
          {type:'monster_group',id:'a_group',entries:[{monster:'a_monster'}]})";
    }
    if (house) { text += ','; text += House(entrance, roof, extraMarker); }
    if (legacy) {
        text += R"(,{type:'building_pattern',id:'legacy',rows:['...','...','...'],
          palette:{'.':'floor'},entrance:{x:1,z:0}})";
    }
    text += "]}";
    const ContentSource source{"inline-house.json5", View(text)};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

// Independent rotation oracle, including the intermediate dimensions.
TileCoord Position(const BuildingPattern& pattern, const BuildingPlan& building, TileCoord local) {
    int width = pattern.width;
    int height = pattern.height;
    for (int i = 0; i < building.quarterTurns; ++i) {
        const int oldX = local.x;
        local.x = height - 1 - local.z;
        local.z = oldX;
        eastl::swap(width, height);
    }
    return {building.bounds.min.x + local.x, building.bounds.min.z + local.z, local.y};
}

Tile At(const ContentCatalog& content, const RegionPlan& plan, TileCoord position) {
    const auto tiles = GenerateChunk(content, plan.seed, plan, ChunkOf(position));
    return tiles[static_cast<std::size_t>(TileIndex(position))];
}

void CheckLayer(const ContentCatalog& content, const RegionPlan& plan, const BuildingPlan& building,
                const Vector<TerrainId>& tiles, int y) {
    const auto& pattern = content.GetBuildingPattern(building.pattern);
    ChunkCoord cachedChunk{};
    bool cached = false;
    eastl::array<Tile, chunk_size * chunk_size> generated{};
    for (int z = 0; z < pattern.height; ++z) {
        for (int x = 0; x < pattern.width; ++x) {
            const auto position = Position(pattern, building, {x, z, y});
            if (!cached || ChunkOf(position) != cachedChunk) {
                cachedChunk = ChunkOf(position);
                generated = GenerateChunk(content, plan.seed, plan, cachedChunk);
                cached = true;
            }
            CHECK(generated[static_cast<std::size_t>(TileIndex(position))]
                == Tile{tiles[static_cast<std::size_t>(z * pattern.width + x)], 0});
        }
    }
}

Vector<ChunkCoord> BuildingChunks(const BuildingPlan& building) {
    Vector<ChunkCoord> chunks;
    const auto first = ChunkOf(building.bounds.min);
    const auto last = ChunkOf({building.bounds.max.x - 1, building.bounds.max.z - 1});
    for (int y : {-1, 0, 1, 2}) {
        for (int x = first.x; x <= last.x; ++x) {
            for (int z = first.z; z <= last.z; ++z) { chunks.push_back({x, z, y}); }
        }
    }
    return chunks;
}

TEST_CASE("Explicit legacy plans exclude layered and populated patterns without changing draws",
          "[worldgen][house][legacy]") {
    const auto baseline = Catalog({9, 0}, false, false, false, false, true);
    const auto expanded = Catalog({9, 0}, false, false, false, true, true);
    for (const auto version : {2U, 3U}) {
        for (std::uint64_t seed = 0; seed < 8; ++seed) {
            const auto before = GenerateRegionPlan(baseline, seed, {-1, 1}, version);
            const auto after = GenerateRegionPlan(expanded, seed, {-1, 1}, version);
            CHECK(after.generationVersion == version);
            CHECK(after.roads == before.roads);
            CHECK(after.parcels == before.parcels);
            REQUIRE(after.buildings.size() == before.buildings.size());
            for (std::size_t i = 0; i < before.buildings.size(); ++i) {
                auto comparable = after.buildings[i];
                CHECK(expanded.GetBuildingPattern(comparable.pattern).id == "legacy");
                comparable.pattern = before.buildings[i].pattern;
                CHECK(comparable == before.buildings[i]);
                for (const auto chunk : BuildingChunks(before.buildings[i])) {
                    const auto oldTiles = GenerateChunk(baseline, seed, before, chunk);
                    const auto newTiles = GenerateChunk(expanded, seed, after, chunk);
                    for (std::size_t j = 0; j < oldTiles.size(); ++j) {
                        CHECK(baseline.GetTerrain(oldTiles[j].terrain).id
                            == expanded.GetTerrain(newTiles[j].terrain).id);
                        CHECK(oldTiles[j].damage == newTiles[j].damage);
                    }
                    ChunkPlacements output;
                    output.items.push_back({});
                    GenerateChunkPlacements(expanded, seed, after, chunk, output);
                    CHECK(output == ChunkPlacements{});
                }
            }
        }
    }
    const auto onlyNew = Catalog();
    CHECK(GenerateRegionPlan(onlyNew, 42, {0, 0}, 3).buildings.empty());
    const auto current = GenerateRegionPlan(onlyNew, 42, {0, 0});
    CHECK(current.generationVersion == world_generation_version);
    CHECK(current == GenerateRegionPlan(onlyNew, 42, {0, 0}, world_generation_version));
    CHECK_FALSE(current.buildings.empty());
}

TEST_CASE("House layers and once-selected basements rotate together with roof fallback",
          "[worldgen][house][vertical]") {
    const auto golden = Catalog();
    // Pins the string/domain/version seed contract independently of chunk draws.
    const auto workshop = GenerateRegionPlan(golden, 0, {0, 0}, 4);
    const auto storage = GenerateRegionPlan(golden, 2, {0, 0}, 4);
    REQUIRE_FALSE(workshop.buildings.empty());
    REQUIRE_FALSE(storage.buildings.empty());
    CHECK(workshop.buildings[0].basementVariant == 2);
    CHECK(storage.buildings[0].basementVariant == 1);
    bool variants[2] = {};
    bool rotations[4] = {};
    for (const auto entrance : {PatternPoint{9, 0}, {18, 3}}) {
        for (const bool roof : {false, true}) {
            const auto content = Catalog(entrance, roof);
            for (std::uint64_t seed = 0; seed < 4; ++seed) {
                const auto plan = GenerateRegionPlan(content, seed, {-1, 0}, 4);
                REQUIRE(plan.buildings.size() == 8);
                CHECK(plan == GenerateRegionPlan(content, seed, {-1, 0}, 4));
                for (const auto& building : plan.buildings) {
                    const auto& pattern = content.GetBuildingPattern(building.pattern);
                    REQUIRE(building.basementVariant >= 1);
                    REQUIRE(building.basementVariant <= 2);
                    variants[building.basementVariant - 1] = true;
                    rotations[building.quarterTurns] = true;
                    CHECK_FALSE(building.abandoned);
                    CheckLayer(content, plan, building, pattern.tiles, 0);
                    CheckLayer(content, plan, building,
                        pattern.basements[building.basementVariant - 1].layer.tiles, -1);
                    for (const auto& layer : pattern.layers) {
                        CheckLayer(content, plan, building, layer.tiles, layer.y);
                    }
                    const auto point = Position(pattern, building, {1, 1, 1});
                    CHECK(At(content, plan, point).terrain == content.FindTerrain(roof ? "workshop" : "floor"));
                    CHECK(At(content, plan, {point.x, point.z, -2}).terrain == content.FindTerrain("wall"));
                    CHECK(At(content, plan, {point.x, point.z, 3}).terrain == content.FindTerrain("air"));
                    for (int y : {-1, 0}) {
                        const auto stair = Position(pattern, building, {3, 3, y});
                        CHECK(content.GetTerrain(At(content, plan, stair).terrain).stairs);
                    }
                }
            }
        }
    }
    CHECK(variants[0]);
    CHECK(variants[1]);
    for (const bool seen : rotations) { CHECK(seen); }
}

TEST_CASE("Chunk populations are owned rotated reusable and independent of request order",
          "[worldgen][house][population]") {
    bool sawApple = false;
    bool sawBolt = false;
    bool sawShambler = false;
    bool sawWalker = false;
    bool sawChanceHit = false;
    bool sawChanceMiss = false;
    for (const auto entrance : {PatternPoint{9, 0}, {18, 3}}) {
        const auto content = Catalog(entrance);
        for (std::uint64_t seed = 0; seed < 8; ++seed) {
            const auto plan = GenerateRegionPlan(content, seed, {0, -1});
            ChunkPlacements scratch;
            scratch.items.reserve(128);
            const auto capacity = scratch.items.capacity();
            for (const auto& building : plan.buildings) {
                const auto& pattern = content.GetBuildingPattern(building.pattern);
                const auto chunks = BuildingChunks(building);
                Vector<ChunkPlacements> forward;
                for (const auto chunk : chunks) {
                    GenerateChunkPlacements(content, seed, plan, chunk, scratch);
                    CHECK(scratch.items.capacity() >= capacity);
                    forward.push_back(scratch);
                    for (const auto& furniture : scratch.furniture) {
                        CHECK(ChunkOf(furniture.position) == chunk);
                        CHECK(furniture.key.kind == GenerationPlacementKind::Furniture);
                        CHECK(furniture.key.ordinal == 0);
                        const auto* owner = BuildingAt(plan, furniture.position);
                        REQUIRE(owner);
                        const auto& recipe = content.GetBuildingPattern(owner->pattern);
                        const auto& population = furniture.key.basementVariant == 0 ? recipe.population
                            : recipe.basements[furniture.key.basementVariant - 1].population;
                        const auto& marker = population.furniture[furniture.key.placement];
                        CHECK(furniture.position == Position(recipe, *owner, marker.position));
                        CHECK(static_cast<unsigned>(furniture.facing)
                            == (static_cast<unsigned>(marker.facing) + owner->quarterTurns) % 4);
                    }
                    for (const auto& item : scratch.items) {
                        CHECK(ChunkOf(item.position) == chunk);
                        CHECK(item.key.kind == GenerationPlacementKind::Loot);
                        CHECK(item.type.value != 0);
                        const auto* owner = BuildingAt(plan, item.position);
                        REQUIRE(owner);
                        CHECK(item.key.owner == owner->owner);
                        const auto& recipe = content.GetBuildingPattern(owner->pattern);
                        const auto& population = item.key.basementVariant == 0 ? recipe.population
                            : recipe.basements[item.key.basementVariant - 1].population;
                        CHECK(item.position == Position(recipe, *owner, population.loot[item.key.placement].position));
                        const auto& name = content.GetItem(item.type).id;
                        sawApple |= name == "apple";
                        sawBolt |= name == "bolt";
                        if (item.key.basementVariant == 0) {
                            CHECK((item.key.placement == 0 || item.key.placement == 3));
                            CHECK(item.destination == (item.key.placement == 0
                                ? LootDestination::Furniture : LootDestination::Ground));
                        }
                    }
                    for (const auto& monster : scratch.monsters) {
                        CHECK(ChunkOf(monster.position) == chunk);
                        CHECK(monster.key.kind == GenerationPlacementKind::Monster);
                        CHECK(monster.key.placement == 0);
                        CHECK(monster.key.ordinal == 0);
                        const auto* owner = BuildingAt(plan, monster.position);
                        REQUIRE(owner);
                        CHECK(monster.key.owner == owner->owner);
                        const auto& recipe = content.GetBuildingPattern(owner->pattern);
                        const auto& population = monster.key.basementVariant == 0 ? recipe.population
                            : recipe.basements[monster.key.basementVariant - 1].population;
                        CHECK(monster.position == Position(recipe, *owner,
                            population.monsters[monster.key.placement].position));
                        const auto& name = content.GetMonster(monster.type).id;
                        sawShambler |= name == "shambler";
                        sawWalker |= name == "walker";
                    }
                }
                std::uint32_t leaves = 0;
                bool chanceHit = false;
                const auto lootPosition = Position(pattern, building, {1, 2, 0});
                for (const auto& batch : forward) {
                    for (const auto& item : batch.items) {
                        if (item.key.owner != building.owner || item.key.basementVariant != 0) { continue; }
                        if (item.key.placement == 3) { chanceHit = true; continue; }
                        CHECK(item.position == lootPosition);
                        CHECK(item.key.ordinal == leaves++);
                    }
                }
                CHECK(leaves >= 4); // Two root rolls, two independent child draws, 1..3 leaves.
                CHECK(leaves <= 12);
                sawChanceHit |= chanceHit;
                sawChanceMiss |= !chanceHit;
                for (std::size_t i = chunks.size(); i > 0; --i) {
                    GenerateChunkPlacements(content, seed, plan, chunks[i - 1], scratch);
                    CHECK(scratch == forward[i - 1]);
                }
            }
        }
    }
    CHECK(sawApple);
    CHECK(sawBolt);
    CHECK(sawShambler);
    CHECK(sawWalker);
    CHECK(sawChanceHit);
    CHECK(sawChanceMiss);
}

TEST_CASE("Population streams use stable strings and do not consume other marker streams",
          "[worldgen][house][determinism]") {
    const auto content = Catalog();
    const auto extended = Catalog({9, 0}, false, true, true);
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
        const auto plan = GenerateRegionPlan(content, seed, {0, 0});
        const auto other = GenerateRegionPlan(extended, seed, {0, 0});
        REQUIRE(plan.buildings == other.buildings);
        for (const auto& building : plan.buildings) {
            for (const auto chunk : BuildingChunks(building)) {
                ChunkPlacements a;
                ChunkPlacements b;
                GenerateChunkPlacements(content, seed, plan, chunk, a);
                GenerateChunkPlacements(extended, seed, other, chunk, b);
                CHECK(a.furniture == b.furniture);
                std::size_t original = 0;
                for (const auto& item : b.items) {
                    if (item.key.basementVariant == 0 && item.key.placement == 4) { continue; }
                    REQUIRE(original < a.items.size());
                    const auto& before = a.items[original++];
                    CHECK(item.key == before.key);
                    CHECK(item.position == before.position);
                    CHECK(item.destination == before.destination);
                    CHECK(extended.GetItem(item.type).id == content.GetItem(before.type).id);
                }
                CHECK(original == a.items.size());
                REQUIRE(a.monsters.size() == b.monsters.size());
                for (std::size_t i = 0; i < a.monsters.size(); ++i) {
                    CHECK(a.monsters[i].key == b.monsters[i].key);
                    CHECK(a.monsters[i].position == b.monsters[i].position);
                    CHECK(content.GetMonster(a.monsters[i].type).id == extended.GetMonster(b.monsters[i].type).id);
                }
            }
        }
    }
}

TEST_CASE("First-house query selects the globally nearest entrance in its bounded neighborhood",
          "[worldgen][house][scenario]") {
    const auto content = Catalog();
    for (const std::uint32_t seed : {0U, 1U, 42U}) {
        const auto start = FindFirstHouseStart(content, seed);
        REQUIRE(start);
        CHECK(start == FindFirstHouseStart(content, seed));
        const auto plan = GenerateRegionPlan(content, seed, RegionOf(start->entrance));
        const auto* house = BuildingAt(plan, start->entrance);
        REQUIRE(house);
        CHECK(house->entrance == start->entrance);
        CHECK(content.GetBuildingPattern(house->pattern).id == "furnished_house");
        CHECK(IsRoad(plan, start->playerPosition));
        CHECK_FALSE(Contains(house->bounds, start->playerPosition));
        CHECK(start->playerPosition.y == 0);
        CHECK(std::abs(start->playerPosition.x - start->entrance.x)
            + std::abs(start->playerPosition.z - start->entrance.z) == 1);
        const auto& floor = content.GetTerrain(At(content, plan, start->playerPosition).terrain);
        CHECK(floor.passable);
        CHECK_FALSE(floor.openSpace);
        ChunkPlacements output;
        GenerateChunkPlacements(content, seed, plan, ChunkOf(start->playerPosition), output);
        for (const auto& object : output.furniture) { CHECK(object.position != start->playerPosition); }
        for (const auto& item : output.items) { CHECK(item.position != start->playerPosition); }
        for (const auto& monster : output.monsters) { CHECK(monster.position != start->playerPosition); }
        const auto distance = [](TileCoord p) { return p.x * p.x + p.z * p.z; };
        for (int x = -1; x <= 1; ++x) {
            for (int z = -1; z <= 1; ++z) {
                const auto candidate = GenerateRegionPlan(content, seed, {x, z});
                for (const auto& building : candidate.buildings) {
                    CHECK(distance(start->entrance) <= distance(building.entrance));
                    if (distance(start->entrance) == distance(building.entrance)) {
                        CHECK(start->entrance.x <= building.entrance.x);
                        if (start->entrance.x == building.entrance.x) {
                            CHECK(start->entrance.z <= building.entrance.z);
                        }
                    }
                }
            }
        }
    }
    const auto noHouse = Catalog({9, 0}, false, false, false, false, true);
    Diagnostics diagnostics;
    CHECK(FindFirstHouseStart(noHouse, 42, &diagnostics) == eastl::unexpected(ContentError::InvalidData));
    CHECK(FormatDiagnostics(diagnostics).find("furnished_house") != String::npos);
    CHECK(FindFirstHouseStart(noHouse, 42) == eastl::unexpected(ContentError::InvalidData));
}

}
