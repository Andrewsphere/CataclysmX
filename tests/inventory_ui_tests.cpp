#include "client/item_ui.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
struct InventoryUiFixture {
    ItemUi ui;
    WorldView view;
    Localization loc;
    Keymap keys = DefaultKeymap();
    InventoryUiFixture() { REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en")); view.playerId = 1; }
    InventoryEntry Rock(PersistentId id, ItemStorage storage = {}) {
        InventoryEntry i; i.id = id; i.type = TestItem("rock"); i.stackable = true;
        i.storage = storage; i.massGrams = 12; i.volumeMl = 3; return i;
    }
    void Refresh() { ui.Refresh(view, TestContent(), loc, keys); }
    void Open(bool advanced = false) { ui.Open(advanced); Refresh(); }
    void Key(const char* key) { ui.Handle({key}, keys, view, TestContent(), loc); }
    void Finished(Outcome outcome = Outcome::transferred, Tick cost = 10) {
        CommandResult result; result.outcome = outcome; result.cost = cost;
        result.advanced = outcome == Outcome::transferred; result.inventoryOutcome = InventoryOutcome::NoCapacity;
        ui.CommandFinished(result);
    }
};

TEST_CASE("Shared inventory groups only interchangeable instances and retains exact identities", "[client][inventory]") {
    InventoryUiFixture f;
    auto wielded = f.Rock(2); wielded.wielded = true;
    auto unique = f.Rock(3); unique.stackable = false;
    f.view.inventory = {f.Rock(9), f.Rock(4), wielded, unique}; f.Open();
    const auto& rows = f.ui.Frame().panes[0].rows;
    REQUIRE(rows.size() == 3); REQUIRE(rows[0].items.size() == 2);
    CHECK(rows[0].items[0] == 4); CHECK(rows[0].items[1] == 9);
    CHECK(rows[0].massGrams == 24); CHECK(rows[0].volumeMl == 6); CHECK(rows[1].wielded);
    f.Key("e"); CHECK(f.ui.Frame().screen == ItemUiScreen::Examine);
    f.Key("Escape"); f.Key("d");
    auto command = f.ui.TakeCommand(f.view); REQUIRE(command); CHECK(command->item == 4);
    CHECK(command->kind == CommandKind::drop); CHECK_FALSE(f.ui.TakeCommand(f.view));
}

TEST_CASE("A grouped selected instance vanishing does not select another member", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4), f.Rock(9)}; f.Open();
    f.view.inventory.erase(f.view.inventory.begin()); f.Refresh();
    CHECK_FALSE(f.ui.Frame().panes[0].selectionValid);
    f.Key("d"); CHECK_FALSE(f.ui.TakeCommand(f.view));
    f.Key("Down"); f.Key("d"); auto command = f.ui.TakeCommand(f.view);
    REQUIRE(command); CHECK(command->item == 9);
}

TEST_CASE("Examination reports physical and normal player melee stats and returns without mutation", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4)}; f.Open(); f.Key("Enter");
    REQUIRE(f.ui.Frame().screen == ItemUiScreen::Examine);
    const auto& detail = f.ui.Frame().examination;
    CHECK(detail.name == "rock"); CHECK(detail.type == TestItem("rock"));
    bool damage = false, cost = false, mass = false;
    for (const auto& line : detail.stats) {
        damage |= line.find("Normal player damage:") != String::npos;
        cost |= line.find("Attack cost:") != String::npos; mass |= line.find("Mass:") != String::npos;
    }
    CHECK(damage); CHECK(cost); CHECK(mass);
    f.Key("Down"); CHECK(f.ui.Frame().examination.scroll == 1);
    f.Key("Escape"); CHECK(f.ui.Frame().screen == ItemUiScreen::Inventory);
    CHECK(f.ui.Frame().panes[0].selectionValid); CHECK_FALSE(f.ui.TakeCommand(f.view));
}

TEST_CASE("Advanced inventory swaps entire panes when a location is selected twice", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4)};
    const ItemStorage floor{ItemStorageKind::Ground, f.view.player_position, 0};
    f.view.nearbyItems = {f.Rock(10, floor)}; f.Open(true);
    f.ui.Frame().panes[0].scroll = 3; f.ui.Frame().panes[1].scroll = 7;
    f.Key("5");
    CHECK(f.ui.Frame().panes[0].storage == floor); CHECK(f.ui.Frame().panes[0].scroll == 7);
    CHECK(f.ui.Frame().panes[1].storage == ItemStorage{}); CHECK(f.ui.Frame().panes[1].scroll == 3);
    f.Refresh(); CHECK(f.ui.Frame().panes[0].rows[0].items[0] == 10);
    f.Key("I"); CHECK(f.ui.Frame().panes[0].storage == ItemStorage{});
}

TEST_CASE("Examination localizes descriptions borrows colored art and preserves fractional HP", "[client][inventory]") {
    const ContentSource source{"examine.json5", R"({format_version:1,definitions:[
        {type:'item',id:'probe',name:'probe',description:'Authored prose',mass_g:7,volume_ml:9,
         art:['<red>AB<reset> C'],
         melee:{damage:[{type:'piercing',amount:425,penetration:125}],attack_cost:80,hands:2}},
        {type:'item',id:'plain',name:'plain',mass_g:1,volume_ml:1}
    ]})"};
    auto content = LoadContent({&source, 1}); REQUIRE(content);
    Localization loc; ItemExamination output; InventoryEntry item; item.type = content->FindItem("probe");
    DescribeInventoryItem(item, *content, loc, output);
    CHECK(output.description == "Authored prose"); REQUIRE(output.art);
    CHECK(output.art == &content->GetItem(item.type).art);
    CHECK(output.art->lines[0][0].color == ArtColor::Rgb(220, 83, 83));
    bool precise = false;
    for (const auto& stat : output.stats) {
        precise |= stat.find("Piercing damage: 4.25 HP | Penetration: 1.25 HP | Normal player damage: 4.25 HP") != String::npos;
    }
    CHECK(precise);
    REQUIRE(loc.SetCatalogs({}, {{"item.probe.description", "Translated prose", {}}}));
    DescribeInventoryItem(item, *content, loc, output); CHECK(output.description == "Translated prose");
    item.type = content->FindItem("plain"); DescribeInventoryItem(item, *content, loc, output);
    CHECK(output.description.empty()); CHECK(output.art == nullptr);
}

TEST_CASE("Tile selection prefers accessible object storage and floor toggle is explicit", "[client][inventory]") {
    InventoryUiFixture f; f.view.width = 3; f.view.height = 3; f.view.origin = {-1, -1}; f.view.cells.resize(9);
    f.view.cells[4].object = 90;
    ItemStorageView object; object.storage = {ItemStorageKind::Object, {}, 90}; object.accessible = true;
    f.view.itemStorages.push_back(object); f.Open(true); f.Key("5");
    CHECK(f.ui.Frame().panes[0].storage == object.storage);
    f.Key("f"); CHECK(f.ui.Frame().panes[0].storage.kind == ItemStorageKind::Ground);
    f.Key("f"); CHECK(f.ui.Frame().panes[0].storage == object.storage);
}

TEST_CASE("Quantity transfers validate cancel and capture ascending IDs with exact storage", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(9), f.Rock(4), f.Rock(7)}; f.Open(true);
    f.Key("m"); f.Key("0"); f.Key("Enter"); CHECK_FALSE(f.ui.TakeCommand(f.view));
    CHECK(f.ui.Frame().screen == ItemUiScreen::Amount);
    f.Key("Escape"); CHECK(f.ui.Frame().screen == ItemUiScreen::Advanced);
    f.Key("m"); f.Key("2"); f.Key("Enter"); CHECK(f.ui.NeedsTime());
    f.view.remainingTicks = 1; CHECK_FALSE(f.ui.TakeCommand(f.view)); f.view.remainingTicks = 0;
    auto command = f.ui.TakeCommand(f.view); REQUIRE(command); CHECK(command->item == 4);
    CHECK(command->source == ItemStorage{});
    CHECK(command->destination == f.ui.Frame().panes[1].storage);
    CommandResult queued; queued.outcome = Outcome::queued; f.ui.CommandFinished(queued);
    CHECK_FALSE(f.ui.TakeCommand(f.view));
    f.Finished(); CHECK(f.ui.NeedsTime()); CHECK_FALSE(f.ui.TakeCommand(f.view));
    ++f.view.revision; f.view.remainingTicks = 1; f.Refresh(); CHECK_FALSE(f.ui.TakeCommand(f.view));
    ++f.view.tick; f.view.remainingTicks = 0; f.Refresh();
    command = f.ui.TakeCommand(f.view); REQUIRE(command); CHECK(command->item == 7);
    f.Finished(Outcome::inventory_refused, 0); CHECK_FALSE(f.ui.NeedsTime());
    CHECK(f.ui.Frame().feedback.find("Moved 1/2") != String::npos); CHECK(f.ui.IsOpen());
}

TEST_CASE("Bulk cancellation preserves the outstanding move and drains its final paid time", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4), f.Rock(7)}; f.Open(true); f.Key("M");
    REQUIRE(f.ui.TakeCommand(f.view)); f.Key("Escape"); f.Finished(); CHECK(f.ui.NeedsTime());
    CHECK_FALSE(f.ui.TakeCommand(f.view)); ++f.view.revision; f.view.remainingTicks = 10; f.Refresh(); CHECK(f.ui.NeedsTime());
    ++f.view.tick; f.view.remainingTicks = 0; f.Refresh(); CHECK_FALSE(f.ui.NeedsTime());
    CHECK_FALSE(f.ui.TakeCommand(f.view)); CHECK(f.ui.Frame().feedback.find("Moved 1/2") != String::npos);
}

TEST_CASE("A portable container is one transfer and cannot be moved into its own tree", "[client][inventory]") {
    InventoryUiFixture f;
    auto pack = f.Rock(4); pack.type = TestItem("backpack"); pack.stackable = false; pack.capacityVolumeMl = 100;
    auto child = f.Rock(7, {ItemStorageKind::Container, {}, 4}); child.parent = 4;
    f.view.inventory = {pack, child}; f.Open(true);
    REQUIRE(f.ui.Frame().panes[0].rows.size() == 1);
    f.Key("o"); f.Refresh(); REQUIRE(f.ui.Frame().panes[0].rows.size() == 1);
    CHECK(f.ui.Frame().panes[0].rows[0].items[0] == 7); f.Key("Backspace"); f.Refresh();
    f.Key("Enter"); auto command = f.ui.TakeCommand(f.view); REQUIRE(command); CHECK(command->item == 4);
    CHECK_FALSE(f.ui.TakeCommand(f.view));
}

TEST_CASE("Ordinary inventory closes on final success and not on admission or failure", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4)}; f.Open(); f.Key("d"); REQUIRE(f.ui.TakeCommand(f.view));
    CHECK(f.ui.IsOpen()); f.Finished(Outcome::not_carried, 0); CHECK(f.ui.IsOpen());
    f.Key("d"); REQUIRE(f.ui.TakeCommand(f.view)); f.Finished(); CHECK_FALSE(f.ui.IsOpen()); CHECK(f.ui.NeedsTime());
    ++f.view.revision; f.Refresh(); CHECK_FALSE(f.ui.NeedsTime());
}

TEST_CASE("Advanced inventory refuses self ancestry and never redirects a changed source", "[client][inventory]") {
    InventoryUiFixture f;
    auto pack = f.Rock(4); pack.type = TestItem("backpack"); pack.stackable = false; pack.capacityVolumeMl = 100;
    f.view.inventory = {pack}; f.Open(true); f.Key("o"); f.Key("Tab"); f.Key("I"); f.Refresh();
    REQUIRE(f.ui.Frame().panes[1].storage == ItemStorage{});
    REQUIRE(f.ui.Frame().panes[0].storage.kind == ItemStorageKind::Container);
    f.Key("Enter"); CHECK_FALSE(f.ui.TakeCommand(f.view));
    CHECK(f.ui.Frame().feedback.find("descendants") != String::npos);
    f.ui.Close(); f.ui = {}; f.Open(true); f.Key("Enter");
    f.view.inventory[0].storage = {ItemStorageKind::Container, {}, 99};
    CHECK_FALSE(f.ui.TakeCommand(f.view)); CHECK_FALSE(f.ui.NeedsTime());
    CHECK(f.ui.Frame().feedback.find("Moved 0/1") != String::npos);
}

TEST_CASE("Advanced totals expose only actual source limits and player changes cancel prompts", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4)};
    ItemStorageView floor; floor.storage = {ItemStorageKind::Ground, f.view.player_position, 0};
    floor.accessible = true; floor.massGrams = 24; floor.volumeMl = 6;
    f.view.itemStorages.push_back(floor); f.Open(true);
    CHECK(f.ui.Frame().panes[1].capacity == "24 g | 6 mL");
    f.Key("m"); ++f.view.playerId; f.Key("2"); CHECK_FALSE(f.ui.IsOpen());
    CHECK_FALSE(f.ui.TakeCommand(f.view)); CHECK_FALSE(f.ui.NeedsTime());
}

TEST_CASE("Inventory measurements retain exact gram and milliliter precision", "[client][inventory]") {
    CHECK(ItemMassText(0) == "0 g");
    CHECK(ItemMassText(999) == "999 g");
    CHECK(ItemMassText(1000) == "1 kg");
    CHECK(ItemMassText(1050) == "1.05 kg");
    CHECK(ItemMassText(UINT64_MAX) == "18446744073709551.615 kg");
    CHECK(ItemVolumeText(1) == "1 mL");
    CHECK(ItemVolumeText(2000) == "2 L");
    CHECK(ItemVolumeText(2001) == "2.001 L");
}

TEST_CASE("Inventory outcome localization handles equipment transfers and refusal reasons", "[client][inventory]") {
    InventoryUiFixture f;
    for (const auto outcome : {Outcome::wielded, Outcome::unwielded, Outcome::transferred}) {
        CommandResult result; result.outcome = outcome;
        CHECK(f.loc.ResultText(result) == OutcomeMessage(outcome));
    }
    CommandResult refused; refused.outcome = Outcome::inventory_refused;
    refused.inventoryOutcome = InventoryOutcome::NoCapacity;
    CHECK(f.loc.ResultText(refused) == InventoryOutcomeMessage(InventoryOutcome::NoCapacity));
}

TEST_CASE("Inventory stale selection needs navigation even if the same item becomes visible again", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4)}; f.Open();
    f.view.inventory.clear(); f.Refresh();
    f.view.inventory = {f.Rock(4)}; f.Refresh();
    f.Key("w"); CHECK_FALSE(f.ui.TakeCommand(f.view));
    f.Key("Down"); f.Key("w");
    auto command = f.ui.TakeCommand(f.view); REQUIRE(command); CHECK(command->item == 4);
}

TEST_CASE("Inventory panes identify tile directions and ordinary ground rows", "[client][inventory]") {
    InventoryUiFixture f; f.Open(true);
    CHECK(f.ui.Frame().panes[1].title == "[5] Here — Floor");
    f.Key("6"); f.Refresh(); CHECK(f.ui.Frame().panes[0].title == "[6] East — Floor");
    f.ui.Close();
    auto rock = f.Rock(4, {ItemStorageKind::Ground, f.view.player_position});
    f.view.groundItems = {rock}; f.Open();
    CHECK(f.ui.Frame().panes[0].rows[0].name == "rock [ground]");
}

TEST_CASE("Advanced furniture selection is independent of map viewport radius and layer", "[client][inventory]") {
    InventoryUiFixture f;
    ItemStorageView cabinet; cabinet.storage = {ItemStorageKind::Object, {}, 90};
    cabinet.objectPosition = {1, 0, 0}; cabinet.accessible = true; cabinet.name = "Cabinet";
    f.view.itemStorages.push_back(cabinet);
    // No map cells are needed; the inspected map can even be on another layer.
    f.view.origin.y = 1; f.Open(true); f.Key("6"); f.Refresh();
    CHECK(f.ui.Frame().panes[0].storage == cabinet.storage);
    CHECK(f.ui.Frame().panes[0].title == "[6] East — Cabinet");
    f.Key("f"); f.Refresh();
    CHECK(f.ui.Frame().panes[0].storage == ItemStorage{ItemStorageKind::Ground, {1, 0, 0}});
    f.Key("f"); f.Refresh(); CHECK(f.ui.Frame().panes[0].storage == cabinet.storage);
}
}
