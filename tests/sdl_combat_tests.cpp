#include "sdl/screens.h"
#include "sdl/inventory_screen.h"
#include "sdl/input.h"
#include "combat/combat_system.h"
#include "client/message_history.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::sdl;

WorldView AbilityViewForUi() {
    WorldView view; view.playerId = 1; view.characterId = 10;
    view.player_position = {10, 20, 1};
    view.width = view.height = 9; view.origin = {6, 16, 1};
    const auto& content = TestContent();
    for (const auto name : {"focus", "leap", "sweep"}) {
        const auto id = content.FindAbility(name); REQUIRE(id.value);
        AbilityView ability; ability.ability = id;
        ability.source = {AbilityGrantSourceKind::Innate, {}};
        ability.targeting = content.GetAbility(id).targeting;
        view.combatInfo.abilities.push_back(ability);
    }
    return view;
}

TEST_CASE("SDL combat hotkeys and menu preserve identical available grant commands", "[sdl][combat]") {
    auto view = AbilityViewForUi();
    // A blocked first grant must not hide a second available source.
    auto blocked = view.combatInfo.abilities.front();
    blocked.source = {AbilityGrantSourceKind::Art, TestContent().FindCombatArt("boxing")};
    blocked.availability.reason = CombatRefusal::Cooldown;
    view.combatInfo.abilities.insert(view.combatInfo.abilities.begin(), blocked);
    CombatUi hotkey; hotkey.Refresh(TestContent(), view);
    REQUIRE(hotkey.SelectHotkey(InputAction::FocusAbility, TestContent()));
    REQUIRE(hotkey.BeginChoice(view.player_position));
    const auto command = hotkey.Accept(); REQUIRE(command);
    CHECK(ValidCommandShape(*command));
    CHECK(command->ability.source.kind == AbilityGrantSourceKind::Innate);
    ScreenFrame frame; hotkey.Present(frame);
    CombatUi menu; menu.Refresh(TestContent(), view);
    for (int i = 0; i < frame.selected; ++i) { menu.MoveSelection(1); }
    REQUIRE(menu.BeginChoice(view.player_position));
    const auto fromMenu = menu.Accept(); REQUIRE(fromMenu);
    CHECK(fromMenu->kind == command->kind); CHECK(fromMenu->ability == command->ability);
    view.combatInfo.abilities.clear(); hotkey = {}; hotkey.Refresh(TestContent(), view);
    CHECK_FALSE(hotkey.SelectHotkey(InputAction::FocusAbility, TestContent()));
    frame = {}; hotkey.Present(frame); REQUIRE_FALSE(frame.details.empty());
    CHECK(frame.details.front().find("not available") != String::npos);
}

TEST_CASE("SDL sweep previews exactly three tiles for each cardinal without submitting", "[sdl][combat]") {
    const auto view = AbilityViewForUi();
    CombatUi ui; ui.Refresh(TestContent(), view);
    REQUIRE(ui.SelectHotkey(InputAction::SweepAbility, TestContent()));
    CHECK_FALSE(ui.Accept()); // Selecting a row is not acceptance of a target.
    REQUIRE(ui.BeginChoice(view.player_position));
    for (const auto direction : {InputAction::North, InputAction::East, InputAction::South, InputAction::West}) {
        ui.MoveTarget(direction);
        ScreenFrame frame; ui.Present(frame);
        REQUIRE(frame.highlights.size() == 3);
        CHECK(frame.presentation == ScreenPresentation::Anchored);
        const int dx = direction == InputAction::East ? 1 : direction == InputAction::West ? -1 : 0;
        const int dz = direction == InputAction::South ? 1 : direction == InputAction::North ? -1 : 0;
        for (int i = -1; i <= 1; ++i) {
            CHECK(frame.highlights[static_cast<std::size_t>(i + 1)]
                == TileCoord{10 + dx - dz * i, 20 + dz + dx * i, 1});
        }
        const auto accepted = ui.Accept(); REQUIRE(accepted);
        CHECK(accepted->kind == CommandKind::use_ability);
        CHECK(ValidCommandShape(*accepted));
    }
    ui.CancelTarget(); CHECK_FALSE(ui.Targeting()); CHECK_FALSE(ui.Accept());
}

TEST_CASE("SDL leap cursor stays within two tiles on the same layer and rechecks grants", "[sdl][combat]") {
    auto view = AbilityViewForUi();
    CombatUi ui; ui.Refresh(TestContent(), view);
    REQUIRE(ui.SelectHotkey(InputAction::LeapAbility, TestContent()));
    REQUIRE(ui.BeginChoice(view.player_position)); CHECK_FALSE(ui.Accept());
    for (int i = 0; i < 10; ++i) { ui.MoveTarget(InputAction::Northeast); ui.MoveTarget(InputAction::MoveUp); }
    ScreenFrame frame; ui.Present(frame);
    REQUIRE(frame.cursor); CHECK(*frame.cursor == TileCoord{12, 18, 1});
    REQUIRE(frame.highlights.size() == 1); CHECK(frame.highlights.front() == *frame.cursor);
    const auto accepted = ui.Accept(); REQUIRE(accepted);
    CHECK(accepted->ability.target.tile == *frame.cursor);
    view.player_position.x += 1; ui.Refresh(TestContent(), view);
    CHECK_FALSE(ui.Targeting()); CHECK_FALSE(ui.Accept());
    REQUIRE(ui.BeginChoice(view.player_position)); ui.MoveTarget(InputAction::East);
    view.combatInfo.abilities.clear(); ui.Refresh(TestContent(), view);
    CHECK_FALSE(ui.Accept()); // A vanished grant never substitutes another source.
    ui.CancelTarget(); CHECK_FALSE(ui.Targeting());
}

TEST_CASE("SDL combat inspection and cancellation leave real resources unchanged until acceptance", "[sdl][combat]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    REQUIRE(world.Combat().GrantArt(server.GetPlayer(), TestContent().FindCombatArt("boxing"), ArtGrantReason::Administration));
    const auto view = server.View();
    const auto before = world.GetActor(server.GetPlayer())->combat;
    const auto tick = server.GetTick();
    CombatUi ui; ui.Refresh(TestContent(), view);
    REQUIRE(ui.SelectHotkey(InputAction::FocusAbility, TestContent()));
    REQUIRE(ui.BeginChoice(view.player_position));
    ScreenFrame frame; ui.Present(frame); ui.CancelTarget();
    PresentCombatInfo(TestContent(), view, frame);
    CHECK(world.GetActor(server.GetPlayer())->combat == before);
    CHECK(server.GetTick() == tick);
    REQUIRE(ui.BeginChoice(view.player_position));
    const auto command = ui.Accept(); REQUIRE(command);
    const auto result = server.Send(*command);
    CHECK(result.outcome == Outcome::ability_used);
    CHECK(world.GetActor(server.GetPlayer())->combat.stamina < before.stamina);
}

TEST_CASE("SDL dead combat menu creates a character in the existing session and world", "[sdl][combat]") {
    Server server(TestContent(), 7);
    auto& world = server.GetWorld();
    const auto player = server.GetHostPlayerId();
    const auto dead = server.GetPlayer();
    world.Combat().Damage(dead, {}, BodyPart::Torso, {DamageType::Blunt, 10000});
    server.Update(0);
    const auto view = server.View();
    CombatUi ui; ui.Refresh(TestContent(), view);
    ScreenFrame frame; ui.Present(frame);
    REQUIRE(frame.rows.size() == 1); CHECK_FALSE(frame.rows.front().unavailable);
    REQUIRE(ui.BeginChoice(view.player_position));
    const auto command = ui.Accept(); REQUIRE(command); CHECK(command->kind == CommandKind::new_character);
    const auto input = server.GetInputContext(player);
    const auto result = server.Submit(player, {input.lastInputSequence + 1, input.opportunity, *command});
    CHECK(result.outcome == Outcome::character_created);
    CHECK(server.GetHostPlayerId() == player);
    CHECK(server.View().characterId != view.characterId);
    REQUIRE(world.GetActor(dead)); CHECK(world.GetActor(dead)->combat.corpse != 0);
}

TEST_CASE("SDL combat history drains serials once across snapshots and character changes", "[sdl][combat]") {
    WorldView view; CombatEventCursor cursor; Vector<String> messages; MessageHistory history;
    CombatEvent event; event.serial = 1; event.kind = CombatEventKind::ActionCommitted;
    view.combatEvents.push_back(event);
    event.serial = 2; event.kind = CombatEventKind::TrainingSuppressed; view.combatEvents.push_back(event);
    event.serial = 3; event.kind = CombatEventKind::Strike;
    event.strike.attacker = 1; event.strike.target = 2; event.strike.outcome = StrikeOutcome::Miss;
    view.combatEvents.push_back(event);
    event.serial = 4; view.combatEvents.push_back(event);
    cursor.Drain(TestContent(), view, 1, messages); REQUIRE(messages.size() == 3);
    for (const auto& message : messages) { history.Add({message.data(), message.size()}); }
    REQUIRE(history.Entries().size() == 2);
    CHECK(history.Entries()[0].text.find("no combat XP") != String::npos);
    CHECK(history.Entries().back().text.find("#1 -> #2 strike: miss") != String::npos);
    CHECK(history.Entries().back().count == 2);
    cursor.Drain(TestContent(), view, 1, messages); CHECK(messages.empty());
    for (const auto& message : messages) { history.Add({message.data(), message.size()}); }
    CHECK(history.Entries().back().count == 2);
    view.characterId = 50;
    event.serial = 5; event.kind = CombatEventKind::ArtStudyCompleted; event.learnedArt = false;
    view.combatEvents.push_back(event);
    cursor.Drain(TestContent(), view, 1, messages); REQUIRE(messages.size() == 1);
    CHECK(messages.front().find("failed attempt") != String::npos);
    view.combatEvents = {event}; view.combatEvents.front().serial = 1;
    cursor.Drain(TestContent(), view, 2, messages); CHECK(messages.size() == 1);
}

TEST_CASE("SDL equipment keys issue Items actions and can hold an actual item", "[sdl][combat][inventory]") {
    Server server(TestContent()); auto& world = server.GetWorld();
    const auto rock = world.CreateItem(TestContent().FindItem("rock"), ItemLocation::Carried(server.GetPlayer())); REQUIRE(rock);
    const auto id = world.GetItem(*rock)->id;
    auto view = server.View();
    InventoryEntry selected;
    for (const auto& item : view.inventory) { if (item.id == id) { selected = item; } }
    REQUIRE(selected.id == id); view.inventory = {selected};
    Localization localization; const auto keys = DefaultKeymap();
    for (const auto key : {"L", "R", "H", "G", "W", "T", "U"}) {
        ItemUi ui; ui.Open(); ui.Refresh(view, TestContent(), localization, keys); HeldKey held;
        if (String(key) == "G" || String(key) == "U") { view.inventory.front().heldHands = HandMask::Both; }
        HandleInventoryKey(ui, held, {key}, keys, view, TestContent(), localization);
        const auto command = ui.TakeCommand(view); REQUIRE(command); CHECK(command->kind == CommandKind::equipment);
        CHECK(ValidCommandShape(*command));
        const String chord(key);
        const auto operation = chord == "G" ? EquipmentOperation::Regrip : chord == "W" ? EquipmentOperation::Wear
            : chord == "T" ? EquipmentOperation::Remove : chord == "U" ? EquipmentOperation::Release : EquipmentOperation::Hold;
        CHECK(command->equipment.operation == operation);
        if (chord == "H") { CHECK(command->equipment.grip == GripKind::TwoHanded); }
        if (chord == "L") { CHECK(command->equipment.hand == Hand::Left); }
        if (String(key) == "R") {
            CHECK(command->equipment.hand == Hand::Right);
            CHECK(server.Send(*command).outcome == Outcome::equipment_changed);
            CHECK(server.View().combatInfo.equipment.hands[1].item == id);
        }
    }
}

TEST_CASE("SDL combat information wraps and scrolls in a headless software renderer", "[sdl][combat][render]") {
    struct Display {
        SDL_Surface* surface = nullptr; SDL_Renderer* renderer = nullptr;
        Display() { SDL_Init(0); surface = SDL_CreateSurface(480, 360, SDL_PIXELFORMAT_RGBA32); if (surface) { renderer = SDL_CreateSoftwareRenderer(surface); } }
        ~Display() { SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit(); }
    } display;
    REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    const auto view = AbilityViewForUi();
    ScreenFrame frame; frame.visible = true; PresentCombatInfo(TestContent(), view, frame);
    REQUIRE(frame.scrollText); REQUIRE(frame.rows.size() == 1);
    CHECK(frame.rows.front().text == CombatDetailsText(TestContent(), view));
    frame.scroll = 100000;
    REQUIRE(DrawScreen(display.renderer, text, frame, 480, 360, &view, 32, 32, 0, 1));
    CHECK(frame.scroll > 0); CHECK(frame.scroll < 100000);
}
}
