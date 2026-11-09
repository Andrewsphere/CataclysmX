#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

#include <EASTL/utility.h>

#include <cstdio>
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

String Terrain(eastl::string_view id, eastl::string_view extra = {},
               eastl::string_view appearance = "{symbol:'.',foreground:'light_gray'}") {
    String text = "{type:'terrain',id:'";
    text.append(id.data(), id.size());
    text += "',name:'terrain',movement:{passable:true},blocks_sight:false";
    if (!appearance.empty()) {
        text += ",appearance:";
        text.append(appearance.data(), appearance.size());
    }
    if (!extra.empty()) { text += ','; text.append(extra.data(), extra.size()); }
    text += '}';
    return text;
}

ContentResult<ContentCatalog> Load(eastl::string_view text, Diagnostics* diagnostics = nullptr) {
    const ContentSource source{"test.json5", text};
    return LoadContent({&source, 1}, diagnostics);
}

ContentCatalog Take(ContentResult<ContentCatalog> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

const TerrainDefinition& Get(const ContentCatalog& catalog, eastl::string_view id) {
    const auto terrainId = catalog.FindTerrain(id);
    REQUIRE(terrainId.value != 0);
    return catalog.GetTerrain(terrainId);
}

void Invalid(eastl::string_view text) {
    Diagnostics diagnostics;
    const auto result = Load(text, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "test.json5");
    const auto withoutDiagnostics = Load(text);
    REQUIRE_FALSE(withoutDiagnostics.has_value());
    CHECK(withoutDiagnostics.error() == ContentError::InvalidData);
}

TEST_CASE("Content parses real JSON5 and owns its catalog strings", "[content]") {
    ContentCatalog catalog;
    {
        String source = R"({
            // These are JSON5 features, not preprocessed JSON.
            format_version: 1,
            definitions: [{
                type: 'terrain', id: 'floor', name: 'Floor',
                movement: {passable: true,}, blocks_sight: false,
                appearance: {symbol: '.', foreground: 'light_gray',},
            },],
        })";
        catalog = Take(Load(View(source)));
    }
    CHECK(catalog.TerrainCount() == 1);
    CHECK(catalog.FindTerrain("floor") == TerrainId{1});
    CHECK(catalog.FindTerrain("missing") == TerrainId{});
    CHECK(catalog.TryGetTerrain({}) == nullptr);
    CHECK(catalog.TryGetTerrain({2}) == nullptr);
    CHECK(catalog.TryGetTerrain({0xffffffff}) == nullptr);
    const auto& floor = Get(catalog, "floor");
    CHECK(floor.name == "Floor");
    CHECK(floor.appearance.symbol == ".");
    CHECK(floor.appearance.foreground == RgbColor{192, 192, 192});
    CHECK(floor.appearance.background == RgbColor{});
    CHECK(floor.passable);
    CHECK_FALSE(floor.blocksSight);
    CHECK_FALSE(floor.openSpace);
    CHECK_FALSE(floor.stairs);
    CHECK(floor.opensTo == TerrainId{});
    CHECK(floor.closesTo == TerrainId{});
    CHECK(Take(Load("{format_version:1,definitions:[]}")).TerrainCount() == 0);
    CHECK(Take(LoadContent({})).TerrainCount() == 0);
}

TEST_CASE("Vertical terrain flags are optional local booleans with strict invariants", "[content][vertical]") {
    const auto catalog = Take(Load(View(Document(View(
        Terrain("air", "open_space:true") + ',' + Terrain("stairs", "stairs:true") + ','
        + Terrain("child", "looks_like:'air'", "{}") + ','
        + Terrain("floor", "open_space:false,stairs:false"))))));
    CHECK(Get(catalog, "air").openSpace);
    CHECK_FALSE(Get(catalog, "air").stairs);
    CHECK(Get(catalog, "stairs").stairs);
    CHECK_FALSE(Get(catalog, "stairs").openSpace);
    CHECK_FALSE(Get(catalog, "child").openSpace);
    CHECK_FALSE(Get(catalog, "child").stairs);
    CHECK_FALSE(Get(catalog, "floor").openSpace);
    const auto airDocument = Document(View(Terrain("air", "open_space:true")));
    const auto stairDocument = Document(View(Terrain("stairs", "stairs:true")));
    const ContentSource sources[] = {{"air", View(airDocument)}, {"stairs", View(stairDocument)}};
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto first = Take(LoadContent({sources, 2}));
    const auto second = Take(LoadContent({reversed, 2}));
    for (const char* id : {"air", "stairs"}) {
        CHECK(first.FindTerrain(id) == second.FindTerrain(id));
        CHECK(Get(first, id).openSpace == Get(second, id).openSpace);
        CHECK(Get(first, id).stairs == Get(second, id).stairs);
    }
    for (const char* extra : {"open_space:null", "open_space:1", "open_space:'true'",
                              "stairs:null", "stairs:1", "stairs:'true'",
                              "open_space:true,stairs:true", "open_space:false,open_space:true",
                              "stairs:false,stairs:true"}) {
        CAPTURE(extra);
        Invalid(View(Document(View(Terrain("invalid", extra)))));
    }
    for (const char* flag : {"open_space", "stairs"}) {
        for (const char* fields : {"movement:{passable:false},blocks_sight:false",
                                   "movement:{passable:true},blocks_sight:true"}) {
            String definition = "{type:'terrain',id:'invalid',name:'Invalid',";
            definition += fields;
            definition += ',';
            definition += flag;
            definition += ":true,appearance:{symbol:'.',foreground:'white'}}";
            const auto text = Document(View(definition));
            Invalid(View(text));
            Diagnostics diagnostics;
            REQUIRE_FALSE(Load(View(text), &diagnostics));
            String path = "definitions[0].";
            path += flag;
            CHECK(diagnostics.Frames().front().location->path == path);
        }
    }
}

TEST_CASE("Content color palette and strict RGB hexadecimal are deterministic", "[content]") {
    struct ColorCase { const char* name; RgbColor color; };
    const ColorCase colors[] = {
        {"black", {0, 0, 0}}, {"red", {128, 0, 0}}, {"green", {0, 128, 0}},
        {"yellow", {128, 128, 0}}, {"blue", {0, 0, 128}}, {"magenta", {128, 0, 128}},
        {"brown", {128, 128, 0}},
        {"cyan", {0, 128, 128}}, {"light_gray", {192, 192, 192}},
        {"dark_gray", {128, 128, 128}}, {"light_red", {255, 0, 0}},
        {"light_green", {0, 255, 0}}, {"light_yellow", {255, 255, 0}},
        {"light_blue", {0, 0, 255}}, {"light_magenta", {255, 0, 255}},
        {"light_cyan", {0, 255, 255}}, {"white", {255, 255, 255}},
        {"#01aBfF", {1, 171, 255}},
    };
    for (const auto& color : colors) {
        CAPTURE(color.name);
        String appearance = "{symbol:'.',foreground:'";
        appearance += color.name;
        appearance += "',background:'#fFeE00'}";
        const auto catalog = Take(Load(View(Document(View(Terrain("floor", {}, View(appearance)))))));
        CHECK(Get(catalog, "floor").appearance.foreground == color.color);
        CHECK(Get(catalog, "floor").appearance.background == RgbColor{255, 238, 0});
    }
    for (const char* color : {"#123", "#1234567", "#gg0000", "red ", "RED", "unknown", ""}) {
        CAPTURE(color);
        String appearance = "{symbol:'.',foreground:'";
        appearance += color;
        appearance += "'}";
        Invalid(View(Document(View(Terrain("floor", {}, View(appearance))))));
    }
}

TEST_CASE("Content resolves cross-source forward appearance inheritance field by field", "[content]") {
    const String child = Document(View(Terrain("child", "looks_like:'middle'", "{foreground:'#123456'}")));
    const String middle = Document(View(Terrain("middle", "looks_like:'root'", "{symbol:'+'}")));
    const String root = Document(View(Terrain("root", {}, "{symbol:'#',foreground:'white',background:'blue'}")));
    const ContentSource sources[] = {{"child", View(child)}, {"middle", View(middle)}, {"root", View(root)}};
    const auto catalog = Take(LoadContent({sources, 3}));
    const auto& appearance = Get(catalog, "child").appearance;
    CHECK(appearance.symbol == "+");
    CHECK(appearance.foreground == RgbColor{18, 52, 86});
    CHECK(appearance.background == RgbColor{0, 0, 128});
    CHECK(Get(catalog, "middle").appearance.foreground == RgbColor{255, 255, 255});

    // A missing intermediate background must not mask the ancestor's explicit blue.
    const ContentSource reversed[] = {sources[2], sources[1], sources[0]};
    const auto reordered = Take(LoadContent({reversed, 3}));
    for (auto id : {"child", "middle", "root"}) {
        CHECK(catalog.FindTerrain(id) == reordered.FindTerrain(id));
        CHECK(Get(catalog, id).appearance == Get(reordered, id).appearance);
    }
    CHECK(catalog.FindTerrain("child") == TerrainId{1});
    CHECK(catalog.FindTerrain("middle") == TerrainId{2});
    CHECK(catalog.FindTerrain("root") == TerrainId{3});

    const String defaults = Document(View(Terrain("parent") + ',' +
        Terrain("child", "looks_like:'parent'", "{}")));
    CHECK(Get(Take(Load(View(defaults))), "child").appearance.background == RgbColor{});
}

TEST_CASE("Content inheritance never supplies gameplay properties or transitions", "[content]") {
    const String parent = R"({type:'terrain',id:'parent',name:'Parent',
        movement:{passable:false},blocks_sight:true,opens_to:'child',
        appearance:{symbol:'#',foreground:'red'}})";
    const auto catalog = Take(Load(View(Document(View(parent + ',' +
        Terrain("child", "looks_like:'parent'", {}))))));
    const auto& child = Get(catalog, "child");
    CHECK(child.name == "terrain");
    CHECK(child.passable);
    CHECK_FALSE(child.blocksSight);
    CHECK(child.opensTo == TerrainId{});
    CHECK(child.closesTo == TerrainId{});
    CHECK(child.appearance.symbol == "#");
    Invalid(View(Document(View(parent + R"(,{type:'terrain',id:'child',name:'Child',looks_like:'parent'})"))));
}

TEST_CASE("Content rejects missing and cyclic appearance references", "[content]") {
    Invalid(View(Document(View(Terrain("a", "looks_like:'missing'")))));
    Invalid(View(Document(View(Terrain("a", "looks_like:'a'")))));
    Invalid(View(Document(View(Terrain("a", "looks_like:'b'") + ',' + Terrain("b", "looks_like:'a'")))));
    Invalid(View(Document(View(Terrain("a", {}, "{}")))));
    Invalid(View(Document(View(Terrain("a", {}, "{symbol:'.'}")))));
    Invalid(View(Document(View(Terrain("a", {}, "{foreground:'red'}")))));
    Invalid(View(Document(View(Terrain("a", {}, {})))));
    // Complete local appearance does not excuse a bad looks_like edge.
    Invalid(View(Document(View(Terrain("a", "looks_like:'b'") + ',' +
        Terrain("b", "looks_like:'c'") + ',' + Terrain("c", "looks_like:'b'")))));
}

TEST_CASE("Content resolves door transitions without traversing their cycles", "[content]") {
    const String text = Document(View(Terrain("closed", "opens_to:'open'") + ',' +
                                     Terrain("open", "closes_to:'closed'")));
    const auto catalog = Take(Load(View(text)));
    CHECK(Get(catalog, "closed").opensTo == catalog.FindTerrain("open"));
    CHECK(Get(catalog, "open").closesTo == catalog.FindTerrain("closed"));
    CHECK(Get(catalog, "closed").closesTo == TerrainId{});
    CHECK(Get(catalog, "open").opensTo == TerrainId{});
    const String closedSource = Document(View(Terrain("closed", "opens_to:'open'")));
    const String openSource = Document(View(Terrain("open", "closes_to:'closed'")));
    const ContentSource sources[] = {{"closed", View(closedSource)}, {"open", View(openSource)}};
    const auto split = Take(LoadContent({sources, 2}));
    CHECK(Get(split, "closed").opensTo == split.FindTerrain("open"));
    CHECK(Get(split, "open").closesTo == split.FindTerrain("closed"));
    const auto self = Take(Load(View(Document(View(Terrain("self", "opens_to:'self'"))))));
    CHECK(Get(self, "self").opensTo == self.FindTerrain("self"));
    Invalid(View(Document(View(Terrain("a", "opens_to:'missing'")))));
    Invalid(View(Document(View(Terrain("a", "closes_to:'missing'")))));
    Invalid(View(Document(View(Terrain("a", "opens_to:'a',closes_to:'a'")))));
    Invalid(View(Document(View(Terrain("a", "opens_to:3")))));
    Invalid(View(Document(View(Terrain("a", "closes_to:''")))));
}

TEST_CASE("Content rejects unknown and duplicate schema keys at every object level", "[content]") {
    for (const char* text : {
        "{format_version:1,definitions:[],extra:0}",
        "{format_version:1,format_version:1,definitions:[]}",
        "{format_version:1,definitions:[],definitions:[]}",
        "[]", "{}", "{format_version:1,definitions:{}}",
        "{format_version:1,definitions:[null]}",
    }) { Invalid(text); }
    for (const char* extra : {"unknown:0", "id:'other'", "type:'terrain'", "name:'Other'",
                              "blocks_sight:true", "movement:{passable:false}", "appearance:{}"}) {
        CAPTURE(extra);
        Invalid(View(Document(View(Terrain("floor", extra)))));
    }
    for (const char* appearance : {"null", "[]", "{cost:1}",
        "{symbol:'.',symbol:'#',foreground:'red'}",
        "{symbol:'.',foreground:'red',foreground:'blue'}",
        "{symbol:'.',foreground:'red',background:'black',background:'black'}",
        "{symbol:'.',foreground:3}", "{symbol:'.',foreground:'red',background:null}"}) {
        CAPTURE(appearance);
        Invalid(View(Document(View(Terrain("floor", {}, appearance)))));
    }
    for (const char* movement : {"{passable:true,cost:100}", "{passable:true,passable:false}",
                                 "{}", "{passable:1}", "null"}) {
        String definition = "{type:'terrain',id:'a',name:'A',blocks_sight:false,movement:";
        definition += movement;
        definition += ",appearance:{symbol:'.',foreground:'red'}}";
        Invalid(View(Document(View(definition))));
    }
}

TEST_CASE("Content validates required values and format versions", "[content]") {
    for (const char* definition : {
        "{id:'a',name:'A',movement:{passable:true},blocks_sight:false}",
        "{type:'unknown_type',id:'a',name:'A',movement:{passable:true},blocks_sight:false}",
        "{type:'terrain',id:'a',movement:{passable:true},blocks_sight:false}",
        "{type:'terrain',id:'a',name:'',movement:{passable:true},blocks_sight:false}",
        "{type:'terrain',id:'a',name:'A\\u0000B',movement:{passable:true},blocks_sight:false}",
        "{type:'terrain',id:'a',name:3,movement:{passable:true},blocks_sight:false}",
        "{type:'terrain',id:'a',name:'A',blocks_sight:false}",
        "{type:'terrain',id:'a',name:'A',movement:{passable:true}}",
        "{type:'terrain',id:'a',name:'A',movement:{passable:true},blocks_sight:0}",
    }) { Invalid(View(Document(definition))); }
    for (const char* id : {"", "A", "_a", "0a", "a-b", "a b", "a\\u0000b", "é"}) {
        CAPTURE(id);
        Invalid(View(Document(View(Terrain(id)))));
    }
    for (const char* version : {"null", "'1'", "1.0", "-1"}) {
        String text = "{format_version:";
        text += version;
        text += ",definitions:[]}";
        Invalid(View(text));
    }
    const auto unsupported = Load("{format_version:2,definitions:[]}");
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error() == ContentError::UnsupportedVersion);
}

TEST_CASE("Content rejects duplicates across sources and does not return partial catalogs", "[content]") {
    const String first = Document(View(Terrain("a")));
    const String second = Document(View(Terrain("b") + ',' + Terrain("a")));
    const ContentSource sources[] = {{"first", View(first)}, {"second", View(second)}};
    Diagnostics diagnostics;
    const auto result = LoadContent({sources, 2}, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE(diagnostics.Frames().size() == 4);
    CHECK(diagnostics.Frames()[0].message == "duplicate terrain definition 'a'");
    REQUIRE(diagnostics.Frames()[0].location.has_value());
    CHECK(diagnostics.Frames()[0].location->file == "second");
    CHECK(diagnostics.Frames()[0].location->path == "definitions[1].id");
    REQUIRE(diagnostics.Frames()[1].location.has_value());
    CHECK(diagnostics.Frames()[1].message == "previous definition is here");
    CHECK(diagnostics.Frames()[1].location->file == "first");
    CHECK(diagnostics.Frames()[1].location->path == "definitions[0].id");
    CHECK(diagnostics.Frames()[2].message == "while loading terrain 'a'");
    CHECK(diagnostics.Frames()[3].message == "could not load content catalog");
    Invalid(View(Document(View(Terrain("a") + ',' + Terrain("a")))));
    const ContentSource malformed[] = {sources[0], {"bad", "{"}};
    CHECK_FALSE(LoadContent({malformed, 2}).has_value());
}

TEST_CASE("Content symbols are one valid non-control Unicode scalar", "[content]") {
    for (const char* symbol : {".", "é", "界", "😀", "\\u0301", " "}) {
        CAPTURE(symbol);
        String appearance = "{symbol:'";
        appearance += symbol;
        appearance += "',foreground:'white'}";
        CHECK(Load(View(Document(View(Terrain("a", {}, View(appearance)))))).has_value());
    }
    for (const char* symbol : {"", "ab", "e\\u0301", "\\n", "\\u0000", "\\u001f",
                               "\\u007f", "\\u0085", "\\u009f", "\\ud800"}) {
        CAPTURE(symbol);
        String appearance = "{symbol:'";
        appearance += symbol;
        appearance += "',foreground:'white'}";
        Invalid(View(Document(View(Terrain("a", {}, View(appearance))))));
    }
    const String invalidUtf8[] = {
        String("\xc0\xaf", 2), String("\xed\xa0\x80", 3), String("\xf4\x90\x80\x80", 4),
        String("\x80", 1), String("\xe2\x82", 2),
    };
    for (const auto& symbol : invalidUtf8) {
        const String appearance = String("{symbol:'") + symbol + "',foreground:'white'}";
        Invalid(View(Document(View(Terrain("a", {}, View(appearance))))));
    }
}

TEST_CASE("Content inheritance handles long forward chains iteratively", "[content]") {
    String definitions;
    constexpr std::size_t count = 2048;
    for (std::size_t i = 0; i < count; ++i) {
        char id[32], extra[64];
        std::snprintf(id, sizeof(id), "terrain_%04zu", i);
        if (i + 1 < count) { std::snprintf(extra, sizeof(extra), "looks_like:'terrain_%04zu'", i + 1); }
        if (i != 0) { definitions += ','; }
        definitions += i + 1 < count ? Terrain(id, extra, {}) : Terrain(id);
    }
    const auto catalog = Take(Load(View(Document(View(definitions)))));
    CHECK(catalog.TerrainCount() == count);
    CHECK(Get(catalog, "terrain_0000").appearance.symbol == ".");
    CHECK(Get(catalog, "terrain_0000").appearance.background == RgbColor{});
}

TEST_CASE("Content diagnostics retain structured paths and syntax locations", "[content]") {
    Diagnostics diagnostics;
    diagnostics.Add("existing diagnostic");
    const String invalid = Document(View(Terrain("a", {}, "{symbol:'ab',foreground:'red'}")));
    REQUIRE_FALSE(Load(View(invalid), &diagnostics).has_value());
    REQUIRE(diagnostics.Frames().size() == 4);
    CHECK(diagnostics.Frames()[0].message == "existing diagnostic");
    CHECK(diagnostics.Frames()[1].location->path == "definitions[0].appearance.symbol");
    CHECK(diagnostics.Frames()[2].message == "while loading terrain 'a'");
    CHECK(diagnostics.Frames()[3].message == "could not load content source");
    CHECK(diagnostics.Frames()[3].location->file == "test.json5");
    diagnostics.Clear();
    REQUIRE_FALSE(Load("{\n format_version:1,\n definitions:[}\n", &diagnostics).has_value());
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames()[0].location->line.has_value());
    CHECK(diagnostics.Frames()[0].location->column.has_value());
}

TEST_CASE("Content dispatch reports unsupported types at the type field", "[content]") {
    Diagnostics diagnostics;
    const String text = Document("{type:'unknown_type',id:'rock'}");
    const auto result = Load(View(text), &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE(diagnostics.Frames().size() == 3);
    CHECK(diagnostics.Frames()[0].message == "unsupported definition type 'unknown_type' (expected terrain, building_pattern, or item)");
    REQUIRE(diagnostics.Frames()[0].location.has_value());
    CHECK(diagnostics.Frames()[0].location->path == "definitions[0].type");
    CHECK(diagnostics.Frames()[1].message == "while loading content definition");
    CHECK(diagnostics.Frames()[2].message == "could not load content source");
    Invalid(View(text));
}

TEST_CASE("Content reference diagnostics identify the target and referring terrain", "[content]") {
    for (const char* field : {"looks_like", "opens_to", "closes_to"}) {
        CAPTURE(field);
        String extra = field;
        extra += ":'missing_target'";
        const String text = Document(View(Terrain("referrer", View(extra))));
        const ContentSource source{"references.json5", View(text)};
        Diagnostics diagnostics;
        const auto result = LoadContent({&source, 1}, &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE(diagnostics.Frames().size() == 3);
        CHECK(diagnostics.Frames()[0].message ==
              "unknown terrain reference 'missing_target' from terrain 'referrer'");
        REQUIRE(diagnostics.Frames()[0].location.has_value());
        CHECK(diagnostics.Frames()[0].location->file == "references.json5");
        CHECK(diagnostics.Frames()[0].location->path == String("definitions[0].") + field);
        CHECK(diagnostics.Frames()[1].message == "while loading terrain 'referrer'");
        CHECK(diagnostics.Frames()[2].message == "could not load content catalog");
        const String formatted = FormatDiagnostics(diagnostics);
        CHECK(formatted.find("could not load content catalog") < formatted.find("while loading terrain"));
        CHECK(formatted.find("while loading terrain") < formatted.find("unknown terrain reference"));
        Invalid(View(text));
    }
}

TEST_CASE("Content cycle diagnostics identify an involved terrain", "[content]") {
    const String text = Document(View(Terrain("a", "looks_like:'b'") + ',' +
                                      Terrain("b", "looks_like:'a'")));
    Diagnostics diagnostics;
    REQUIRE_FALSE(Load(View(text), &diagnostics).has_value());
    REQUIRE(diagnostics.Frames().size() == 3);
    CHECK(diagnostics.Frames()[0].message == "appearance inheritance cycle involving terrain 'a'");
    REQUIRE(diagnostics.Frames()[0].location.has_value());
    CHECK(diagnostics.Frames()[0].location->path == "definitions[0].looks_like");
    CHECK(diagnostics.Frames()[1].message == "while loading terrain 'a'");
    CHECK(diagnostics.Frames()[2].message == "could not load content catalog");
}

TEST_CASE("Bundled terrain content loads from stdio with door transitions", "[content]") {
    String path = __FILE__;
    const auto separator = path.find_last_of("/\\");
    REQUIRE(separator != String::npos);
    path.resize(separator);
    path += "/../data/terrain.json5";
    const auto catalog = Take(LoadContentFile(path.c_str()));
    CHECK(catalog.TerrainCount() == 10);
    CHECK(catalog.FindTerrain("air") == TerrainId{1});
    CHECK(catalog.FindTerrain("closed_door") == TerrainId{2});
    CHECK(catalog.FindTerrain("floor") == TerrainId{3});
    CHECK(catalog.FindTerrain("grass") == TerrainId{4});
    CHECK(catalog.FindTerrain("open_door") == TerrainId{5});
    CHECK(catalog.FindTerrain("road") == TerrainId{6});
    CHECK(catalog.FindTerrain("rubble") == TerrainId{7});
    CHECK(catalog.FindTerrain("stairs") == TerrainId{8});
    CHECK(catalog.FindTerrain("tree") == TerrainId{9});
    CHECK(catalog.FindTerrain("wall") == TerrainId{10});
    CHECK(Get(catalog, "air").openSpace);
    CHECK(Get(catalog, "air").passable);
    CHECK_FALSE(Get(catalog, "air").blocksSight);
    CHECK(Get(catalog, "stairs").stairs);
    CHECK(Get(catalog, "stairs").passable);
    CHECK_FALSE(Get(catalog, "stairs").blocksSight);
    CHECK(Get(catalog, "floor").appearance.symbol == ".");
    CHECK(Get(catalog, "wall").appearance.symbol == "#");
    CHECK(Get(catalog, "closed_door").appearance.symbol == "+");
    CHECK(Get(catalog, "open_door").appearance.symbol == "/");
    CHECK(Get(catalog, "floor").passable);
    CHECK(Get(catalog, "open_door").passable);
    CHECK_FALSE(Get(catalog, "wall").passable);
    CHECK_FALSE(Get(catalog, "closed_door").passable);
    CHECK(Get(catalog, "wall").blocksSight);
    CHECK(Get(catalog, "closed_door").blocksSight);
    CHECK_FALSE(Get(catalog, "floor").blocksSight);
    CHECK_FALSE(Get(catalog, "open_door").blocksSight);
    CHECK(Get(catalog, "closed_door").opensTo == catalog.FindTerrain("open_door"));
    CHECK(Get(catalog, "open_door").closesTo == catalog.FindTerrain("closed_door"));
    for (const char* id : {"floor", "wall", "closed_door", "open_door"}) {
        CHECK(Get(catalog, id).appearance.foreground == RgbColor{192, 192, 192});
        CHECK(Get(catalog, id).appearance.background == RgbColor{});
    }
    const eastl::string_view paths[] = {View(path), View(path)};
    const auto duplicateFiles = LoadContentFiles({paths, 2});
    REQUIRE_FALSE(duplicateFiles.has_value());
    CHECK(duplicateFiles.error() == ContentError::InvalidData);
    const auto missing = LoadContentFile("content_tests_nonexistent_directory/missing.json5");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == ContentError::Io);
    CHECK(LoadContentFile(nullptr).error() == ContentError::Io);
    const eastl::string_view invalidPath("bad\0path", 8);
    CHECK(LoadContentFiles({&invalidPath, 1}).error() == ContentError::Io);
}

}
