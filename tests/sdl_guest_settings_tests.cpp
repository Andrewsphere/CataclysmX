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
    for (int category = 1; category <= 3; ++category) {
        OptionsUi ui; ui.Open(store, runtime, true);
        const auto key = [&](const char* name) { return ui.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
        key("BackTab");
        for (int i = 0; i < category; ++i) { key("Right"); }
        key("Enter");
        const int count = category == 1 ? 5 : category == 2 ? 3 : 1;
        for (int i = 0; i < count; ++i) {
            ScreenFrame frame; ui.Present(store, loc, frame);
            REQUIRE(frame.rows[i].unavailable);
            CHECK(frame.details.back() == "Controlled by host");
            if (category == 2 && i == 0) { CHECK(frame.rows[i].text.find("19") != String::npos); }
            key("Enter"); CHECK_FALSE(ui.EditingText()); key("Backspace");
            key("Down");
        }
        SettingsStore candidate = store; REQUIRE(ui.Prepare(store, candidate, nullptr));
        CHECK(candidate.Overrides(SettingsScope::Global) == overrides);
        ui.Applied(store, runtime, true);
        key("Enter"); CHECK_FALSE(ui.EditingText());
    }
}

TEST_CASE("SDL guests can edit preferences and bindings while local worlds remain editable", "[sdl][settings]") {
    SettingsDirectory files;
    SettingsStore store(String(files.File("user").c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    OptionsUi ui; ui.Open(store, store.Effective(), true);
    const auto key = [&](const char* name) { return ui.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
    key("Down"); key("Enter"); REQUIRE(ui.EditingText()); ui.Text("12"); key("Enter");
    key("BackTab"); key("BackTab"); key("Right"); key("Enter"); key("Enter");
    ui.Key({"Left", false, true}, store, loc, CATACLYSMX_LOC_DIRECTORY);
    SettingsStore candidate = store; REQUIRE(ui.Prepare(store, candidate, nullptr));
    CHECK(candidate.Effective().viewRadius == 12);
    CHECK(ResolveAction(candidate.Keys(), InputContext::Gameplay, {"Left", false, true}) == InputAction::West);

    OptionsUi local; local.Open(store, store.Effective());
    const auto localKey = [&](const char* name) { local.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
    localKey("BackTab"); localKey("Right"); localKey("Enter"); localKey("Enter");
    REQUIRE(local.EditingText()); local.Text("123"); localKey("Enter");
    REQUIRE(local.Prepare(store, candidate, nullptr));
    REQUIRE(candidate.Overrides(SettingsScope::Global).seed);
    CHECK(*candidate.Overrides(SettingsScope::Global).seed == 123);
}

TEST_CASE("SDL options opens multiplayer settings for local and guest players", "[sdl][settings]") {
    SettingsDirectory files;
    SettingsStore store(String(files.File("user").c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    for (bool remote : {false, true}) {
        OptionsUi ui; ui.Open(store, store.Effective(), remote);
        const auto key = [&](const char* name) { return ui.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
        ScreenFrame frame; ui.Present(store, loc, frame);
        REQUIRE(frame.selected == 0);
        REQUIRE(frame.rows.size() == 5);
        CHECK(frame.rows[0].text.find("Language") != String::npos);
        for (int i = 0; i < 4; ++i) { key("Down"); }
        ui.Present(store, loc, frame);
        CHECK(frame.selected == 4);
        CHECK(frame.rows[4].text == "Multiplayer settings...");
        REQUIRE_FALSE(frame.details.empty());
        CHECK(frame.details[0].find("F9") != String::npos);
        CHECK(frame.details[0].find("host") != String::npos);
        CHECK(frame.details[0].find("join") != String::npos);
        CHECK(frame.footer.find("F9 Multiplayer") != String::npos);
        CHECK(key("Backspace") == OptionsEvent::None);
        SettingsStore candidate = store; REQUIRE(ui.Prepare(store, candidate, nullptr));
        CHECK(candidate.Overrides(SettingsScope::Global) == store.Overrides(SettingsScope::Global));
        CHECK(key("Enter") == OptionsEvent::Multiplayer);
        key("Down"); ui.Present(store, loc, frame); CHECK(frame.selected == 0);
        key("Down"); key("Enter"); REQUIRE(ui.EditingText()); ui.Text("12"); key("Enter");
        REQUIRE(ui.Prepare(store, candidate, nullptr));
        CHECK(candidate.Effective().viewRadius == 12);
        key("Up"); key("Up"); key("BackTab"); key("Right");
        ui.Present(store, loc, frame); CHECK(frame.selected == 0);
    }
}
