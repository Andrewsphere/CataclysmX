#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

constexpr ContentSource basics{"house_basics.json5", R"({format_version:1,
    objects:[{id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:1000,
              blocks_movement:true,blocks_sight:true,movable:true,kind:'table'}],
    ai_profiles:[{id:'hunter'}],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
       appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'stair',name:'Stair',movement:{passable:true},blocks_sight:false,
       stairs:true,appearance:{symbol:'>',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
       appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,
       open_space:true,appearance:{symbol:' ',foreground:'white'}},
      {type:'item',id:'supply',name:'Supply',mass_g:1,volume_ml:100}
    ]})"};

String Document(eastl::string_view definitions) {
    String text = "{format_version:1,definitions:[";
    text.append(definitions.data(), definitions.size());
    text += "]}";
    return text;
}

ContentResult<ContentCatalog> Load(eastl::string_view definitions, Diagnostics* diagnostics = nullptr) {
    const auto document = Document(definitions);
    const ContentSource sources[] = {{"house.json5", View(document)}, basics};
    return LoadContent({sources, 2}, diagnostics);
}

void Invalid(eastl::string_view definitions) {
    Diagnostics diagnostics;
    const auto result = Load(definitions, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "house.json5");
    CHECK_FALSE(Load(definitions).has_value());
}

String Replace(eastl::string_view source, eastl::string_view from, eastl::string_view to) {
    String result(source.data(), source.size());
    const auto offset = result.find(from.data(), 0, from.size());
    REQUIRE(offset != String::npos);
    result.replace(offset, from.size(), to.data(), to.size());
    return result;
}

constexpr eastl::string_view recipes = R"(
    {type:'loot_pool',id:'supplies',entries:[{item:'supply',weight:3,count:{min:1,max:2}}]},
    {type:'monster',id:'zombie',name:'Zombie',ai_profile:'hunter',body:'simple',
     faction:'undead',speed:100,attack_damage:12,accuracy:9000},
    {type:'monster_group',id:'zombies',entries:[{monster:'zombie',weight:1}]}
)";

constexpr eastl::string_view house = R"({type:'building_pattern',id:'furnished_house',
    rows:['....','...>','....'],palette:{'.':'floor','>':'stair','#':'wall',' ':'air'},
    entrance:{x:0,z:1},layers:[{y:1,rows:['....','....','....']}],
    furniture:[{x:1,z:2,y:0,object:'cabinet',facing:'east'}],
    loot:[{x:1,z:2,y:0,pool:'supplies',rolls:2,destination:'furniture'}],
    monsters:[{x:2,z:0,y:0,group:'zombies'}],
    basements:[
      {id:'storage_cellar',weight:2,rows:['....','...>','....'],
       furniture:[{x:0,z:2,y:-1,object:'cabinet'}],
       loot:[{x:0,z:2,y:-1,pool:'supplies',destination:'furniture'}]},
      {id:'workshop_cellar',weight:1,rows:['....','...>','....'],
       monsters:[{x:2,z:2,y:-1,group:'zombies',chance:5000}]}
    ]})";

String WithRecipes(eastl::string_view pattern) {
    String definitions(recipes.data(), recipes.size());
    definitions += ',';
    definitions.append(pattern.data(), pattern.size());
    return definitions;
}

TEST_CASE("House definitions resolve forward references across sources with lexical IDs", "[content][house-content]") {
    const auto definitions = WithRecipes(house);
    const auto loaded = Load(View(definitions));
    REQUIRE(loaded.has_value());
    CHECK(loaded->LootPoolCount() == 1);
    CHECK(loaded->MonsterCount() == 1);
    CHECK(loaded->MonsterGroupCount() == 1);
    CHECK(loaded->FindLootPool("supplies") == LootPoolId{1});
    CHECK(loaded->FindMonster("zombie") == MonsterTypeId{1});
    CHECK(loaded->FindMonsterGroup("zombies") == MonsterGroupId{1});
    CHECK(loaded->FindLootPool("missing") == LootPoolId{});
    CHECK(loaded->FindMonster("missing") == MonsterTypeId{});
    CHECK(loaded->FindMonsterGroup("missing") == MonsterGroupId{});
    CHECK(loaded->TryGetLootPool({}) == nullptr);
    CHECK(loaded->TryGetLootPool({2}) == nullptr);
    CHECK(loaded->TryGetMonster({}) == nullptr);
    CHECK(loaded->TryGetMonster({2}) == nullptr);
    CHECK(loaded->TryGetMonsterGroup({}) == nullptr);
    CHECK(loaded->TryGetMonsterGroup({2}) == nullptr);
    CHECK(loaded->GetLootPool({1}).entries.front().item == loaded->FindItem("supply"));
    CHECK(loaded->GetMonster({1}).aiProfile == loaded->FindAiProfile("hunter"));
    CHECK(loaded->GetMonster({1}).accuracy == 9000);
    CHECK(loaded->GetMonsterGroup({1}).entries.front().monster == loaded->FindMonster("zombie"));

    const auto document = Document(View(definitions));
    const ContentSource reversed[] = {basics, {"house.json5", View(document)}};
    const auto other = LoadContent({reversed, 2});
    REQUIRE(other.has_value());
    CHECK(other->GetLootPool({1}) == loaded->GetLootPool({1}));
    CHECK(other->GetMonster({1}) == loaded->GetMonster({1}));
    CHECK(other->GetBuildingPattern({1}) == loaded->GetBuildingPattern({1}));
}

TEST_CASE("Authored population keeps x z y coordinates independent of rotation", "[content][house-content]") {
    const auto definitions = WithRecipes(house);
    const auto loaded = Load(View(definitions));
    REQUIRE(loaded.has_value());
    const auto& pattern = loaded->GetBuildingPattern(loaded->FindBuildingPattern("furnished_house"));
    CHECK(pattern.width == 4);
    CHECK(pattern.height == 3);
    REQUIRE(pattern.layers.size() == 1);
    CHECK(pattern.layers[0].y == 1);
    CHECK(pattern.population.furniture.front().position == TileCoord{1, 2, 0});
    CHECK(pattern.population.furniture.front().facing == ObjectFacing::East);
    CHECK(pattern.population.loot.front().position == TileCoord{1, 2, 0});
    CHECK(pattern.population.monsters.front().position == TileCoord{2, 0, 0});
    REQUIRE(pattern.basements.size() == 2);
    CHECK(pattern.basements[0].id == "storage_cellar");
    CHECK(pattern.basements[0].layer.y == -1);
    CHECK(pattern.basements[0].population.furniture.front().position == TileCoord{0, 2, -1});
    CHECK(pattern.basements[1].population.monsters.front().position == TileCoord{2, 2, -1});
    const auto stairIndex = 1 * pattern.width + 3;
    CHECK(pattern.tiles[stairIndex] == loaded->FindTerrain("stair"));
    for (const auto& basement : pattern.basements) { CHECK(basement.layer.tiles[stairIndex] == pattern.tiles[stairIndex]); }
}

TEST_CASE("Spawn catalog IDs sort lexically while weighted entries keep authored order", "[content][house-content]") {
    const ContentSource sources[] = {
        {"forward.json5", R"({format_version:1,definitions:[
            {type:'loot_pool',id:'z_pool',entries:[{pool:'a_pool',weight:7},{item:'supply',weight:1}]},
            {type:'monster_group',id:'z_group',entries:[{monster:'z_monster'},{monster:'a_monster'}]},
            {type:'monster_group',id:'a_group',entries:[{monster:'a_monster'}]}
        ]})"}, basics,
        {"targets.json5", R"({format_version:1,definitions:[
            {type:'monster',id:'z_monster',name:'Z',ai_profile:'hunter'},
            {type:'monster',id:'a_monster',name:'A',ai_profile:'hunter'},
            {type:'loot_pool',id:'a_pool',entries:[{item:'supply'}]}
        ]})"}
    };
    const auto loaded = LoadContent({sources, 3});
    REQUIRE(loaded.has_value());
    CHECK(loaded->FindLootPool("a_pool") == LootPoolId{1});
    CHECK(loaded->FindLootPool("z_pool") == LootPoolId{2});
    CHECK(loaded->FindMonster("a_monster") == MonsterTypeId{1});
    CHECK(loaded->FindMonster("z_monster") == MonsterTypeId{2});
    CHECK(loaded->FindMonsterGroup("a_group") == MonsterGroupId{1});
    CHECK(loaded->FindMonsterGroup("z_group") == MonsterGroupId{2});
    CHECK(loaded->GetLootPool({2}).entries.front().pool == LootPoolId{1});
    CHECK(loaded->GetLootPool({2}).entries.front().weight == 7);
    CHECK(loaded->GetMonsterGroup({2}).entries.front().monster == MonsterTypeId{2});
    const ContentSource reversed[] = {sources[2], sources[1], sources[0]};
    const auto other = LoadContent({reversed, 3});
    REQUIRE(other.has_value());
    CHECK(other->GetLootPool({2}) == loaded->GetLootPool({2}));
    CHECK(other->GetMonsterGroup({2}) == loaded->GetMonsterGroup({2}));
}

TEST_CASE("Loot graph rejects cycles unknown references and ambiguous strict schemas", "[content][house-content]") {
    const eastl::string_view invalid[] = {
        "{type:'loot_pool',id:'pool',entries:[]}",
        "{type:'loot_pool',id:'pool',entries:[{pool:'pool'}]}",
        "{type:'loot_pool',id:'a',entries:[{pool:'b'}]},{type:'loot_pool',id:'b',entries:[{pool:'a'}]}",
        "{type:'loot_pool',id:'pool',entries:[{pool:'missing'}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'missing'}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',pool:'pool'}]}",
        "{type:'loot_pool',id:'pool',entries:[{}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',weight:0}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',weight:4294967295},{item:'supply'}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',count:{min:0,max:1}}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',count:{min:2,max:1}}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',count:{min:1,max:1025}}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',count:1}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',count:{min:1,max:1,extra:0}}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',weight:1,weight:2}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply',extra:0}]}",
        "{type:'loot_pool',id:'pool',entries:[{item:'supply'}],extra:0}",
        "{type:'loot_pool',id:'same',entries:[{item:'supply'}]},{type:'monster_group',id:'same',entries:[{monster:'missing'}]}"
    };
    for (const auto definition : invalid) { INFO(definition.data()); Invalid(definition); }
}

TEST_CASE("Cross-kind duplicate diagnostics identify the later source and the original definition", "[content][house-content]") {
    constexpr ContentSource pool{"house.json5", R"({format_version:1,definitions:[
        {type:'loot_pool',id:'supply',entries:[{item:'supply'}]}
    ]})"};
    for (const bool poolFirst : {true, false}) {
        const ContentSource sources[] = {poolFirst ? pool : basics, poolFirst ? basics : pool};
        Diagnostics diagnostics;
        const auto result = LoadContent({sources, 2}, &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE(diagnostics.Frames().size() >= 2);
        const auto& duplicate = diagnostics.Frames()[0];
        const auto& original = diagnostics.Frames()[1];
        REQUIRE(duplicate.location.has_value());
        REQUIRE(original.location.has_value());
        CHECK(View(duplicate.location->file) == sources[1].name);
        CHECK(duplicate.location->path == (poolFirst ? "definitions[4].id" : "definitions[0].id"));
        CHECK(duplicate.message.find("duplicate") != String::npos);
        CHECK(duplicate.message.find("'supply'") != String::npos);
        CHECK(View(original.location->file) == sources[0].name);
        CHECK(original.location->path == (poolFirst ? "definitions[0].id" : "definitions[4].id"));
        CHECK(original.message == "previous definition is here");
        CHECK_FALSE(LoadContent({sources, 2}).has_value());
    }
}

String Chain(unsigned length, unsigned count = 1) {
    String result;
    for (unsigned i = 0; i < length; ++i) {
        if (i) { result += ','; }
        char text[256];
        if (i + 1 == length) {
            std::snprintf(text, sizeof(text), "{type:'loot_pool',id:'p%u',entries:[{item:'supply'}]}", i);
        } else {
            std::snprintf(text, sizeof(text), "{type:'loot_pool',id:'p%u',entries:[{pool:'p%u',count:{min:1,max:%u}}]}", i, i + 1, count);
        }
        result += text;
    }
    return result;
}

TEST_CASE("Loot depth expansion work and leaves have independent inclusive limits", "[content][house-content]") {
    CHECK(Load(View(Chain(16))).has_value());
    Invalid(View(Chain(17)));
    CHECK(Load("{type:'loot_pool',id:'pool',entries:[{item:'supply',count:{min:1,max:1024}}]}").has_value());
    Invalid(View(Chain(3, 33))); // 1089 leaves, well below the work cap.
    // 1024 leaves but five pool invocations on each branch: work=4097.
    Invalid(R"({type:'loot_pool',id:'root',entries:[{pool:'a',count:{min:1,max:1024}}]},
       {type:'loot_pool',id:'a',entries:[{pool:'b'}]},
       {type:'loot_pool',id:'b',entries:[{pool:'c'}]},
       {type:'loot_pool',id:'c',entries:[{pool:'d'}]},
       {type:'loot_pool',id:'d',entries:[{item:'supply'}]})");
    // A previously finished shared child must still contribute its full depth.
    String shared = Chain(16);
    shared += ",{type:'loot_pool',id:'z',entries:[{pool:'p0'}]}";
    Invalid(View(shared));
}

TEST_CASE("Monster recipes enforce current combat bounds and exact percent conversion", "[content][house-content]") {
    const eastl::string_view invalidFields[] = {
        "accuracy:9001", "accuracy:10001", "accuracy:-100", "accuracy:90.5",
        "attack_damage:100001", "speed:0", "speed:4294967296", "body:'blob'",
        "faction:'hostile'", "extra:1", "speed:1,speed:2"
    };
    for (const auto fields : invalidFields) {
        String definition = "{type:'monster',id:'zombie',name:'Zombie',ai_profile:'hunter',";
        definition.append(fields.data(), fields.size());
        definition += '}';
        Invalid(View(definition));
    }
    CHECK(Load("{type:'monster',id:'zombie',name:'Z',ai_profile:'hunter',accuracy:0,attack_damage:0,speed:4294967295}").has_value());
    Invalid("{type:'monster',id:'zombie',name:'Z',ai_profile:'unknown'}");
    CHECK(Load("{type:'monster',id:'zombie',name:'Z',ai_profile:'hunter',body:'detailed',faction:'survivors',accuracy:10000,attack_damage:100000}").has_value());
    Invalid("{type:'monster_group',id:'group',entries:[]}");
    Invalid("{type:'monster_group',id:'group',entries:[{monster:'missing'}]}");
    Invalid("{type:'monster_group',id:'group',entries:[{monster:'missing',weight:0}]}");
    Invalid("{type:'monster_group',id:'group',entries:[{monster:'missing',weight:4294967295},{monster:'missing',weight:1}]}");
}

TEST_CASE("House placements reject invalid coordinates layers overlaps refs and stair pairs", "[content][house-content]") {
    struct Change { eastl::string_view from, to; };
    const Change invalid[] = {
        {"x:1,z:2,y:0,object", "x:4,z:2,y:0,object"},
        {"x:1,z:2,y:0,object", "x:1,z:3,y:0,object"},
        {"x:1,z:2,y:0,object", "x:1,z:2,y:2,object"},
        {"x:1,z:2,y:0,object", "x:1,z:2,object"},
        {"x:1,z:2,y:0,object", "x:1,z:2,y:0,y:0,object"},
        {"object:'cabinet',facing:'east'", "object:'unknown',facing:'east'"},
        {"facing:'east'", "facing:'up'"},
        {"pool:'supplies',rolls:2", "pool:'unknown',rolls:2"},
        {"group:'zombies'", "group:'unknown'"},
        {"rolls:2", "rolls:1025"},
        {"rolls:2", "chance:10001"},
        {"destination:'furniture'", "destination:'pocket'"},
        {"x:1,z:2,y:0,pool", "x:0,z:2,y:0,pool"},
        {"x:2,z:0,y:0,group", "x:1,z:2,y:0,group"},
        {"x:1,z:2,y:0,object", "x:3,z:1,y:0,object"},
        {"x:2,z:0,y:0,group", "x:3,z:1,y:0,group"},
        {"x:1,z:2,y:0,pool", "x:3,z:1,y:0,pool"},
        {"x:0,z:2,y:-1,object", "x:0,z:2,y:0,object"},
        {"y:1,rows", "y:0,rows"},
        {"y:1,rows", "y:-1,rows"},
        {"y:1,rows", "y:11,rows"},
        {"y:1,rows", "y:18446744073709551615,rows"},
        {"y:1,rows:['....','....','....']", "y:1,rows:['....','....']"},
        {"y:1,rows:['....','....','....']", "y:1,rows:['?....','....','....']"},
        {"y:1,rows:['....','....','....']", "y:1,rows:['?...','....','....']"},
        {"layers:[{y:1,rows:['....','....','....']}]", "layers:[{y:1,rows:['....','....','....']},{y:1,rows:['....','....','....']}]"},
        {"id:'workshop_cellar'", "id:'storage_cellar'"},
        {"id:'workshop_cellar',weight:1,rows:['....','...>','....']", "id:'workshop_cellar',weight:1,rows:['....','....','....']"},
        {"id:'workshop_cellar',weight:1", "id:'workshop_cellar',weight:0"},
        {"id:'workshop_cellar',weight:1", "id:'workshop_cellar',weight:1,extra:0"},
        {"id:'workshop_cellar',weight:1", "id:'workshop_cellar',weight:1,weight:2"},
        {"pool:'supplies',rolls:2", "pool:'supplies',rolls:2,extra:0"},
        {"object:'cabinet',facing:'east'", "object:'cabinet',facing:'east',extra:0"},
        {"group:'zombies'", "group:'zombies',extra:0"},
        {"weight:2,rows:['....','...>','....']", "weight:2,rows:['....','....','....']"},
        {"weight:2,rows:['....','...>','....']", "weight:2,rows:['>...','...>','....']"},
        {"rows:['....','...>','....'],palette", "rows:['....','...>','.#..'],palette"},
        {"rows:['....','...>','....'],palette", "rows:['....','...>','. ..'],palette"},
        {"furniture:[{x:1,z:2,y:0,object:'cabinet',facing:'east'}]", "furniture:[{x:1,z:2,y:0,object:'cabinet'},{x:1,z:2,y:0,object:'cabinet'}]"},
        {"monsters:[{x:2,z:0,y:0,group:'zombies'}]", "monsters:[{x:2,z:0,y:0,group:'zombies'},{x:2,z:0,y:0,group:'zombies'}]"},
        {"furniture:[{x:1,z:2,y:0,object:'cabinet',facing:'east'}]", "furniture:[{x:1,z:2,y:0,object:'cabinet'},{x:0,z:2,y:-1,object:'cabinet'}]"}
    };
    for (const auto& change : invalid) {
        INFO(change.to.data());
        const auto changed = Replace(house, change.from, change.to);
        const auto definitions = WithRecipes(View(changed));
        Invalid(View(definitions));
    }
}

TEST_CASE("Furniture capacity sums worst-case alternatives counts and all placement rolls", "[content][house-content]") {
    const auto exactHouse = Replace(house, "rolls:2", "rolls:5"); // 2 * 5 * 100 ml = 1000.
    CHECK(Load(View(WithRecipes(View(exactHouse)))).has_value());
    const auto overflow = Replace(house, "rolls:2", "rolls:6");
    Invalid(View(WithRecipes(View(overflow))));
    const auto combined = Replace(house,
        "loot:[{x:1,z:2,y:0,pool:'supplies',rolls:2,destination:'furniture'}]",
        "loot:[{x:1,z:2,y:0,pool:'supplies',rolls:3,destination:'furniture'},"
        "{x:1,z:2,y:0,pool:'supplies',rolls:3,destination:'furniture'}]");
    Invalid(View(WithRecipes(View(combined))));
    for (const auto zero : {"chance:0,rolls:1024", "rolls:0"}) {
        const auto empty = Replace(house, "rolls:2", zero);
        CHECK(Load(View(WithRecipes(View(empty)))).has_value());
    }
    const auto ground = Replace(house, "rolls:2,destination:'furniture'", "rolls:513,destination:'ground'");
    Invalid(View(WithRecipes(View(ground)))); // Per-placement leaf count includes every roll.
    const auto missingCapacity = Replace(basics.text, "capacity_volume_ml:1000,", "");
    const auto document = Document(View(WithRecipes(house)));
    const ContentSource sources[] = {{"house.json5", View(document)}, {"basics.json5", View(missingCapacity)}};
    CHECK_FALSE(LoadContent({sources, 2}).has_value());
    // A nested choice must use the largest possible alternative, never an average.
    String recursive = "{type:'loot_pool',id:'nested',entries:[{pool:'supplies',count:{min:1,max:3}}]},";
    const auto nestedHouse = Replace(house, "pool:'supplies',rolls:2", "pool:'nested',rolls:2");
    recursive += WithRecipes(View(nestedHouse));
    Invalid(View(recursive)); // 3 * 2 * 2 * 100 ml.
    String alternatives = "{type:'loot_pool',id:'choice',entries:[{item:'supply',weight:4294967294},"
        "{item:'supply',weight:1,count:{min:1,max:11}}]},";
    const auto rareOverflow = Replace(house, "pool:'supplies',rolls:2", "pool:'choice',rolls:1");
    alternatives += WithRecipes(View(rareOverflow));
    Invalid(View(alternatives)); // Even an extremely rare 1100 ml draw must fit.
    const auto hugeItems = Replace(basics.text, "volume_ml:100}", "volume_ml:4294967295}");
    const auto hugeCapacity = Replace(View(hugeItems), "capacity_volume_ml:1000", "capacity_volume_ml:4294967295");
    const ContentSource hugeSources[] = {{"house.json5", View(document)}, {"basics.json5", View(hugeCapacity)}};
    CHECK_FALSE(LoadContent({hugeSources, 2}).has_value()); // Checked 64-bit volume, no uint32 wrap.
}

TEST_CASE("Placement work includes every root draw even at the exact expansion boundary", "[content][house-content]") {
    constexpr eastl::string_view marker = R"({type:'building_pattern',id:'simple_house',
        rows:['..'],palette:{'.':'floor'},entrance:{x:0,z:0},
        loot:[{x:1,z:0,y:0,pool:'p0',rolls:1024}]})";
    String definitions = Chain(4);
    definitions += ',';
    definitions.append(marker.data(), marker.size());
    CHECK(Load(View(definitions)).has_value()); // 4096 pool invocations, 1024 items.
    definitions = Chain(5);
    definitions += ',';
    definitions.append(marker.data(), marker.size());
    Invalid(View(definitions)); // Work fails while leaf count remains legal.
}
}
