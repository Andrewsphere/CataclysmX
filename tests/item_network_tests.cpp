#include "network/network.h"
#include "network/wire.h"
#include "items/item_behavior.h"
#include "items/item_registry.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

constexpr char ItemNetworkContent[] = R"({format_version:1,definitions:[
    {type:'item',id:'bag',name:'Bag',mass_g:5,volume_ml:10,capabilities:[
      {type:'container',id:'storage',capacity_volume_ml:1000}]},
    {type:'item',id:'battery',name:'Battery',mass_g:20,volume_ml:10,capabilities:[
      {type:'energy_store',id:'cell',capacity_energy:100,initial_energy:17,rechargeable:true,battery_class:'aa'}]},
    {type:'item',id:'device',name:'Device',mass_g:100,volume_ml:50,capabilities:[
      {type:'charger',id:'charger',power_source:'power',battery_class:'aa',energy_per_turn:3,action_cost:101},
      {type:'energy_store',id:'power',capacity_energy:1000,initial_energy:800,rechargeable:false},
      {type:'door_controller',id:'timer',delay_ticks:300,action_cost:102}]}
]})";

ContentCatalog ItemCatalog(eastl::string_view text = ItemNetworkContent) {
    const ContentSource source{"item-network.json5", text};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

WorldView ItemView(const ContentCatalog& content) {
    WorldView view;
    view.width = 1; view.height = 1; view.playerId = 1;
    view.cells.push_back({});
    InventoryEntry bag;
    bag.id = 10; bag.type = content.FindItem("bag"); bag.contained_items = 1;
    bag.massGrams = 125; bag.volumeMl = 10; bag.capacityVolumeMl = 1000; bag.usedVolumeMl = 50;
    InventoryEntry device;
    device.id = 20; device.type = content.FindItem("device"); device.parent = 10;
    device.contained_items = 1; device.massGrams = 120; device.volumeMl = 50;
    const auto& definition = content.GetItem(device.type);
    const auto charger = FindItemCapability(definition, "charger");
    const auto power = FindItemCapability(definition, "power");
    const auto timer = FindItemCapability(definition, "timer");
    device.energy.push_back({power, 800, 1000});
    device.activities.push_back({charger, true, 0});
    device.activities.push_back({timer, true, 123});
    const auto append = [&](ItemCapabilityId capability, const char* id, ItemUseReason reason = {}) {
        const auto* action = ItemBehaviorConfig(definition, capability)->FindAction(id);
        REQUIRE(action);
        device.actions.push_back({capability, action->id, action->argument,
            reason.code.empty() ? ItemUseOutcome::Succeeded : ItemUseOutcome::Refused,
            action->baseCost, action->category, action->label, eastl::move(reason)});
    };
    append(charger, "remove", {"custom_capacity", {"custom.capacity", "No room for this item."}});
    append(charger, "stop");
    append(timer, "disarm");
    InventoryEntry battery;
    battery.id = 30; battery.type = content.FindItem("battery"); battery.parent = 20;
    battery.installation = charger; battery.massGrams = 20; battery.volumeMl = 10;
    battery.energy.push_back({{1}, 17, 100});
    view.inventory = {bag, device, battery};
    bag.id = 40; bag.contained_items = 1; bag.massGrams = 25; bag.usedVolumeMl = 10;
    battery.id = 50; battery.parent = 40; battery.installation = {};
    view.groundItems = {bag, battery};
    view.carriedMassGrams = 125; view.carryCapacityGrams = 500;
    return view;
}

TEST_CASE("Item commands preserve explicit capability, target and secondary IDs", "[network][item][codec]") {
    Command use;
    use.kind = CommandKind::use_item; use.item = 42; use.capability = {2};
    for (const auto action : {"start", "stop", "install", "remove", "arm", "disarm", "unknown_local_action"}) {
        use.itemAction = action;
        use.otherItem = use.itemAction == "install" ? 99 : 0;
        use.dx = use.itemAction == "arm" ? -1 : 0;
        wire::Writer writer; REQUIRE(writer.CommandValue(use));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        Command result;
        REQUIRE(reader.CommandValue(result)); REQUIRE(reader.Done());
        CHECK(result.kind == use.kind); CHECK(result.item == 42); CHECK(result.capability == use.capability);
        CHECK(result.itemAction == action); CHECK(result.otherItem == use.otherItem); CHECK(result.dx == use.dx);
        for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
            wire::Reader truncated{{writer.bytes.data(), size}};
            CHECK_FALSE(truncated.CommandValue(result));
        }
    }
    Command store;
    store.kind = CommandKind::store_item; store.item = 42;
    for (PersistentId destination : {PersistentId{0}, PersistentId{99}}) {
        store.otherItem = destination;
        wire::Writer writer; writer.CommandValue(store);
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        Command result; REQUIRE(reader.CommandValue(result)); CHECK(reader.Done());
        CHECK(result.item == 42); CHECK(result.otherItem == destination);
    }
    use.itemAction = "install"; use.dx = 0; use.otherItem = 0;
    CHECK(ValidCommandShape(use)); // Declared argument validation belongs to ItemSystem.
    use.itemAction = "arm"; use.dx = 1; use.dz = 1;
    CHECK_FALSE(ValidCommandShape(use));
    use.dx = 0; use.dz = 1; use.item = 0;
    CHECK_FALSE(ValidCommandShape(use));
    use.item = 42; use.capability = {};
    CHECK_FALSE(ValidCommandShape(use));
    use.capability = {17};
    wire::Writer malformed; CHECK_FALSE(malformed.CommandValue(use));
    malformed.U8(static_cast<std::uint8_t>(use.kind)); malformed.I32(0); malformed.I32(1);
    malformed.U64(42); malformed.U32(use.waitCost); malformed.U16(17);
    REQUIRE(malformed.Text("arm", maxItemActionName)); malformed.U64(0);
    wire::Reader reader{{malformed.bytes.data(), malformed.bytes.size()}};
    Command result; CHECK_FALSE(reader.CommandValue(result));
}

TEST_CASE("Local action IDs are bounded independently of behavior declarations", "[network][item][codec]") {
    Command command;
    command.kind = CommandKind::use_item; command.item = 42; command.capability = {1};
    command.itemAction.assign(64, 'a');
    wire::Writer writer; REQUIRE(writer.CommandValue(command));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    Command result; REQUIRE(reader.CommandValue(result)); CHECK(reader.Done());
    CHECK(result.itemAction == command.itemAction);
    SECTION("long ID") { command.itemAction.push_back('a'); }
    SECTION("empty ID") { command.itemAction.clear(); }
    SECTION("invalid ID") { command.itemAction = "not an id"; }
    SECTION("self target") { command.otherItem = command.item; }
    SECTION("mixed arguments") { command.otherItem = 99; command.dx = 1; }
    wire::Writer invalid; CHECK_FALSE(invalid.CommandValue(command));
}

TEST_CASE("Item receipts preserve typed failure and new command outcomes", "[network][item][codec]") {
    for (const auto outcome : {Outcome::used_item, Outcome::stored_item, Outcome::item_use_failed,
        Outcome::capacity_exceeded, Outcome::invalid_container}) {
        PlayerCommandResult receipt{7, 3, 2, {outcome, false, 123}};
        receipt.result.itemOutcome = ItemUseOutcome::Refused;
        receipt.result.itemReason = {"custom_refusal", {"third_party.reason", "A free-form reason with spaces: café."}};
        receipt.result.cost = 101;
        wire::Writer writer; REQUIRE(writer.Result(receipt));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        PlayerCommandResult result;
        REQUIRE(reader.Result(result)); CHECK(reader.Done());
        CHECK(result.result.outcome == outcome);
        CHECK(result.result.itemOutcome == ItemUseOutcome::Refused);
        CHECK(result.result.itemReason == receipt.result.itemReason);
        CHECK(result.result.cost == 101);
        for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
            wire::Reader truncated{{writer.bytes.data(), size}};
            CHECK_FALSE(truncated.Result(result));
        }
        wire::Writer prefix;
        prefix.U64(receipt.player); prefix.U64(receipt.sequence); prefix.U64(receipt.opportunity);
        prefix.U8(static_cast<std::uint8_t>(outcome)); prefix.U8(0); prefix.U64(123); prefix.U64(101); prefix.U8(0);
        writer.bytes[prefix.bytes.size()] = 255;
        wire::Reader invalid{{writer.bytes.data(), writer.bytes.size()}};
        CHECK_FALSE(invalid.Result(result));
    }
}

TEST_CASE("Generic item diagnostics have explicit bounds and outcome semantics", "[network][item][codec]") {
    PlayerCommandResult receipt{7, 3, 2, {Outcome::item_use_failed, false, 123}};
    for (const auto status : {ItemUseOutcome::Succeeded, ItemUseOutcome::InvalidAction,
        ItemUseOutcome::Inaccessible, ItemUseOutcome::Refused}) {
        receipt.result.itemOutcome = status;
        wire::Writer writer; REQUIRE(writer.Result(receipt));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        PlayerCommandResult result; REQUIRE(reader.Result(result)); CHECK(reader.Done());
        CHECK(result.result.itemOutcome == status); CHECK(result.result.itemReason == ItemUseReason{});
    }
    receipt.result.itemOutcome = ItemUseOutcome::Refused;
    receipt.result.itemReason.code.assign(64, 'a');
    receipt.result.itemReason.message.key.assign(192, 'k');
    receipt.result.itemReason.message.fallback.assign(192, 'f');
    wire::Writer bounded; REQUIRE(bounded.Result(receipt));
    wire::Reader reader{{bounded.bytes.data(), bounded.bytes.size()}};
    PlayerCommandResult decoded; REQUIRE(reader.Result(decoded)); CHECK(reader.Done());
    CHECK(decoded.result.itemReason == receipt.result.itemReason);
    // Locate variable-length diagnostic fields by constructing the fixed prefix.
    wire::Writer prefix;
    prefix.U64(7); prefix.U64(3); prefix.U64(2);
    prefix.U8(static_cast<std::uint8_t>(receipt.result.outcome)); prefix.U8(0);
    prefix.U64(123); prefix.U64(0); prefix.U8(0); prefix.U8(static_cast<std::uint8_t>(ItemUseOutcome::Refused));
    const auto codeOffset = prefix.bytes.size();
    REQUIRE(prefix.Text(receipt.result.itemReason.code, maxItemActionName));
    const auto keyOffset = prefix.bytes.size();
    REQUIRE(prefix.Text(receipt.result.itemReason.message.key, maxItemText));
    const auto fallbackOffset = prefix.bytes.size();
    const auto rejects = [&](std::size_t offset, std::uint8_t value) {
        auto bytes = bounded.bytes; bytes[offset] = value;
        wire::Reader malformed{{bytes.data(), bytes.size()}};
        CHECK_FALSE(malformed.Result(decoded));
    };
    rejects(codeOffset, 65); rejects(codeOffset + 1, ' ');
    rejects(keyOffset, 193); rejects(fallbackOffset, 193);
    rejects(fallbackOffset + 1, '\n');
    SECTION("long code") { receipt.result.itemReason.code.push_back('a'); }
    SECTION("non-ID code") { receipt.result.itemReason.code = "has spaces"; }
    SECTION("long key") { receipt.result.itemReason.message.key.push_back('a'); }
    SECTION("long fallback") { receipt.result.itemReason.message.fallback.push_back('a'); }
    SECTION("control fallback") { receipt.result.itemReason.message.fallback = "bad\ttext"; }
    SECTION("generic status with diagnostic") { receipt.result.itemOutcome = ItemUseOutcome::InvalidAction; }
    wire::Writer invalid; CHECK_FALSE(invalid.Result(receipt));
}

TEST_CASE("Complete carried and ground item trees roundtrip with bounded metadata", "[network][item][snapshot]") {
    const auto content = ItemCatalog();
    const auto view = ItemView(content);
    REQUIRE(wire::ValidItemDefinitions(view, content));
    wire::Writer writer; REQUIRE(writer.View(view));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView result;
    REQUIRE(reader.View(result)); CHECK(reader.Done());
    CHECK(result.inventory == view.inventory); CHECK(result.groundItems == view.groundItems);
    CHECK(result.carriedMassGrams == 125); CHECK(result.carryCapacityGrams == 500);
    REQUIRE(wire::ValidItemDefinitions(result, content));
    for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
        wire::Reader truncated{{writer.bytes.data(), size}};
        CHECK_FALSE(truncated.View(result));
    }
    // A later complete snapshot can remove both lists; no stale entries survive.
    auto empty = view; empty.inventory.clear(); empty.groundItems.clear();
    wire::Writer next; REQUIRE(next.View(empty));
    wire::Reader nextReader{{next.bytes.data(), next.bytes.size()}};
    REQUIRE(nextReader.View(result)); CHECK(result.inventory.empty()); CHECK(result.groundItems.empty());
}

TEST_CASE("Item snapshot rejects broken trees, ordering and packet budget", "[network][item][snapshot]") {
    const auto content = ItemCatalog();
    const auto baseline = ItemView(content);
    auto invalid = baseline;
    SECTION("missing parent") { invalid.inventory[1].parent = 999; }
    SECTION("cross-list parent") { invalid.inventory[1].parent = 40; }
    SECTION("cycle") { invalid.inventory[0].parent = 30; }
    SECTION("self parent") { invalid.inventory[1].parent = 20; }
    SECTION("duplicate ID across lists") { invalid.groundItems[0].id = 10; }
    SECTION("unsorted IDs") { invalid.inventory[0].id = 21; }
    SECTION("zero type") { invalid.inventory[0].type = {}; }
    SECTION("invalid installation") { invalid.inventory[2].installation = {17}; }
    SECTION("zero capability") { invalid.inventory[1].energy[0].capability = {}; }
    SECTION("energy overflow") { invalid.inventory[1].energy[0].energy = 1001; }
    SECTION("duplicate activity") { invalid.inventory[1].activities[1] = invalid.inventory[1].activities[0]; }
    SECTION("bad argument") { invalid.inventory[1].actions[0].argument = static_cast<ItemActionArgument>(255); }
    SECTION("bad category") { invalid.inventory[1].actions[0].category = static_cast<ItemActionCategory>(255); }
    SECTION("bad action ID") { invalid.inventory[1].actions[0].action = "has spaces"; }
    SECTION("long action ID") { invalid.inventory[1].actions[0].action.assign(65, 'a'); }
    SECTION("long label") { invalid.inventory[1].actions[0].label.fallback.assign(193, 'a'); }
    SECTION("control in status") { invalid.inventory[1].activities[0].status.fallback = "bad\nstatus"; }
    SECTION("unsorted actions") { eastl::swap(invalid.inventory[1].actions[0], invalid.inventory[1].actions[1]); }
    SECTION("bad enum") { invalid.inventory[1].actions[0].availability = static_cast<ItemUseOutcome>(255); }
    SECTION("too many capabilities") { invalid.inventory[1].energy.resize(17); }
    SECTION("too many actions") { invalid.inventory[1].actions.resize(65); }
    SECTION("too many entries") { invalid.groundItems.resize(2049); }
    SECTION("packet budget before entry bound") {
        invalid.inventory.clear(); invalid.groundItems.clear();
        auto entry = baseline.inventory[0]; entry.parent = 0; entry.contained_items = 0;
        for (unsigned i = 1; i <= 700; ++i) { entry.id = i; invalid.inventory.push_back(entry); }
    }
    SECTION("excessive parent depth") {
        invalid.inventory.clear(); invalid.groundItems.clear();
        auto entry = baseline.inventory[0];
        for (unsigned i = 1; i <= 66; ++i) {
            entry.id = i; entry.parent = i - 1; invalid.inventory.push_back(entry);
        }
    }
    wire::Writer writer; CHECK_FALSE(writer.View(invalid));
}

TEST_CASE("Item decoder rejects malformed bounded fields before catalog lookup", "[network][item][codec]") {
    const auto content = ItemCatalog();
    auto view = ItemView(content);
    view.inventory = {view.inventory[1]}; view.inventory[0].parent = 0;
    view.groundItems.clear();
    wire::Writer writer; REQUIRE(writer.View(view));
    // Walk the encoded fields to locate variable-length metadata boundaries.
    wire::Reader cursor{{writer.bytes.data(), writer.bytes.size()}};
    std::int32_t coordinate; std::uint8_t byte; std::uint16_t shortValue; std::uint32_t number; std::uint64_t wide;
    REQUIRE(cursor.I32(coordinate)); REQUIRE(cursor.I32(coordinate));
    REQUIRE(cursor.U8(byte)); REQUIRE(cursor.U8(byte));
    REQUIRE(cursor.I32(coordinate)); REQUIRE(cursor.I32(coordinate));
    for (int i = 0; i < 6; ++i) REQUIRE(cursor.U64(wide));
    REQUIRE(cursor.U32(number)); REQUIRE(cursor.U32(number));
    for (int i = 0; i < 3; ++i) REQUIRE(cursor.U8(byte));
    REQUIRE(cursor.U16(shortValue)); REQUIRE(cursor.U8(byte)); // single hidden cell
    const auto countOffset = cursor.offset;
    REQUIRE(cursor.U16(shortValue));
    REQUIRE(cursor.U64(wide)); REQUIRE(cursor.U32(number)); REQUIRE(cursor.U32(number));
    REQUIRE(cursor.U64(wide)); REQUIRE(cursor.U16(shortValue)); REQUIRE(cursor.U64(wide));
    REQUIRE(cursor.U32(number)); REQUIRE(cursor.U32(number)); REQUIRE(cursor.U64(wide));
    const auto energyOffset = cursor.offset;
    REQUIRE(cursor.U8(byte)); REQUIRE(byte == 1);
    REQUIRE(cursor.U16(shortValue)); REQUIRE(cursor.U32(number)); REQUIRE(cursor.U32(number));
    const auto activityOffset = cursor.offset;
    REQUIRE(cursor.U8(byte)); REQUIRE(byte == 2);
    const auto activeOffset = cursor.offset + sizeof(std::uint16_t);
    String text;
    for (int i = 0; i < 2; ++i) {
        REQUIRE(cursor.U16(shortValue)); REQUIRE(cursor.U8(byte)); REQUIRE(cursor.U64(wide));
        REQUIRE(cursor.Text(text, maxItemText)); REQUIRE(cursor.Text(text, maxItemText));
    }
    const auto actionOffset = cursor.offset;
    REQUIRE(cursor.U8(byte)); REQUIRE(cursor.U16(shortValue));
    const auto actionIdOffset = cursor.offset;
    REQUIRE(cursor.Text(text, maxItemActionName));
    const auto argumentOffset = cursor.offset;
    const auto rejects = [&](std::size_t offset, std::uint8_t value) {
        auto bytes = writer.bytes; bytes[offset] = value;
        wire::Reader reader{{bytes.data(), bytes.size()}};
        WorldView result; CHECK_FALSE(reader.View(result));
    };
    rejects(countOffset, 9); // count > 2048
    rejects(energyOffset, 17);
    rejects(energyOffset + 2, 17); // capability low byte
    rejects(activityOffset, 17);
    rejects(activeOffset, 2);
    rejects(actionOffset, 65);
    rejects(actionIdOffset, 65); // overlong local ID
    rejects(argumentOffset, 255);
    rejects(argumentOffset + 1, 255); // availability
    rejects(argumentOffset + 6, 255); // category
}

TEST_CASE("Item snapshot metadata is checked against the client's catalog", "[network][item][snapshot]") {
    const auto content = ItemCatalog();
    auto view = ItemView(content);
    SECTION("unknown type") { view.inventory[1].type = {999}; }
    SECTION("unknown capability") { view.inventory[1].energy[0].capability = {16}; }
    SECTION("wrong capability kind") { view.inventory[1].energy[0].capability = {1}; }
    SECTION("capacity differs") { view.inventory[1].energy[0].capacity = 1001; }
    SECTION("physical volume differs") { ++view.inventory[0].volumeMl; }
    SECTION("container capacity differs") { ++view.inventory[0].capacityVolumeMl; }
    SECTION("action belongs to another behavior") {
        view.inventory[1].actions.back().capability = {2};
    }
    SECTION("action cost differs") { ++view.inventory[1].actions[0].baseCost; }
    SECTION("action argument differs") { view.inventory[1].actions[0].argument = ItemActionArgument::Item; }
    SECTION("action category differs") { view.inventory[1].actions[0].category = ItemActionCategory::Operate; }
    SECTION("action label differs") { view.inventory[1].actions[0].label.fallback = "Different label"; }
    SECTION("undeclared action") { view.inventory[1].actions.back().action = "unknown"; }
    SECTION("installation on energy store") { view.inventory[2].installation = {2}; }
    SECTION("missing energy view") { view.inventory[1].energy.clear(); }
    SECTION("missing activity view") { view.inventory[1].activities.clear(); }
    SECTION("timer beyond definition") { view.inventory[1].activities[1].remainingTicks = 301; }
    SECTION("inactive countdown") { view.inventory[1].activities[1].active = false; }
    SECTION("non-countdown delay") { view.inventory[1].activities[0].remainingTicks = 1; }
    SECTION("wrong child count") { ++view.inventory[0].contained_items; }
    SECTION("wrong subtree mass") { ++view.inventory[0].massGrams; }
    SECTION("wrong used volume") { ++view.inventory[0].usedVolumeMl; }
    wire::Writer writer; REQUIRE(writer.View(view));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; REQUIRE(reader.View(decoded));
    CHECK_FALSE(wire::ValidItemDefinitions(decoded, content));
}

// Test-only behavior: the network only sees its declared schema and canonical data.
class FingerprintBehaviorDefinition final : public ItemBehaviorDefinition {
public:
    explicit FingerprintBehaviorDefinition(ItemBehaviorSchema schema) : ItemBehaviorDefinition(eastl::move(schema)) {}
    StateResult<eastl::unique_ptr<ItemBehavior>> Create(ItemCapabilityId, const DataObject*, Diagnostics*) const override {
        return eastl::unexpected(StateError::InvalidData);
    }
    StateResult<void> ValidateSnapshot(const ItemSnapshotContext&, PersistentId, ItemCapabilityId, Diagnostics*) const override {
        return {};
    }
};

ContentResult<ParsedItemBehavior> ParseFingerprintBehavior(const DataObject& input, const ItemDefinition&,
    ItemCapabilityId, Diagnostics*) {
    ItemBehaviorSchema schema;
    const auto number = [&](const char* key, std::uint64_t fallback) {
        const auto value = input.GetUnsigned(key);
        return value ? *value : fallback;
    };
    const auto text = [&](const char* key, const char* fallback) -> String {
        const auto value = input.GetString(key);
        return value ? String(value->data(), value->size()) : String(fallback);
    };
    schema.actions.push_back({text("action", "pulse"), static_cast<ItemActionArgument>(number("argument", 0)),
        static_cast<ItemActionCategory>(number("category", 0)), static_cast<std::uint32_t>(number("cost", 100)),
        {text("key", "probe.pulse"), text("label", "Pulse this device")}});
    schema.hasActivity = number("activity", 1) != 0;
    schema.countdown = number("countdown", 1) != 0;
    schema.maximumDelay = number("delay", 500);
    schema.implementationVersion = static_cast<std::uint32_t>(number("version", 1));
    DataObject config;
    if (const auto* payload = input.Find("payload")) config.Set("payload", *payload);
    return ParsedItemBehavior{eastl::allocate_shared<FingerprintBehaviorDefinition>(FatalAllocator{}, eastl::move(schema)), eastl::move(config)};
}

ContentCatalog FingerprintCatalog(eastl::string_view fields) {
    ItemBehaviorRegistry registry;
    REQUIRE(registry.Add("probe", ParseFingerprintBehavior));
    String source = "{format_version:1,definitions:[{type:'item',id:'probe',name:'Probe',mass_g:1,volume_ml:1,capabilities:[{type:'probe',id:'probe',";
    source.append(fields.data(), fields.size()); source += "}]}]}";
    const ContentSource input{"probe.json5", {source.data(), source.size()}};
    auto result = LoadContent({&input, 1}, registry);
    REQUIRE(result);
    return eastl::move(*result);
}

TEST_CASE("Content fingerprint hashes canonical typed data and the complete behavior schema", "[network][item][fingerprint]") {
    const auto baseline = ContentFingerprint(FingerprintCatalog("payload:{a:1,b:{c:true,d:['text',2]}}"));
    CHECK(ContentFingerprint(FingerprintCatalog("payload:{b:{d:['text',2],c:true},a:1}")) == baseline);
    CHECK(ContentFingerprint(FingerprintCatalog("payload:{a:'1',b:{c:true,d:['text',2]}}")) != baseline);
    CHECK(ContentFingerprint(FingerprintCatalog("payload:{a:1,b:{c:true,d:[2,'text']}}")) != baseline);
    const auto schemaBaseline = ContentFingerprint(FingerprintCatalog("payload:{}"));
    for (const char* change : {"action:'other'", "argument:1", "category:1", "cost:101", "key:'other.key'",
        "label:'Other label'", "activity:0,countdown:0,delay:0", "countdown:0,delay:0", "delay:501", "version:2"}) {
        CAPTURE(change);
        String fields = "payload:{},"; fields += change;
        CHECK(ContentFingerprint(FingerprintCatalog({fields.data(), fields.size()})) != schemaBaseline);
    }
    CHECK(ContentFingerprint(FingerprintCatalog("payload:{},activity:1,countdown:0,delay:0")) !=
        ContentFingerprint(FingerprintCatalog("payload:{},activity:0,countdown:0,delay:0")));
    // Config defaults are normalized at the content boundary.
    String explicitDefaults = ItemNetworkContent;
    const auto offset = explicitDefaults.find("action_cost:101"); REQUIRE(offset != String::npos);
    explicitDefaults.replace(offset, eastl::string_view("action_cost:101").size(), "action_cost:100");
    String implicitDefaults = explicitDefaults;
    implicitDefaults.erase(implicitDefaults.find(",action_cost:100"), eastl::string_view(",action_cost:100").size());
    CHECK(ContentFingerprint(ItemCatalog({explicitDefaults.data(), explicitDefaults.size()})) ==
        ContentFingerprint(ItemCatalog({implicitDefaults.data(), implicitDefaults.size()})));
}

TEST_CASE("Generic behavior metadata needs no networking registration", "[network][item][codec]") {
    const auto content = FingerprintCatalog("payload:{}");
    WorldView view;
    view.width = 1; view.height = 1; view.playerId = 1; view.cells.push_back({});
    InventoryEntry entry;
    entry.id = 1; entry.type = content.FindItem("probe"); entry.massGrams = 1; entry.volumeMl = 1;
    entry.activities.push_back({{1}, true, 100, {"probe.status", "Counting down: café"}});
    const auto* behavior = ItemBehaviorConfig(content.GetItem(entry.type), {1}); REQUIRE(behavior);
    const auto& action = behavior->Schema().actions.front();
    entry.actions.push_back({{1}, action.id, action.argument, ItemUseOutcome::Refused, action.baseCost,
        action.category, action.label, {"local_refusal", {"probe.refusal", "Please try later."}}});
    view.inventory.push_back(entry);
    REQUIRE(wire::ValidItemDefinitions(view, content));
    wire::Writer writer; REQUIRE(writer.View(view));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; REQUIRE(reader.View(decoded)); CHECK(reader.Done());
    CHECK(decoded.inventory == view.inventory); CHECK(wire::ValidItemDefinitions(decoded, content));
    const auto mismatch = FingerprintCatalog("payload:{},label:'Changed label'");
    CHECK_FALSE(wire::ValidItemDefinitions(decoded, mismatch));
    const auto passive = FingerprintCatalog("payload:{},activity:0,countdown:0,delay:0");
    decoded.inventory[0].activities[0].active = false;
    decoded.inventory[0].activities[0].remainingTicks = 0;
    CHECK(wire::ValidItemDefinitions(decoded, passive)); // Passive behavior status is still presented.
    decoded.inventory[0].activities[0].active = true;
    CHECK_FALSE(wire::ValidItemDefinitions(decoded, passive));
}

TEST_CASE("Unknown local actions pass shape validation and fail authoritative item use", "[network][item]") {
    Server server(TestContent());
    const auto type = TestContent().FindItem("field_charger"); REQUIRE(type.value);
    auto& world = server.GetWorld();
    const auto item = world.CreateItem(type, ItemLocation::Carried(server.GetPlayer())); REQUIRE(item);
    Command command;
    command.kind = CommandKind::use_item; command.item = world.GetItem(*item)->id;
    command.capability = FindItemCapability(TestContent().GetItem(type), "charger");
    command.itemAction = "unknown_local_action";
    REQUIRE(ValidCommandShape(command));
    wire::Writer writer; REQUIRE(writer.CommandValue(command));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    Command decoded; REQUIRE(reader.CommandValue(decoded)); REQUIRE(reader.Done());
    const auto result = server.Send(decoded);
    CHECK(result.outcome == Outcome::item_use_failed); CHECK(result.itemOutcome == ItemUseOutcome::InvalidAction);
    CHECK_FALSE(result.advanced);
}

TEST_CASE("Content fingerprint covers immutable item balance and capability configuration", "[network][item][fingerprint]") {
    const auto baseline = ContentFingerprint(ItemCatalog());
    struct Change { const char* from; const char* to; };
    for (const auto& change : {
        Change{"mass_g:100", "mass_g:101"}, Change{"volume_ml:50", "volume_ml:51"},
        Change{"name:'Device'", "name:'Tool'"}, Change{"capacity_volume_ml:1000", "capacity_volume_ml:1001"},
        Change{"capacity_energy:1000", "capacity_energy:1001"}, Change{"initial_energy:800", "initial_energy:799"},
        Change{"rechargeable:true", "rechargeable:false"}, Change{"battery_class:'aa'", "battery_class:'aaa'"},
        Change{"energy_per_turn:3", "energy_per_turn:4"}, Change{"action_cost:101", "action_cost:103"},
        Change{"delay_ticks:300", "delay_ticks:301"}, Change{"action_cost:102", "action_cost:104"},
        Change{"id:'timer'", "id:'timer_changed'"}}) {
        CAPTURE(change.from);
        String text = ItemNetworkContent;
        const auto offset = text.find(change.from); REQUIRE(offset != String::npos);
        text.replace(offset, eastl::string_view(change.from).size(), change.to);
        CHECK(ContentFingerprint(ItemCatalog({text.data(), text.size()})) != baseline);
    }
    // Changing the resolved power source must matter, even when both stores are equivalent.
    String first = ItemNetworkContent;
    const auto offset = first.find("{type:'charger'"); REQUIRE(offset != String::npos);
    first.insert(offset, "{type:'energy_store',id:'alternate',capacity_energy:1000,initial_energy:800,rechargeable:false},");
    String second = first;
    const auto source = second.find("power_source:'power'"); REQUIRE(source != String::npos);
    second.replace(source, eastl::string_view("power_source:'power'").size(), "power_source:'alternate'");
    CHECK(ContentFingerprint(ItemCatalog({first.data(), first.size()})) !=
        ContentFingerprint(ItemCatalog({second.data(), second.size()})));
}

template<class Predicate>
bool PollItems(LanHost& host, LanClient& client, Predicate predicate) {
    for (int i = 0; i < 500; ++i) {
        host.Pump(10000); client.Pump(10000);
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

TEST_CASE("LAN item use preserves authoritative ownership rejection", "[network][item][loopback]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto type = TestContent().FindItem("field_charger"); REQUIRE(type.value);
    const auto charger = world.CreateItem(type, ItemLocation::Carried(server.GetPlayer())); REQUIRE(charger);
    const auto itemId = world.GetItem(*charger)->id;
    const auto capability = FindItemCapability(TestContent().GetItem(type), "charger"); REQUIRE(capability.value);
    LanHost host(**runtime, server, TestContent()); REQUIRE(host.Open({0, 0, "Items"}));
    LanClient client(**runtime, TestContent());
    REQUIRE(client.Connect({"127.0.0.1", host.GetPort(), "Guest", 0}));
    REQUIRE(PollItems(host, client, [&] { return client.GetState() == ConnectionState::Active; }));
    Command use;
    use.kind = CommandKind::use_item; use.item = itemId; use.capability = capability;
    use.itemAction = "start";
    const auto sequence = client.Submit(use); REQUIRE(sequence);
    host.QueueLocal({CommandKind::wait});
    bool rejected = false;
    REQUIRE(PollItems(host, client, [&] {
        for (const auto& receipt : client.TakeResults()) {
            if (receipt.sequence == *sequence && receipt.result.outcome == Outcome::item_use_failed) {
                CHECK(receipt.result.itemOutcome == ItemUseOutcome::Inaccessible);
                CHECK_FALSE(receipt.result.advanced); rejected = true;
            }
        }
        return rejected;
    }));
    CHECK(world.GetItem(*charger)->location.actor == server.GetPlayer());
    CHECK(client.GetState() == ConnectionState::Active);
}

TEST_CASE("LAN installs an explicit battery and rejects an old item-use opportunity", "[network][item][loopback]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    LanHost host(**runtime, server, TestContent()); REQUIRE(host.Open({0, 0, "Item opportunities"}));
    LanClient client(**runtime, TestContent());
    REQUIRE(client.Connect({"127.0.0.1", host.GetPort(), "Guest", 0}));
    REQUIRE(PollItems(host, client, [&] { return client.GetState() == ConnectionState::Active; }));
    auto& world = server.GetWorld();
    const auto actor = world.FindActor(client.View().playerId); REQUIRE(actor.value);
    const auto type = TestContent().FindItem("field_charger"); REQUIRE(type.value);
    const auto batteryType = TestContent().FindItem("rechargeable_battery"); REQUIRE(batteryType.value);
    const auto charger = world.CreateItem(type, ItemLocation::Carried(actor)); REQUIRE(charger);
    const auto battery = world.CreateItem(batteryType, ItemLocation::Carried(actor)); REQUIRE(battery);
    Command use;
    use.kind = CommandKind::use_item; use.item = world.GetItem(*charger)->id;
    use.capability = FindItemCapability(TestContent().GetItem(type), "charger"); REQUIRE(use.capability.value);
    use.itemAction = "install"; use.otherItem = world.GetItem(*battery)->id;
    const auto installedSequence = client.Submit(use); REQUIRE(installedSequence);
    host.QueueLocal({CommandKind::wait});
    // Flush the input, then observe execution at the host without consuming its receipt at the client.
    client.Pump(0);
    bool installed = false;
    for (int i = 0; i < 500 && !installed; ++i) {
        const auto update = host.Pump(10000);
        for (const auto& receipt : update.commandResults) {
            if (receipt.player == client.View().playerId && receipt.sequence == *installedSequence &&
                receipt.result.outcome == Outcome::used_item) {
                CHECK(receipt.result.itemOutcome == ItemUseOutcome::Succeeded); installed = true;
            }
        }
        if (!installed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(installed);
    CHECK(world.GetItem(*battery)->location.item == *charger);
    CHECK(world.GetItem(*battery)->location.installation == use.capability);
    use.itemAction = "start"; use.otherItem = 0;
    const auto staleSequence = client.Submit(use); REQUIRE(staleSequence);
    bool stale = false;
    REQUIRE(PollItems(host, client, [&] {
        for (const auto& receipt : client.TakeResults()) {
            if (receipt.sequence == *staleSequence && receipt.result.outcome == Outcome::stale_input) {
                CHECK_FALSE(receipt.result.advanced); stale = true;
            }
        }
        return stale;
    }));
    CHECK(client.GetState() == ConnectionState::Active);
}
}
