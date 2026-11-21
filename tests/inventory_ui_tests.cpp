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
    void FloorDestination() { Key("Right"); Key("5"); Key("Left"); Refresh(); }
    ItemStorage WorldStorage(ItemStorageKind kind, TileCoord position, PersistentId id = 0,
        const char* name = "", bool accessible = true) {
        ItemStorageView meta;
        meta.storage = {kind, kind == ItemStorageKind::Ground ? position : TileCoord{}, id};
        meta.objectPosition = position; meta.name = name; meta.accessible = accessible;
        view.itemStorages.push_back(meta); return meta.storage;
    }
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

TEST_CASE("Examination reports physical and authored melee stats and returns without mutation", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(4)}; f.Open(); f.Key("Enter");
    REQUIRE(f.ui.Frame().screen == ItemUiScreen::Examine);
    const auto& detail = f.ui.Frame().examination;
    CHECK(detail.name == "rock"); CHECK(detail.type == TestItem("rock"));
    bool damage = false, cost = false, mass = false;
    for (const auto& line : detail.stats) {
        damage |= line.find("damage:") != String::npos && line.find("authored base") != String::npos;
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
    f.view.nearbyItems = {f.Rock(10, floor)}; f.Open(true); f.FloorDestination();
    f.ui.Frame().panes[0].scroll = 3; f.ui.Frame().panes[1].scroll = 7;
    f.Key("5");
    CHECK(f.ui.Frame().panes[0].storage == floor); CHECK(f.ui.Frame().panes[0].scroll == 7);
    CHECK(f.ui.Frame().panes[1].storage == ItemStorage{}); CHECK(f.ui.Frame().panes[1].scroll == 3);
    f.Refresh(); CHECK(f.ui.Frame().panes[0].rows[0].items[0] == 10);
    f.Key("5");
    CHECK(f.ui.Frame().activePane == 0);
    CHECK(f.ui.Frame().panes[0].storage == ItemStorage{}); CHECK(f.ui.Frame().panes[0].scroll == 3);
    CHECK(f.ui.Frame().panes[1].storage == floor); CHECK(f.ui.Frame().panes[1].scroll == 7);
    CHECK(f.ui.Frame().panes[1].selectionValid);
    f.Key("I"); CHECK(f.ui.Frame().panes[0].storage == ItemStorage{});
}

TEST_CASE("Advanced inventory arrows focus a side and keypad digits point it at a tile", "[client][inventory]") {
    InventoryUiFixture f;
    f.view.player_position = {17, 23, 2};
    const TileCoord northeast{18, 22, 2};
    const auto cupboard = f.WorldStorage(ItemStorageKind::Object, northeast, 90, "cupboard");
    f.view.inventory = {f.Rock(4)};
    f.Open(true);
    const auto& frame = f.ui.Frame();
    CHECK(frame.advanced);
    CHECK(frame.panes[0].anchor == f.view.player_position);
    CHECK(frame.panes[1].anchor == f.view.player_position);
    CHECK(frame.footer.find("Left left pane") != String::npos);

    f.Key("Right"); f.Key("Right"); f.Key("9"); f.Refresh();
    f.Key("Tab"); f.Key("BackTab");
    CHECK(frame.activePane == 1);
    CHECK(frame.panes[0].anchor == f.view.player_position);
    CHECK(frame.panes[1].storage == cupboard);
    CHECK(frame.panes[1].anchor == northeast);
    f.Key("Left"); f.Key("Left"); f.Key("9"); f.Refresh();
    CHECK(frame.activePane == 0);
    CHECK(frame.panes[0].anchor == northeast);
    CHECK(frame.panes[1].anchor == f.view.player_position);

    f.view.itemStorages.clear(); f.Refresh();
    CHECK_FALSE(frame.panes[0].anchor);
    f.Key("1"); f.Refresh();
    CHECK(frame.panes[0].anchor == TileCoord{16, 24, 2});

    f.Key("Right"); f.ui.Frame().panes[1].scroll = 4;
    f.Key("e"); f.Key("Left"); f.Key("6"); f.Refresh();
    CHECK(frame.screen == ItemUiScreen::Examine);
    CHECK(frame.advanced);
    CHECK(frame.activePane == 1);
    CHECK(frame.panes[1].anchor == f.view.player_position);
    f.Key("Escape"); f.Refresh();
    CHECK(frame.screen == ItemUiScreen::Advanced);
    CHECK(frame.activePane == 1);
    CHECK(frame.panes[1].scroll == 4);
    REQUIRE(frame.panes[1].selectionValid);
    CHECK(frame.panes[1].rows[frame.panes[1].selected].items.front() == 4);
    SetBinding(f.keys, {InputContext::Menu, InputAction::West, {{"h"}}});
    f.Key("h");
    CHECK(frame.activePane == 0);
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
        precise |= stat.find("Piercing damage: 4.25 HP | Penetration: 1.25 HP (authored base; actual damage depends on move, grip, defense and armor)") != String::npos;
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
    f.Key("5");
    CHECK(f.ui.Frame().panes[0].mode == ItemUiPaneMode::AllNearby);
    CHECK(f.ui.Frame().panes[1].storage.kind == ItemStorageKind::Ground);
    f.Key("Right"); f.Key("5"); f.Key("Left");
    f.Key("f"); CHECK(f.ui.Frame().panes[0].storage == object.storage);
    f.Key("f");
    f.Key("Right"); f.Key("5");
    CHECK(f.ui.Frame().panes[0].storage.kind == ItemStorageKind::Ground);
    CHECK(f.ui.Frame().panes[1].storage == object.storage);
    f.Key("5");
    CHECK(f.ui.Frame().activePane == 1);
    CHECK(f.ui.Frame().panes[0].storage == object.storage);
    CHECK(f.ui.Frame().panes[1].storage.kind == ItemStorageKind::Ground);
}

TEST_CASE("Quantity transfers validate cancel and capture ascending IDs with exact storage", "[client][inventory]") {
    InventoryUiFixture f; f.view.inventory = {f.Rock(9), f.Rock(4), f.Rock(7)}; f.Open(true); f.FloorDestination();
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
    InventoryUiFixture f; f.view.inventory = {f.Rock(4), f.Rock(7)}; f.Open(true); f.FloorDestination(); f.Key("M");
    REQUIRE(f.ui.TakeCommand(f.view)); f.Key("Escape"); f.Finished(); CHECK(f.ui.NeedsTime());
    CHECK_FALSE(f.ui.TakeCommand(f.view)); ++f.view.revision; f.view.remainingTicks = 10; f.Refresh(); CHECK(f.ui.NeedsTime());
    ++f.view.tick; f.view.remainingTicks = 0; f.Refresh(); CHECK_FALSE(f.ui.NeedsTime());
    CHECK_FALSE(f.ui.TakeCommand(f.view)); CHECK(f.ui.Frame().feedback.find("Moved 1/2") != String::npos);
}

TEST_CASE("A portable container is one transfer and cannot be moved into its own tree", "[client][inventory]") {
    InventoryUiFixture f;
    auto pack = f.Rock(4); pack.type = TestItem("backpack"); pack.stackable = false; pack.capacityVolumeMl = 100;
    auto child = f.Rock(7, {ItemStorageKind::Container, {}, 4}); child.parent = 4;
    f.view.inventory = {pack, child}; f.Open(true); f.FloorDestination();
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
    f.view.inventory = {pack}; f.Open(true); f.Key("o"); f.Key("Right"); f.Key("I"); f.Refresh();
    REQUIRE(f.ui.Frame().panes[1].storage == ItemStorage{});
    REQUIRE(f.ui.Frame().panes[0].storage.kind == ItemStorageKind::Container);
    f.Key("Enter"); CHECK_FALSE(f.ui.TakeCommand(f.view));
    CHECK(f.ui.Frame().feedback.find("descendants") != String::npos);
    f.ui.Close(); f.ui = {}; f.Open(true); f.FloorDestination(); f.Key("Enter");
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
    CHECK(f.ui.Frame().panes[1].title == "All nearby");
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

TEST_CASE("All nearby defaults group by concrete source and sum source metadata once", "[client][inventory]") {
    InventoryUiFixture f;
    const auto floor = f.WorldStorage(ItemStorageKind::Ground, {-1, -1});
    const auto cupboard = f.WorldStorage(ItemStorageKind::Object, {1, 0, 0}, 90, "cupboard");
    const auto closed = f.WorldStorage(ItemStorageKind::Object, {0, -1}, 91, "closed", false);
    const auto upstairs = f.WorldStorage(ItemStorageKind::Ground, {0, 0, 1});
    const auto distant = f.WorldStorage(ItemStorageKind::Ground, {2, 0, 0});
    f.view.itemStorages[0].massGrams = 24; f.view.itemStorages[0].volumeMl = 6;
    f.view.itemStorages[1].massGrams = 50; f.view.itemStorages[1].volumeMl = 9;
    f.view.itemStorages[1].volumeLimitMl = 100;
    auto pack = f.Rock(12, cupboard); pack.capacityVolumeMl = 100; pack.stackable = false;
    auto child = f.Rock(13, {ItemStorageKind::Container, {}, 12}); child.parent = 12;
    f.view.nearbyItems = {f.Rock(9, floor), f.Rock(4, floor), f.Rock(11, cupboard), pack, child,
        f.Rock(20, closed), f.Rock(21, upstairs), f.Rock(22, distant)};
    f.view.inventory = {f.Rock(2)}; f.Open(true);
    CHECK(f.ui.Frame().panes[0].mode == ItemUiPaneMode::Storage);
    CHECK(f.ui.Frame().panes[0].storage == ItemStorage{});
    const auto& pane = f.ui.Frame().panes[1];
    REQUIRE(pane.mode == ItemUiPaneMode::AllNearby); REQUIRE(pane.rows.size() == 3);
    CHECK(pane.rows[0].storage == floor); CHECK(pane.rows[0].items == Vector<PersistentId>{4, 9});
    CHECK(pane.rows[0].name == "rock [7 floor]");
    CHECK(pane.rows[1].storage == cupboard); CHECK(pane.rows[1].name == "rock [6 cupboard]");
    CHECK(pane.rows[1].items == Vector<PersistentId>{11});
    CHECK(pane.rows[2].items == Vector<PersistentId>{12});
    CHECK(pane.capacity == "74 g | 15 mL");
    f.Key("Right"); f.Key("M");
    auto command = f.ui.TakeCommand(f.view); REQUIRE(command);
    CHECK(command->item == 4); CHECK(command->source == floor); CHECK(command->destination == ItemStorage{});
    f.Finished(Outcome::transferred, 0); command = f.ui.TakeCommand(f.view); REQUIRE(command);
    CHECK(command->item == 9); CHECK(command->source == floor);
}

TEST_CASE("All nearby excludes both sources on the opposite container root tile and refreshes ancestry", "[client][inventory]") {
    InventoryUiFixture f;
    const auto floor = f.WorldStorage(ItemStorageKind::Ground, {});
    const auto object = f.WorldStorage(ItemStorageKind::Object, {}, 90, "cupboard");
    const auto east = f.WorldStorage(ItemStorageKind::Ground, {1, 0, 0});
    auto pack = f.Rock(4, floor); pack.capacityVolumeMl = 100; pack.stackable = false;
    auto inner = f.Rock(5, {ItemStorageKind::Container, {}, 4}); inner.parent = 4;
    inner.capacityVolumeMl = 100; inner.stackable = false;
    f.view.nearbyItems = {pack, inner, f.Rock(6, object), f.Rock(7, east)};
    f.Open(true); f.Key("5"); f.Key("f"); f.Key("o"); f.Key("o"); f.Refresh();
    REQUIRE(f.ui.Frame().panes[0].storage == ItemStorage{ItemStorageKind::Container, {}, 5});
    CHECK(f.ui.Frame().panes[0].anchor == floor.position);
    REQUIRE(f.ui.Frame().panes[1].rows.size() == 1);
    CHECK(f.ui.Frame().panes[1].rows[0].items.front() == 7);
    f.view.nearbyItems[0].storage = east; f.Refresh();
    CHECK(f.ui.Frame().panes[0].anchor == east.position);
    REQUIRE(f.ui.Frame().panes[1].rows.size() == 1);
    CHECK(f.ui.Frame().panes[1].rows[0].storage == object);
    f.Key("6"); f.Refresh();
    CHECK(f.ui.Frame().panes[0].mode == ItemUiPaneMode::AllNearby);
    CHECK(f.ui.Frame().panes[1].storage == ItemStorage{ItemStorageKind::Container, {}, 5});
    f.Key("Right"); f.Key("6"); f.Key("Left");
    f.view.inventory = {f.view.nearbyItems[0], f.view.nearbyItems[1]};
    f.view.inventory[0].storage = {};
    f.view.nearbyItems.erase(f.view.nearbyItems.begin(), f.view.nearbyItems.begin() + 2);
    f.Refresh(); CHECK(f.ui.Frame().panes[1].rows.size() == 2);
    CHECK(f.ui.Frame().panes[0].anchor == f.view.player_position);
    f.view.inventory.clear(); f.Refresh();
    CHECK_FALSE(f.ui.Frame().panes[0].anchor);
}

TEST_CASE("All nearby swaps complete panes retains stale identities and distinguishes inventory mode", "[client][inventory]") {
    InventoryUiFixture f; const auto floor = f.WorldStorage(ItemStorageKind::Ground, {});
    f.view.nearbyItems = {f.Rock(4, floor), f.Rock(9, floor)}; f.view.inventory = {f.Rock(2)};
    f.Open(true); f.ui.Frame().panes[0].scroll = 3; f.ui.Frame().panes[1].scroll = 7;
    f.Key("A");
    CHECK(f.ui.Frame().panes[0].mode == ItemUiPaneMode::AllNearby);
    CHECK(f.ui.Frame().panes[0].scroll == 7); CHECK(f.ui.Frame().panes[1].scroll == 3);
    f.view.nearbyItems.erase(f.view.nearbyItems.begin()); f.Refresh();
    CHECK_FALSE(f.ui.Frame().panes[0].selectionValid);
    f.Key("Enter"); CHECK_FALSE(f.ui.TakeCommand(f.view));
    f.Key("A"); CHECK_FALSE(f.ui.Frame().panes[0].selectionValid);
    f.Key("Down"); f.Refresh(); CHECK(f.ui.Frame().panes[0].selectionValid);
    f.ui.Close(); f.Open(true); CHECK(f.ui.Frame().panes[0].mode == ItemUiPaneMode::AllNearby);
    CHECK(f.ui.Frame().panes[0].rows[0].items.front() == 9);
    f.Key("Right"); f.Key("5"); f.Key("I"); f.Refresh();
    CHECK(f.ui.Frame().panes[1].mode == ItemUiPaneMode::Storage);
    CHECK(f.ui.Frame().panes[1].storage == ItemStorage{});
    CHECK(f.ui.Frame().panes[0].mode == ItemUiPaneMode::AllNearby);
}

TEST_CASE("All nearby opens exact containers and floor toggle uses the selected source", "[client][inventory]") {
    InventoryUiFixture f;
    const auto floor = f.WorldStorage(ItemStorageKind::Ground, {1, 0, 0});
    const auto object = f.WorldStorage(ItemStorageKind::Object, {1, 0, 0}, 90, "cupboard");
    auto pack = f.Rock(4, object); pack.capacityVolumeMl = 100; pack.stackable = false;
    auto child = f.Rock(5, {ItemStorageKind::Container, {}, 4}); child.parent = 4;
    f.view.nearbyItems = {pack, child}; f.Open(true); f.Key("Right"); f.Key("o"); f.Refresh();
    CHECK(f.ui.Frame().panes[1].storage == child.storage);
    REQUIRE(f.ui.Frame().panes[1].rows.size() == 1);
    CHECK(f.ui.Frame().panes[1].rows[0].items.front() == 5);
    f.Key("Backspace"); CHECK(f.ui.Frame().panes[1].storage == object);
    f.Key("A"); f.Refresh(); f.Key("f"); CHECK(f.ui.Frame().panes[1].storage == floor);
}

TEST_CASE("Transfers into All nearby choose empty concrete destinations without changing mode", "[client][inventory]") {
    for (const auto* action : {"Enter", "m", "M"}) {
        InventoryUiFixture f;
        const auto floor = f.WorldStorage(ItemStorageKind::Ground, {-1, -1});
        f.WorldStorage(ItemStorageKind::Object, {1, 0, 0}, 90, "cupboard");
        f.view.inventory = {f.Rock(9), f.Rock(4), f.Rock(7)}; f.Open(true); f.Key(action);
        REQUIRE(f.ui.Frame().screen == ItemUiScreen::Destination);
        REQUIRE(f.ui.Frame().choices.size() == 2);
        CHECK(f.ui.Frame().choices[0].find("[7]") != String::npos);
        CHECK_FALSE(f.ui.TakeCommand(f.view)); f.Key("Escape");
        CHECK(f.ui.Frame().screen == ItemUiScreen::Advanced); CHECK_FALSE(f.ui.NeedsTime());
        f.Key(action); f.Key("Enter");
        if (String(action) == "m") {
            REQUIRE(f.ui.Frame().screen == ItemUiScreen::Amount);
            CHECK(f.ui.Frame().choices.empty());
            f.Key("Escape"); CHECK_FALSE(f.ui.NeedsTime());
            f.Key(action); f.Key("Enter");
            // A vanished member outside the requested prefix does not block it.
            f.view.inventory.erase(f.view.inventory.begin());
            f.Key("2"); f.Key("Enter");
        }
        CHECK(f.ui.Frame().screen == ItemUiScreen::Advanced);
        CHECK(f.ui.Frame().panes[1].mode == ItemUiPaneMode::AllNearby);
        const auto command = f.ui.TakeCommand(f.view); REQUIRE(command);
        CHECK(command->item == 4); CHECK(command->source == ItemStorage{}); CHECK(command->destination == floor);
        f.Key("Escape"); f.Finished(Outcome::transferred, 0); CHECK_FALSE(f.ui.TakeCommand(f.view));
        CHECK_FALSE(f.ui.NeedsTime());
    }
}

TEST_CASE("All nearby destination prompts refuse stale captured sources and destinations", "[client][inventory]") {
    for (const bool changedSource : {false, true}) {
        InventoryUiFixture f;
        const auto floor = f.WorldStorage(ItemStorageKind::Ground, {});
        const auto east = f.WorldStorage(ItemStorageKind::Ground, {1, 0, 0});
        f.view.inventory = {f.Rock(4)}; f.Open(true); f.Key("Enter");
        REQUIRE(f.ui.Frame().screen == ItemUiScreen::Destination);
        if (changedSource) { f.view.inventory[0].storage = {ItemStorageKind::Container, {}, 99}; }
        else { f.view.itemStorages.erase(f.view.itemStorages.begin()); }
        f.Key("Enter"); CHECK_FALSE(f.ui.TakeCommand(f.view));
        CHECK(f.ui.Frame().screen == ItemUiScreen::Destination);
        CHECK(f.ui.Frame().feedback.find("no longer accessible") != String::npos);
        CHECK(floor != east);
    }
}

TEST_CASE("Captured quantity ancestry cannot follow a portable source moved during destination selection", "[client][inventory]") {
    InventoryUiFixture f;
    const auto floor = f.WorldStorage(ItemStorageKind::Ground, {});
    const auto east = f.WorldStorage(ItemStorageKind::Ground, {1, 0, 0});
    auto pack = f.Rock(4); pack.capacityVolumeMl = 100; pack.stackable = false;
    auto child = f.Rock(5, {ItemStorageKind::Container, {}, 4}); child.parent = 4;
    f.view.inventory = {pack, child}; f.Open(true); f.Key("o"); f.Key("m");
    REQUIRE(f.ui.Frame().screen == ItemUiScreen::Destination);
    f.view.inventory[0].storage = east; f.Key("Enter");
    CHECK_FALSE(f.ui.TakeCommand(f.view));
    CHECK(f.ui.Frame().screen == ItemUiScreen::Destination);
    CHECK(f.ui.Frame().feedback.find("ancestry") != String::npos);
    CHECK(floor != east);
}

TEST_CASE("Destination and amount confirmation revalidate captured storage accessibility", "[client][inventory]") {
    for (const bool amount : {false, true}) {
        InventoryUiFixture f; f.WorldStorage(ItemStorageKind::Ground, {});
        f.view.inventory = {f.Rock(4), f.Rock(9)}; f.Open(true);
        f.Key(amount ? "m" : "M"); f.Key("Enter");
        if (amount) {
            REQUIRE(f.ui.Frame().screen == ItemUiScreen::Amount);
            f.view.itemStorages[0].accessible = false; f.Key("1"); f.Key("Enter");
            CHECK(f.ui.Frame().screen == ItemUiScreen::Amount);
        } else {
            CHECK(f.ui.NeedsTime()); f.view.itemStorages.clear();
        }
        CHECK_FALSE(f.ui.TakeCommand(f.view));
    }
    InventoryUiFixture f; const auto floor = f.WorldStorage(ItemStorageKind::Ground, {});
    f.view.nearbyItems = {f.Rock(4, floor)}; f.Open(true); f.Key("Right"); f.Key("Enter");
    f.view.itemStorages.clear(); CHECK_FALSE(f.ui.TakeCommand(f.view));
    CHECK_FALSE(f.ui.NeedsTime());
}

TEST_CASE("All nearby action remains case-sensitive configurable and migration-safe", "[client][inventory][input]") {
    InventoryUiFixture f;
    CHECK(ResolveAction(f.keys, InputContext::Items, {"A"}) == InputAction::AllNearby);
    CHECK(ResolveAction(f.keys, InputContext::Items, {"a"}) == InputAction::Apply);
    CHECK(ActionName(InputAction::AllNearby) == "all_nearby");
    CHECK(ParseAction("all_nearby") == InputAction::AllNearby);
    f.keys = ResolveKeymap({{InputContext::Items, InputAction::AllNearby, {{"N"}}}}, {});
    CHECK(ValidateKeymap(f.keys)); f.Open(true); f.Key("N");
    CHECK(f.ui.Frame().panes[0].mode == ItemUiPaneMode::AllNearby);
    CHECK(f.ui.Frame().footer.find("N all nearby") != String::npos);
    const auto migrated = ResolveKeymap({{InputContext::Items, InputAction::Apply, {{"A"}}}}, {});
    CHECK(ValidateKeymap(migrated));
    CHECK(ResolveAction(migrated, InputContext::Items, {"A"}) == InputAction::Apply);
    CHECK(BindingHint(migrated, InputContext::Items, InputAction::AllNearby) == "—");
}
}
