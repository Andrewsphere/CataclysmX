#include "client/settings.h"
#include "client/localization.h"
#include "core/save_database.h"
#include "content_fixture.h"
#include "settings_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {
using namespace cataclysmx;

TEST_CASE("Message history size defaults to ten and persists sparse overrides", "[settings][messages]") {
    CHECK(Settings{}.messageHistorySize == 10);
    SettingsDirectory files;
    SettingsOverrides global; global.messageHistorySize = 20;
    SettingsOverrides world; world.messageHistorySize = 1;
    CHECK(ResolveSettings(global, world).messageHistorySize == 1);
    REQUIRE(HasOverride(world, SettingsField::MessageHistorySize));
    ResetOverride(world, SettingsField::MessageHistorySize);
    CHECK(ResolveSettings(global, world).messageHistorySize == 20);
    for (int size : {1, 10, 100}) {
        REQUIRE(SetOverride(world, SettingsField::MessageHistorySize, NumberText(size)));
        REQUIRE(ValidateOverrides(world));
        const auto path = files.File("history/settings.json5");
        REQUIRE(SaveSettingsFile(path.c_str(), world));
        const auto loaded = LoadSettingsFile(path.c_str());
        REQUIRE(loaded); CHECK(*loaded == world);
    }
    for (const char* text : {"0", "-1", "101", "2147483647"}) {
        String json = "{format_version:2,settings:{message_history_size:";
        json += text; json += "}}";
        CHECK_FALSE(ParseSettings({json.data(), json.size()}));
    }
}

TEST_CASE("View radius defaults to 64 and validates both bounds with explicit overrides", "[settings]") {
    CHECK(Settings{}.viewRadius == 64);
    CHECK(ResolveSettings({}, {}).viewRadius == 64);
    Settings settings;
    for (const int radius : {0, 13, 63, 64}) {
        settings.viewRadius = radius;
        REQUIRE(ValidateSettings(settings));
    }
    for (const int radius : {-1, 65, INT32_MAX}) {
        settings.viewRadius = radius;
        CHECK_FALSE(ValidateSettings(settings));
    }
    SettingsOverrides global; global.viewRadius = 64;
    SettingsOverrides world; world.viewRadius = 0;
    CHECK(ResolveSettings(global, world).viewRadius == 0);
    world.viewRadius = 8;
    CHECK(ResolveSettings(global, world).viewRadius == 8);
    ResetOverride(world, SettingsField::ViewRadius);
    CHECK(ResolveSettings(global, world).viewRadius == 64);
    CHECK(ParseSettings("{format_version:1,settings:{view_radius:64}}"));
}

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
        "{format_version:1,settings:{view_radius:65}}",
        "{format_version:1,settings:{view_radius:1,view_radius:2}}",
        "{format_version:1,settings:{unknown:1}}",
        "{format_version:1,settings:{language:'../escape'}}",
        "{format_version:1,settings:{rate_limited:0}}",
        "{format_version:1,settings:{seed:-1}}",
        "{format_version:1,settings:{autosave_interval:1.5}}",
        "{format_version:1,settings:{view_radius:null}}"}) { CHECK_FALSE(ParseSettings(text)); }
    const auto future = ParseSettings("{format_version:3,settings:{}}");
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
        REQUIRE(ApplySettings(**loaded, store));
        CHECK((*loaded)->GetWorld().GetSeed() == 321);
        CHECK((*loaded)->GetAutosaveInterval() == 222);
        CHECK(store.Effective().viewRadius == 2);
        CHECK_FALSE(std::filesystem::exists(files.File("world/a.sqlite.settings.json5")));
    }
}

TEST_CASE("Settings v1 migrates bubble choices to square and v2 circle choices round trip", "[settings][circle]") {
    for (const char* text : {"{format_version:1,settings:{active_radius:1}}",
        "{format_version:1,settings:{resident_radius:5}}"}) {
        const auto legacy = ParseSettings(text);
        REQUIRE(legacy);
        CHECK(legacy->residencyMode == ResidencyMode::Square);
        CHECK(ResolveSettings({}, *legacy).residencyMode == ResidencyMode::Square);
    }
    const auto unrelated = ParseSettings("{format_version:1,settings:{language:'en'}}");
    REQUIRE(unrelated);
    CHECK_FALSE(HasResidencyOverride(*unrelated));
    const auto circle = ParseSettings("{format_version:2,settings:{residency_mode:'circle',resident_radius_tiles:0,active_radius:8}}");
    REQUIRE(circle);
    CHECK(ResolveSettings({}, *circle).residentRadiusTiles == 0);
    CHECK(ValidateSettings(ResolveSettings({}, *circle)));
    SettingsDirectory files;
    REQUIRE(SaveSettingsFile(files.File("circle.json5").c_str(), *circle));
    const auto loaded = LoadSettingsFile(files.File("circle.json5").c_str());
    REQUIRE(loaded);
    CHECK(*loaded == *circle);
    CHECK(files.Read("circle.json5").find("resident_radius\"") == std::string::npos);
    CHECK(ParseSettings("{format_version:2,settings:{resident_radius_tiles:128}}"));
    for (const char* text : {
        "{format_version:1,settings:{residency_mode:'square'}}",
        "{format_version:1,settings:{resident_radius_tiles:78}}",
        "{format_version:1,settings:{active_radius:2,resident_radius:1}}",
        "{format_version:2,settings:{unknown:1}}",
        "{format_version:2,settings:{residency_mode:'triangle'}}",
        "{format_version:2,settings:{residency_mode:1}}",
        "{format_version:2,settings:{resident_radius_tiles:'78'}}",
        "{format_version:2,settings:{resident_radius_tiles:-1}}",
        "{format_version:2,settings:{resident_radius_tiles:129}}",
        "{format_version:2,settings:{active_radius:9}}",
        "{format_version:2,settings:{resident_radius:1,resident_radius_tiles:78}}",
        "{format_version:2,settings:{residency_mode:'square',resident_radius_tiles:78}}",
        "{format_version:2,settings:{residency_mode:'circle',resident_radius:5}}"}) {
        CHECK_FALSE(ParseSettings(text));
    }
    CHECK(ParseKeymap("{format_version:2,bindings:[]}"));
}

TEST_CASE("Store application preserves default and saved residency without bubble overrides", "[settings][server][save][circle]") {
    SettingsDirectory files;
    SettingsStore store("");
    Server original(TestContent());
    REQUIRE(original.GetCircleResidency());
    CHECK(original.GetCircleResidency()->radiusTiles == 78);
    REQUIRE(ApplySettings(original, store));
    REQUIRE(original.GetCircleResidency());
    CHECK(original.GetCircleResidency()->radiusTiles == 78);
    const auto legacy = ParseSettings("{format_version:1,settings:{active_radius:1}}");
    REQUIRE(legacy);
    SettingsStore legacyStore("");
    REQUIRE(legacyStore.SetOverrides(SettingsScope::Global, *legacy, {}));
    REQUIRE(ApplySettings(original, legacyStore));
    CHECK_FALSE(original.GetCircleResidency()); // Explicit even though active equals its default.
    REQUIRE(SaveSettingsFile(files.File("migrated.json5").c_str(), *legacy));
    const auto migrated = LoadSettingsFile(files.File("migrated.json5").c_str());
    REQUIRE(migrated);
    CHECK(*migrated == *legacy);
    REQUIRE(original.SetCircleResidency(0, {13}));
    REQUIRE(original.CreateSave(files.File("circle.sqlite").c_str()));
    auto loaded = Server::LoadSave(TestContent(), files.File("circle.sqlite").c_str());
    REQUIRE(loaded);
    SettingsOverrides global; global.language = "en"; global.ticksPerSecond = 37;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    SettingsOverrides world; world.autosaveInterval = 17;
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    const auto chunks = (*loaded)->GetWorld().LoadedChunks();
    REQUIRE(ApplySettings(**loaded, store));
    REQUIRE((*loaded)->GetCircleResidency());
    CHECK((*loaded)->GetCircleResidency()->radiusTiles == 13);
    CHECK((*loaded)->GetActiveRadius() == 0);
    CHECK((*loaded)->GetWorld().LoadedChunks() == chunks);
    CHECK((*loaded)->GetPacing().ticksPerSecond == 37);
    CHECK((*loaded)->GetAutosaveInterval() == 17);
    CHECK((*loaded)->GetTick() == 0);

    world.residencyMode = ResidencyMode::Square; world.residentRadius = 0; world.activeRadius = 0;
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    REQUIRE(ApplySettings(**loaded, store));
    CHECK_FALSE((*loaded)->GetCircleResidency());
    CHECK((*loaded)->GetResidentRadius() == 0);
    // Removing overrides retains the now-current square policy, not circle defaults.
    world.residencyMode.reset(); world.residentRadius.reset(); world.activeRadius.reset();
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    REQUIRE(ApplySettings(**loaded, store));
    CHECK_FALSE((*loaded)->GetCircleResidency());
    CHECK((*loaded)->GetResidentRadius() == 0);
    REQUIRE(ApplySettings(**loaded, Settings{}));
    CHECK_FALSE((*loaded)->GetCircleResidency());
    CHECK((*loaded)->GetResidentRadius() == 5);
}

TEST_CASE("World residency selectors mask global mode while active and radius values inherit explicitly", "[settings][circle]") {
    SettingsStore store("");
    SettingsOverrides global; global.residentRadiusTiles = 20; global.activeRadius = 0;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    CHECK(store.Source(SettingsField::ResidencyMode) == SettingsSource::Global);
    SettingsOverrides world; world.language = "en";
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    CHECK(store.Effective().residencyMode == ResidencyMode::Circle);
    world.residentRadius = 1;
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    CHECK(store.Effective().residencyMode == ResidencyMode::Square);
    CHECK(store.Source(SettingsField::ResidencyMode) == SettingsSource::World);
    global.residentRadiusTiles = 31;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    CHECK(store.Effective().residencyMode == ResidencyMode::Square);
    ResetOverride(world, SettingsField::ResidentRadius);
    world.activeRadius = 2; // Active chunks imply residency beyond a small circle.
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    CHECK(store.Effective().residencyMode == ResidencyMode::Circle);
    CHECK(store.Effective().residentRadiusTiles == 31);
    CHECK(store.Effective().activeRadius == 2);
    world.residentRadiusTiles = 31; // Explicit equality is retained across parent edits.
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    global.residentRadiusTiles = 0;
    REQUIRE(store.SetOverrides(SettingsScope::Global, global, {}));
    CHECK(store.Effective().residentRadiusTiles == 31);
    ResetOverride(world, SettingsField::ResidentRadiusTiles);
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    CHECK(store.Effective().residentRadiusTiles == 0);
    world.residencyMode = ResidencyMode::Square;
    REQUIRE(store.SetOverrides(SettingsScope::World, world, {}));
    CHECK(store.Effective().residencyMode == ResidencyMode::Square);
}

TEST_CASE("Active-only v2 overrides retain current mode and failed policy updates preserve preferences", "[settings][server][circle]") {
    Server server(TestContent());
    REQUIRE(server.SetCircleResidency(0, {0}));
    SettingsStore store("");
    SettingsOverrides active; active.activeRadius = 2;
    REQUIRE(store.SetOverrides(SettingsScope::Global, active, {}));
    REQUIRE(ApplySettings(server, store));
    REQUIRE(server.GetCircleResidency());
    CHECK(server.GetCircleResidency()->radiusTiles == 0);
    CHECK(server.GetActiveRadius() == 2);

    REQUIRE(server.SetBubbleRadii(0, 0));
    World& world = server.GetWorld();
    const auto request = world.RequestChunks({{world_coordinate_limit / chunk_size, 0}, 0}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({world_coordinate_limit, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(server.GetPlayer(), {world_coordinate_limit, 0}));
    REQUIRE(server.SetBubbleRadii(0, 0));
    REQUIRE(world.ReleaseRequest(*request));
    REQUIRE(world.ReconcileChunks());
    const auto chunks = world.LoadedChunks();
    const auto pacing = server.GetPacing();
    const auto autosave = server.GetAutosaveInterval();
    SettingsOverrides invalidAtBoundary;
    invalidAtBoundary.activeRadius = 0; invalidAtBoundary.residentRadius = 1;
    invalidAtBoundary.ticksPerSecond = 17; invalidAtBoundary.autosaveInterval = 91;
    REQUIRE(store.SetOverrides(SettingsScope::Global, invalidAtBoundary, {}));
    const auto result = ApplySettings(server, store);
    REQUIRE_FALSE(result);
    CHECK(result.error() == SettingsError::ApplyFailed);
    CHECK(server.GetActiveRadius() == 0);
    CHECK(server.GetResidentRadius() == 0);
    CHECK_FALSE(server.GetCircleResidency());
    CHECK(world.LoadedChunks() == chunks);
    CHECK(server.GetPacing() == pacing);
    CHECK(server.GetAutosaveInterval() == autosave);
    CHECK(server.GetTick() == 0);
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

TEST_CASE("Default navigation uses arrows and debug region jumps use Shift arrows", "[settings][input]") {
    const auto keys = DefaultKeymap();
    REQUIRE(ValidateKeymap(keys));
    struct Direction { const char* key; InputAction move; InputAction jump; };
    for (const auto direction : {Direction{"Left", InputAction::West, InputAction::JumpWest},
        {"Right", InputAction::East, InputAction::JumpEast},
        {"Up", InputAction::North, InputAction::JumpNorth},
        {"Down", InputAction::South, InputAction::JumpSouth}}) {
        for (const auto context : {InputContext::Gameplay, InputContext::Interaction, InputContext::Menu, InputContext::Debug}) {
            CHECK(ResolveAction(keys, context, {direction.key, false}) == direction.move);
        }
        CHECK(ResolveAction(keys, InputContext::Debug, {direction.key, false, false, true}) == direction.jump);
    }
    for (const auto context : {InputContext::Gameplay, InputContext::Interaction, InputContext::Menu, InputContext::Debug, InputContext::Items}) {
        for (const char* key : {"h", "j", "k", "l", "H", "J", "K", "L"}) {
            CHECK_FALSE(ResolveAction(keys, context, {key, false}));
        }
    }
    const auto overridden = ResolveKeymap({{InputContext::Debug, InputAction::Inspect, {{"Left", false, false, true}}}}, {});
    REQUIRE(ValidateKeymap(overridden));
    CHECK(ResolveAction(overridden, InputContext::Debug, {"Left", false, false, true}) == InputAction::Inspect);
    CHECK(BindingHint(overridden, InputContext::Debug, InputAction::JumpWest) == "—");
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

TEST_CASE("Keybinding v2 persists Alt and named Shift while reading v1", "[settings][input]") {
    using namespace cataclysmx;
    SettingsDirectory files;
    const Keymap keys{{InputContext::Gameplay,InputAction::MoveUp,{{"Up",false,true}}},
        {InputContext::Gameplay,InputAction::Northwest,{{"Left",false,false,true}}}};
    REQUIRE(SaveKeymapFile(files.File("modifiers.json5").c_str(),keys));
    const auto loaded = LoadKeymapFile(files.File("modifiers.json5").c_str());
    REQUIRE(loaded); CHECK(*loaded == keys);
    const auto legacy = ParseKeymap("{format_version:1,bindings:[{context:'gameplay',action:'move_up',keys:[{key:'>'}]}]}");
    REQUIRE(legacy); CHECK_FALSE(legacy->front().keys.front().alt); CHECK_FALSE(legacy->front().keys.front().shift);
    CHECK(ChordName({"Left",false,false,true}) == "Shift+Left");
    CHECK(ChordName({"Up",false,true}) == "Alt+Up");
    CHECK_FALSE(ParseKeymap("{format_version:2,bindings:[{context:'gameplay',action:'move_up',keys:[{key:'Up',alt:1}]}]}"));
    CHECK_FALSE(ParseKeymap("{format_version:2,bindings:[{context:'gameplay',action:'wait',keys:[{key:'t',shift:true}]}]}"));
}

TEST_CASE("Keyboard repeat preference validates overlays and persists", "[settings]") {
    using namespace cataclysmx;
    SettingsDirectory files; SettingsStore store(String(files.File("user").c_str()));
    CHECK(store.Effective().keyboardRepeatRate == 10);
    for (const char* value : {"0","61","-1"}) {
        SettingsOverrides overrides;
        const auto parsed = SetOverride(overrides,SettingsField::KeyboardRepeatRate,value);
        CHECK_FALSE((parsed && ValidateOverrides(overrides)));
    }
    SettingsOverrides global; global.keyboardRepeatRate = 30;
    REQUIRE(store.SetOverrides(SettingsScope::Global,global,{}));
    REQUIRE(store.Save(SettingsScope::Global));
    SettingsStore loaded(store.UserRoot()); REQUIRE(loaded.Load());
    CHECK(loaded.Effective().keyboardRepeatRate == 30);
    SettingsOverrides world; world.keyboardRepeatRate = 60;
    REQUIRE(loaded.SetOverrides(SettingsScope::World,world,{})); CHECK(loaded.Effective().keyboardRepeatRate == 60);
}
