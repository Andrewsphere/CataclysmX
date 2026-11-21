#include "sdl/screens.h"
#include "settings_fixture.h"

#include <catch2/catch_test_macros.hpp>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

TEST_CASE("SDL guest cannot edit or reset host settings", "[sdl][settings]") {
    SettingsDirectory files;
    SettingsStore store(String(files.File("user").c_str()));
    SettingsOverrides overrides;
    overrides.seed = 123; overrides.activeRadius = 2; overrides.residentRadius = 7;
    overrides.ticksPerSecond = 12; overrides.rateLimited = false;
    overrides.pauseWhenIdle = false; overrides.autosaveInterval = 300;
    REQUIRE(store.SetOverrides(SettingsScope::Global, overrides, {}, nullptr));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    Settings runtime = store.Effective(); runtime.pacing.ticksPerSecond = 19;
    for (int section = 1; section <= 3; ++section) {
        OptionsUi guest; guest.Open(store, runtime, true);
        for (int i = 0; i < section; ++i) { guest.Key({"Down", true}, store, loc, CATACLYSMX_LOC_DIRECTORY); }
        const int count = section == 1 ? 5 : section == 2 ? 3 : 1;
        for (int i = 0; i < count; ++i) {
            ScreenFrame frame; guest.Present(store, loc, frame);
            REQUIRE(frame.rows[frame.selected].unavailable);
            REQUIRE_FALSE(frame.settingsDetails.empty());
            CHECK(frame.settingsDetails.back().text == "Controlled by host");
            if (section == 2 && i == 0) { CHECK(frame.rows[frame.selected].value == "19"); }
            for (const char* name : {"Enter", "Left", "Right", "Backspace"}) {
                guest.Key({name}, store, loc, CATACLYSMX_LOC_DIRECTORY);
                CHECK_FALSE(guest.EditingText());
            }
            guest.Key({"Down"}, store, loc, CATACLYSMX_LOC_DIRECTORY);
        }
        auto candidate = guest.Prepare(store, nullptr); REQUIRE(candidate);
        CHECK(candidate->Overrides(SettingsScope::Global) == overrides);
    }
}

TEST_CASE("SDL guests can edit preferences and bindings while local worlds remain editable", "[sdl][settings]") {
    SettingsDirectory files;
    SettingsStore store(String(files.File("user").c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    OptionsUi ui; ui.Open(store, store.Effective(), true);
    const auto key = [&](const char* name) { return ui.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
    key("Down"); key("Enter"); REQUIRE(ui.EditingText()); ui.Text("12"); key("Enter");
    key("Tab"); key("Enter");
    ui.Key({"Left", false, true}, store, loc, CATACLYSMX_LOC_DIRECTORY);
    auto candidate = ui.Prepare(store, nullptr); REQUIRE(candidate);
    CHECK(candidate->Effective().viewRadius == 12);
    CHECK(ResolveAction(candidate->Keys(), InputContext::Gameplay, {"Left", false, true}) == InputAction::West);

    OptionsUi local; local.Open(store, store.Effective());
    const auto localKey = [&](const char* name) { local.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
    local.Key({"Down", true}, store, loc, CATACLYSMX_LOC_DIRECTORY); localKey("Enter");
    REQUIRE(local.EditingText()); local.Text("123"); localKey("Enter");
    candidate = local.Prepare(store, nullptr); REQUIRE(candidate);
    REQUIRE(candidate->Overrides(SettingsScope::Global).seed);
    CHECK(*candidate->Overrides(SettingsScope::Global).seed == 123);
}

TEST_CASE("SDL settings have no Multiplayer launcher and own F9 during key capture", "[sdl][settings]") {
    SettingsDirectory files;
    SettingsStore store(String(files.File("user").c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    OptionsUi ui; ui.Open(store, store.Effective());
    ScreenFrame frame; ui.Present(store, loc, frame);
    CHECK(frame.footer.find("Multiplayer") == String::npos);
    for (const auto& row : frame.rows) { CHECK(row.text.find("Multiplayer") == String::npos); }
    const auto key = [&](const char* name) { return ui.Key({name}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
    CHECK(key("F9") == OptionsEvent::None);
    key("Tab"); key("Enter");
    REQUIRE(ui.Capturing());
    CHECK(key("F9") == OptionsEvent::None);
    CHECK(ui.Capturing());
    ui.Present(store, loc, frame);
    CHECK(frame.status.find("Multiplayer") != String::npos);
    key("F8");
    CHECK_FALSE(ui.Capturing());
    auto candidate = ui.Prepare(store, nullptr); REQUIRE(candidate);
    CHECK(ResolveAction(candidate->Keys(), InputContext::Gameplay, {"F8"}) == InputAction::West);
}

TEST_CASE("SDL text size edits are global and staged for both local and guest players", "[sdl][settings][text-size]") {
    SettingsDirectory files;
    SettingsStore store(String(files.File("user").c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    for (bool remote : {false, true}) {
        OptionsUi ui; ui.Open(store, store.Effective(), remote);
        const auto key = [&](const char* name) { return ui.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
        for (int i = 0; i < 4; ++i) { key("Down"); }
        ScreenFrame frame; ui.Present(store, loc, frame);
        CHECK(frame.rows[frame.selected].text.find("Text size") != String::npos);
        CHECK_FALSE(frame.rows[frame.selected].unavailable);
        key("Enter"); REQUIRE(ui.EditingText()); ui.Text("48"); key("Escape");
        auto candidate = ui.Prepare(store, nullptr); REQUIRE(candidate); CHECK(candidate->Effective().textSize == 14);
        key("Enter"); ui.Text("49"); key("Enter");
        CHECK(ui.EditingText()); key("Escape");
        key("Enter"); ui.Text("24"); key("Enter");
        CHECK(store.Effective().textSize == 14);
        candidate = ui.Prepare(store, nullptr); REQUIRE(candidate); CHECK(candidate->Effective().textSize == 24);
        key("Enter"); ui.Text("32"); ui.Present(store, loc, frame);
        CHECK(frame.rows[frame.selected].value.find("32 _") != String::npos);
        key("Escape");
        key("Backspace");
        candidate = ui.Prepare(store, nullptr); REQUIRE(candidate); CHECK(candidate->Effective().textSize == 14);
        key("g");
        ui.Present(store, loc, frame);
        CHECK(ui.Scope() == SettingsScope::World);
        for (const auto& row : frame.rows) { CHECK(row.text.find("Text size") == String::npos); }
    }
}
