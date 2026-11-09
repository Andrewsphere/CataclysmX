#include "terminal/gameplay_screen.h"
#include "client/text.h"
#include "content_fixture.h"
#include "items/item_behavior.h"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>

namespace {
using namespace cataclysmx;

struct ItemUiFixture {
    GameplayUi ui;
    Localization localization;
    Keymap keys = DefaultKeymap();
    WorldView view;
    Vector<Command> commands;
    bool accepted = true;

    ItemUiFixture() { REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en")); }
    InventoryEntry Item(PersistentId id, eastl::string_view name) {
        InventoryEntry item; item.id = id; item.type = TestItem(name); return item;
    }
    ItemActionInfo Action(eastl::string_view type, eastl::string_view capability, eastl::string_view action) {
        const auto& definition = TestContent().GetItem(TestItem(type));
        const auto id = FindItemCapability(definition, capability);
        const auto* behavior = ItemBehaviorConfig(definition, id);
        REQUIRE(behavior);
        const auto* schema = behavior->FindAction(action);
        REQUIRE(schema);
        ItemActionInfo info;
        info.capability = id;
        info.action = schema->id;
        info.argument = schema->argument;
        info.category = schema->category;
        info.baseCost = schema->baseCost;
        info.label = schema->label;
        return info;
    }
    std::string Draw() {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(32));
        ftxui::Render(screen, ui.Render(view, TestContent(), localization, keys, {}));
        return screen.ToString();
    }
    void Event(const ftxui::Event& event) {
        REQUIRE(ui.HandleEvent(event, view, localization, keys, [&](Command command) {
            commands.push_back(command); return accepted;
        }, {}, &TestContent()));
    }
    void Open(InputAction action = InputAction::Inventory) { REQUIRE(ui.Open(action)); Draw(); }
};

TEST_CASE("Item use executes one operational action without the battery management menu", "[terminal][items]") {
    ItemUiFixture f;
    auto charger = f.Item(70, "field_charger");
    charger.actions = {f.Action("field_charger", "charger", "start"),
        f.Action("field_charger", "charger", "install")};
    f.view.inventory.push_back(charger);
    f.Open(); f.Event(ftxui::Event::Character('u'));
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].kind == CommandKind::use_item);
    CHECK(f.commands[0].item == 70);
    CHECK(f.commands[0].itemAction == "start");
    CHECK_FALSE(f.ui.IsOpen());
    CHECK(f.view.inventory[0] == charger);
}

TEST_CASE("Multiple functional uses require a menu choice and door arming collects direction", "[terminal][items]") {
    ItemUiFixture f;
    auto utility = f.Item(80, "utility_controller");
    utility.actions = {f.Action("utility_controller", "charger", "start"),
        f.Action("utility_controller", "timer", "arm")};
    f.view.groundItems.push_back(utility);
    f.Open(InputAction::Use); f.Event(ftxui::Event::Return);
    CHECK(f.commands.empty());
    f.Draw(); f.Event(ftxui::Event::ArrowDown); f.Event(ftxui::Event::Return);
    CHECK(f.commands.empty());
    f.Draw(); f.Event(ftxui::Event::ArrowLeft);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].item == 80);
    CHECK(f.commands[0].itemAction == "arm");
    CHECK(f.commands[0].dx == -1);
    CHECK(f.commands[0].dz == 0);
}

TEST_CASE("Generic item selection never substitutes a stale target", "[terminal][items]") {
    ItemUiFixture f;
    auto charger = f.Item(70, "field_charger");
    charger.actions = {f.Action("field_charger", "charger", "install")};
    f.view.inventory = {charger, f.Item(71, "disposable_battery"), f.Item(72, "rechargeable_battery"), f.Item(73, "rechargeable_battery")};
    f.Open(); f.Event(ftxui::Event::Character('a')); f.Event(ftxui::Event::Return);
    f.Event(ftxui::Event::ArrowDown); f.Event(ftxui::Event::ArrowDown);
    f.Draw();
    f.view.inventory.erase(f.view.inventory.begin() + 2);
    f.Draw(); f.Event(ftxui::Event::Return);
    CHECK(f.commands.empty());
    CHECK(f.ui.IsOpen());
    f.Event(ftxui::Event::ArrowDown); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].otherItem == 73);
}

TEST_CASE("Snapshots cannot replace an inventory source even when rendered before activation", "[terminal][items]") {
    ItemUiFixture f;
    f.view.inventory = {f.Item(10, "rock"), f.Item(11, "rock")};
    f.Open();
    f.view.inventory.erase(f.view.inventory.begin());
    f.Draw(); f.Event(ftxui::Event::Return); f.Event(ftxui::Event::Character('u'));
    CHECK(f.commands.empty());
    f.Event(ftxui::Event::ArrowDown); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].kind == CommandKind::drop);
    CHECK(f.commands[0].item == 11);
}

TEST_CASE("Generic item targets include incompatible items for authoritative refusal", "[terminal][items]") {
    ItemUiFixture f;
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto charger = world.CreateItem(TestItem("field_charger"), ItemLocation::Carried(server.GetPlayer()));
    const auto battery = world.CreateItem(TestItem("disposable_battery"), ItemLocation::Carried(server.GetPlayer()));
    REQUIRE(charger); REQUIRE(battery);
    const auto chargerId = world.GetItem(*charger)->id;
    const auto batteryId = world.GetItem(*battery)->id;
    auto entry = f.Item(chargerId, "field_charger");
    entry.actions = {f.Action("field_charger", "charger", "install")};
    f.view.inventory = {entry, f.Item(batteryId, "disposable_battery")};
    f.Open(); f.Event(ftxui::Event::Character('a')); f.Event(ftxui::Event::Return);
    f.Event(ftxui::Event::ArrowDown);
    CommandResult result;
    REQUIRE(f.ui.HandleEvent(ftxui::Event::Return, f.view, f.localization, f.keys, [&](Command command) {
        CHECK(command.otherItem == batteryId);
        result = server.Send(command);
        return result.outcome == Outcome::used_item;
    }));
    CHECK(result.outcome == Outcome::item_use_failed);
    CHECK(result.itemOutcome == ItemUseOutcome::Refused);
    CHECK_FALSE(result.itemReason.code.empty());
    CHECK_FALSE(f.localization.ResultText(result).empty());
    CHECK_FALSE(result.advanced); CHECK(result.cost == 0);
    CHECK(f.ui.IsOpen()); CHECK(f.view.inventory[0] == entry);
}

TEST_CASE("Unknown behavior-local actions use metadata labels categories and arguments", "[terminal][items]") {
    ItemUiFixture f;
    auto entry = f.Item(70, "field_charger");
    auto custom = f.Action("field_charger", "charger", "start");
    custom.action = "calibrate";
    custom.label = {"custom.calibrate", "Calibrate instrument"};
    auto other = custom;
    other.action = "measure"; other.label = {"custom.measure", "Measure signal"};
    entry.actions = {custom, other};
    f.view.inventory = {entry};
    f.Open(InputAction::Use); f.Event(ftxui::Event::Return);
    const auto rendered = f.Draw();
    CHECK(rendered.find("Calibrate instrument") != std::string::npos);
    CHECK(rendered.find("Measure signal") != std::string::npos);
    f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands.back().itemAction == "calibrate");
    CHECK(f.view.inventory[0] == entry);

    custom.category = ItemActionCategory::ManageContents;
    custom.argument = ItemActionArgument::Item;
    f.view.inventory[0].actions = {custom};
    f.Open(); f.Event(ftxui::Event::Character('u'));
    CHECK(f.commands.size() == 1);
    f.Event(ftxui::Event::Character('a'));
    CHECK(f.Draw().find("Calibrate instrument") != std::string::npos);
    f.Event(ftxui::Event::Return); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 2);
    CHECK(f.commands.back().itemAction == "calibrate");
    CHECK(f.commands.back().otherItem == 70);
}

TEST_CASE("Item text resolves keys and falls back without interpreting reason codes", "[client][items]") {
    ItemUiFixture f;
    CHECK(f.localization.RenderItemText({"item.action.0", "Fallback"}) == "Start charging");
    CHECK(f.localization.RenderItemText({"unknown.item.key", "Fallback"}) == "Fallback");
    CHECK(f.localization.RenderItemText({"", "Plain text"}) == "Plain text");
    CHECK(f.localization.RenderItemText({}).empty());
    CommandResult result;
    result.outcome = Outcome::item_use_failed;
    result.itemOutcome = ItemUseOutcome::Refused;
    result.itemReason = {"custom_reason", {"unknown.reason", "Custom refusal"}};
    CHECK(f.localization.ResultText(result) == "Custom refusal");
    result.itemReason.message = {"item.refusal.3", "Fallback refusal"};
    CHECK(f.localization.ResultText(result) == "Install a battery first.");
    result.itemReason = {};
    CHECK(f.localization.ResultText(result) == "That action is unavailable.");
}

TEST_CASE("Headless item details print local identity labels and typed reasons", "[client][items]") {
    ItemUiFixture f;
    auto entry = f.Item(70, "field_charger");
    auto custom = f.Action("field_charger", "charger", "start");
    custom.action = "calibrate"; custom.label = {"custom.label", "Calibrate instrument"};
    custom.availability = ItemUseOutcome::Refused;
    custom.reason = {"uncalibrated", {"custom.reason", "Calibration required"}};
    entry.actions = {custom}; f.view.inventory = {entry};
    const auto text = ItemDetails(f.view, TestContent());
    CHECK(text.find("action=calibrate label=Calibrate instrument") != String::npos);
    CHECK(text.find("reason=uncalibrated message=Calibration required") != String::npos);
}

TEST_CASE("Inventory selection belongs to the viewed player rather than a previous player's ground item", "[terminal][items]") {
    ItemUiFixture f;
    f.view.playerId = 1;
    f.view.inventory = {f.Item(70, "rock")};
    f.Open(); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].item == 70);
    f.view.playerId = 2;
    f.view.inventory = {f.Item(71, "backpack")};
    f.view.groundItems = {f.Item(70, "rock")};
    f.Open(); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 2);
    CHECK(f.commands[1].item == 71);
}

TEST_CASE("A player change between rendering and activation cancels the pending item prompt", "[terminal][items]") {
    ItemUiFixture f;
    f.view.playerId = 1;
    auto controller = f.Item(70, "door_controller");
    controller.actions = {f.Action("door_controller", "timer", "arm")};
    f.view.groundItems = {controller};
    f.Open(); f.Event(ftxui::Event::Character('u')); f.Draw();
    // Even an accessible identical ID/action in the new view cannot inherit
    // the previous player's uncommitted direction prompt.
    f.view.playerId = 2;
    f.Event(ftxui::Event::ArrowLeft);
    CHECK(f.commands.empty()); CHECK_FALSE(f.ui.IsOpen());
    f.Open(); f.Event(ftxui::Event::Character('u')); f.Event(ftxui::Event::ArrowRight);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].item == 70); CHECK(f.commands[0].dx == 1);
}

TEST_CASE("Item management sends authoritative storage and extraction commands", "[terminal][items]") {
    ItemUiFixture f;
    auto pack = f.Item(20, "backpack"); pack.capacityVolumeMl = 20000;
    f.view.inventory = {f.Item(10, "rock"), pack};
    f.Open(); f.Event(ftxui::Event::Character('a')); f.Event(ftxui::Event::Return);
    f.Draw(); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].kind == CommandKind::store_item);
    CHECK(f.commands[0].item == 10);
    CHECK(f.commands[0].otherItem == 20);
    f.view.inventory[0].parent = 20;
    f.Open(); f.Event(ftxui::Event::Character('a')); f.Event(ftxui::Event::ArrowDown); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 2);
    CHECK(f.commands[1].kind == CommandKind::store_item);
    CHECK(f.commands[1].otherItem == 0);
}

TEST_CASE("Item refusals and changed actions send no predicted mutations", "[terminal][items]") {
    ItemUiFixture f;
    auto charger = f.Item(70, "field_charger");
    auto action = f.Action("field_charger", "charger", "start");
    action.availability = ItemUseOutcome::Refused;
    action.reason = {"missing_battery", {"item.refusal.3", "Install a battery first."}};
    charger.actions.push_back(action); f.view.inventory.push_back(charger);
    f.Open(); f.Event(ftxui::Event::Character('u'));
    CHECK(f.commands.empty()); CHECK(f.ui.IsOpen()); CHECK(f.view.inventory[0] == charger);
    f.view.inventory[0].actions[0].availability = ItemUseOutcome::Succeeded;
    f.accepted = false; f.Event(ftxui::Event::Character('u'));
    REQUIRE(f.commands.size() == 1); CHECK(f.ui.IsOpen());
}

TEST_CASE("A changed capability action cannot activate the neighboring current action", "[terminal][items]") {
    ItemUiFixture f;
    auto utility = f.Item(80, "utility_controller");
    utility.actions = {f.Action("utility_controller", "charger", "start"),
        f.Action("utility_controller", "timer", "arm")};
    f.view.groundItems.push_back(utility);
    f.Open(); f.Event(ftxui::Event::Character('u'));
    f.view.groundItems[0].actions[0] = f.Action("utility_controller", "charger", "stop");
    f.Draw(); f.Event(ftxui::Event::Return);
    CHECK(f.commands.empty()); CHECK(f.ui.IsOpen());
    f.Event(ftxui::Event::Escape);
    f.Event(ftxui::Event::Character('u')); f.Event(ftxui::Event::Return);
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].itemAction == "stop");
}

TEST_CASE("Inventory use honors the configurable Items binding", "[terminal][items]") {
    ItemUiFixture f;
    auto charger = f.Item(70, "field_charger");
    charger.actions = {f.Action("field_charger", "charger", "stop")};
    f.view.inventory.push_back(charger);
    SetBinding(f.keys, {InputContext::Items, InputAction::Use, {{"v", false}}});
    REQUIRE(ValidateKeymap(f.keys));
    f.Open(); f.Event(ftxui::Event::Character('u'));
    CHECK(f.commands.empty());
    f.Event(ftxui::Event::Character('v'));
    REQUIRE(f.commands.size() == 1);
    CHECK(f.commands[0].item == 70);
}

TEST_CASE("Headless item grammar is strict and exposes capability IDs", "[client][items]") {
    Command command;
    REQUIRE(ParseItemCommand("use 70 1 install 72", command));
    CHECK(command.itemAction == "install"); CHECK(command.otherItem == 72);
    REQUIRE(ParseItemCommand("use 80 2 arm n", command)); CHECK(command.dz == -1);
    REQUIRE(ParseItemCommand("store 72 0", command)); CHECK(command.kind == CommandKind::store_item);
    REQUIRE(ParseItemCommand("use 70 1 calibrate", command)); CHECK(command.itemAction == "calibrate");
    REQUIRE(ParseItemCommand("use 70 1 calibrate 72", command)); CHECK(command.otherItem == 72);
    REQUIRE(ParseItemCommand("use 70 1 calibrate e", command)); CHECK(command.dx == 1);
    REQUIRE(ParseItemCommand("use 1 1 install", command));
    REQUIRE(ParseItemCommand("use 1 1 start 2", command));
    for (const auto* invalid : {"use 0 1 start", "use 1 0 start", "use 1 1 Bad", "use 1 1 a-b", "use 1 1 action 0",
        "use 1 1 arm ne", "use 1 65536 stop", "store 1 -1", "store 1 2 extra"}) {
        CHECK_FALSE(ParseItemCommand(invalid, command));
    }
    Localization localization;
    CHECK(localization.ItemName(TestContent(), TestItem("field_charger")) == "field charger");
}
}
