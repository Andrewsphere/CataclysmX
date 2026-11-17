#include "client/combat_menu.h"
#include "client/combat_text.h"
#include "client/crafting_menu.h"
#include "client/item_ui.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

const ContentCatalog& TestContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto result = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(result);
        return eastl::move(*result);
    }();
    return content;
}
ItemTypeId TestItem(const char* id) {
    const auto type = TestContent().FindItem(id); REQUIRE(type.value); return type;
}

bool Contains(const String& text, const char* part) { return text.find(part) != String::npos; }

TEST_CASE("Combat menu exposes basic and learned styles without inventing art knowledge", "[client][combat-ui]") {
    const auto& content = TestContent();
    WorldView view; view.characterId = 10;
    Vector<CombatMenuEntry> rows;
    CollectCombatMenu(content, view, rows);
    REQUIRE(rows.size() == 3);
    for (const auto& row : rows) {
        CHECK(row.command.kind == CommandKind::select_combat_style);
        CHECK(row.enabled);
        CHECK(ValidCommandShape(row.command));
        CHECK(Contains(row.details, "weight"));
        CHECK(Contains(row.details, "stamina"));
        CHECK_FALSE(content.GetCombatStyle(row.command.combatStyle).art.value);
    }
    const auto art = content.FindCombatArt("boxing"); REQUIRE(art.value);
    view.combatInfo.arts.push_back({art, 0});
    CollectCombatMenu(content, view, rows);
    REQUIRE(rows.size() == 4);
    CHECK(rows.back().command.combatStyle == content.FindCombatStyle("boxing_guard"));
    view.remainingTicks = 12;
    CollectCombatMenu(content, view, rows);
    for (const auto& row : rows) { CHECK_FALSE(row.enabled); CHECK(Contains(row.details, "not ready")); }
    view.combatInfo.combat.life = ActorLifeState::Dead;
    CollectCombatMenu(content, view, rows);
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().command.kind == CommandKind::new_character);
    CHECK(rows.front().enabled);
    CHECK(ValidCommandShape(rows.front().command));
}

TEST_CASE("Combat ability choices preserve grant identity targeting costs and refusals", "[client][combat-ui]") {
    const auto& content = TestContent();
    WorldView view; view.characterId = 10;
    const auto focus = content.FindAbility("focus"); REQUIRE(focus.value);
    const auto boxing = content.FindCombatArt("boxing");
    const auto weapons = content.FindCombatArt("weapon_forms");
    AbilityView ability; ability.ability = focus; ability.source.art = boxing;
    ability.availability.cost = {15, 60, 8}; ability.cooldownRemaining = 120;
    ability.availability.reason = CombatRefusal::Cooldown;
    view.combatInfo.abilities.push_back(ability);
    ability.source.art = weapons; ability.availability.reason = CombatRefusal::None; ability.cooldownRemaining = 0;
    view.combatInfo.abilities.push_back(ability);
    ability.ability = content.FindAbility("leap"); ability.targeting = AbilityTargetKind::Tile;
    view.combatInfo.abilities.push_back(ability);
    ability.ability = content.FindAbility("sweep"); ability.targeting = AbilityTargetKind::CardinalDirection;
    view.combatInfo.abilities.push_back(ability);
    Vector<CombatMenuEntry> rows; CollectCombatMenu(content, view, rows);
    REQUIRE(rows.size() == 7);
    CHECK_FALSE(rows[3].enabled); CHECK(rows[4].enabled);
    CHECK(Contains(rows[3].details, "120")); CHECK(Contains(rows[3].details, "stamina 8"));
    CHECK(rows[3].command.ability.source.art == boxing);
    CHECK(rows[4].command.ability.source.art == weapons);
    CHECK_FALSE(SameCraftingChoice(rows[3].command, rows[4].command));
    CHECK(rows[5].targeting == AbilityTargetKind::Tile);
    CHECK(rows[6].targeting == AbilityTargetKind::CardinalDirection);
    for (const auto& row : rows) { CHECK(ValidCommandShape(row.command)); }
}

TEST_CASE("Art manual choices retain information identity and show repeated attempt policy", "[client][combat-ui]") {
    const auto& content = TestContent();
    WorldView view; view.characterId = 10;
    ArtManualView manual; manual.book = content.FindBook("boxing_manual_text");
    manual.art = content.FindCombatArt("boxing"); manual.successChance = 7525;
    manual.duration = 600; manual.remaining = 123; manual.reason = CombatRefusal::MissingReference;
    REQUIRE(manual.book.value); REQUIRE(manual.art.value);
    view.combatInfo.manuals.push_back(manual);
    Vector<CombatMenuEntry> rows; CollectCombatMenu(content, view, rows);
    REQUIRE(rows.size() == 4);
    CHECK_FALSE(rows.back().enabled);
    CHECK(rows.back().command.book == manual.book); CHECK(rows.back().command.art == manual.art);
    CHECK(Contains(rows.back().details, "75.25%")); CHECK(Contains(rows.back().details, "600"));
    CHECK(Contains(rows.back().details, "123")); CHECK(Contains(rows.back().details, "retried"));
    CHECK(Contains(rows.back().details, "manual required"));
    view.remainingTicks = 20; view.combatInfo.manuals.front().active = true;
    CollectCombatMenu(content, view, rows);
    REQUIRE(rows.size() == 5);
    CHECK(rows.back().command.kind == CommandKind::pause_art_study);
    CHECK(rows.back().enabled); CHECK(ValidCommandShape(rows.back().command));
}

TEST_CASE("Canonical events distinguish armor contact death and training suppression", "[client][combat-ui]") {
    const auto& content = TestContent();
    CombatEvent event; event.tick = 12; event.actor = 10; event.opponent = 20;
    event.strike.attacker = 10; event.strike.target = 20; event.strike.region = ContactRegion::Eyes;
    event.strike.outcome = StrikeOutcome::Contact; event.strike.response = DefenseKind::Block;
    event.strike.defenseSucceeded = true; event.strike.blockAbsorbed = 125;
    event.strike.armorStopped = true; event.strike.hpLoss = 0;
    const auto strike = CombatEventText(event, content);
    CHECK(Contains(strike, "eyes")); CHECK(Contains(strike, "block succeeded"));
    CHECK(Contains(strike, "1.25")); CHECK(Contains(strike, "armor stopped")); CHECK(Contains(strike, "HP lost 0.00"));
    event.kind = CombatEventKind::TrainingSuppressed;
    CHECK(Contains(CombatEventText(event, content), "no combat XP"));
    CHECK(Contains(CombatEventText(event, content), "rank too low"));
    event.kind = CombatEventKind::ArtStudyCompleted; event.art = content.FindCombatArt("boxing");
    CHECK(Contains(CombatEventText(event, content), "failed attempt"));
    event.learnedArt = true; CHECK(Contains(CombatEventText(event, content), "learned"));
    event.kind = CombatEventKind::ActorDied; CHECK(Contains(CombatEventText(event, content), "#10 died"));
    event.kind = CombatEventKind::ActionInvalidated; event.reason = CombatRefusal::MissingSource;
    CHECK(Contains(CombatEventText(event, content), "source missing"));
}

TEST_CASE("Player details show distinct facial and foot encumbrance and learned versus effective skills", "[client][combat-ui]") {
    const auto& content = TestContent();
    WorldView view; view.characterId = 10;
    view.combatInfo.equipment.encumbrance[static_cast<std::size_t>(ContactRegion::Eyes)] = 7;
    const auto text = CombatDetailsText(content, view);
    for (const char* region : {"head: 0", "eyes: 7", "mouth: 0", "ears: 0", "left foot: 0", "right foot: 0"}) {
        CHECK(Contains(text, region));
    }
    CHECK(Contains(text, "ceil(base cost x speed/100)"));
    CHECK(Contains(text, "effective weapon-family"));
    CHECK(Contains(text, "Learned levels unlock"));
}

TEST_CASE("Equipped inventory leaves stay separate and direct equipment actions are typed", "[client][combat-ui]") {
    const auto& content = TestContent();
    WorldView view; view.characterId = 10;
    InventoryEntry item; item.id = 1; item.type = TestItem("rock"); item.stackable = true;
    view.inventory.push_back(item); item.id = 2; view.inventory.push_back(item);
    item.id = 3; item.heldHands = HandMask::Left; view.inventory.push_back(item);
    item.id = 4; item.heldHands = HandMask::None; item.worn = true; view.inventory.push_back(item);
    Localization localization; const auto keys = DefaultKeymap();
    ItemUi ui; ui.Open(); ui.Refresh(view, content, localization, keys);
    REQUIRE(ui.Frame().panes[0].rows.size() == 3);
    CHECK(ui.Frame().panes[0].rows[0].items.size() == 2);
    CHECK(Contains(ui.Frame().panes[0].rows[1].name, "[L]"));
    CHECK(Contains(ui.Frame().panes[0].rows[2].name, "[worn]"));
    const ItemUiAction actions[] = {ItemUiAction::HoldLeft, ItemUiAction::HoldRight, ItemUiAction::TwoHanded,
        ItemUiAction::Regrip, ItemUiAction::Wear, ItemUiAction::Remove, ItemUiAction::Release};
    for (auto action : actions) {
        ItemUi selected; selected.Open();
        WorldView held; held.characterId = 10; item.id = 9; item.heldHands = HandMask::Right;
        held.inventory.push_back(item);
        selected.Refresh(held, content, localization, keys);
        selected.HandleAction(action, held);
        const auto command = selected.TakeCommand(held); REQUIRE(command);
        CHECK(command->kind == CommandKind::equipment); CHECK(ValidCommandShape(*command));
        if (action == ItemUiAction::HoldLeft) { CHECK(command->equipment.hand == Hand::Left); }
        if (action == ItemUiAction::HoldRight) { CHECK(command->equipment.hand == Hand::Right); }
        if (action == ItemUiAction::Regrip) { CHECK(command->equipment.operation == EquipmentOperation::Regrip); }
        if (action == ItemUiAction::Release) { CHECK(command->equipment.operation == EquipmentOperation::Release); }
    }
}

TEST_CASE("Armor inspection reports layer coverage and protection without player damage prediction", "[client][combat-ui]") {
    InventoryEntry item; item.id = 1; item.type = TestItem("cloth_face_wrap"); item.worn = true;
    ItemExamination inspection; DescribeInventoryItem(item, TestContent(), Localization{}, inspection);
    String text;
    for (const auto& line : inspection.stats) { text += line + "\n"; }
    CHECK(Contains(text, "Currently worn")); CHECK(Contains(text, "Armor layer: normal"));
    CHECK(Contains(text, "mouth | coverage 90.00%")); CHECK(Contains(text, "Slashing armor protection: 0.40 HP"));
    CHECK_FALSE(Contains(text, "Normal player damage"));
    CommandResult result; result.outcome = Outcome::combat_refused; result.combatReason = CombatRefusal::Exhausted;
    CHECK(Contains(Localization{}.ResultText(result), "insufficient stamina"));
    for (const auto outcome : {Outcome::equipment_changed, Outcome::ability_used, Outcome::art_study_started,
        Outcome::art_study_paused, Outcome::character_dead, Outcome::character_created}) {
        CHECK_FALSE(Localization{}.OutcomeText(outcome).empty());
    }
}

TEST_CASE("Hand-only commands never retarget a replacement item between input and issuance", "[client][combat-ui]") {
    WorldView view; view.characterId = 10;
    InventoryEntry item; item.id = 7; item.type = TestItem("rock"); item.heldHands = HandMask::Left;
    view.inventory.push_back(item);
    ItemUi ui; ui.Open(); ui.Refresh(view, TestContent(), Localization{}, DefaultKeymap());
    ui.HandleAction(ItemUiAction::Release, view);
    view.inventory.front().heldHands = HandMask::None;
    CHECK_FALSE(ui.TakeCommand(view));
    CHECK(Contains(ui.Frame().feedback, "source missing"));
    CHECK_FALSE(ui.NeedsTime());
}
}
