#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

ContentResult<ContentCatalog> Load(eastl::string_view definitions, Diagnostics* diagnostics = nullptr) {
    String text = "{format_version:1,definitions:[";
    text.append(definitions.data(), definitions.size());
    text += "]}";
    const ContentSource source{"light.json5", View(text)};
    return LoadContent({&source, 1}, diagnostics);
}

String Terrain(eastl::string_view extra) {
    String text = "{type:'terrain',id:'lamp',name:'lamp',movement:{passable:true},"
                  "blocks_sight:false,appearance:{symbol:'.',foreground:'white'},";
    text.append(extra.data(), extra.size());
    text += '}';
    return text;
}

TEST_CASE("Terrain content stores independent light intensity and range", "[content][lighting-content]") {
    const auto catalog = Load(R"(
        {type:'terrain',id:'bright',name:'bright',movement:{passable:true},blocks_sight:false,
         appearance:{symbol:'*',foreground:'white'},light:{intensity:255,range:1}},
        {type:'terrain',id:'wide',name:'wide',movement:{passable:true},blocks_sight:false,
         appearance:{symbol:'.',foreground:'black'},light:{intensity:1,range:12}},
        {type:'terrain',id:'off',name:'off',movement:{passable:true},blocks_sight:false,
         looks_like:'bright',light:{intensity:0,range:0}},
        {type:'terrain',id:'child',name:'child',movement:{passable:true},blocks_sight:false,
         looks_like:'bright'},
        {type:'terrain',id:'own',name:'own',movement:{passable:true},blocks_sight:false,
         looks_like:'bright',light:{intensity:4,range:5}}
    )");
    REQUIRE(catalog.has_value());
    const auto& bright = catalog->GetTerrain(catalog->FindTerrain("bright"));
    const auto& wide = catalog->GetTerrain(catalog->FindTerrain("wide"));
    const auto& off = catalog->GetTerrain(catalog->FindTerrain("off"));
    const auto& child = catalog->GetTerrain(catalog->FindTerrain("child"));
    const auto& own = catalog->GetTerrain(catalog->FindTerrain("own"));
    CHECK(bright.light == LightEmission{255, 1});
    CHECK(wide.light == LightEmission{1, 12});
    CHECK(off.light == LightEmission{});
    CHECK(child.light == LightEmission{});
    CHECK(own.light == LightEmission{4, 5});
    CHECK(child.appearance.symbol == bright.appearance.symbol);
    CHECK(child.appearance.foreground == bright.appearance.foreground);
}

TEST_CASE("Terrain light rejects invalid fields with source paths", "[content][lighting-content]") {
    struct InvalidCase { const char* extra; const char* path; };
    const InvalidCase cases[] = {
        {"light:null", "definitions[0].light"},
        {"light:{}", "definitions[0].light.intensity"},
        {"light:{intensity:1}", "definitions[0].light.range"},
        {"light:{range:1}", "definitions[0].light.intensity"},
        {"light:{intensity:-1,range:1}", "definitions[0].light.intensity"},
        {"light:{intensity:256,range:1}", "definitions[0].light.intensity"},
        {"light:{intensity:18446744073709551616,range:1}", "definitions[0].light.intensity"},
        {"light:{intensity:true,range:1}", "definitions[0].light.intensity"},
        {"light:{intensity:1.0,range:1}", "definitions[0].light.intensity"},
        {"light:{intensity:1,range:-1}", "definitions[0].light.range"},
        {"light:{intensity:1,range:13}", "definitions[0].light.range"},
        {"light:{intensity:1,range:false}", "definitions[0].light.range"},
        {"light:{intensity:1,range:1.5}", "definitions[0].light.range"},
        {"light:{intensity:0,range:1}", "definitions[0].light"},
        {"light:{intensity:1,range:0}", "definitions[0].light"},
        {"light:{intensity:1,range:1,extra:0}", "definitions[0].light.extra"},
        {"light:{intensity:1,intensity:1,range:1}", "definitions[0].light.intensity"},
        {"light:{intensity:1,range:1,range:1}", "definitions[0].light.range"},
        {"light:{intensity:1,range:1},light:{intensity:1,range:1}", "definitions[0].light"},
    };
    for (const auto& invalid : cases) {
        CAPTURE(invalid.extra);
        const auto terrain = Terrain(invalid.extra);
        Diagnostics diagnostics;
        const auto result = Load(View(terrain), &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE_FALSE(diagnostics.Empty());
        REQUIRE(diagnostics.Frames().front().location.has_value());
        CHECK(diagnostics.Frames().front().location->file == "light.json5");
        CHECK(diagnostics.Frames().front().location->path == invalid.path);
        const auto withoutDiagnostics = Load(View(terrain));
        REQUIRE_FALSE(withoutDiagnostics.has_value());
        CHECK(withoutDiagnostics.error() == ContentError::InvalidData);
    }
}
}
