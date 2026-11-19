#include "content/content.h"
#include "network/network.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <initializer_list>

namespace {
using namespace cataclysmx;

ContentResult<ContentCatalog> LoadPower(eastl::string_view fields,
    eastl::string_view objects = {}, Diagnostics* diagnostics = nullptr) {
    String text = "{format_version:1,definitions:[{type:'item',id:'part',name:'Part',mass_g:1,volume_ml:1";
    if (!fields.empty()) { text += ','; text.append(fields.data(), fields.size()); }
    text += "},{type:'item',id:'copper_wire',name:'Copper wire',mass_g:200,volume_ml:200},"
        "{type:'tool_trait',id:'soldering',name:'Soldering'}],objects:[";
    text.append(objects.data() ? objects.data() : "", objects.size());
    text += "]}";
    const ContentSource source{"power.json5", {text.data(), text.size()}};
    return LoadContent({&source, 1}, diagnostics);
}

constexpr eastl::string_view batteryObject = "{id:'station',name:'Station',behavior:'grid_battery'}";
constexpr eastl::string_view generatorObject = R"({id:'generator',name:'Generator',behavior:'generator',
    generator:{fuel_item:'part',fuel_capacity:6000,energy_per_fuel:2000,output_per_tick:4}})";
constexpr eastl::string_view gridMachine = R"({id:'machine',name:'Machine',behavior:'crafting_machine',
    machine:{input:'part',input_count:1,output:'part',output_count:1,duration_ticks:100,
      energy_capacity:0,energy_per_tick:2,grid_powered:true}})";

TEST_CASE("Power item schema resolves ordinary materials and placed objects", "[content][power-content]") {
    const auto wiring = LoadPower("wiring_material:'copper_wire',grid_meter:true");
    REQUIRE(wiring.has_value());
    const auto& kit = wiring->GetItem(wiring->FindItem("part"));
    CHECK(kit.gridMeter);
    CHECK(kit.wiringMaterial == wiring->FindItem("copper_wire"));
    CHECK(kit.wiringTilesPerMaterial == 16);
    CHECK(kit.energyStores.empty());
    for (const auto coverage : {1, 256}) {
        const auto fields = coverage == 1 ? "wiring_material:'copper_wire',wiring_tiles_per_material:1"
            : "wiring_material:'copper_wire',wiring_tiles_per_material:256";
        const auto custom = LoadPower(fields);
        REQUIRE(custom.has_value());
        CHECK(custom->GetItem(custom->FindItem("part")).wiringTilesPerMaterial == coverage);
    }

    const auto battery = LoadPower(R"(placed_object:'station',
      capabilities:[{id:'power',type:'energy_store',capacity_energy:1000,rechargeable:true}])", batteryObject);
    REQUIRE(battery.has_value());
    CHECK(battery->GetItem(battery->FindItem("part")).placedObject == battery->FindObject("station"));

    const auto generator = LoadPower("placed_object:'generator'", generatorObject);
    REQUIRE(generator.has_value());
    const auto& config = generator->GetObject(generator->FindObject("generator")).generator;
    CHECK(config.fuelItem == generator->FindItem("part"));
    CHECK(config.fuelCapacity == 6000);
    CHECK(config.energyPerFuel == 2000);
    CHECK(config.outputPerTick == 4);
}

TEST_CASE("Grid crafting and machine power are explicit without a local store", "[content][power-content]") {
    const auto loaded = LoadPower("crafting_power:{store:'grid',energy_per_work:10000}", gridMachine);
    REQUIRE(loaded.has_value());
    const auto& tool = loaded->GetItem(loaded->FindItem("part"));
    CHECK(tool.craftingGridPower);
    CHECK(tool.craftingPower.value == 0);
    CHECK(tool.energyStores.empty());
    CHECK(tool.craftingEnergyPerWork == 10000);
    const auto& machine = loaded->GetObject(loaded->FindObject("machine")).machine;
    CHECK(machine.gridPowered);
    CHECK(machine.energyCapacity == 0);
    const auto defaults = LoadPower({});
    REQUIRE(defaults.has_value());
    const auto& item = defaults->GetItem(defaults->FindItem("part"));
    CHECK_FALSE(item.craftingGridPower);
    CHECK_FALSE(item.gridMeter);
    CHECK(item.wiringMaterial.value == 0);
    CHECK(item.wiringTilesPerMaterial == 16);
    CHECK(LoadPower("wiring_tiles_per_material:16").has_value());
    CHECK(item.placedObject.value == 0);
}

TEST_CASE("Power item schema rejects invalid references rates and conflicting stores", "[content][power-content]") {
    for (const auto fields : {
        "crafting_power:'grid'",
        "crafting_power:{store:'grid',energy_per_work:0}",
        "crafting_power:{store:'grid',energy_per_work:-1}",
        "crafting_power:{store:'grid',energy_per_work:1.5}",
        "crafting_power:{store:'grid',energy_per_work:4294967296}",
        "crafting_power:{store:'grid',energy_per_work:1,local_store:'battery'}",
        "crafting_power:{store:'grid',energy_per_work:1},capabilities:[{id:'power',type:'energy_store',capacity_energy:1,rechargeable:false}]",
        "wiring_supply:'wire'", "wiring_material:'missing'", "wiring_material:'part'",
        "wiring_material:1", "wiring_tiles_per_material:1", "wiring_tiles_per_material:256",
        "wiring_material:'copper_wire',wiring_tiles_per_material:0",
        "wiring_material:'copper_wire',wiring_tiles_per_material:257",
        "wiring_material:'copper_wire',wiring_tiles_per_material:1.5",
        "wiring_material:'copper_wire',wiring_tiles_per_material:true",
        "placed_object:'unknown'", "placed_object:4", "grid_meter:1"}) {
        INFO(fields);
        Diagnostics diagnostics;
        CHECK_FALSE(LoadPower(fields, {}, &diagnostics).has_value());
        CHECK_FALSE(diagnostics.Empty());
        CHECK_FALSE(LoadPower(fields).has_value());
    }
    CHECK_FALSE(LoadPower("placed_object:'station'", batteryObject).has_value());
    CHECK_FALSE(LoadPower(R"(placed_object:'station',capabilities:[
      {id:'power',type:'energy_store',capacity_energy:1000,rechargeable:false}])", batteryObject).has_value());
}

TEST_CASE("Power object schema rejects mismatched configs and invalid fuel references", "[content][power-content]") {
    for (const auto object : {
        "{id:'g',name:'G',behavior:'generator'}",
        "{id:'g',name:'G',behavior:'generator',generator:{fuel_item:'unknown',fuel_capacity:10,energy_per_fuel:2,output_per_tick:1}}",
        "{id:'g',name:'G',behavior:'generator',generator:{fuel_item:'part',fuel_capacity:1,energy_per_fuel:2,output_per_tick:1}}",
        "{id:'g',name:'G',behavior:'generator',generator:{fuel_item:'part',fuel_capacity:10,energy_per_fuel:0,output_per_tick:1}}",
        "{id:'g',name:'G',behavior:'generator',generator:{fuel_item:'part',fuel_capacity:10,energy_per_fuel:2,output_per_tick:0}}",
        "{id:'g',name:'G',behavior:'grid_battery',generator:{}}",
        "{id:'g',name:'G',behavior:'grid_battery',machine:{}}",
        "{id:'g',name:'G',behavior:'grid_battery',capacity_volume_ml:1}",
        "{id:'g',name:'G',behavior:'none',generator:{}}",
        "{id:'g',name:'G',behavior:'crafting_machine',machine:{input:'part',input_count:1,output:'part',output_count:1,duration_ticks:1,energy_capacity:1,energy_per_tick:1,grid_powered:true}}",
        "{id:'g',name:'G',behavior:'crafting_machine',machine:{input:'part',input_count:1,output:'part',output_count:1,duration_ticks:1,energy_capacity:0,energy_per_tick:1}}"}) {
        INFO(object);
        Diagnostics diagnostics;
        CHECK_FALSE(LoadPower({}, object, &diagnostics).has_value());
        CHECK_FALSE(diagnostics.Empty());
    }
}

TEST_CASE("Powered manual station schema validates rates traits and behaviors", "[content][power-content]") {
    constexpr auto passive = "{id:'bench',name:'Bench',behavior:'none',tool_traits:[{id:'soldering',tier:1}]}";
    const auto baseline = LoadPower({}, passive);
    REQUIRE(baseline);
    CHECK(baseline->GetObject(baseline->FindObject("bench")).craftingEnergyPerWork == 0);
    for (const auto rate : {"1", "10000", "4294967295"}) {
        String object = "{id:'bench',name:'Bench',behavior:'none',tool_traits:[{id:'soldering',tier:1}],crafting_power:{store:'grid',energy_per_work:";
        object += rate;
        object += "}}";
        const auto loaded = LoadPower({}, object);
        REQUIRE(loaded);
        CHECK(ValidObjectDefinition(loaded->GetObject(loaded->FindObject("bench"))));
        CHECK(network::ContentFingerprint(*baseline) != network::ContentFingerprint(*loaded));
    }
    for (const auto power : {"null", "'grid'", "{}", "{store:'grid'}", "{energy_per_work:1}",
                             "{store:'battery',energy_per_work:1}", "{store:'grid',energy_per_work:0}",
                             "{store:'grid',energy_per_work:-1}", "{store:'grid',energy_per_work:1.5}",
                             "{store:'grid',energy_per_work:4294967296}", "{store:'grid',energy_per_work:true}",
                             "{store:'grid',energy_per_work:1,extra:1}",
                             "{store:'grid',store:'grid',energy_per_work:1}",
                             "{store:'grid',energy_per_work:1,energy_per_work:2}"}) {
        INFO(power);
        String object = "{id:'bench',name:'Bench',behavior:'none',tool_traits:[{id:'soldering',tier:1}],crafting_power:";
        object += power;
        object += '}';
        Diagnostics diagnostics;
        CHECK_FALSE(LoadPower({}, object, &diagnostics));
        CHECK_FALSE(diagnostics.Empty());
    }
    for (const auto object : {
        "{id:'bench',name:'Bench',behavior:'none',crafting_power:{store:'grid',energy_per_work:1}}",
        "{id:'bench',name:'Bench',behavior:'none',tool_traits:[],crafting_power:{store:'grid',energy_per_work:1}}",
        "{id:'bench',name:'Bench',behavior:'grid_battery',crafting_power:{store:'grid',energy_per_work:1}}",
        "{id:'bench',name:'Bench',behavior:'generator',crafting_power:{store:'grid',energy_per_work:1}}",
        "{id:'bench',name:'Bench',behavior:'crafting_machine',crafting_power:{store:'grid',energy_per_work:1}}",
        "{id:'bench',name:'Bench',behavior:'forge',crafting_power:{store:'grid',energy_per_work:1}}"}) {
        INFO(object);
        CHECK_FALSE(LoadPower({}, object));
    }
    auto invalid = baseline->GetObject(baseline->FindObject("bench"));
    invalid.craftingEnergyPerWork = 1;
    CHECK(ValidObjectDefinition(invalid));
    invalid.behavior = ObjectBehaviorKind::Forge;
    CHECK_FALSE(ValidObjectDefinition(invalid));
    invalid.behavior = ObjectBehaviorKind::None;
    invalid.toolTraits = {};
    CHECK_FALSE(ValidObjectDefinition(invalid));
}

TEST_CASE("Powered stations preserve the four trait limit", "[content][power-content]") {
    constexpr ContentSource four{"four.json5", R"({format_version:1,definitions:[
        {type:'tool_trait',id:'a',name:'A'},{type:'tool_trait',id:'b',name:'B'},
        {type:'tool_trait',id:'c',name:'C'},{type:'tool_trait',id:'d',name:'D'},
        {type:'tool_trait',id:'e',name:'E'}
    ],objects:[{id:'bench',name:'Bench',behavior:'none',crafting_power:{store:'grid',energy_per_work:1},
        tool_traits:[{id:'a',tier:1},{id:'b',tier:1},{id:'c',tier:1},{id:'d',tier:1}]}]})"};
    constexpr ContentSource five{"five.json5", R"({format_version:1,definitions:[
        {type:'tool_trait',id:'a',name:'A'},{type:'tool_trait',id:'b',name:'B'},
        {type:'tool_trait',id:'c',name:'C'},{type:'tool_trait',id:'d',name:'D'},
        {type:'tool_trait',id:'e',name:'E'}
    ],objects:[{id:'bench',name:'Bench',behavior:'none',crafting_power:{store:'grid',energy_per_work:1},
        tool_traits:[{id:'a',tier:1},{id:'b',tier:1},{id:'c',tier:1},{id:'d',tier:1},{id:'e',tier:1}]}]})"};
    CHECK(LoadContent({&four, 1}));
    CHECK_FALSE(LoadContent({&five, 1}));
}

TEST_CASE("Powered packing stations export and reload their grid rate", "[content][power-content][content-pack]") {
    constexpr ContentSource source{"bench.json5", R"({format_version:1,definitions:[
        {type:'tool_trait',id:'soldering',name:'Soldering'},
        {type:'item',id:'kit',name:'Kit',mass_g:1,volume_ml:1,placed_object:'bench'}
    ],objects:[{id:'bench',name:'Bench',behavior:'none',packing_item:'kit',capacity_volume_ml:1,
        tool_traits:[{id:'soldering',tier:1}],crafting_power:{store:'grid',energy_per_work:4294967295}}]})"};
    const auto loaded = LoadContent({&source, 1});
    REQUIRE(loaded);
    const auto canonical = WriteCombatContent(*loaded);
    const auto* objects = canonical.Find("objects");
    REQUIRE(objects);
    const auto* power = objects->At(0).Find("crafting_power");
    REQUIRE(power);
    CHECK(power->GetString("store") == eastl::string_view("grid"));
    CHECK(power->GetUnsigned("energy_per_work") == UINT32_MAX);
    DataObject document;
    document.SetUnsigned("format_version", 1);
    document.Set("objects", *objects);
    const auto encoded = EncodeDataObject(document);
    const ContentSource exported{"exported.json", {encoded.data(), encoded.size()}};
    constexpr ContentSource definitions{"definitions.json5", R"({format_version:1,definitions:[
        {type:'tool_trait',id:'soldering',name:'Soldering'},
        {type:'item',id:'kit',name:'Kit',mass_g:1,volume_ml:1,placed_object:'bench'}
    ]})"};
    const ContentSource sources[] = {exported, definitions};
    const auto roundTrip = LoadContent({sources, 2});
    REQUIRE(roundTrip);
    CHECK(roundTrip->GetObject(roundTrip->FindObject("bench")) == loaded->GetObject(loaded->FindObject("bench")));
    CHECK(network::ContentFingerprint(*roundTrip) == network::ContentFingerprint(*loaded));
}

TEST_CASE("Power schema participates in item equality and content fingerprint", "[content][power-content][fingerprint]") {
    const auto baseline = LoadPower({}, generatorObject);
    REQUIRE(baseline.has_value());
    for (const auto fields : {"grid_meter:true", "placed_object:'generator'",
                             "crafting_power:{store:'grid',energy_per_work:10000}"}) {
        const auto changed = LoadPower(fields, generatorObject);
        REQUIRE(changed.has_value());
        CHECK_FALSE(baseline->GetItem(baseline->FindItem("part")) == changed->GetItem(changed->FindItem("part")));
        CHECK(network::ContentFingerprint(*baseline) != network::ContentFingerprint(*changed));
    }
    const auto reordered = LoadPower("grid_meter:false", R"({name:'Generator',id:'generator',behavior:'generator',
      generator:{output_per_tick:4,energy_per_fuel:2000,fuel_capacity:6000,fuel_item:'part'}})");
    REQUIRE(reordered.has_value());
    CHECK(network::ContentFingerprint(*baseline) == network::ContentFingerprint(*reordered));
    const auto plain = LoadPower({});
    const auto wiring = LoadPower("wiring_material:'copper_wire'");
    const auto customCoverage = LoadPower("wiring_material:'copper_wire',wiring_tiles_per_material:32");
    REQUIRE(plain.has_value());
    REQUIRE(wiring.has_value());
    REQUIRE(customCoverage.has_value());
    CHECK(network::ContentFingerprint(*plain) != network::ContentFingerprint(*wiring));
    CHECK(network::ContentFingerprint(*wiring) != network::ContentFingerprint(*customCoverage));
    for (const auto object : {
        "{id:'generator',name:'Generator',behavior:'generator',generator:{fuel_item:'part',fuel_capacity:6001,energy_per_fuel:2000,output_per_tick:4}}",
        "{id:'generator',name:'Generator',behavior:'generator',generator:{fuel_item:'part',fuel_capacity:6000,energy_per_fuel:2001,output_per_tick:4}}",
        "{id:'generator',name:'Generator',behavior:'generator',generator:{fuel_item:'part',fuel_capacity:6000,energy_per_fuel:2000,output_per_tick:5}}"}) {
        const auto changed = LoadPower({}, object);
        REQUIRE(changed.has_value());
        CHECK(network::ContentFingerprint(*baseline) != network::ContentFingerprint(*changed));
    }
}

TEST_CASE("Full and first-house packs provide the electrical bootstrap content", "[content][power-content][content-pack]") {
    for (const auto* path : {CATACLYSMX_FIRST_HOUSE_PACK, CATACLYSMX_CONTENT_PACK}) {
        INFO(path);
        Diagnostics diagnostics;
        const auto loaded = LoadContentPack(path, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded.has_value());
        for (const auto id : {"wiring_kit", "portable_fuel_generator", "generator_fuel_packet"}) {
            const auto type = loaded->FindItem(id);
            REQUIRE(type.value != 0);
            CHECK_FALSE(loaded->GetItem(type).art.lines.empty());
        }
        const auto& kit = loaded->GetItem(loaded->FindItem("wiring_kit"));
        CHECK(kit.wiringMaterial == loaded->FindItem("copper_wire"));
        CHECK(kit.wiringTilesPerMaterial == 16);
        CHECK(kit.energyStores.empty());
        CHECK(loaded->GetItem(loaded->FindItem("multimeter")).gridMeter);
        CHECK(loaded->GetItem(loaded->FindItem("soldering_iron")).craftingGridPower);
        CHECK(loaded->GetItem(loaded->FindItem("aa_rechargeable_battery")).placedObject
            == loaded->FindObject("grid_battery_station"));
        CHECK(loaded->FindCraftingRecipe("assemble_wiring_kit").value != 0);
        CHECK(loaded->FindCraftingRecipe("solder_electrical_connectors").value != 0);
        CHECK(loaded->GetObject(loaded->FindObject("grid_scrap_press")).machine.gridPowered);
    }
    const auto demo = LoadContentPack(CATACLYSMX_TERRAIN_FILE);
    REQUIRE(demo.has_value());
    CHECK_FALSE(demo->GetObject(demo->FindObject("scrap_press")).machine.gridPowered);
}

TEST_CASE("Canonical content writes electrical item and object schemas with stable references", "[content][power-content]") {
    String objects(generatorObject.data(), generatorObject.size());
    objects += ',';
    objects.append(gridMachine.data(), gridMachine.size());
    objects += ',';
    objects.append(batteryObject.data(), batteryObject.size());
    const auto loaded = LoadPower("wiring_material:'copper_wire',wiring_tiles_per_material:32,grid_meter:true,placed_object:'generator',crafting_power:{store:'grid',energy_per_work:10000}",
        {objects.data(), objects.size()});
    REQUIRE(loaded.has_value());
    const auto canonical = WriteCombatContent(*loaded);
    const auto* items = canonical.Find("items");
    REQUIRE(items != nullptr);
    const auto& item = items->At(loaded->FindItem("part").value - 1);
    CHECK(item.GetString("wiring_material") == eastl::string_view("copper_wire"));
    CHECK(item.GetUnsigned("wiring_tiles_per_material") == 32);
    CHECK(item.GetBoolean("grid_meter") == true);
    CHECK(item.GetString("placed_object") == eastl::string_view("generator"));
    REQUIRE(item.Find("crafting_power") != nullptr);
    CHECK(item.Find("crafting_power")->GetString("store") == eastl::string_view("grid"));
    CHECK(item.Find("wiring_supply") == nullptr);
    const auto* writtenObjects = canonical.Find("objects");
    REQUIRE(writtenObjects != nullptr);
    const auto& generator = writtenObjects->At(loaded->FindObject("generator").value - 1);
    REQUIRE(generator.Find("generator") != nullptr);
    CHECK(generator.Find("generator")->GetString("fuel_item") == eastl::string_view("part"));
    CHECK(generator.Find("generator")->GetUnsigned("fuel_capacity") == 6000);
    const auto& machine = writtenObjects->At(loaded->FindObject("machine").value - 1);
    REQUIRE(machine.Find("machine") != nullptr);
    CHECK(machine.Find("machine")->GetBoolean("grid_powered") == true);
    CHECK(machine.Find("machine")->GetUnsigned("energy_capacity") == 0);
    const auto& station = writtenObjects->At(loaded->FindObject("station").value - 1);
    CHECK(station.GetString("behavior") == eastl::string_view("grid_battery"));
    CHECK(station.Find("machine") == nullptr);
    CHECK(station.Find("generator") == nullptr);
    // Object export uses the actual closed loading schema, including grid machines
    // with zero local capacity and config-free battery stations.
    DataObject document;
    document.SetUnsigned("format_version", 1);
    document.Set("objects", *writtenObjects);
    DataObject definitions = DataObject::List();
    for (const auto id : {"part", "copper_wire"}) {
        DataObject definition;
        definition.SetString("type", "item");
        definition.SetString("id", id);
        definition.SetString("name", id);
        definition.SetUnsigned("mass_g", 1);
        definition.SetUnsigned("volume_ml", 1);
        definitions.Append(eastl::move(definition));
    }
    document.Set("definitions", eastl::move(definitions));
    const auto encoded = EncodeDataObject(document);
    const ContentSource source{"exported-power.json", {encoded.data(), encoded.size()}};
    const auto roundTrip = LoadContent({&source, 1});
    REQUIRE(roundTrip.has_value());
    for (const auto id : {"generator", "machine", "station"}) {
        CHECK(roundTrip->GetObject(roundTrip->FindObject(id)) == loaded->GetObject(loaded->FindObject(id)));
    }
}
}
