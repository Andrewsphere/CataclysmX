#include "terminal/gameplay_screen.h"
#include "terminal/keyboard.h"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <string>

namespace {
using namespace cataclysmx;

const ContentCatalog& CombatContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded);
        return eastl::move(*loaded);
    }();
    return content;
}

struct CombatFixture {
    GameplayUi ui;
    Localization localization;
    Keymap keys = DefaultKeymap();
    WorldView view;
    Vector<Command> commands;
    CombatFixture() {
        REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
        view.playerId = 10; view.characterId = 20;
        view.player_position = {0, 0, 3}; view.origin = {-3, -3, 3};
        view.width = 7; view.height = 7;
        ViewCell floor; floor.tile.terrain = CombatContent().FindTerrain("floor");
        floor.terrainVisibility = Visibility::Visible; floor.terrainLight = LightBand::Ordinary;
        view.cells.assign(49, floor);
    }
    bool Submit(Command command) { commands.push_back(command); return true; }
    void Event(const ftxui::Event& event) {
        REQUIRE(ui.HandleEvent(event, view, localization, keys,
            [&](Command command) { return Submit(command); }, {}, &CombatContent()));
    }
    void Ability(const char* name, AbilityTargetKind target, CombatArtId art = {}) {
        AbilityView ability; ability.ability = CombatContent().FindAbility(name); REQUIRE(ability.ability.value);
        ability.source.kind = art.value ? AbilityGrantSourceKind::Art : AbilityGrantSourceKind::Innate;
        ability.source.art = art; ability.targeting = target;
        view.combatInfo.abilities.push_back(ability);
    }
    void Hotkey(InputAction action) {
        REQUIRE(ui.CombatAction(action, view, CombatContent(), [&](Command command) { return Submit(command); }));
    }
    ftxui::Screen Draw() {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
        GameplayPresentation presentation;
        ftxui::Render(screen, ui.Render(view, CombatContent(), localization, keys, presentation));
        return screen;
    }
    std::string Text() {
        auto screen = Draw(); std::string text;
        for (int y = 0; y < screen.dimy(); ++y) {
            for (int x = 0; x < screen.dimx(); ++x) {
                const auto& character = screen.PixelAt(x, y).character;
                text += character.empty() ? " " : character;
            }
            text += '\n';
        }
        return text;
    }
};

TEST_CASE("Terminal combat and equipment defaults decode through current controls", "[terminal][combat][input]") {
    const auto keys = DefaultKeymap();
    REQUIRE(ValidateKeymap(keys));
    const auto check = [&](InputContext context, const char* key, InputAction expected) {
        const auto chord = DecodeTerminalInput(key); REQUIRE(chord);
        CHECK(ResolveAction(keys, context, *chord) == expected);
    };
    check(InputContext::Gameplay, "C", InputAction::CombatMenu);
    check(InputContext::Gameplay, "@", InputAction::CombatInfo);
    check(InputContext::Gameplay, "4", InputAction::FocusAbility);
    check(InputContext::Gameplay, "5", InputAction::LeapAbility);
    check(InputContext::Gameplay, "6", InputAction::SweepAbility);
    check(InputContext::Items, "L", InputAction::HoldLeft);
    check(InputContext::Items, "R", InputAction::HoldRight);
    check(InputContext::Items, "H", InputAction::TwoHanded);
    check(InputContext::Items, "G", InputAction::Regrip);
    check(InputContext::Items, "W", InputAction::Wear);
    check(InputContext::Items, "T", InputAction::RemoveWorn);
    check(InputContext::Items, "U", InputAction::ReleaseHand);
}

TEST_CASE("Terminal combat menu selects shared entries and offers new character while dead and busy", "[terminal][combat]") {
    CombatFixture f;
    REQUIRE(f.ui.Open(InputAction::CombatMenu));
    CHECK(f.Text().find("Style:") != std::string::npos);
    CHECK(f.commands.empty());
    f.Event(ftxui::Event::ArrowDown); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    Vector<CombatMenuEntry> rows; CollectCombatMenu(CombatContent(), f.view, rows);
    CHECK(f.commands.front().combatStyle == rows[1].command.combatStyle);
    CHECK_FALSE(f.ui.IsOpen());
    f.view.combatInfo.combat.life = ActorLifeState::Dead; f.view.remainingTicks = 400; f.view.commandQueued = true;
    REQUIRE(f.ui.Open(InputAction::CombatMenu));
    CHECK(f.Text().find("Create new character") != std::string::npos);
    f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 2);
    CHECK(f.commands.back().kind == CommandKind::new_character);
    REQUIRE(f.ui.Open(InputAction::CombatInfo));
    CHECK(f.Text().find("Character #20 [dead]") != std::string::npos);
    f.Event(ftxui::Event::Escape); CHECK_FALSE(f.ui.IsOpen());
}

TEST_CASE("Terminal ability hotkeys select exact available grants and report absent abilities", "[terminal][combat]") {
    CombatFixture f;
    f.Hotkey(InputAction::FocusAbility);
    CHECK(f.commands.empty()); CHECK(f.Text().find("no valid grant") != std::string::npos);
    f.ui.Close();
    const auto boxing = CombatContent().FindCombatArt("boxing"); REQUIRE(boxing.value);
    const auto weapons = CombatContent().FindCombatArt("weapon_forms"); REQUIRE(weapons.value);
    f.Ability("focus", AbilityTargetKind::Self, boxing);
    f.view.combatInfo.abilities.back().availability.reason = CombatRefusal::Cooldown;
    f.Ability("focus", AbilityTargetKind::Self, weapons);
    f.Hotkey(InputAction::FocusAbility);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands.back().ability.ability == CombatContent().FindAbility("focus"));
    CHECK(f.commands.back().ability.source.art == weapons);
    CHECK_FALSE(f.ui.IsOpen());
}

TEST_CASE("Terminal combat manuals show refusals and submit the selected information identity", "[terminal][combat]") {
    CombatFixture f;
    ArtManualView manual; manual.book = CombatContent().FindBook("boxing_manual_text");
    manual.art = CombatContent().FindCombatArt("boxing");
    REQUIRE(manual.book.value); REQUIRE(manual.art.value);
    manual.reason = CombatRefusal::MissingReference; f.view.combatInfo.manuals.push_back(manual);
    f.ui.Open(InputAction::CombatMenu); f.Draw();
    f.Event(ftxui::Event::ArrowUp); // Wrap to the manual.
    CHECK(f.Text().find("manual required") != std::string::npos);
    f.Event(ftxui::Event::Return); CHECK(f.commands.empty());
    f.view.combatInfo.manuals.front().reason = CombatRefusal::None;
    f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands.back().kind == CommandKind::study_art);
    CHECK(f.commands.back().book == manual.book); CHECK(f.commands.back().art == manual.art);
}

TEST_CASE("Terminal sweep menu and hotkey share targeting and cancel without submission", "[terminal][combat]") {
    CombatFixture f; f.Ability("sweep", AbilityTargetKind::CardinalDirection);
    f.Hotkey(InputAction::SweepAbility); f.Event(ftxui::Event::ArrowRight);
    const auto targetingText = f.Text(); INFO(targetingText);
    CHECK(f.commands.empty()); CHECK(targetingText.find("Sweep: choose one cardinal direction") != std::string::npos);
    const auto preview = f.Draw();
    int highlighted = 0;
    for (int y = 0; y < preview.dimy(); ++y) {
        for (int x = 0; x < preview.dimx(); ++x) { highlighted += preview.PixelAt(x, y).inverted ? 1 : 0; }
    }
    CHECK(highlighted == 3);
    f.Event(ftxui::Event::Escape); CHECK(f.commands.empty()); CHECK_FALSE(f.ui.IsOpen());
    f.ui.Open(InputAction::CombatMenu); f.Draw();
    Vector<CombatMenuEntry> rows; CollectCombatMenu(CombatContent(), f.view, rows);
    for (std::size_t i = 1; i < rows.size(); ++i) { f.Event(ftxui::Event::ArrowDown); }
    f.Event(ftxui::Event::Return); CHECK(f.commands.empty());
    f.Event(ftxui::Event::ArrowRight); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    const auto menuCommand = f.commands.front();
    CHECK(menuCommand.ability.target.direction == CardinalDirection::East);
    CHECK(menuCommand.ability.target.tile == TileCoord{});
    CHECK(ValidCommandShape(menuCommand));
    f.Hotkey(InputAction::SweepAbility); f.Event(ftxui::Event::ArrowRight); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 2);
    CHECK(f.commands.back().ability == menuCommand.ability);
}

TEST_CASE("Terminal combat targeting honors rebound menu accept and cancel", "[terminal][combat][input]") {
    CombatFixture f; f.Ability("sweep", AbilityTargetKind::CardinalDirection);
    f.keys = ResolveKeymap({{InputContext::Menu, InputAction::Accept, {{"v"}}},
        {InputContext::Menu, InputAction::Cancel, {{"x"}}}}, {});
    REQUIRE(ValidateKeymap(f.keys));
    f.Hotkey(InputAction::SweepAbility); f.Event(ftxui::Event::ArrowUp);
    f.Event(ftxui::Event::Return); CHECK(f.commands.empty());
    f.Event(ftxui::Event::Character("x")); CHECK_FALSE(f.ui.IsOpen()); CHECK(f.commands.empty());
    f.Hotkey(InputAction::SweepAbility); f.Event(ftxui::Event::ArrowUp);
    f.Event(ftxui::Event::Character("v"));
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands.back().ability.target.direction == CardinalDirection::North);
}

TEST_CASE("Terminal leap cursor stays within two tiles on the same level and invalidates stale prompts", "[terminal][combat]") {
    CombatFixture f; f.Ability("leap", AbilityTargetKind::Tile);
    f.Hotkey(InputAction::LeapAbility); f.Event(ftxui::Event::Return); CHECK(f.commands.empty());
    for (int i = 0; i < 4; ++i) { f.Event(ftxui::Event::ArrowRight); }
    f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands.back().ability.target.tile == TileCoord{2, 0, 3});
    f.Hotkey(InputAction::LeapAbility); f.Event(ftxui::Event::ArrowLeft);
    ++f.view.opportunity; f.Event(ftxui::Event::Return);
    CHECK(f.commands.size() == 1); CHECK_FALSE(f.ui.IsOpen());
    f.Hotkey(InputAction::LeapAbility); f.Event(ftxui::Event::ArrowLeft);
    ++f.view.characterId; f.Event(ftxui::Event::Return);
    CHECK(f.commands.size() == 1); CHECK_FALSE(f.ui.IsOpen());
    f.Hotkey(InputAction::LeapAbility); f.Event(ftxui::Event::ArrowLeft);
    f.view.combatInfo.abilities.clear(); f.Event(ftxui::Event::Return);
    CHECK(f.commands.size() == 1); CHECK_FALSE(f.ui.IsOpen());
}

TEST_CASE("Terminal equipment keys use shared typed actions", "[terminal][combat][inventory]") {
    CombatFixture f;
    InventoryEntry item; item.id = 90; item.type = CombatContent().FindItem("rock"); REQUIRE(item.type.value);
    item.heldHands = HandMask::Right; f.view.inventory.push_back(item);
    struct Expected {
        const char* key;
        EquipmentOperation operation;
        PersistentId item;
        Hand hand;
        GripKind grip;
    };
    const Expected actions[] = {
        {"L", EquipmentOperation::Hold, 90, Hand::Left, GripKind::OneHanded},
        {"R", EquipmentOperation::Hold, 90, Hand::Right, GripKind::OneHanded},
        {"H", EquipmentOperation::Hold, 90, Hand::Right, GripKind::TwoHanded},
        {"G", EquipmentOperation::Regrip, 0, Hand::Right, GripKind::TwoHanded},
        {"W", EquipmentOperation::Wear, 90, Hand::Left, GripKind::OneHanded},
        {"T", EquipmentOperation::Remove, 90, Hand::Left, GripKind::OneHanded},
        {"U", EquipmentOperation::Release, 0, Hand::Right, GripKind::OneHanded},
    };
    for (std::size_t i = 0; i < std::size(actions); ++i) {
        const auto& expected = actions[i]; INFO(expected.key);
        f.ui.ResetItems(); f.ui.Open(InputAction::Inventory); f.Draw();
        f.Event(ftxui::Event::Character(expected.key));
        REQUIRE(f.commands.size() == i + 1); CHECK(f.commands.back().kind == CommandKind::equipment);
        CHECK(f.commands.back().equipment.operation == expected.operation);
        CHECK(f.commands.back().equipment.item == expected.item);
        CHECK(f.commands.back().equipment.hand == expected.hand);
        CHECK(f.commands.back().equipment.grip == expected.grip);
        CHECK(ValidCommandShape(f.commands.back()));
    }
    CHECK(f.commands.size() == 7);
    // Regrip toggles the selected two-handed item's primary hand back to 1H.
    f.view.inventory.front().heldHands = HandMask::Both;
    f.ui.ResetItems(); f.ui.Open(InputAction::Inventory); f.Draw();
    f.Event(ftxui::Event::Character("G"));
    REQUIRE(f.commands.size() == 8);
    CHECK(f.commands.back().equipment.operation == EquipmentOperation::Regrip);
    CHECK(f.commands.back().equipment.item == 0);
    CHECK(f.commands.back().equipment.hand == Hand::Left);
    CHECK(f.commands.back().equipment.grip == GripKind::OneHanded);
    CHECK(ValidCommandShape(f.commands.back()));
}

TEST_CASE("Terminal combat serial cursor deduplicates snapshots suppresses commitments and survives new avatars", "[terminal][combat][messages]") {
    CombatFixture f; MessageHistory history;
    CombatEvent event; event.serial = 1; event.kind = CombatEventKind::ActionCommitted;
    f.view.combatEvents.push_back(event);
    event.serial = 2; event.kind = CombatEventKind::ActorDied; event.actor = 30;
    f.view.combatEvents.push_back(event);
    f.ui.RecordCombatEvents(f.view, CombatContent(), history);
    REQUIRE(history.Entries().size() == 1);
    CHECK(history.Entries().front() == CombatEventText(event, CombatContent()));
    ++f.view.revision;
    f.ui.RecordCombatEvents(f.view, CombatContent(), history); CHECK(history.Entries().size() == 1);
    event.serial = 3; event.actor = 31; f.view.combatEvents.push_back(event);
    ++f.view.characterId;
    f.ui.RecordCombatEvents(f.view, CombatContent(), history); CHECK(history.Entries().size() == 1);
    event.serial = 4; event.actor = 32; f.view.combatEvents.push_back(event);
    f.ui.RecordCombatEvents(f.view, CombatContent(), history); REQUIRE(history.Entries().size() == 2);
    f.ui.ResetItems(); f.view.combatEvents.clear(); event.serial = 1; event.actor = 40;
    f.view.combatEvents.push_back(event);
    f.ui.RecordCombatEvents(f.view, CombatContent(), history); REQUIRE(history.Entries().size() == 3);
    CHECK(history.Entries().back() == CombatEventText(event, CombatContent()));
}

}
