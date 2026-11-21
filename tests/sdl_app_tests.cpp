#include "sdl/app.h"
#include "sdl/screens.h"
#include "client/tile_inspection.h"
#include "content_fixture.h"
#include "settings_fixture.h"


#include <catch2/catch_test_macros.hpp>

TEST_CASE("SDL gameplay dispatch preserves axial directions and action kinds", "[sdl][input]") {
    using namespace cataclysmx;
    CHECK(sdl::AppOptions{}.scenario == NewGameScenario::Town);
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

TEST_CASE("SDL examine separates present categories and keeps item contents together", "[sdl][perception]") {
    using namespace cataclysmx;
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto position = server.View().player_position;
    Vector<String> lines;
    DescribeTile(world, server.View(), position, lines);
    REQUIRE_FALSE(lines.empty());
    CHECK_FALSE(lines.front().empty());
    CHECK_FALSE(lines.back().empty());
    int separators = 0;
    for (const auto& line : lines) { separators += line.empty(); }
    CHECK(separators == 2); // Terrain, player, and the starting backpack.
    REQUIRE(lines.size() >= 2);
    CHECK(lines[lines.size() - 2].find("backpack") != String::npos);
    CHECK(lines.back().find("  bandage") == 0); // Contained items stay with their parent.

    REQUIRE(world.CreateItem(TestContent().FindItem("bandage"), ItemLocation::Ground(position)));
    REQUIRE(world.CreateItem(TestContent().FindItem("bandage"), ItemLocation::Ground(position)));
    lines.clear();
    DescribeTile(world, server.View(), position, lines);
    separators = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            ++separators;
            REQUIRE(i > 0);
            REQUIRE(i + 1 < lines.size());
            CHECK_FALSE(lines[i - 1].empty());
            CHECK_FALSE(lines[i + 1].empty());
        }
    }
    CHECK(separators == 2); // Both ground items form one category.
    CHECK(lines[lines.size() - 2].find("bandage") != String::npos);
    CHECK(lines.back().find("bandage") != String::npos);
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
    Diagnostics diagnostics;
    auto candidate = editor.Prepare(store, &diagnostics); REQUIRE(candidate);
    CHECK(candidate->Effective().viewRadius == 12);
    ScreenFrame frame; editor.Present(store,localization,frame);
    CHECK(frame.visible); CHECK(frame.presentation == ScreenPresentation::Large); CHECK_FALSE(frame.rows.empty());
    CHECK(frame.status.find("Unapplied") != String::npos);
    // Tab opens Keybindings; Enter replaces the selected chord.
    key("Tab"); key("Enter");
    editor.Key({"Left",false,true},store,localization,CATACLYSMX_LOC_DIRECTORY);
    candidate = editor.Prepare(store, &diagnostics); REQUIRE(candidate);
    CHECK(ResolveAction(candidate->Keys(),InputContext::Gameplay,{"Left",false,true}) == InputAction::West);
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
        ScreenFrame frame; frame.visible = true; frame.presentation = ScreenPresentation::Large; frame.title = "Options"; frame.footer = "Escape closes";
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

TEST_CASE("SDL settings discard edits at scope changes and retain applied changes after a disk failure", "[sdl][settings]") {
    using namespace cataclysmx; using namespace cataclysmx::sdl;
    SettingsDirectory files; files.Write("blocked", "not a directory");
    SettingsStore store(String(files.File("blocked").c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    OptionsUi editor; editor.Open(store, store.Effective());
    const auto key = [&](const char* name) { return editor.Key({String(name)}, store, loc, CATACLYSMX_LOC_DIRECTORY); };
    key("Down"); key("Enter"); editor.Text("12"); key("Enter");
    key("Tab"); key("BackTab");
    auto candidate = editor.Prepare(store, nullptr); REQUIRE(candidate);
    CHECK(candidate->Effective().viewRadius == 12);
    key("g"); key("g");
    REQUIRE(editor.Scope() == SettingsScope::Global);
    candidate = editor.Prepare(store, nullptr); REQUIRE(candidate);
    CHECK_FALSE(candidate->Overrides(SettingsScope::Global).viewRadius);

    key("Down"); key("Left");
    CHECK(key("s") == OptionsEvent::Save);
    candidate = editor.Prepare(store, nullptr); REQUIRE(candidate);
    CHECK(candidate->Effective().viewRadius == 63);
    CHECK_FALSE(candidate->Overrides(SettingsScope::World).viewRadius);
    store = eastl::move(*candidate);
    editor.Applied(store, store.Effective());
    Diagnostics diagnostics;
    CHECK_FALSE(store.Save(SettingsScope::Global, &diagnostics));
    CHECK_FALSE(diagnostics.Empty());
    candidate = editor.Prepare(store, nullptr); REQUIRE(candidate);
    CHECK(candidate->Overrides(SettingsScope::Global) == store.Overrides(SettingsScope::Global));
    CHECK(candidate->Dirty(SettingsScope::Global));
    CHECK(key("R") == OptionsEvent::None);
    CHECK(key("a") == OptionsEvent::None);
    ScreenFrame frame; editor.Present(store, loc, frame);
    CHECK(frame.status == "Applied, unsaved");
    CHECK(frame.footer.find("s Save") != String::npos);
    CHECK(frame.footer.find("Reload") == String::npos);
}
