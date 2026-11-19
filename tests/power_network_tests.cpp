#include "network/wire.h"
#include "power/power_system.h"
#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <initializer_list>

namespace {
using namespace cataclysmx;
namespace wire = cataclysmx::network::wire;
const ContentCatalog& PowerNetworkContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics; auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
        return eastl::move(*loaded);
    }();
    return content;
}

constexpr const char* powerItemFingerprintFields = "wiring_material:'wire',wiring_tiles_per_material:16,placed_object:'generator',grid_meter:true,crafting_power:{store:'grid',energy_per_work:10000}";
constexpr const char* generatorFingerprintFields = "fuel_item:'fuel',fuel_capacity:1000,energy_per_fuel:100,output_per_tick:1";
std::uint64_t PowerFingerprint(const char* itemFields = powerItemFingerprintFields,
    const char* generatorFields = generatorFingerprintFields, bool gridMachine = true,
    const char* batteryBehavior = "grid_battery") {
    String text = "{format_version:1,definitions:[";
    text += "{type:'item',id:'wire',name:'Wire',mass_g:1,volume_ml:1},";
    text += "{type:'item',id:'other_wire',name:'Other wire',mass_g:1,volume_ml:1},";
    text += "{type:'item',id:'fuel',name:'Fuel',mass_g:1,volume_ml:1},";
    text += "{type:'item',id:'other_fuel',name:'Other fuel',mass_g:1,volume_ml:1},";
    text += "{type:'item',id:'tool',name:'Tool',mass_g:1,volume_ml:1,";
    text += itemFields;
    text += "}],objects:[{id:'generator',name:'Generator',behavior:'generator',generator:{";
    text += generatorFields;
    text += "}},{id:'other_generator',name:'Other generator',behavior:'generator',generator:{";
    text += generatorFingerprintFields;
    text += "}},{id:'battery',name:'Battery',behavior:'";
    text += batteryBehavior;
    text += "'},{id:'machine',name:'Machine',behavior:'crafting_machine',machine:{input:'wire',input_count:1,";
    text += "output:'other_wire',output_count:1,duration_ticks:100,energy_per_tick:1,";
    text += gridMachine ? "grid_powered:true,energy_capacity:0" : "energy_capacity:100";
    text += "}}]}";
    const ContentSource source{"power-fingerprint", {text.data(), text.size()}};
    Diagnostics diagnostics; auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content);
    return cataclysmx::network::ContentFingerprint(*content);
}

TEST_CASE("Power item fingerprints cover reusable wiring configuration and player tools", "[power][network][fingerprint]") {
    const auto original = PowerFingerprint();
    const char* changes[] = {
        "wiring_material:'other_wire',wiring_tiles_per_material:16,placed_object:'generator',grid_meter:true,crafting_power:{store:'grid',energy_per_work:10000}",
        "wiring_material:'wire',wiring_tiles_per_material:8,placed_object:'generator',grid_meter:true,crafting_power:{store:'grid',energy_per_work:10000}",
        "placed_object:'generator',grid_meter:true,crafting_power:{store:'grid',energy_per_work:10000}",
        "wiring_material:'wire',wiring_tiles_per_material:16,placed_object:'other_generator',grid_meter:true,crafting_power:{store:'grid',energy_per_work:10000}",
        "wiring_material:'wire',wiring_tiles_per_material:16,grid_meter:true,crafting_power:{store:'grid',energy_per_work:10000}",
        "wiring_material:'wire',wiring_tiles_per_material:16,placed_object:'generator',grid_meter:false,crafting_power:{store:'grid',energy_per_work:10000}",
        "wiring_material:'wire',wiring_tiles_per_material:16,placed_object:'generator',grid_meter:true",
        "wiring_material:'wire',wiring_tiles_per_material:16,placed_object:'generator',grid_meter:true,crafting_power:{store:'grid',energy_per_work:20000}"
    };
    for (const auto* change : changes) { CAPTURE(change); CHECK(PowerFingerprint(change) != original); }
    CHECK(PowerFingerprint() == original);
}

TEST_CASE("Power object fingerprints cover every generator parameter and machine power mode", "[power][network][fingerprint]") {
    const auto original = PowerFingerprint();
    const char* changes[] = {
        "fuel_item:'other_fuel',fuel_capacity:1000,energy_per_fuel:100,output_per_tick:1",
        "fuel_item:'fuel',fuel_capacity:1001,energy_per_fuel:100,output_per_tick:1",
        "fuel_item:'fuel',fuel_capacity:1000,energy_per_fuel:101,output_per_tick:1",
        "fuel_item:'fuel',fuel_capacity:1000,energy_per_fuel:100,output_per_tick:2"
    };
    for (const auto* change : changes) { CAPTURE(change); CHECK(PowerFingerprint(powerItemFingerprintFields, change) != original); }
    CHECK(PowerFingerprint(powerItemFingerprintFields, generatorFingerprintFields, false) != original);
    CHECK(PowerFingerprint(powerItemFingerprintFields, generatorFingerprintFields, true, "none") != original);
}

TEST_CASE("Power commands use explicit canonical bounded wire payloads", "[power][network]") {
    Command command; command.kind = CommandKind::wire_grid; command.item = 42;
    command.powerBounds = {{-8, -2, -1}, {7, 0, -1}};
    REQUIRE(ValidCommandShape(command));
    wire::Writer writer; REQUIRE(writer.CommandValue(command));
    Command decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.CommandValue(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.powerBounds == command.powerBounds); CHECK(decoded.item == 42);
    for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
        wire::Reader truncated{{writer.bytes.data(), size}};
        CHECK_FALSE(truncated.CommandValue(decoded));
    }
    auto maximum = command; maximum.powerBounds.max.x = maximum.powerBounds.min.x + maxPowerWiringExtent - 1;
    maximum.powerBounds.max.z = maximum.powerBounds.min.z + maxPowerWiringExtent - 1;
    wire::Writer maximumWriter; REQUIRE(maximumWriter.CommandValue(maximum));
    wire::Reader maximumReader{{maximumWriter.bytes.data(), maximumWriter.bytes.size()}};
    REQUIRE(maximumReader.CommandValue(decoded)); CHECK(decoded.powerBounds == maximum.powerBounds);
    auto invalid = maximum; invalid.powerBounds.max.x += 1; // Width 65 exceeds the named protocol bound.
    CHECK_FALSE(ValidCommandShape(invalid));
    invalid = maximum; invalid.powerBounds.max.z += 1;
    CHECK_FALSE(ValidCommandShape(invalid));
    invalid = command; invalid.powerBounds.max.y = 0;
    CHECK_FALSE(ValidCommandShape(invalid));
    invalid = command; invalid.powerBounds.min.x = world_coordinate_limit + 1;
    CHECK_FALSE(ValidCommandShape(invalid));
    invalid = command; invalid.otherItem = 3;
    CHECK_FALSE(ValidCommandShape(invalid));
    invalid = command; invalid.position = {1, 0};
    CHECK_FALSE(ValidCommandShape(invalid));
    invalid = command; invalid.kind = CommandKind::wait; invalid.item = 0;
    CHECK_FALSE(ValidCommandShape(invalid));

    command = {}; command.kind = CommandKind::place_power_object; command.item = 17;
    command.otherItem = 42; command.position = {1, -1, 0};
    wire::Writer placement; REQUIRE(placement.CommandValue(command));
    wire::Reader placementReader{{placement.bytes.data(), placement.bytes.size()}};
    REQUIRE(placementReader.CommandValue(decoded)); REQUIRE(placementReader.Done());
    CHECK(decoded.position == command.position); CHECK(decoded.otherItem == 42);
    command.otherItem = command.item; CHECK_FALSE(ValidCommandShape(command));
    command.otherItem = 0; command.position.y = world_max_y + 1; CHECK_FALSE(ValidCommandShape(command));
}

TEST_CASE("Power view codec preserves meter inspection and rejects unauthorized coverage", "[power][network]") {
    Server server(PowerNetworkContent()); auto view = server.View(1);
    REQUIRE_FALSE(view.currentPower);
    auto invalid = view; invalid.cells[0].powerGridId = 1;
    wire::Writer forbidden; CHECK_FALSE(forbidden.View(invalid));
    view.currentPower.emplace(); auto& p = *view.currentPower;
    p.grid = PowerGridId{1234567890123}; p.mainsAvailable = true;
    p.batteryEnergy = 11; p.batteryCapacity = 100; p.generatorOutputAvailable = 25;
    p.batteries = 2; p.runningGenerators = 1; p.runningMachines = 3; p.waitingMachines = 4; p.unloadedDevices = 5;
    view.cells[0].powerGridId = p.grid->value;
    wire::Writer writer; REQUIRE(writer.View(view));
    WorldView decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done()); REQUIRE(decoded.currentPower);
    CHECK(decoded.currentPower->grid == p.grid); CHECK(decoded.currentPower->mainsAvailable);
    CHECK(decoded.currentPower->batteryEnergy == 11); CHECK(decoded.currentPower->batteryCapacity == 100);
    CHECK(decoded.currentPower->generatorOutputAvailable == 25); CHECK(decoded.currentPower->unloadedDevices == 5);
    CHECK(decoded.cells == view.cells);
    auto bad = writer.bytes;
    bad[bad.size() - 56] = 255; // Strict presence boolean, followed by meter data and the empty station count.
    wire::Reader invalidPresence{{bad.data(), bad.size()}};
    CHECK_FALSE(invalidPresence.View(decoded));
    wire::Reader truncated{{writer.bytes.data(), writer.bytes.size() - 1}};
    CHECK_FALSE(truncated.View(decoded));
    invalid = view; invalid.currentPower->grid.reset();
    wire::Writer noGrid; CHECK_FALSE(noGrid.View(invalid));
    invalid = view; invalid.currentPower->batteryEnergy = 101;
    wire::Writer overCapacity; CHECK_FALSE(overCapacity.View(invalid));
}

TEST_CASE("Power result wire validation distinguishes refusals from committed work", "[power][network]") {
    PlayerCommandResult receipt{1, 1, 1, {Outcome::power_refused}};
    receipt.result.powerOutcome = PowerActionOutcome::MissingMaterials;
    wire::Writer writer; REQUIRE(writer.Result(receipt));
    PlayerCommandResult decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.Result(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.result.powerOutcome == PowerActionOutcome::MissingMaterials);
    receipt.result.advanced = true; wire::Writer invalid; CHECK_FALSE(invalid.Result(receipt));
    receipt.result.outcome = Outcome::power_changed; receipt.result.powerOutcome = PowerActionOutcome::Succeeded;
    receipt.result.cost = 100; wire::Writer success; CHECK(success.Result(receipt));
}

TEST_CASE("Power view settings accept the named maximum and reject out of range wire values", "[power][network]") {
    Server server(PowerNetworkContent()); auto view = server.View(0);
    view.powerSettings = {maxPowerWiringExtent, maxPowerWiringExtent};
    wire::Writer writer; REQUIRE(writer.View(view));
    WorldView decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.powerSettings == view.powerSettings);
    for (const int invalid : {0, maxPowerWiringExtent + 1}) {
        auto bad = view; bad.powerSettings.maxWiringWidth = invalid;
        wire::Writer badWidth; CHECK_FALSE(badWidth.View(bad));
        bad = view; bad.powerSettings.maxWiringDepth = invalid;
        wire::Writer badDepth; CHECK_FALSE(badDepth.View(bad));
        // No meter/stations: width I32, depth I32, presence boolean, station count U16.
        auto bytes = writer.bytes; bytes[bytes.size() - 8] = static_cast<std::uint8_t>(invalid);
        wire::Reader widthReader{{bytes.data(), bytes.size()}}; CHECK_FALSE(widthReader.View(decoded));
        bytes = writer.bytes; bytes[bytes.size() - 4] = static_cast<std::uint8_t>(invalid);
        wire::Reader depthReader{{bytes.data(), bytes.size()}}; CHECK_FALSE(depthReader.View(decoded));
    }
}

TEST_CASE("Changing only a future mains cutoff cannot change the player wire snapshot", "[power][network][privacy]") {
    Server server(PowerNetworkContent()); auto& world = server.GetWorld();
    const auto position = world.GetActor(server.GetPlayer())->position;
    REQUIRE(world.Power().RegisterBuilding(position, {position, position}));
    const auto meter = world.GetContent().FindItem("multimeter"); REQUIRE(meter.value != 0);
    REQUIRE(world.CreateItem(meter, ItemLocation::Carried(server.GetPlayer())));
    auto saved = world.Power().Capture(); saved.mainsCutoffTick = 8 * powerDayTicks;
    REQUIRE(world.Power().Restore(saved, world.GetTick()));
    auto view = server.View(1); REQUIRE(view.currentPower); REQUIRE(view.currentPower->mainsAvailable);
    wire::Writer before; REQUIRE(before.View(view));
    saved.mainsCutoffTick = 9 * powerDayTicks;
    REQUIRE(world.Power().Restore(saved, world.GetTick()));
    // Explicitly refresh inspection instead of relying on the server view cache.
    world.Power().Inspect(position, *view.currentPower);
    REQUIRE(view.currentPower->mainsAvailable);
    Vector<PowerGridDebug> debug; world.Power().CollectDebugGrids(debug);
    REQUIRE_FALSE(debug.empty()); CHECK(debug.front().mainsCutoffTick == 9 * powerDayTicks);
    wire::Writer after; REQUIRE(after.View(view));
    CHECK(after.bytes == before.bytes);
}

TEST_CASE("Powered object inspection transports generator and funded operation state", "[power][network][inspection]") {
    Server server(PowerNetworkContent()); PlayerInspection inspection;
    inspection.requestId = 1; inspection.view = server.View(0); inspection.cursor = inspection.view.player_position;
    inspection.mapItems.resize(1);
    auto& cell = inspection.view.cells[0];
    cell.object = 123; cell.objectAppearance.kind = ObjectKind::Generic;
    cell.objectAppearance.type = PowerNetworkContent().FindObject("generator");
    REQUIRE(cell.objectAppearance.type.value != 0);
    REQUIRE(PowerNetworkContent().GetObject(cell.objectAppearance.type).behavior == ObjectBehaviorKind::Generator);
    cell.overlayVisibility = Visibility::Visible;
    auto& details = inspection.details;
    details.object = cell.object; details.objectKind = cell.objectAppearance.kind; details.objectType = cell.objectAppearance.type;
    details.machine = true; details.energy = 10; details.energyCapacity = 100;
    details.running = true; details.runRequested = true; details.gridPowered = true;
    details.fundedTicks = 25; details.generatorIntervalInitialized = true; details.generatorOutputRemaining = 50;
    wire::Writer writer; REQUIRE(writer.Inspection(inspection));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}}; PlayerInspection decoded;
    REQUIRE(reader.Inspection(decoded)); REQUIRE(reader.Done());
    REQUIRE(wire::ValidInspectionDefinitions(decoded, PowerNetworkContent()));
    CHECK(decoded.details.gridPowered); CHECK(decoded.details.runRequested); CHECK(decoded.details.fundedTicks == 25);
    CHECK(decoded.details.generatorOutputRemaining == 50); CHECK(decoded.details.generatorIntervalInitialized);
    CHECK(decoded.details.generatorIntervalStart == 0);
    details.fundedTicks = powerQuantumTicks + 1;
    wire::Writer excessiveFunding; CHECK_FALSE(excessiveFunding.Inspection(inspection));
    details.fundedTicks = 0; details.generatorIntervalStart = powerQuantumTicks;
    wire::Writer futureInterval; CHECK_FALSE(futureInterval.Inspection(inspection));

    // Behavior identity is content-defined, not a wire enum: both new behaviors
    // cross the boundary by a validated object type ID.
    const auto batteryType = PowerNetworkContent().FindObject("grid_battery_station"); REQUIRE(batteryType.value != 0);
    REQUIRE(PowerNetworkContent().GetObject(batteryType).behavior == ObjectBehaviorKind::GridBattery);
    cell.objectAppearance.type = batteryType;
    details = {}; details.object = cell.object; details.objectKind = cell.objectAppearance.kind; details.objectType = batteryType;
    wire::Writer batteryWriter; REQUIRE(batteryWriter.Inspection(inspection));
    wire::Reader batteryReader{{batteryWriter.bytes.data(), batteryWriter.bytes.size()}};
    REQUIRE(batteryReader.Inspection(decoded)); REQUIRE(batteryReader.Done());
    REQUIRE(wire::ValidInspectionDefinitions(decoded, PowerNetworkContent()));
    CHECK(PowerNetworkContent().GetObject(decoded.details.objectType).behavior == ObjectBehaviorKind::GridBattery);

    Command refuel; refuel.kind = CommandKind::interact_action;
    refuel.interaction = {inspection.cursor, 123, {}, InteractionKind::Object, ObjectAction::Refuel};
    wire::Writer action; REQUIRE(action.CommandValue(refuel));
    Command command; wire::Reader actionReader{{action.bytes.data(), action.bytes.size()}};
    REQUIRE(actionReader.CommandValue(command)); CHECK(command.interaction.action == ObjectAction::Refuel);
}
}
