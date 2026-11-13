#include "sdl/app.h"
#include "sdl/screens.h"
#include "client/tile_inspection.h"
#include "content_fixture.h"
#include "settings_fixture.h"


#include <catch2/catch_test_macros.hpp>

TEST_CASE("SDL gameplay dispatch preserves axial directions and action kinds", "[sdl][input]") {
    using namespace cataclysmx;
    for (auto action : {InputAction::West, InputAction::East, InputAction::North,
        InputAction::South, InputAction::MoveDown, InputAction::MoveUp}) {
        const auto move = sdl::GameplayCommand(action, false);
        const auto interact = sdl::GameplayCommand(action, true);
        REQUIRE(move); REQUIRE(interact);
        CHECK(ValidCommandShape(*move)); CHECK(ValidCommandShape(*interact) == (move->dy == 0));
        CHECK(move->kind == CommandKind::move);
        CHECK(interact->kind == CommandKind::interact);
        CHECK(move->dx == interact->dx); CHECK(move->dy == interact->dy); CHECK(move->dz == interact->dz);
    }
    CHECK(sdl::GameplayCommand(InputAction::West, false)->dx == -1);
    CHECK(sdl::GameplayCommand(InputAction::East, false)->dx == 1);
    CHECK(sdl::GameplayCommand(InputAction::North, false)->dz == -1);
    CHECK(sdl::GameplayCommand(InputAction::South, false)->dz == 1);
    CHECK(sdl::GameplayCommand(InputAction::MoveDown, false)->dy == -1);
    CHECK(sdl::GameplayCommand(InputAction::MoveUp, false)->dy == 1);
    CHECK(sdl::GameplayCommand(InputAction::Wait, false)->kind == CommandKind::wait);
    const auto attack = sdl::GameplayCommand(InputAction::Attack, false);
    REQUIRE(attack); CHECK(attack->kind == CommandKind::attack); CHECK(attack->target == 0);
    for (const auto [action, style] : {eastl::pair<InputAction, CombatStyle>{InputAction::StyleBalanced, CombatStyle::Balanced},
        {InputAction::StyleAggressive, CombatStyle::Aggressive},
        {InputAction::StyleDefensive, CombatStyle::Defensive}}) {
        const auto command = sdl::GameplayCommand(action, false);
        REQUIRE(command); CHECK(command->kind == CommandKind::combat_style); CHECK(command->style == style);
    }
    CHECK(sdl::GameplayCommand(InputAction::PickUp, false)->kind == CommandKind::pick_up);
    CHECK(sdl::GameplayCommand(InputAction::Drop, false)->kind == CommandKind::drop);
    CHECK_FALSE(sdl::GameplayCommand(InputAction::Help, false));
    CHECK_FALSE(sdl::GameplayCommand(InputAction::Cancel, true));
}


TEST_CASE("SDL examine uses filtered memories and reveals no live hidden contents", "[sdl][perception]") {
    using namespace cataclysmx;
    Server server(TestContent());
    auto& world = server.GetWorld();
    REQUIRE(world.SetObjectAppearance(world.ObjectAt({2,0}),ObjectKind::Chair));
    REQUIRE(server.Send({}).advanced);
    REQUIRE(world.SetTile({1,0},{TestTerrain("wall"),0}));
    const auto item = world.CreateItem(TestContent().FindItem("bandage"),ItemLocation::Ground({2,0}));
    REQUIRE(item);
    const auto memories = world.GetPlayerMemories();
    Vector<String> lines;
    DescribeTile(world,server.View(),{2,0},lines);
    bool remembered = false;
    for (const auto& line : lines) {
        remembered |= line.find("remembered") != String::npos;
        CHECK(line.find("bandage") == String::npos);
    }
    CHECK(remembered);
    CHECK(world.GetPlayerMemories() == memories);
    lines.clear(); DescribeTile(world,server.View(),{10000,10000},lines);
    REQUIRE(lines.size() == 1); CHECK(lines.front() == "Outside current view");
}

TEST_CASE("SDL options stage preferences and capture modifier bindings before apply", "[sdl][settings]") {
    using namespace cataclysmx;
    using namespace cataclysmx::sdl;
    SettingsDirectory files;
    SettingsStore store(String(files.File("user").c_str()));
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY,"en"));
    OptionsUi editor; editor.Open(store,store.Effective());
    const auto key = [&](const char* name) { return editor.Key({String(name)},store,localization,CATACLYSMX_LOC_DIRECTORY); };
    key("Down"); key("Enter"); REQUIRE(editor.EditingText());
    editor.Text("12"); key("Enter"); CHECK_FALSE(editor.EditingText());
    CHECK(store.Effective().viewRadius == 64);
    SettingsStore candidate = store; Diagnostics diagnostics;
    REQUIRE(editor.Prepare(store,candidate,&diagnostics));
    CHECK(candidate.Effective().viewRadius == 12);
    ScreenFrame frame; editor.Present(store,localization,frame);
    CHECK(frame.visible); CHECK(frame.full); CHECK_FALSE(frame.rows.empty());
    CHECK(frame.title.find("Unapplied") != String::npos);
    // Move focus from List to Section, then select Keybindings.
    key("BackTab"); key("BackTab"); key("Right"); key("Enter"); key("Enter");
    editor.Key({"Left",false,true},store,localization,CATACLYSMX_LOC_DIRECTORY);
    REQUIRE(editor.Prepare(store,candidate,&diagnostics));
    CHECK(ResolveAction(candidate.Keys(),InputContext::Gameplay,{"Left",false,true}) == InputAction::West);
    CHECK(ResolveAction(store.Keys(),InputContext::Gameplay,{"Left",false,true}) != InputAction::West);
}

TEST_CASE("SDL overlay renders compact and wide layouts with scroll clamping", "[sdl][render]") {
    using namespace cataclysmx;
    using namespace cataclysmx::sdl;
    REQUIRE(SDL_Init(0));
    SDL_Surface* surface = SDL_CreateSurface(1100,700,SDL_PIXELFORMAT_RGBA32); REQUIRE(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface); REQUIRE(renderer);
    {
        SdlText text(renderer); REQUIRE(text.Ready());
        ScreenFrame frame; frame.visible = frame.full = true; frame.title = "Options"; frame.footer = "Escape closes";
        for (int i = 0; i < 30; ++i) { frame.rows.push_back({"Setting value",i % 2 == 0}); }
        frame.details = {"Description", "Constraint", "Effect"}; frame.selected = 29; frame.scroll = 999;
        REQUIRE(DrawScreen(renderer,text,frame,1100,700,nullptr,32,32,0,1));
        CHECK(frame.scroll >= 0); CHECK(frame.scroll <= 29);
        REQUIRE(DrawScreen(renderer,text,frame,480,360,nullptr,32,32,0,1));
        frame.selected = 0;
        REQUIRE(DrawScreen(renderer,text,frame,480,360,nullptr,32,32,0,1)); CHECK(frame.scroll == 0);
    }
    SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit();
}

TEST_CASE("SDL automatically executes only the sole available interaction", "[sdl][interaction]") {
    using namespace cataclysmx;
    Vector<InteractionChoice> choices;
    CHECK_FALSE(sdl::SoleAvailableAction(choices));
    choices.push_back({{},"door","Open",ObjectUseReason::Locked});
    CHECK_FALSE(sdl::SoleAvailableAction(choices));
    choices.push_back({{},"door","Unlock",ObjectUseReason::None});
    const auto single = sdl::SoleAvailableAction(choices); REQUIRE(single); CHECK(*single == 1);
    choices.push_back({{},"door","Lock",ObjectUseReason::None});
    CHECK_FALSE(sdl::SoleAvailableAction(choices));
}

TEST_CASE("SDL Save and apply retains drafts across scopes and failed persistence", "[sdl][settings]") {
    using namespace cataclysmx; using namespace cataclysmx::sdl;
    SettingsDirectory files; files.Write("blocked","not a directory");
    SettingsStore store(String(files.File("blocked").c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY,"en"));
    OptionsUi editor; editor.Open(store,store.Effective());
    const auto key = [&](const char* name) { return editor.Key({String(name)},store,loc,CATACLYSMX_LOC_DIRECTORY); };
    key("Down"); key("Enter"); editor.Text("12"); key("Enter");
    // Keep the global draft while editing and applying a world draft.
    key("BackTab"); key("BackTab"); key("BackTab"); key("Right"); key("Enter");
    REQUIRE(editor.Scope() == SettingsScope::World);
    key("Down"); key("Enter"); editor.Text("7"); key("Enter");
    CHECK(key("s") == OptionsEvent::Save);
    SettingsStore candidate = store; REQUIRE(editor.Prepare(store,candidate,nullptr));
    store = eastl::move(candidate); editor.Applied(store,store.Effective(),false);
    CHECK(store.Effective().viewRadius == 7);
    key("BackTab"); key("BackTab"); key("BackTab"); key("Left"); key("Enter");
    REQUIRE(editor.Scope() == SettingsScope::Global);
    REQUIRE(editor.Prepare(store,candidate,nullptr));
    REQUIRE(candidate.Overrides(SettingsScope::Global).viewRadius);
    CHECK(*candidate.Overrides(SettingsScope::Global).viewRadius == 12);
    store = eastl::move(candidate); editor.Applied(store,store.Effective(),false);
    Diagnostics diagnostics; CHECK_FALSE(store.Save(SettingsScope::Global,&diagnostics));
    CHECK_FALSE(diagnostics.Empty()); CHECK(store.Dirty(SettingsScope::Global));
    // A retry prepares the same applied draft instead of discarding it.
    REQUIRE(editor.Prepare(store,candidate,nullptr));
    CHECK(candidate.Overrides(SettingsScope::Global) == store.Overrides(SettingsScope::Global));
    CHECK(key("r") == OptionsEvent::None);
    ScreenFrame frame; editor.Present(store,loc,frame);
    CHECK(frame.title.find("Applied, unsaved") != String::npos);
    CHECK(frame.footer.find("Save and apply") != String::npos);
    CHECK(frame.footer.find("Reload") == String::npos);
}
