#include "content/content.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

String Document(eastl::string_view definitions) {
    String text = "{format_version:1,definitions:[";
    text.append(definitions.data(), definitions.size());
    text += "]}";
    return text;
}

String Pattern(eastl::string_view id = "house", eastl::string_view fields =
               "rows:['###','#.#','#+#'],palette:{'#':'wall','.':'floor','+':'closed'},entrance:{x:1,z:2}") {
    String text = "{type:'building_pattern',id:'";
    text.append(id.data(), id.size());
    text += "',";
    text.append(fields.data(), fields.size());
    text += '}';
    return text;
}

constexpr eastl::string_view terrains = R"(
    {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
     appearance:{symbol:'#',foreground:'white'}},
    {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
     appearance:{symbol:'.',foreground:'white'}},
    {type:'terrain',id:'closed',name:'Door',movement:{passable:false},blocks_sight:true,
     opens_to:'floor',looks_like:'wall'}
)";

ContentResult<ContentCatalog> Load(eastl::string_view definition, Diagnostics* diagnostics = nullptr) {
    const String text = Document(View(String(terrains.data(), terrains.size()) + ','
                                      + String(definition.data(), definition.size())));
    const ContentSource source{"patterns.json5", View(text)};
    return LoadContent({&source, 1}, diagnostics);
}

ContentCatalog Take(ContentResult<ContentCatalog> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

void Invalid(eastl::string_view definition, eastl::string_view path = {}) {
    Diagnostics diagnostics;
    const auto result = Load(definition, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames()[0].location.has_value());
    CHECK(diagnostics.Frames()[0].location->file == "patterns.json5");
    if (!path.empty()) { CHECK(diagnostics.Frames()[0].location->path == path); }
    const auto quiet = Load(definition);
    REQUIRE_FALSE(quiet.has_value());
    CHECK(quiet.error() == ContentError::InvalidData);
}

TEST_CASE("Building patterns own resolved row-major tiles and use typed lookup", "[content][building_pattern]") {
    ContentCatalog catalog;
    {
        String definition = Pattern();
        catalog = Take(Load(View(definition)));
    }
    CHECK(catalog.BuildingPatternCount() == 1);
    CHECK(catalog.TerrainCount() == 3);
    CHECK(catalog.FindBuildingPattern("house") == BuildingPatternId{1});
    CHECK(catalog.FindBuildingPattern("floor") == BuildingPatternId{});
    CHECK(catalog.FindTerrain("house") == TerrainId{});
    CHECK(catalog.TryGetBuildingPattern({}) == nullptr);
    CHECK(catalog.TryGetBuildingPattern({2}) == nullptr);
    CHECK(catalog.TryGetBuildingPattern({0xffffffff}) == nullptr);
    const auto& pattern = catalog.GetBuildingPattern({1});
    CHECK(catalog.TryGetBuildingPattern({1}) == &pattern);
    CHECK(pattern.id == "house");
    CHECK(pattern.width == 3);
    CHECK(pattern.height == 3);
    CHECK(pattern.entrance == PatternPoint{1, 2});
    CHECK(pattern.weight == 1);
    REQUIRE(pattern.tiles.size() == 9);
    for (std::size_t index = 0; index < pattern.tiles.size(); ++index) {
        CHECK(pattern.tiles[index] == catalog.FindTerrain(index == 4 ? "floor" : index == 7 ? "closed" : "wall"));
    }
    CHECK(Take(LoadContent({})).BuildingPatternCount() == 0);
}

TEST_CASE("Building pattern palettes resolve air and stairs without changing their planar shape", "[content][building_pattern][vertical]") {
    const String vertical = R"({type:'terrain',id:'air',name:'Air',movement:{passable:true},
        blocks_sight:false,open_space:true,looks_like:'floor'},
        {type:'terrain',id:'stairs',name:'Stairs',movement:{passable:true},
        blocks_sight:false,stairs:true,looks_like:'floor'},)";
    const auto catalog = Take(Load(View(vertical + Pattern("landing",
        "rows:['s.',' a'],palette:{'s':'stairs','.':'floor',' ':'air','a':'air'},entrance:{x:0,z:0}"))));
    const auto& pattern = catalog.GetBuildingPattern(catalog.FindBuildingPattern("landing"));
    CHECK(pattern.width == 2);
    CHECK(pattern.height == 2);
    REQUIRE(pattern.tiles.size() == 4);
    CHECK(catalog.GetTerrain(pattern.tiles[0]).stairs);
    CHECK_FALSE(catalog.GetTerrain(pattern.tiles[1]).openSpace);
    CHECK(catalog.GetTerrain(pattern.tiles[2]).openSpace);
    CHECK(catalog.GetTerrain(pattern.tiles[3]).openSpace);
}

TEST_CASE("Building pattern references resolve forward across sources with independent sorted IDs", "[content][building_pattern]") {
    const String first = Document(View(Pattern("z_house") + ',' + Pattern("a_house")));
    const String second = Document(terrains);
    const ContentSource sources[] = {{"patterns", View(first)}, {"terrains", View(second)}};
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto catalog = Take(LoadContent({sources, 2}));
    const auto reordered = Take(LoadContent({reversed, 2}));
    CHECK(catalog.FindBuildingPattern("a_house") == BuildingPatternId{1});
    CHECK(catalog.FindBuildingPattern("z_house") == BuildingPatternId{2});
    CHECK(catalog.FindTerrain("closed") == TerrainId{1});
    CHECK(catalog.FindTerrain("floor") == TerrainId{2});
    CHECK(catalog.FindTerrain("wall") == TerrainId{3});
    for (auto name : {"a_house", "z_house"}) {
        const auto id = catalog.FindBuildingPattern(name);
        CHECK(id == reordered.FindBuildingPattern(name));
        CHECK(catalog.GetBuildingPattern(id).tiles == reordered.GetBuildingPattern(id).tiles);
    }
}

TEST_CASE("Building pattern schema rejects missing unknown and duplicate fields", "[content][building_pattern]") {
    for (const char* fields : {
        "palette:{'.':'floor'},entrance:{x:0,z:0}",
        "rows:['.'],entrance:{x:0,z:0}",
        "rows:['.'],palette:{'.':'floor'}",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0}",
        "rows:['.'],palette:{'.':'floor'},entrance:{z:0}",
        "rows:['.'],palette:{'.':'floor'},entrance:null",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0,y:0}",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0,x:0}",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0,z:0}",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0},extra:0",
        "rows:['.'],rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0}",
        "rows:['.'],palette:{'.':'floor'},palette:{'.':'floor'},entrance:{x:0,z:0}",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0},entrance:{x:0,z:0}",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0},weight:1,weight:2",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0},id:'other'",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0},type:'building_pattern'",
    }) {
        CAPTURE(fields);
        Invalid(View(Pattern("house", fields)));
    }
    for (const char* id : {"", "House", "a-b", "a\\u0000b"}) { Invalid(View(Pattern(id))); }
    Invalid("{type:'building_pattern',rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0}}");
}

TEST_CASE("Building pattern rows and palettes require printable single-byte glyphs", "[content][building_pattern]") {
    for (const char* rows : {"null", "{}", "[]", "['']", "[1]", "['..','.']", "['é']",
                             "['\\t']", "['\\u0000']", "['\\u001f']", "['\\u007f']"}) {
        CAPTURE(rows);
        String fields = "rows:";
        fields += rows;
        fields += ",palette:{'.':'floor'},entrance:{x:0,z:0}";
        Invalid(View(Pattern("house", View(fields))));
    }
    for (const char* palette : {"null", "[]", "{}", "{'..':'floor'}", "{'':'floor'}",
                                "{'é':'floor'}", "{'\\n':'floor'}", "{'\\u007f':'floor'}",
                                "{'.':3}", "{'.':''}", "{'.':'bad-id'}", "{'.':'floor','.':'wall'}"}) {
        CAPTURE(palette);
        String fields = "rows:['.'],palette:";
        fields += palette;
        fields += ",entrance:{x:0,z:0}";
        Invalid(View(Pattern("house", View(fields))));
    }
    CHECK(Load(View(Pattern("house", "rows:[' ~'],palette:{' ':'floor','~':'floor'},entrance:{x:0,z:0}"))).has_value());
    CHECK(Load(View(Pattern("house", "rows:['.'],palette:{'.':'floor','#':'wall'},entrance:{x:0,z:0}"))).has_value());
    Invalid(View(Pattern("house", "rows:['.?'],palette:{'.':'floor'},entrance:{x:0,z:0}")),
            "definitions[3].rows[0]");
}

TEST_CASE("Building pattern dimensions include 64 and reject larger footprints", "[content][building_pattern]") {
    for (int width : {1, 64, 65}) {
        for (int height : {1, 64, 65}) {
            CAPTURE(width, height);
            String fields = "rows:[";
            for (int z = 0; z < height; ++z) {
                if (z != 0) { fields += ','; }
                fields += '\'';
                fields.append(static_cast<std::size_t>(width), '.');
                fields += '\'';
            }
            fields += "],palette:{'.':'floor'},entrance:{x:0,z:0}";
            const auto result = Load(View(Pattern("house", View(fields))));
            CHECK(result.has_value() == (width <= 64 && height <= 64));
            if (result) {
                CHECK(result->GetBuildingPattern({1}).tiles.size() == static_cast<std::size_t>(width * height));
            } else { CHECK(result.error() == ContentError::InvalidData); }
        }
    }
}

TEST_CASE("Building pattern weight is a bounded optional integer", "[content][building_pattern]") {
    for (const char* weight : {"0", "-1", "1000001", "1.0", "true", "null", "'1'"}) {
        String fields = "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0},weight:";
        fields += weight;
        Invalid(View(Pattern("house", View(fields))), "definitions[3].weight");
    }
    const auto catalog = Take(Load(View(Pattern("house",
        "rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0},weight:1000000"))));
    CHECK(catalog.GetBuildingPattern({1}).weight == 1000000);
}

TEST_CASE("Building pattern entrances must be in bounds on an accessible boundary", "[content][building_pattern]") {
    for (const char* entrance : {"{x:-1,z:0}", "{x:0,z:-1}", "{x:3,z:0}", "{x:0,z:3}",
                                 "{x:1,z:1}", "{x:1.0,z:0}", "{x:0,z:true}", "{x:'0',z:0}"}) {
        String fields = "rows:['...','...','...'],palette:{'.':'floor'},entrance:";
        fields += entrance;
        Invalid(View(Pattern("house", View(fields))));
    }
    Invalid(View(Pattern("house", "rows:['#'],palette:{'#':'wall'},entrance:{x:0,z:0}")),
            "definitions[3].entrance");
    for (const char* entrance : {"{x:1,z:0}", "{x:0,z:1}", "{x:2,z:1}", "{x:1,z:2}"}) {
        String fields = "rows:['...','...','...'],palette:{'.':'floor'},entrance:";
        fields += entrance;
        CHECK(Load(View(Pattern("house", View(fields)))).has_value());
    }
    // An opens_to chain is not sufficient: the immediate target must be passable.
    const String chained = R"({type:'terrain',id:'chained',name:'Chained',movement:{passable:false},
        blocks_sight:true,looks_like:'wall',opens_to:'closed'},)" +
        Pattern("house", "rows:['+'],palette:{'+':'chained'},entrance:{x:0,z:0}");
    Invalid(View(chained), "definitions[4].entrance");
}

TEST_CASE("Building pattern reference diagnostics cover unused palette entries", "[content][building_pattern]") {
    const String definition = Pattern("house",
        "rows:['.'],palette:{'.':'floor','?':'missing'},entrance:{x:0,z:0}");
    Diagnostics diagnostics;
    REQUIRE_FALSE(Load(View(definition), &diagnostics).has_value());
    REQUIRE(diagnostics.Frames().size() == 3);
    CHECK(diagnostics.Frames()[0].message == "unknown terrain reference 'missing' from building pattern 'house'");
    CHECK(diagnostics.Frames()[0].location->path == "definitions[3].palette.?");
    CHECK(diagnostics.Frames()[1].message == "while loading building pattern 'house'");
    CHECK(diagnostics.Frames()[2].message == "could not load content catalog");
    Invalid(View(definition));
    Invalid(View(Pattern("house", "rows:['.'],palette:{'.':'house'},entrance:{x:0,z:0}")));
}

TEST_CASE("Building pattern duplicate IDs retain source order across definition kinds", "[content][building_pattern]") {
    const String terrainDocument = Document(terrains);
    const String patternDocument = Document(View(Pattern("floor")));
    const ContentSource terrainSource{"terrain", View(terrainDocument)};
    const ContentSource patternSource{"pattern", View(patternDocument)};
    for (bool patternFirst : {false, true}) {
        const ContentSource sources[] = {patternFirst ? patternSource : terrainSource,
                                         patternFirst ? terrainSource : patternSource};
        Diagnostics diagnostics;
        const auto result = LoadContent({sources, 2}, &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE(diagnostics.Frames().size() == 4);
        CHECK(View(diagnostics.Frames()[0].location->file) == sources[1].name);
        CHECK(View(diagnostics.Frames()[1].location->file) == sources[0].name);
        CHECK(diagnostics.Frames()[1].message == "previous definition is here");
        CHECK_FALSE(LoadContent({sources, 2}).has_value());
    }
    Invalid(View(Pattern() + ',' + Pattern()));
    const String duplicate = Document(View(Pattern()));
    const ContentSource sources[] = {{"first", View(duplicate)}, {"second", View(duplicate)}};
    Diagnostics diagnostics;
    REQUIRE_FALSE(LoadContent({sources, 2}, &diagnostics).has_value());
    CHECK(diagnostics.Frames()[0].message == "duplicate building pattern definition 'house'");
    CHECK(diagnostics.Frames()[0].location->file == "second");
    CHECK(diagnostics.Frames()[1].location->file == "first");
}

TEST_CASE("Bundled building patterns are enclosed with south-facing closed door entrances", "[content][building_pattern]") {
    String path = __FILE__;
    const auto separator = path.find_last_of("/\\");
    REQUIRE(separator != String::npos);
    path.resize(separator);
    path += "/../data/terrain.json5";
    const auto catalog = Take(LoadContentFile(path.c_str()));
    REQUIRE(catalog.BuildingPatternCount() == 3);
    CHECK(catalog.FindBuildingPattern("compact_house") == BuildingPatternId{1});
    CHECK(catalog.FindBuildingPattern("divided_house") == BuildingPatternId{2});
    CHECK(catalog.FindBuildingPattern("long_house") == BuildingPatternId{3});
    for (const char* id : {"compact_house", "divided_house", "long_house"}) {
        const auto& pattern = catalog.GetBuildingPattern(catalog.FindBuildingPattern(id));
        CHECK(pattern.entrance.z == pattern.height - 1);
        REQUIRE(pattern.tiles.size() == static_cast<std::size_t>(pattern.width * pattern.height));
        for (int z = 0; z < pattern.height; ++z) {
            for (int x = 0; x < pattern.width; ++x) {
                if (x != 0 && z != 0 && x != pattern.width - 1 && z != pattern.height - 1) { continue; }
                const auto expected = PatternPoint{x, z} == pattern.entrance ? "closed_door" : "wall";
                CHECK(pattern.tiles[static_cast<std::size_t>(z * pattern.width + x)] == catalog.FindTerrain(expected));
            }
        }
    }
    const auto& longHouse = catalog.GetBuildingPattern(catalog.FindBuildingPattern("long_house"));
    CHECK(longHouse.width == 24);
    CHECK(longHouse.height == 12);
    CHECK(catalog.GetTerrain(catalog.FindTerrain("grass")).passable);
    CHECK(catalog.GetTerrain(catalog.FindTerrain("road")).passable);
    CHECK(catalog.GetTerrain(catalog.FindTerrain("rubble")).passable);
    CHECK_FALSE(catalog.GetTerrain(catalog.FindTerrain("tree")).passable);
    CHECK(catalog.GetTerrain(catalog.FindTerrain("tree")).blocksSight);
}

}
