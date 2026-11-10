#include "content/content.h"
#include "content_fixture.h"
#include "items/item_registry.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

ContentResult<ContentCatalog> Load(eastl::string_view definitions, Diagnostics* diagnostics = nullptr) {
    String text = "{format_version:1,definitions:[";
    text.append(definitions.data(), definitions.size());
    text += "]}";
    const ContentSource source{"items.json5", View(text)};
    return LoadContent({&source, 1}, diagnostics);
}

String Item(eastl::string_view capabilities) {
    String text = "{type:'item',id:'device',name:'device',mass_g:0,volume_ml:1,capabilities:[";
    text.append(capabilities.data(), capabilities.size());
    text += "]}";
    return text;
}

void Invalid(eastl::string_view definitions) {
    Diagnostics diagnostics;
    auto result = Load(definitions, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "items.json5");
    CHECK_FALSE(diagnostics.Frames().front().location->path.empty());
    result = Load(definitions);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
}

TEST_CASE("Item capability references resolve after lexical sorting", "[content][item-content]") {
    const auto first = Item(R"(
        {type:'door_controller',id:'timer',delay_ticks:300},
        {type:'energy_store',id:'power',capacity_energy:2000,rechargeable:false},
        {type:'charger',id:'charger',power_source:'power',battery_class:'aa',energy_per_turn:3}
    )");
    const auto second = Item(R"(
        {type:'charger',id:'charger',power_source:'power',battery_class:'aa',energy_per_turn:3},
        {type:'door_controller',id:'timer',delay_ticks:300},
        {type:'energy_store',id:'power',capacity_energy:2000,rechargeable:false}
    )");
    const auto loaded = Load(View(first));
    const auto reordered = Load(View(second));
    REQUIRE(loaded.has_value());
    REQUIRE(reordered.has_value());
    const auto& item = loaded->GetItem(loaded->FindItem("device"));
    CHECK(item == reordered->GetItem(reordered->FindItem("device")));
    CHECK(item.massGrams == 0);
    CHECK(item.volumeMl == 1);
    CHECK(FindItemCapability(item, "charger") == ItemCapabilityId{1});
    CHECK(FindItemCapability(item, "power") == ItemCapabilityId{2});
    CHECK(FindItemCapability(item, "timer") == ItemCapabilityId{3});
    CHECK(FindItemCapability(item, "missing") == ItemCapabilityId{});
    CHECK(TryGetItemCapability(item, {}) == nullptr);
    CHECK(TryGetItemCapability(item, {4}) == nullptr);
    CHECK(TryGetItemCapability(item, {65535}) == nullptr);
    CHECK(ItemContainer(item) == nullptr);
    const auto& charger = item.capabilities[0].config;
    CHECK(charger.GetString("power_source").value() == "power");
    CHECK(charger.GetUnsigned("action_cost").value() == 100);
    REQUIRE(ItemEnergyStore(item, {2}) != nullptr);
    CHECK(ItemEnergyStore(item, {2})->initialEnergy == 2000);
    CHECK(ItemEnergyStore(item, {1}) == nullptr);
    CHECK(ItemEnergyStore(item, {}) == nullptr);
    CHECK(item.capabilities[2].config.GetUnsigned("action_cost").value() == 100);
    REQUIRE(ItemBehaviorConfig(item, {1}) != nullptr);
    REQUIRE(ItemBehaviorConfig(item, {3}) != nullptr);
    CHECK(ItemBehaviorConfig(item, {2}) == nullptr);
    CHECK(ItemBehaviorConfig(item, {}) == nullptr);
    CHECK(ItemBehaviorConfig(item, {3})->Schema().countdown);
    CHECK(ItemBehaviorConfig(item, {3})->Schema().maximumDelay == 300);
}

TEST_CASE("Item IDs are source-order independent and globally unique", "[content][item-content]") {
    const ContentSource sources[] = {
        {"z.json5", "{format_version:1,definitions:[{type:'item',id:'z',name:'Z',mass_g:1,volume_ml:1}]}"},
        {"a.json5", "{format_version:1,definitions:[{type:'item',id:'a',name:'A',mass_g:4294967295,volume_ml:4294967295}]}"},
    };
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto loaded = LoadContent({sources, 2});
    const auto reordered = LoadContent({reversed, 2});
    REQUIRE(loaded.has_value());
    REQUIRE(reordered.has_value());
    CHECK(loaded->ItemCount() == 2);
    CHECK(loaded->FindItem("a") == ItemTypeId{1});
    CHECK(loaded->FindItem("z") == ItemTypeId{2});
    CHECK(loaded->FindItem("z") == reordered->FindItem("z"));
    CHECK(loaded->FindItem("missing") == ItemTypeId{});
    CHECK(loaded->TryGetItem({}) == nullptr);
    CHECK(loaded->TryGetItem({3}) == nullptr);
    CHECK(loaded->TryGetItem({0xffffffff}) == nullptr);
    Invalid("{type:'item',id:'same',name:'A',mass_g:1,volume_ml:1},"
            "{type:'item',id:'same',name:'B',mass_g:1,volume_ml:1}");
    Invalid("{type:'item',id:'same',name:'A',mass_g:1,volume_ml:1},"
            "{type:'terrain',id:'same',name:'floor',movement:{passable:true},blocks_sight:false,"
            "appearance:{symbol:'.',foreground:'white'}}");
    Invalid("{type:'item',id:'same',name:'A',mass_g:1,volume_ml:1},"
            "{type:'building_pattern',id:'same',rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0}}");
    const auto empty = Load("");
    REQUIRE(empty.has_value());
    CHECK(empty->ItemCount() == 0);
}

TEST_CASE("Item physical schema is strict", "[content][item-content]") {
    for (const char* definition : {
        "{type:'item',id:'x',name:'X',volume_ml:1}",
        "{type:'item',id:'x',name:'X',mass_g:0}",
        "{type:'item',id:'x',mass_g:0,volume_ml:1}",
        "{type:'item',id:'x',name:'',mass_g:0,volume_ml:1}",
        "{type:'item',id:'Bad',name:'X',mass_g:0,volume_ml:1}",
        "{type:'item',id:'x',name:'X',mass_g:-1,volume_ml:1}",
        "{type:'item',id:'x',name:'X',mass_g:1.5,volume_ml:1}",
        "{type:'item',id:'x',name:'X',mass_g:4294967296,volume_ml:1}",
        "{type:'item',id:'x',name:'X',mass_g:false,volume_ml:1}",
        "{type:'item',id:'x',name:'X',mass_g:0,volume_ml:0}",
        "{type:'item',id:'x',name:'X',mass_g:0,volume_ml:4294967296}",
        "{type:'item',id:'x',name:'X',mass_g:0,volume_ml:1,extra:1}",
        "{type:'item',id:'x',name:'X',mass_g:0,mass_g:0,volume_ml:1}",
        "{type:'item',id:'x',name:'X',mass_g:0,volume_ml:1,capabilities:{}}",
        "{type:'item',id:'x',name:'X',mass_g:0,volume_ml:1,capabilities:null}",
    }) { CAPTURE(definition); Invalid(definition); }
}

TEST_CASE("Item capability schemas reject ambiguous or invalid definitions", "[content][item-content]") {
    for (const char* capabilities : {
        "null",
        "{type:'unknown',id:'x'}",
        "{type:'container',capacity_volume_ml:1}",
        "{type:'container',id:'Bad',capacity_volume_ml:1}",
        "{type:'container',id:'x',capacity_volume_ml:0}",
        "{type:'container',id:'x',capacity_volume_ml:4294967296}",
        "{type:'container',id:'x',capacity_volume_ml:1,extra:1}",
        "{type:'container',id:'x',capacity_volume_ml:1,capacity_volume_ml:1}",
        "{type:'container',id:'x',capacity_volume_ml:1},{type:'container',id:'y',capacity_volume_ml:1}",
        "{type:'door_controller',id:'x',delay_ticks:1},{type:'door_controller',id:'x',delay_ticks:2}",
        "{type:'energy_store',id:'x',capacity_energy:1}",
        "{type:'energy_store',id:'x',capacity_energy:0,rechargeable:true}",
        "{type:'energy_store',id:'x',capacity_energy:1,rechargeable:1}",
        "{type:'energy_store',id:'x',capacity_energy:1,rechargeable:true,initial_energy:2}",
        "{type:'energy_store',id:'x',capacity_energy:1,rechargeable:true,battery_class:''}",
        "{type:'energy_store',id:'x',capacity_energy:1,rechargeable:true,battery_class:'AA'}",
        "{type:'energy_store',id:'x',capacity_energy:1,rechargeable:true,extra:1}",
        "{type:'energy_store',id:'x',capacity_energy:1,rechargeable:true,rechargeable:true}",
        "{type:'door_controller',id:'x'}",
        "{type:'door_controller',id:'x',delay_ticks:0}",
        "{type:'door_controller',id:'x',delay_ticks:1,action_cost:0}",
        "{type:'door_controller',id:'x',delay_ticks:1,extra:1}",
        "{type:'door_controller',id:'x',delay_ticks:1,delay_ticks:1}",
        "{type:'charger',id:'x',power_source:'missing',battery_class:'aa',energy_per_turn:3}",
        "{type:'charger',id:'x',power_source:'x',battery_class:'aa',energy_per_turn:3}",
        "{type:'charger',id:'x',battery_class:'aa',energy_per_turn:3}",
        "{type:'charger',id:'x',power_source:'p',energy_per_turn:3}",
        "{type:'charger',id:'x',power_source:'p',battery_class:'aa',energy_per_turn:0}",
        "{type:'charger',id:'x',power_source:'p',battery_class:'aa',energy_per_turn:3,action_cost:0}",
        "{type:'charger',id:'x',power_source:'p',battery_class:'aa',energy_per_turn:3,extra:1}",
        "{type:'charger',id:'x',power_source:'p',power_source:'p',battery_class:'aa',energy_per_turn:3}",
        "{type:'door_controller',id:'p',delay_ticks:1},{type:'charger',id:'x',power_source:'p',battery_class:'aa',energy_per_turn:3}",
        "{type:'energy_store',id:'p',capacity_energy:1,rechargeable:true,battery_class:'aa'},"
            "{type:'charger',id:'x',power_source:'p',battery_class:'aa',energy_per_turn:3}",
        "{type:'energy_store',id:'p',capacity_energy:1,rechargeable:true,battery_class:'aa'},"
            "{type:'energy_store',id:'q',capacity_energy:1,rechargeable:false}",
        "{type:'energy_store',id:'p',capacity_energy:1,rechargeable:false},"
            "{type:'charger',id:'x',power_source:'p',battery_class:'aa',energy_per_turn:3},"
            "{type:'charger',id:'y',power_source:'p',battery_class:'aa',energy_per_turn:3}",
    }) { CAPTURE(capabilities); Invalid(View(Item(capabilities))); }
}

TEST_CASE("Item capability limit and independent internal stores are accepted", "[content][item-content]") {
    String capabilities;
    for (int i = 0; i < 16; ++i) {
        if (i != 0) { capabilities += ','; }
        capabilities += "{type:'door_controller',id:'timer_";
        capabilities += static_cast<char>('a' + i);
        capabilities += "',delay_ticks:1,action_cost:7}";
    }
    const auto loaded = Load(View(Item(View(capabilities))));
    REQUIRE(loaded.has_value());
    CHECK(loaded->GetItem({1}).capabilities.size() == 16);
    CHECK(loaded->GetItem({1}).capabilities[0].config.GetUnsigned("action_cost").value() == 7);
    capabilities += ",{type:'container',id:'storage',capacity_volume_ml:1}";
    Invalid(View(Item(View(capabilities))));
    const auto independent = Load(View(Item(R"(
        {type:'energy_store',id:'p',capacity_energy:1,rechargeable:false},
        {type:'energy_store',id:'q',capacity_energy:2,rechargeable:false},
        {type:'charger',id:'x',power_source:'p',battery_class:'aa',energy_per_turn:3,action_cost:9},
        {type:'charger',id:'y',power_source:'q',battery_class:'aa',energy_per_turn:4}
    )")));
    REQUIRE(independent.has_value());
    const auto& definition = independent->GetItem({1});
    CHECK(definition.capabilities[2].config.GetString("power_source").value() == "p");
    CHECK(definition.capabilities[2].config.GetUnsigned("action_cost").value() == 9);
    CHECK(definition.capabilities[3].config.GetString("power_source").value() == "q");
}

TEST_CASE("Bundled items expose physical values and capability combinations", "[content][item-content]") {
    const auto& catalog = TestContent();
    CHECK(catalog.ItemCount() == 14);
    for (const char* id : {"blinking_lightbulb", "toggle_lightbulb", "single_blink_lightbulb"}) {
        const auto& bulb = catalog.GetItem(catalog.FindItem(id));
        CHECK(bulb.massGrams == 50);
        CHECK(bulb.volumeMl == 100);
        CHECK(bulb.capabilities.empty());
    }
    const auto& backpack = catalog.GetItem(catalog.FindItem("backpack"));
    CHECK(backpack.name == "backpack");
    CHECK(backpack.massGrams == 1000);
    CHECK(backpack.volumeMl == 2000);
    REQUIRE(ItemContainer(backpack) != nullptr);
    CHECK(ItemContainer(backpack)->capacityVolumeMl == 20000);
    const auto& rock = catalog.GetItem(catalog.FindItem("rock"));
    CHECK(rock.name == "rock");
    CHECK(rock.massGrams == 500);
    CHECK(rock.volumeMl == 250);
    const auto& bandage = catalog.GetItem(catalog.FindItem("bandage"));
    CHECK(bandage.name == "bandage");
    CHECK(bandage.massGrams == 20);
    CHECK(bandage.volumeMl == 50);
    const auto& disposable = catalog.GetItem(catalog.FindItem("disposable_battery"));
    REQUIRE(ItemEnergyStore(disposable, {1}) != nullptr);
    const auto& disposableStore = *ItemEnergyStore(disposable, {1});
    CHECK(disposableStore.batteryClass == "aa");
    CHECK(disposableStore.capacityEnergy == 1000);
    CHECK(disposableStore.initialEnergy == 1000);
    CHECK_FALSE(disposableStore.rechargeable);
    const auto& rechargeable = catalog.GetItem(catalog.FindItem("rechargeable_battery"));
    REQUIRE(ItemEnergyStore(rechargeable, {1}) != nullptr);
    const auto& rechargeableStore = *ItemEnergyStore(rechargeable, {1});
    CHECK(rechargeableStore.initialEnergy == 0);
    CHECK(rechargeableStore.rechargeable);
    const auto& utility = catalog.GetItem(catalog.FindItem("utility_controller"));
    CHECK(utility.capabilities.size() == 3);
    CHECK(utility.capabilities[0].config.GetUnsigned("energy_per_turn").value() == 3);
    CHECK(utility.capabilities[2].config.GetUnsigned("delay_ticks").value() == 300);
}

ContentResult<ParsedItemBehavior> InspectCustomInput(const DataObject& input,
    const ItemDefinition& item, ItemCapabilityId self, Diagnostics* diagnostics) {
    CHECK(self == ItemCapabilityId{1});
    REQUIRE(item.capabilities.size() == 2);
    CHECK(item.capabilities[0].type == "custom");
    REQUIRE(ItemEnergyStore(item, {2}) != nullptr);
    CHECK(ItemEnergyStore(item, {2})->initialEnergy == 5);
    CHECK(input.GetUnsigned("positive").value() == 7);
    CHECK(input.GetSigned("negative").value() == -7);
    const auto tagged = input.Get("nested", DataType::Object);
    REQUIRE(tagged.has_value());
    // A content object resembling an archive signed tag remains an object.
    CHECK((*tagged)->GetString("$i64").value() == "7");
    if (diagnostics) { diagnostics->Add("custom factory inspected input"); }
    return eastl::unexpected(ContentError::InvalidData);
}

TEST_CASE("Behavior registry validates explicit registrations and owns type names", "[content][item-content]") {
    ItemBehaviorRegistry registry;
    String type = "custom";
    REQUIRE(registry.Add(View(type), InspectCustomInput).has_value());
    type = "changed";
    CHECK(registry.Find("custom") == InspectCustomInput);
    CHECK(registry.Find("changed") == nullptr);
    for (const char* invalid : {"", "Bad", "two-words", "container", "energy_store", "custom"}) {
        const auto result = registry.Add(invalid, InspectCustomInput);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
    }
    CHECK_FALSE(registry.Add("null_parser", nullptr).has_value());
    const auto definition = Item("{type:'custom',id:'a',positive:7,negative:-7,nested:{$i64:'7'}},"
        "{type:'energy_store',id:'z',capacity_energy:5,rechargeable:false}");
    String text = "{format_version:1,definitions:[";
    text += definition;
    text += "]}";
    const ContentSource source{"custom.json5", View(text)};
    Diagnostics diagnostics;
    const auto result = LoadContent({&source, 1}, registry, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames().front().message == "custom factory inspected input");
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "custom.json5");
    CHECK(diagnostics.Frames().front().location->path == "definitions[0].capabilities[0]");
}

TEST_CASE("Explicit builtin registry may die before its loaded catalog", "[content][item-content]") {
    const auto definition = Item("{type:'door_controller',id:'timer',delay_ticks:7}");
    String text = "{format_version:1,definitions:[";
    text += definition;
    text += "]}";
    const ContentSource source{"explicit.json5", View(text)};
    const auto load = [&]() {
        ItemBehaviorRegistry registry;
        RegisterBuiltinItemBehaviors(registry);
        return LoadContent({&source, 1}, registry);
    };
    const auto loaded = load();
    const auto separate = load();
    REQUIRE(loaded.has_value());
    REQUIRE(separate.has_value());
    const auto& item = loaded->GetItem({1});
    CHECK(item == separate->GetItem({1}));
    REQUIRE(item.behaviors.size() == 1);
    CHECK(item.behaviors[0].definition.get() != separate->GetItem({1}).behaviors[0].definition.get());
    CHECK(ItemBehaviorConfig(item, {1})->Schema().maximumDelay == 7);
    auto copy = *loaded;
    CHECK(copy.GetItem({1}) == item);
    CHECK(copy.GetItem({1}).behaviors[0].definition == item.behaviors[0].definition);
    ItemBehaviorRegistry empty;
    CHECK_FALSE(LoadContent({&source, 1}, empty).has_value());
}

TEST_CASE("Content capability data rejects unsupported values and excessive nesting", "[content][item-content]") {
    for (const char* value : {"null", "1.5", "NaN", "Infinity", "{a:1,a:1}", "{a:{b:1,b:1}}"}) {
        String capability = "{type:'door_controller',id:'timer',delay_ticks:1,extra:";
        capability += value;
        capability += '}';
        Invalid(View(Item(View(capability))));
    }
    String capability = "{type:'door_controller',id:'timer',delay_ticks:1,extra:";
    for (int i = 0; i < 66; ++i) { capability += '['; }
    capability += '0';
    for (int i = 0; i < 66; ++i) { capability += ']'; }
    capability += '}';
    Invalid(View(Item(View(capability))));
}

TEST_CASE("Shared capability configs normalize defaults and object field order", "[content][item-content]") {
    const auto implicit = Load(View(Item("{type:'container',id:'bag',capacity_volume_ml:9},"
        "{type:'energy_store',id:'power',capacity_energy:5,rechargeable:false}")));
    const auto explicitDefaults = Load(View(Item("{rechargeable:false,initial_energy:5,capacity_energy:5,id:'power',type:'energy_store'},"
        "{capacity_volume_ml:9,id:'bag',type:'container'}")));
    REQUIRE(implicit.has_value());
    REQUIRE(explicitDefaults.has_value());
    CHECK(implicit->GetItem({1}) == explicitDefaults->GetItem({1}));
    const auto& item = implicit->GetItem({1});
    REQUIRE(item.container.has_value());
    CHECK(item.container->capability == ItemCapabilityId{1});
    CHECK(item.capabilities[0].config.GetUnsigned("capacity_volume_ml").value() == 9);
    CHECK(item.capabilities[1].config.GetUnsigned("initial_energy").value() == 5);
    CHECK(item.capabilities[1].config.GetString("battery_class").value().empty());
    auto different = item;
    different.capabilities[1].config.SetUnsigned("initial_energy", 4);
    CHECK_FALSE(item == different);
}

// This extension only supplies metadata. Loading must validate it without ever
// creating runtime state or relying on behavior-specific composition checks.
class SchemaTestDefinition final : public ItemBehaviorDefinition {
public:
    explicit SchemaTestDefinition(ItemBehaviorSchema schema) : ItemBehaviorDefinition(eastl::move(schema)) {}
    StateResult<eastl::unique_ptr<ItemBehavior>> Create(ItemCapabilityId,
        const DataObject*, Diagnostics*) const override {
        return eastl::unexpected(StateError::UnsupportedVersion);
    }
    StateResult<void> ValidateSnapshot(const ItemSnapshotContext&, PersistentId,
        ItemCapabilityId, Diagnostics*) const override {
        return eastl::unexpected(StateError::UnsupportedVersion);
    }
};

ContentResult<ParsedItemBehavior> ParseSchemaTest(const DataObject& input,
    const ItemDefinition&, ItemCapabilityId, Diagnostics*) {
    ItemBehaviorSchema schema;
    std::uint64_t count = 1;
    if (const auto value = input.GetUnsigned("actions")) { count = *value; }
    for (std::uint64_t i = 0; i < count; ++i) {
        ItemActionDefinition action;
        action.id = "action_";
        action.id += static_cast<char>('a' + i / 26);
        action.id += static_cast<char>('a' + i % 26);
        action.label = {"item.action", "Act"};
        schema.actions.push_back(eastl::move(action));
    }
    const auto modeValue = input.GetString("mode");
    const auto mode = modeValue ? *modeValue : eastl::string_view{};
    if (mode == "zero_cost") { schema.actions[0].baseCost = 0; }
    else if (mode == "bad_argument") { schema.actions[0].argument = static_cast<ItemActionArgument>(3); }
    else if (mode == "bad_category") { schema.actions[0].category = static_cast<ItemActionCategory>(2); }
    else if (mode == "empty_id") { schema.actions[0].id.clear(); }
    else if (mode == "bad_id") { schema.actions[0].id = "Bad"; }
    else if (mode == "long_id") { schema.actions[0].id.assign(maxItemActionName + 1, 'a'); }
    else if (mode == "duplicate_ids") { schema.actions.push_back(schema.actions[0]); }
    else if (mode == "unsorted_ids") {
        auto action = schema.actions[0];
        schema.actions[0].id = "z";
        schema.actions.push_back(eastl::move(action));
    }
    else if (mode == "long_key") { schema.actions[0].label.key.assign(maxItemText + 1, 'k'); }
    else if (mode == "long_fallback") { schema.actions[0].label.fallback.assign(maxItemText + 1, 'f'); }
    else if (mode == "control_key") { schema.actions[0].label.key = "bad\nkey"; }
    else if (mode == "control_fallback") { schema.actions[0].label.fallback = "bad\tfallback"; }
    else if (mode == "nul_key") { schema.actions[0].label.key.assign(1, '\0'); }
    else if (mode == "delete_fallback") { schema.actions[0].label.fallback.assign(1, '\x7f'); }
    else if (mode == "zero_version") { schema.implementationVersion = 0; }
    else if (mode == "countdown_no_activity") { schema.countdown = true; schema.maximumDelay = 1; }
    else if (mode == "countdown_zero_delay") { schema.hasActivity = true; schema.countdown = true; }
    else if (mode == "delay_no_countdown") { schema.hasActivity = true; schema.maximumDelay = 1; }
    else if (mode == "delay_no_activity") { schema.maximumDelay = 1; }
    else if (mode == "valid_activity") { schema.hasActivity = true; }
    else if (mode == "valid_countdown") { schema.hasActivity = true; schema.countdown = true; schema.maximumDelay = 1; }
    else if (mode == "valid_boundaries") {
        auto& action = schema.actions[0];
        action.id.assign(maxItemActionName, 'a');
        action.baseCost = 1;
        action.argument = ItemActionArgument::Direction;
        action.category = ItemActionCategory::ManageContents;
        action.label.key.assign(maxItemText, 'k');
        action.label.fallback.assign(maxItemText, 'f');
    }
    BehaviorDefinitionPtr definition = eastl::allocate_shared<SchemaTestDefinition>(FatalAllocator{}, eastl::move(schema));
    return ParsedItemBehavior{eastl::move(definition), input};
}

ContentResult<ContentCatalog> LoadSchemaTest(eastl::string_view capabilities, Diagnostics* diagnostics = nullptr) {
    ItemBehaviorRegistry registry;
    if (const auto registered = registry.Add("schema_test", ParseSchemaTest); !registered) {
        return eastl::unexpected(registered.error());
    }
    String text = "{format_version:1,definitions:[";
    text += Item(capabilities);
    text += "]}";
    const ContentSource source{"schemas.json5", View(text)};
    return LoadContent({&source, 1}, registry, diagnostics);
}

TEST_CASE("Content rejects malformed extension schemas before publishing a catalog", "[content][item-content]") {
    for (const char* mode : {"zero_cost", "bad_argument", "bad_category", "empty_id", "bad_id", "long_id",
        "duplicate_ids", "unsorted_ids", "long_key", "long_fallback", "control_key", "control_fallback",
        "nul_key", "delete_fallback", "zero_version", "countdown_no_activity", "countdown_zero_delay",
        "delay_no_countdown", "delay_no_activity"}) {
        CAPTURE(mode);
        String capability = "{type:'schema_test',id:'extension',mode:'";
        capability += mode;
        capability += "'}";
        Diagnostics diagnostics;
        const auto result = LoadSchemaTest(View(capability), &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE_FALSE(diagnostics.Empty());
        CHECK(diagnostics.Frames().front().message == "invalid item behavior schema");
        REQUIRE(diagnostics.Frames().front().location.has_value());
        CHECK(diagnostics.Frames().front().location->file == "schemas.json5");
        CHECK(diagnostics.Frames().front().location->path == "definitions[0].capabilities[0].schema");
        const auto withoutDiagnostics = LoadSchemaTest(View(capability));
        REQUIRE_FALSE(withoutDiagnostics.has_value());
        CHECK(withoutDiagnostics.error() == ContentError::InvalidData);
    }
}

TEST_CASE("Valid extension schema boundaries load with default composition validation", "[content][item-content]") {
    for (const char* mode : {"valid_boundaries", "valid_activity", "valid_countdown"}) {
        CAPTURE(mode);
        String capability = "{type:'schema_test',id:'extension',mode:'";
        capability += mode;
        capability += "'}";
        const auto loaded = LoadSchemaTest(View(capability));
        REQUIRE(loaded.has_value());
        const auto* definition = ItemBehaviorConfig(loaded->GetItem({1}), {1});
        REQUIRE(definition != nullptr);
        CHECK(ValidItemBehaviorSchema(definition->Schema()));
    }
    const auto noActions = LoadSchemaTest("{type:'schema_test',id:'extension',actions:0}");
    REQUIRE(noActions.has_value());
    CHECK(ItemBehaviorConfig(noActions->GetItem({1}), {1})->Schema().actions.empty());
}

TEST_CASE("Declared action bounds apply to both individual schemas and item composition", "[content][item-content]") {
    const auto maximum = LoadSchemaTest("{type:'schema_test',id:'extension',actions:64}");
    REQUIRE(maximum.has_value());
    CHECK(ItemBehaviorConfig(maximum->GetItem({1}), {1})->Schema().actions.size() == maxItemActionsPerItem);
    Diagnostics diagnostics;
    const auto oversized = LoadSchemaTest("{type:'schema_test',id:'extension',actions:65}", &diagnostics);
    REQUIRE_FALSE(oversized.has_value());
    CHECK(oversized.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames().front().message == "invalid item behavior schema");
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "schemas.json5");
    CHECK(diagnostics.Frames().front().location->path == "definitions[0].capabilities[0].schema");
    const auto combinedMaximum = LoadSchemaTest("{type:'schema_test',id:'a',actions:32},"
        "{type:'schema_test',id:'b',actions:32}");
    REQUIRE(combinedMaximum.has_value());
    diagnostics.Clear();
    // Input order differs from lexical processing order: report the source entry
    // that pushes the composition past the bound, not its sorted ordinal.
    const auto combinedOversized = LoadSchemaTest("{type:'schema_test',id:'b',actions:33},"
        "{type:'schema_test',id:'a',actions:32}", &diagnostics);
    REQUIRE_FALSE(combinedOversized.has_value());
    CHECK(combinedOversized.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames().front().message == "at most 64 declared actions per item are allowed");
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "schemas.json5");
    CHECK(diagnostics.Frames().front().location->path == "definitions[0].capabilities[0].schema.actions");
    CHECK_FALSE(LoadSchemaTest("{type:'schema_test',id:'a',actions:32},"
        "{type:'schema_test',id:'b',actions:33}").has_value());
}

TEST_CASE("Shared item text validation matches wire byte bounds", "[content][item-content]") {
    CHECK(ValidItemText({}));
    ItemText text{String(maxItemText, 'k'), String(maxItemText, 'f')};
    CHECK(ValidItemText(text));
    text.key += 'k';
    CHECK_FALSE(ValidItemText(text));
    text.key.clear();
    text.fallback += 'f';
    CHECK_FALSE(ValidItemText(text));
    for (unsigned byte = 0; byte <= 0x7f; ++byte) {
        if (byte >= 0x20 && byte != 0x7f) { continue; }
        CAPTURE(byte);
        const String control(1, static_cast<char>(byte));
        CHECK_FALSE(ValidItemText({control, "fallback"}));
        CHECK_FALSE(ValidItemText({"key", control}));
    }
    CHECK(ValidItemText({"item.label", "\xc3\xa9"}));
}

TEST_CASE("Shared use reason validation ties nonempty reasons to refusal", "[content][item-content]") {
    const ItemUseReason reason{"no_power", {"item.no_power", "No power"}};
    for (const auto outcome : {ItemUseOutcome::Succeeded, ItemUseOutcome::InvalidAction,
        ItemUseOutcome::Inaccessible, ItemUseOutcome::Refused}) {
        CAPTURE(outcome);
        CHECK(ValidItemUseReason(outcome, {}));
        CHECK(ValidItemUseReason(outcome, reason) == (outcome == ItemUseOutcome::Refused));
    }
    CHECK_FALSE(ValidItemUseReason(static_cast<ItemUseOutcome>(4), {}));
    CHECK_FALSE(ValidItemUseReason(static_cast<ItemUseOutcome>(255), reason));
    CHECK(ValidItemUseReason(ItemUseOutcome::Refused, {"reason", {}}));
    CHECK_FALSE(ValidItemUseReason(ItemUseOutcome::Refused, {"", {"key", "message"}}));
    CHECK_FALSE(ValidItemUseReason(ItemUseOutcome::Refused, {"Bad", {}}));
    CHECK_FALSE(ValidItemUseReason(ItemUseOutcome::Refused, {String(maxItemActionName + 1, 'a'), {}}));
    CHECK_FALSE(ValidItemUseReason(ItemUseOutcome::Refused, {"reason", {"key", "bad\nmessage"}}));
    CHECK_FALSE(ValidItemUseReason(ItemUseOutcome::Refused, {"reason", {"key", String(maxItemText + 1, 'a')}}));
}

}
