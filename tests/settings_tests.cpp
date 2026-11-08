#include "client/settings.h"
#include "core/save_database.h"
#include "content_fixture.h"
#include "settings_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {
using namespace cataclysmx;

TEST_CASE("World files contain only explicit overrides and inherit subsequent global changes", "[settings]") {
    SettingsDirectory files;
    SettingsStore store(String(files.root.string().c_str()), String(files.File("world").c_str()));
    REQUIRE(store.Load());
    CHECK(store.Source(SettingsField::ViewRadius) == SettingsSource::BuiltIn);
    SettingsOverrides global;
    global.viewRadius = 8; global.autosaveInterval = 1000;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    REQUIRE(store.Save(SettingsScope::Global));
    SettingsOverrides world;
    world.viewRadius = 10;
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    CHECK(store.Source(SettingsField::ViewRadius) == SettingsSource::World);
    CHECK(store.Source(SettingsField::AutosaveInterval) == SettingsSource::Global);
    REQUIRE(store.Save(SettingsScope::World));
    const auto text = files.Read("world/settings.json5");
    CHECK(text.find("view_radius") != std::string::npos);
    CHECK(text.find("autosave_interval") == std::string::npos);
    CHECK(text.find("language") == std::string::npos);

    global.viewRadius = 6; global.autosaveInterval = 500;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    REQUIRE(store.Save(SettingsScope::Global));
    SettingsStore other(String(files.root.string().c_str()), String(files.File("other-world").c_str()));
    REQUIRE(other.Load());
    CHECK(other.Effective().viewRadius == 6);
    REQUIRE(store.Load());
    CHECK(store.Effective().viewRadius == 10);
    CHECK(store.Effective().autosaveInterval == 500);
    CHECK(store.Overrides(SettingsScope::World) == world);

    global.viewRadius = 10; // Matching the parent never erases an explicit override.
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    REQUIRE(store.Save(SettingsScope::World));
    global.viewRadius = 4;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    CHECK(store.Effective().viewRadius == 10);
    ResetOverride(world, SettingsField::ViewRadius);
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    CHECK(store.Effective().viewRadius == 4);
    REQUIRE(store.Save(SettingsScope::World));
    CHECK(files.Read("world/settings.json5").find("view_radius") == std::string::npos);
}

TEST_CASE("Settings IO preserves integer precision and explicit false and zero values", "[settings]") {
    SettingsDirectory files;
    SettingsOverrides overrides;
    overrides.autosaveInterval = std::numeric_limits<std::uint64_t>::max();
    overrides.seed = std::numeric_limits<std::uint32_t>::max();
    overrides.viewRadius = 0; overrides.rateLimited = false; overrides.pauseWhenIdle = false;
    const auto path = files.File("with spaces/settings.json5");
    REQUIRE(SaveSettingsFile(path.c_str(), overrides));
    const auto loaded = LoadSettingsFile(path.c_str());
    REQUIRE(loaded);
    CHECK(*loaded == overrides);
    CHECK(ParseSettings("{format_version:1, settings:{resident_radius:0}}"));
    CHECK(ParseSettings("{format_version:1, settings:{active_radius:8}}"));
}

TEST_CASE("Invalid configuration never replaces valid state or overwrites a malformed file", "[settings]") {
    SettingsDirectory files;
    SettingsStore store(String(files.root.string().c_str()));
    SettingsOverrides overrides; overrides.viewRadius = 3;
    REQUIRE(store.SetOverrides(SettingsScope::Global, overrides, {}));
    REQUIRE(store.Save(SettingsScope::Global));
    const auto before = store.Effective();
    files.Write("config/settings.json5", "{broken}");
    Diagnostics diagnostics;
    REQUIRE_FALSE(store.Load(&diagnostics));
    CHECK_FALSE(diagnostics.Empty());
    CHECK(store.Effective() == before);
    REQUIRE_FALSE(store.Save(SettingsScope::Global));
    CHECK(files.Read("config/settings.json5") == "{broken}");
    for (const char* text : {
        "{format_version:1,settings:{view_radius:13}}",
        "{format_version:1,settings:{view_radius:1,view_radius:2}}",
        "{format_version:1,settings:{unknown:1}}",
        "{format_version:1,settings:{language:'../escape'}}",
        "{format_version:1,settings:{rate_limited:0}}",
        "{format_version:1,settings:{seed:-1}}",
        "{format_version:1,settings:{autosave_interval:1.5}}",
        "{format_version:1,settings:{view_radius:null}}"}) { CHECK_FALSE(ParseSettings(text)); }
    const auto future = ParseSettings("{format_version:2,settings:{}}");
    REQUIRE_FALSE(future); CHECK(future.error() == SettingsError::UnsupportedVersion);
}

TEST_CASE("Settings validate the resolved layer and apply without advancing simulation or changing seed", "[settings][server]") {
    SettingsStore store("unused");
    SettingsOverrides global; global.activeRadius = 0; global.residentRadius = 0;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    auto world = ParseSettings("{format_version:1,settings:{resident_radius:0}}");
    REQUIRE(world);
    REQUIRE(store.SetOverrides(SettingsScope::World, *world, {}));
    Server server(TestContent(), 123);
    const auto tick = server.GetTick();
    auto settings = store.Effective();
    settings.seed = 987; settings.autosaveInterval = 7; settings.pacing.rateLimited = true;
    REQUIRE(ApplySettings(server, settings));
    CHECK(server.GetTick() == tick);
    CHECK(server.GetWorld().GetSeed() == 123);
    CHECK(server.GetAutosaveInterval() == 7);
    CHECK(server.GetPacing() == settings.pacing);
    const auto chunks = server.GetWorld().LoadedChunks();
    settings.residentRadius = -1; settings.autosaveInterval = 77;
    CHECK_FALSE(ApplySettings(server, settings));
    CHECK(server.GetAutosaveInterval() == 7);
    CHECK(server.GetWorld().LoadedChunks() == chunks);
    CHECK(server.GetTick() == tick);
}

TEST_CASE("Autosave settings use turns and checkpoint at the 100 tick boundary", "[settings][save][autosave]") {
    SettingsDirectory files;
    const auto overrides = ParseSettings("{format_version:1,settings:{autosave_interval:1}}");
    REQUIRE(overrides);
    Server server(TestContent());
    REQUIRE(ApplySettings(server, ResolveSettings({}, *overrides)));
    const auto path = files.File("world.sqlite");
    REQUIRE(server.CreateSave(path.c_str()));
    const auto checkpointTick = [&] {
        SaveDatabase database(TestContent());
        const auto snapshot = database.Open(path.c_str());
        REQUIRE(snapshot);
        CHECK(snapshot->session.autosaveInterval == 1);
        return snapshot->session.tick;
    };
    REQUIRE(checkpointTick() == 0);

    Command wait;
    wait.waitCost = 99;
    const auto before = server.Send(wait);
    REQUIRE(before.advanced);
    CHECK_FALSE(before.storageError);
    REQUIRE(before.tick == 99);
    CHECK(checkpointTick() == 0);

    wait.waitCost = 1;
    const auto boundary = server.Send(wait);
    REQUIRE(boundary.advanced);
    CHECK_FALSE(boundary.storageError);
    REQUIRE(boundary.tick == 100);
    CHECK(checkpointTick() == 100);
}

TEST_CASE("Multiple saves use one world configuration and current globals override checkpoint preferences", "[settings][save]") {
    SettingsDirectory files;
    const auto world = files.File("world");
    SettingsStore store(String(files.root.string().c_str()), String(world.c_str()));
    SettingsOverrides global; global.autosaveInterval = 111;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    REQUIRE(store.Save(SettingsScope::Global));
    SettingsOverrides overrides; overrides.viewRadius = 2;
    REQUIRE(store.SetOverrides(SettingsScope::World, overrides, {}));
    REQUIRE(store.Save(SettingsScope::World));
    for (const auto file : {"world/a.sqlite", "world/b.sqlite"}) {
        Server original(TestContent(), 321);
        original.SetAutosaveInterval(9);
        REQUIRE(original.CreateSave(files.File(file).c_str()));
    }
    global.autosaveInterval = 222; global.seed = 999;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    REQUIRE(store.Save(SettingsScope::Global));
    for (const auto file : {"world/a.sqlite", "world/b.sqlite"}) {
        auto loaded = Server::LoadSave(TestContent(), files.File(file).c_str());
        REQUIRE(loaded);
        REQUIRE(store.Load());
        REQUIRE(ApplySettings(**loaded, store.Effective()));
        CHECK((*loaded)->GetWorld().GetSeed() == 321);
        CHECK((*loaded)->GetAutosaveInterval() == 222);
        CHECK(store.Effective().viewRadius == 2);
        CHECK_FALSE(std::filesystem::exists(files.File("world/a.sqlite.settings.json5")));
    }
}

TEST_CASE("Keybindings inherit per action and distinguish both ordinary and Ctrl letter case", "[settings][input]") {
    Keymap global{
        {InputContext::Gameplay, InputAction::Wait, {{"t", false}}},
        {InputContext::Gameplay, InputAction::PickUp, {{"T", false}}},
        {InputContext::Gameplay, InputAction::Drop, {{"t", true}}},
        {InputContext::Gameplay, InputAction::Save, {{"T", true}}}
    };
    auto resolved = ResolveKeymap(global, {});
    REQUIRE(ValidateKeymap(resolved));
    CHECK(ResolveAction(resolved, InputContext::Gameplay, {"t", false}) == InputAction::Wait);
    CHECK(ResolveAction(resolved, InputContext::Gameplay, {"T", false}) == InputAction::PickUp);
    CHECK(ResolveAction(resolved, InputContext::Gameplay, {"t", true}) == InputAction::Drop);
    CHECK(ResolveAction(resolved, InputContext::Gameplay, {"T", true}) == InputAction::Save);
    CHECK_FALSE(LegacyKeyAvailable({"T", true}));
    CHECK(LegacyKeyAvailable({"t", true}));
    CHECK_FALSE(LegacyKeyAvailable({"i", true}));
    CHECK(ChordName({"T", true}) == "Ctrl+T");
    Keymap world{{InputContext::Gameplay, InputAction::Wait, {}}};
    global[0].keys = {{"x", false}};
    resolved = ResolveKeymap(global, world);
    CHECK_FALSE(ResolveAction(resolved, InputContext::Gameplay, {"x", false}));
    ResetBinding(world, InputContext::Gameplay, InputAction::Wait);
    CHECK(ResolveAction(ResolveKeymap(global, world), InputContext::Gameplay, {"x", false}) == InputAction::Wait);
    world = {{InputContext::Gameplay, InputAction::Wait, {{"T", false}}}};
    CHECK_FALSE(ValidateKeymap(ResolveKeymap(global, world)));
}

TEST_CASE("Keybinding files round trip sparse entries and reject unsupported modifiers and duplicate actions", "[settings][input]") {
    SettingsDirectory files;
    const Keymap overrides{{InputContext::Gameplay, InputAction::Wait, {{"T", true}, {"t", true}}},
        {InputContext::Debug, InputAction::West, {}}};
    REQUIRE(SaveKeymapFile(files.File("keys.json5").c_str(), overrides));
    const auto loaded = LoadKeymapFile(files.File("keys.json5").c_str());
    REQUIRE(loaded); CHECK(*loaded == overrides);
    for (const char* text : {
        "{format_version:1,bindings:[{context:'gameplay',action:'wait',keys:[{key:'t',shift:true}]}]}",
        "{format_version:1,bindings:[{context:'gameplay',action:'wait',keys:[{key:'t',alt:true}]}]}",
        "{format_version:1,bindings:[{context:'gameplay',action:'wait',keys:[{key:'Escape'}]}]}",
        "{format_version:1,bindings:[{context:'gameplay',action:'wait',keys:[]},{context:'gameplay',action:'wait',keys:[]}]}",
        "{format_version:1,bindings:[{context:'gameplay',action:'wait',keys:[{key:'ab'}]}]}"}) { CHECK_FALSE(ParseKeymap(text)); }
}

TEST_CASE("Gameplay modal bindings preserve case round trip overrides and report existing key conflicts", "[settings][input]") {
    const auto defaults = DefaultKeymap();
    REQUIRE(ValidateKeymap(defaults));
    for (auto context : {InputContext::Gameplay, InputContext::Interaction}) {
        CHECK(ResolveAction(defaults, context, {"i", false}) == InputAction::Inventory);
        CHECK(ResolveAction(defaults, context, {"P", false}) == InputAction::Players);
        CHECK(ResolveAction(defaults, context, {"F1", false}) == InputAction::Help);
        CHECK(ResolveAction(defaults, context, {"a", false}) == InputAction::QuickActions);
    }
    CHECK(ResolveAction(defaults, InputContext::Gameplay, {"p", false}) == InputAction::ToggleIdle);
    CHECK(ResolveAction(defaults, InputContext::Menu, {"i", false}) == InputAction::Inspect);
    SettingsDirectory files;
    const Keymap overrides{{InputContext::Gameplay, InputAction::Inventory, {{"I", false}}},
        {InputContext::Interaction, InputAction::Players, {}},
        {InputContext::Gameplay, InputAction::Help, {{"?", false}}},
        {InputContext::Gameplay, InputAction::QuickActions, {{"A", false}}}};
    REQUIRE(SaveKeymapFile(files.File("modal-keys.json5").c_str(), overrides));
    const auto loaded = LoadKeymapFile(files.File("modal-keys.json5").c_str());
    REQUIRE(loaded); CHECK(*loaded == overrides);
    const auto keys = ResolveKeymap(*loaded, {});
    REQUIRE(ValidateKeymap(keys));
    CHECK(ResolveAction(keys, InputContext::Gameplay, {"I", false}) == InputAction::Inventory);
    CHECK_FALSE(ResolveAction(keys, InputContext::Interaction, {"P", false}));
    const auto conflict = ResolveKeymap({{InputContext::Gameplay, InputAction::Wait, {{"F1", false}}}}, {});
    const auto validation = ValidateKeymap(conflict);
    REQUIRE_FALSE(validation); CHECK(validation.error() == InputError::Conflict);
}

}
