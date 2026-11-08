#include "client/localization.h"
#include "settings_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Bundled English catalog covers outcomes actions and settings and supports fallback", "[localization]") {
    Localization localization;
    Diagnostics diagnostics;
    REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en", &diagnostics));
    for (unsigned i = 0; i <= static_cast<unsigned>(Outcome::queued); ++i) {
        const auto outcome = static_cast<Outcome>(i);
        CHECK(localization.OutcomeText(outcome) == OutcomeMessage(outcome));
    }
    for (const auto& binding : DefaultKeymap()) {
        String key = "action."; const auto name = ActionName(binding.action); key.append(name.data(), name.size());
        CHECK(localization.Text({key.data(), key.size()}) != String("[") + key + "]");
    }
    CHECK(localization.Text("missing") == "[missing]");
    CHECK(localization.Text("missing", "custom content") == "custom content");
    CHECK(localization.Format("inventory.inside", {{"count", "2"}}, 2) == "2 inside");
    CHECK(localization.Format("game.position", {{"tick", "1"}, {"x", "-2"}, {"z", "3"}}) == "tick=1 position=-2,3");
    REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "absent"));
    CHECK(localization.Text("game.title") == "CataclysmX");
}

TEST_CASE("Translated messages reorder named values preserve Unicode and validate plural placeholders", "[localization]") {
    auto english = ParseLanguageCatalog(R"({format_version:1,messages:{
        greeting:'Hello {name}', count:{one:'{count} item',other:'{count} items'}, escaped:'{{{value}}}'}})");
    auto translated = ParseLanguageCatalog(R"({format_version:1,messages:{greeting:'{name}, Привет!'}})");
    REQUIRE(english); REQUIRE(translated);
    Localization localization;
    REQUIRE(localization.SetCatalogs(*english, *translated));
    CHECK(localization.Format("greeting", {{"name", "猫"}}) == "猫, Привет!");
    CHECK(localization.Format("count", {{"count", "1"}}, 1) == "1 item");
    CHECK(localization.Format("count", {{"count", "3"}}, 3) == "3 items");
    CHECK(localization.Format("escaped", {{"value", "yes"}}) == "{yes}");
    CHECK(localization.Format("greeting", {}) == "[greeting]");
    auto wrong = ParseLanguageCatalog("{format_version:1,messages:{greeting:'Hello {wrong}'}}");
    REQUIRE(wrong);
    CHECK_FALSE(localization.SetCatalogs(*english, *wrong));
    CHECK(localization.Format("greeting", {{"name", "猫"}}) == "猫, Привет!");
    CHECK_FALSE(ParseLanguageCatalog("{format_version:1,messages:{greeting:'a',greeting:'b'}}"));
    CHECK_FALSE(ParseLanguageCatalog("{format_version:1,messages:{greeting:'{broken'}}"));
    CHECK_FALSE(ParseLanguageCatalog("{format_version:1,messages:{count:{one:'{a}',other:'{b}'}}}"));
    const char invalidUtf8[] = "{\"format_version\":1,\"messages\":{\"bad\":\"\xff\"}}";
    CHECK_FALSE(ParseLanguageCatalog(invalidUtf8));
}

TEST_CASE("Language loads are staged and custom terrain names remain a fallback", "[localization]") {
    SettingsDirectory files;
    files.Write("en.json5", "{format_version:1,messages:{'game.title':'English','terrain.grass.name':'Grass'}}");
    files.Write("xx.json5", "{format_version:1,messages:{'game.title':'Translated'}}");
    Localization localization;
    REQUIRE(localization.Load(files.root.string().c_str(), "xx"));
    CHECK(localization.Text("game.title") == "Translated");
    files.Write("xx.json5", "invalid");
    CHECK_FALSE(localization.Load(files.root.string().c_str(), "xx"));
    CHECK(localization.Text("game.title") == "Translated");
    TerrainDefinition definition;
    definition.id = "custom"; definition.name = "Custom Name";
    CHECK(localization.TerrainName(definition) == "Custom Name");
    definition.id = "grass";
    CHECK(localization.TerrainName(definition) == "Grass");
    CHECK_FALSE(localization.Load(files.root.string().c_str(), "../xx"));
}

}
