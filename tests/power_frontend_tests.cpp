#include "client/power_view.h"
#include "client/tile_inspection.h"
#include "core/ratio.h"
#include "network/wire.h"
#include "power/power_system.h"
#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <initializer_list>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }
const ContentCatalog& PowerContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics; auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
        return eastl::move(*loaded);
    }();
    return content;
}
ItemTypeId PowerItem(eastl::string_view id) {
    const auto item = PowerContent().FindItem(id); REQUIRE(item.value != 0); return item;
}

TEST_CASE("Solar kits use ordinary power workshop placement without packing", "[power][ui][solar]") {
    Server server(PowerContent()); auto& world = server.GetWorld();
    const auto kit = Take(world.CreateItem(PowerItem("solar_generator_kit"), ItemLocation::Carried(server.GetPlayer())));
    const auto view = server.View(1);
    PowerWorkshop workshop; workshop.Open(view);
    workshop.Handle({"p"}, view, PowerContent());
    const auto place = workshop.Handle({"Enter"}, view, PowerContent()); REQUIRE(place);
    CHECK(place->kind == CommandKind::place_power_object);
    CHECK(place->item == world.GetItem(kit)->id); CHECK(place->otherItem == 0);
    CHECK(ValidCommandShape(*place));
    const auto& definition = PowerContent().GetObject(PowerContent().GetItem(PowerItem("solar_generator_kit")).placedObject);
    CHECK(definition.behavior == ObjectBehaviorKind::Solar);
    CHECK(definition.packingItem.value == 0); CHECK_FALSE(definition.movable);
}

TEST_CASE("Solar inspection and grid presentation share owned local and LAN values", "[power][ui][network][solar]") {
    const auto& content = PowerContent();
    const auto type = content.FindObject("solar_generator_station"); REQUIRE(type.value != 0);
    const auto peak = content.GetObject(type).solar.peakOutputPerTick;
    Server server(content); PlayerInspection inspection;
    inspection.requestId = 1; inspection.view = server.View(0); inspection.cursor = inspection.view.player_position;
    inspection.mapItems.resize(1);
    auto& cell = inspection.view.cells[0];
    cell.object = 123; cell.objectAppearance.kind = ObjectKind::Generic; cell.objectAppearance.type = type;
    cell.overlayVisibility = Visibility::Visible;
    auto& d = inspection.details;
    d.object = cell.object; d.objectType = type; d.objectKind = ObjectKind::Generic;
    d.enabled = true; d.machine = true;
    for (const unsigned state : {0u, 1u, 2u, 3u, 4u, 5u, 6u}) {
        inspection.view.tick = state == 2 ? powerDayTicks * 10 / 24 : state == 4 ? powerDayTicks * 22 / 24 : 0;
        d.runRequested = state != 0; d.solarExposed = state != 1;
        d.enabled = state != 5; d.broken = state == 6;
        d.solarDaylight = SolarDaylightRatio(inspection.view.tick);
        d.running = d.runRequested && d.solarExposed && IsSolarDaytime(inspection.view.tick);
        Vector<String> local;
        DescribeTile(content, inspection.view, inspection.cursor, d, local);
        const char* status = state == 0 ? "Stopped" : state == 1 ? "Covered" : state == 2 ? "Nighttime"
            : state == 5 ? "Disabled" : state == 6 ? "Broken" : "Generating";
        bool statusShown = false, factorShown = false, currentShown = false;
        for (const auto& line : local) {
            statusShown |= line.find(String("Solar: ") + status + " | Peak " + UnsignedText(peak)) != String::npos;
            factorShown |= line.find("Daylight " + UnsignedText(d.solarDaylight) + "/" + UnsignedText(ratioScale)) != String::npos;
            currentShown |= line.find("Current " + UnsignedText(d.enabled && !d.broken && d.running
                ? std::uint64_t{peak} * d.solarDaylight : 0)
                + "/" + UnsignedText(ratioScale) + " EU/tick") != String::npos;
            CHECK(line.find("Energy 0/0") == String::npos);
        }
        CHECK(statusShown); CHECK(factorShown); CHECK(currentShown);
        namespace wire = cataclysmx::network::wire;
        wire::Writer writer; REQUIRE(writer.Inspection(inspection));
        PlayerInspection decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        REQUIRE(reader.Inspection(decoded)); REQUIRE(reader.Done());
        CHECK(wire::ValidInspectionDefinitions(decoded, content));
        Vector<String> remote;
        DescribeTile(content, decoded.view, decoded.cursor, decoded.details, remote);
        CHECK(remote == local);
    }
    auto& view = inspection.view;
    view.currentPower.emplace();
    view.currentPower->grid = PowerGridId{7};
    view.currentPower->solarOutputAvailable = 33; view.currentPower->generatorOutputAvailable = 44;
    view.currentPower->runningSolar = 2; view.currentPower->runningGenerators = 3;
    PowerWorkshop workshop; workshop.Open(view); Vector<String> lines; workshop.Describe(view, content, lines);
    bool outputsShown = false, countsShown = false;
    for (const auto& line : lines) {
        outputsShown |= line.find("Solar output 33 | Fueled output 44") != String::npos;
        countsShown |= line.find("Running solar 2 | Running fueled generators 3") != String::npos;
    }
    CHECK(outputsShown); CHECK(countsShown);
}

TEST_CASE("Meter eligibility follows carried ownership and view layer without debug cutoff", "[power][view]") {
    Server server(PowerContent()); auto& world = server.GetWorld(); const auto actor = server.GetPlayer();
    const auto position = world.GetActor(actor)->position;
    const auto grid = Take(world.Power().ExtendBounds({position, {position.x + 1, position.z, position.y}}));
    REQUIRE_FALSE(world.CanInspectPower(actor));
    auto view = server.View(1); CHECK_FALSE(view.currentPower);
    for (const auto& cell : view.cells) CHECK(cell.powerGridId == 0);
    const auto meter = Take(world.CreateItem(PowerItem("multimeter"), ItemLocation::Carried(actor)));
    REQUIRE(world.CanInspectPower(actor));
    view = server.View(1); REQUIRE(view.currentPower); CHECK(view.currentPower->grid == grid);
    CHECK(view.cells[4].powerGridId == grid.value); CHECK(view.cells[5].powerGridId == grid.value);
    // The player DTO carries only present availability; the future cutoff stays
    // in the separately captured, host-only DEBUG snapshot.
    PowerDebugSnapshot debug; CapturePowerDebug(world, debug); REQUIRE_FALSE(debug.grids.empty());
    CHECK(debug.grids.front().mainsCutoffTick > world.GetTick());
    const auto upper = server.ViewForPlayerAtLayer(server.GetHostPlayerId(), 1, position.y + 1);
    REQUIRE(upper.currentPower); CHECK(upper.cells[4].powerGridId == 0);
    REQUIRE(world.TransferItem(meter, ItemLocation::Ground(position)));
    CHECK_FALSE(world.CanInspectPower(actor));
    view = server.View(1); CHECK_FALSE(view.currentPower);
    for (const auto& cell : view.cells) CHECK(cell.powerGridId == 0);
}

TEST_CASE("Power workshop can submit small rectangular wiring and exact carried equipment", "[power][ui]") {
    Server server(PowerContent()); auto& world = server.GetWorld(); const auto actor = server.GetPlayer();
    const auto kit = Take(world.CreateItem(PowerItem("wiring_kit"), ItemLocation::Carried(actor)));
    const auto equipment = Take(world.CreateItem(PowerItem("portable_fuel_generator"), ItemLocation::Carried(actor)));
    auto view = server.View(1); PowerWorkshop workshop; workshop.Open(view);
    const auto& content = world.GetContent();
    const auto& kitDefinition = content.GetItem(world.GetItem(kit)->type);
    REQUIRE(kitDefinition.wiringMaterial.value != 0);
    CHECK(kitDefinition.energyStores.empty());
    Vector<String> workshopLines; workshop.Describe(view, content, workshopLines);
    bool materialShown = false, batteryCostShown = false;
    for (const auto& line : workshopLines) {
        materialShown |= line.find("Material: " + content.GetItem(kitDefinition.wiringMaterial).name) != String::npos;
        batteryCostShown |= line.find("Battery installation: 4 tile equivalents = 1 material item(s)") != String::npos;
    }
    CHECK(materialShown); CHECK(batteryCostShown);
    workshop.Handle({"w"}, view, content); // 16 wraps to 1.
    workshop.Handle({"d"}, view, content); workshop.Handle({"d"}, view, content); // depth 2.
    const auto wire = workshop.Handle({"Enter"}, view, content); REQUIRE(wire);
    CHECK(wire->kind == CommandKind::wire_grid); CHECK(wire->item == world.GetItem(kit)->id);
    CHECK(wire->powerBounds.min == view.player_position);
    CHECK(wire->powerBounds.max == TileCoord{view.player_position.x, view.player_position.z + 1, view.player_position.y});
    CHECK(ValidCommandShape(*wire));
    workshop.Handle({"p"}, view, content); workshop.Handle({"Right"}, view, content);
    const auto place = workshop.Handle({"Enter"}, view, content); REQUIRE(place);
    CHECK(place->kind == CommandKind::place_power_object); CHECK(place->item == world.GetItem(equipment)->id);
    CHECK(place->otherItem == world.GetItem(kit)->id); CHECK(place->position.x == view.player_position.x + 1);
    CHECK(ValidCommandShape(*place));
    REQUIRE(world.TransferItem(equipment, ItemLocation::Ground(view.player_position)));
    CHECK_FALSE(workshop.Handle({"Enter"}, server.View(1), content));
    view.powerSettings = {maxPowerWiringExtent, maxPowerWiringExtent};
    workshop.Open(view);
    const auto large = workshop.Handle({"Enter"}, view, content); REQUIRE(large);
    CHECK(large->powerBounds.max.x - large->powerBounds.min.x + 1 == maxPowerWiringExtent);
    CHECK(large->powerBounds.max.z - large->powerBounds.min.z + 1 == maxPowerWiringExtent);
    CHECK(ValidCommandShape(*large));
    workshop.Handle({"w"}, view, content);
    const auto narrow = workshop.Handle({"Enter"}, view, content); REQUIRE(narrow);
    CHECK(narrow->powerBounds.max.x == narrow->powerBounds.min.x);
    CHECK(narrow->powerBounds.max.z - narrow->powerBounds.min.z + 1 == maxPowerWiringExtent);
}

TEST_CASE("Powered manual station presentation uses catalog rates and snapshot coverage", "[power][workshop][ui][network]") {
    constexpr const char* text = R"({
        format_version:1,
        definitions:[{type:'tool_trait',id:'soldering',name:'Soldering'}],
        objects:[{id:'bench',name:'Powered soldering bench',behavior:'none',
            tool_traits:[{id:'soldering',tier:1}],
            crafting_power:{store:'grid',energy_per_work:12500}}]
    })";
    const ContentSource source{"powered-presentation", text};
    Diagnostics diagnostics; auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    const auto& content = *loaded;
    const auto& definition = content.GetObject(content.FindObject("bench"));
    Server server(PowerContent());
    auto view = server.View(1);
    view.stations.clear();
    StationView station;
    station.id = 999; station.type = content.FindObject("bench");
    station.position = {view.player_position.x + 1, view.player_position.z, view.player_position.y};
    station.enabled = true; station.packing = StationActionOutcome::FixedFixture;
    view.stations.push_back(station);
    CHECK(String(StationAvailabilityText(station, definition)) == "idle");
    const auto withoutMeter = StationPowerText(view, station, definition);
    CHECK(withoutMeter.find("12500/10000 energy/base work | grid at station") != String::npos);
    CHECK(withoutMeter.find("meter required") != String::npos);

    view.currentPower.emplace();
    view.currentPower->grid = PowerGridId{11};
    view.cells[4].powerGridId = 11;
    view.cells[5].powerGridId = 22;
    const auto original = StationPowerText(view, station, definition);
    CHECK(original.find("station grid: 22") != String::npos);
    PowerWorkshop workshop; workshop.Open(view);
    Vector<String> lines; workshop.Describe(view, content, lines);
    bool stationShown = false, workerGridShown = false;
    for (const auto& line : lines) {
        stationShown |= line.find("[idle] | " + original) != String::npos;
        workerGridShown |= line.find("Worker's current grid: 11") != String::npos;
    }
    CHECK(stationShown); CHECK(workerGridShown);

    namespace wire = cataclysmx::network::wire;
    wire::Writer writer; REQUIRE(writer.View(view));
    WorldView decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    REQUIRE(decoded.stations.size() == 1);
    CHECK(StationPowerText(decoded, decoded.stations.front(), definition) == original);
    view.cells[5].powerGridId = 0;
    CHECK(StationPowerText(view, station, definition).find("station grid: unwired") != String::npos);
    view.origin.y += 1;
    CHECK(StationPowerText(view, station, definition).find("outside view") != String::npos);
    view.origin.y -= 1; view.origin.x += 10;
    CHECK(StationPowerText(view, station, definition).find("outside view") != String::npos);

    station.worker = 42;
    CHECK(String(StationAvailabilityText(station, definition)) == "in use");
    station.worker = 0; station.enabled = false;
    CHECK(String(StationAvailabilityText(station, definition)) == "disabled");
    station.broken = true;
    CHECK(String(StationAvailabilityText(station, definition)) == "broken");
}

TEST_CASE("Power debug snapshots own geometry and chunk demand counters", "[power][debug]") {
    Server server(PowerContent()); auto& world = server.GetWorld();
    const auto position = world.GetActor(server.GetPlayer())->position;
    Take(world.Power().ExtendBounds({position, position}));
    PowerDebugSnapshot snapshot; CapturePowerDebug(world, snapshot);
    REQUIRE_FALSE(snapshot.grids.empty()); REQUIRE_FALSE(snapshot.columns.empty());
    CHECK(snapshot.residency.playerColumns > 0); CHECK(snapshot.residency.loadedColumns >= snapshot.residency.activeColumns);
    const auto regions = snapshot.grids.front().regions;
    Take(world.Power().ExtendBounds({{position.x + 1, position.z, position.y}, {position.x + 1, position.z, position.y}}));
    CHECK(snapshot.grids.front().regions == regions);
    Vector<String> lines; DescribePowerDebug(snapshot, position, lines);
    REQUIRE(lines.size() > 34);
    CHECK(lines[1].find("loadedColumns=") != String::npos);
    CHECK(lines[2].find("archivedChunks=") != String::npos);
}

TEST_CASE("Server power actions revalidate carried materials settings and layer", "[power][server]") {
    Server server(PowerContent()); auto& world = server.GetWorld(); const auto actor = server.GetPlayer();
    const auto position = world.GetActor(actor)->position;
    const auto kit = Take(world.CreateItem(PowerItem("wiring_kit"), ItemLocation::Ground(position)));
    Command wire; wire.kind = CommandKind::wire_grid; wire.item = world.GetItem(kit)->id;
    wire.powerBounds = {position, position};
    auto result = server.Send(wire);
    CHECK(result.outcome == Outcome::power_refused); CHECK_FALSE(result.advanced); CHECK(result.cost == 0);
    REQUIRE(world.TransferItem(kit, ItemLocation::Carried(actor)));
    wire.powerBounds.max.x = position.x + 16; // Width 17 is protocol-valid, but default settings allow 16.
    REQUIRE(ValidCommandShape(wire));
    result = server.Send(wire);
    CHECK(result.outcome == Outcome::power_refused); CHECK_FALSE(result.advanced);
    CHECK(result.powerOutcome == PowerActionOutcome::InvalidArea);
    wire.powerBounds = {position, position};
    REQUIRE(world.Power().SetSettings({1, 1}));
    wire.powerBounds.max.x += 1;
    result = server.Send(wire);
    CHECK(result.outcome == Outcome::power_refused); CHECK_FALSE(result.advanced);
    wire.powerBounds = {{position.x, position.z, position.y + 1}, {position.x, position.z, position.y + 1}};
    result = server.Send(wire);
    CHECK(result.outcome == Outcome::power_refused); CHECK_FALSE(result.advanced);
    wire.powerBounds = {position, position};
    result = server.Send(wire);
    CHECK(result.outcome == Outcome::power_refused); CHECK_FALSE(result.advanced);
    CHECK(result.powerOutcome == PowerActionOutcome::MissingMaterials);
    const auto materialType = world.GetContent().GetItem(world.GetItem(kit)->type).wiringMaterial;
    const auto material = Take(world.CreateItem(materialType, ItemLocation::Carried(actor)));
    const auto materialId = world.GetItem(material)->id;
    result = server.Send(wire);
    CHECK(result.outcome == Outcome::power_changed); CHECK(result.advanced); CHECK(result.cost > 0);
    CHECK(result.powerOutcome == PowerActionOutcome::Succeeded);
    CHECK(world.Power().GridAt(position).has_value());
    CHECK(world.GetItem(kit) != nullptr); // Reusable kit survives; the ordinary material is consumed.
    CHECK_FALSE(world.GetItem(world.FindItem(materialId)));
}

TEST_CASE("Power diagnostics list remote machine-only columns and all demand reasons", "[power][debug]") {
    PowerDebugSnapshot snapshot;
    ChunkResidencyDebug remote; remote.column = {100, -200}; remote.active = true;
    remote.machineOwners = 2; remote.machineDependencies = 3;
    remote.otherActiveRequests = 4; remote.otherResidentRequests = 5;
    snapshot.columns.push_back(remote);
    auto shared = remote; shared.column = {101, -200}; shared.playerResidentRequests = 1;
    snapshot.columns.push_back(shared);
    auto resident = remote; resident.column = {-300, 400}; resident.active = false;
    snapshot.columns.push_back(resident);
    Vector<String> lines; DescribePowerDebug(snapshot, {0, 0}, lines);
    bool remoteShown = false, residentShown = false, sharedListed = false;
    for (const auto& line : lines) {
        remoteShown |= line == "Column 100,-200 active | machineOwners=2 machineDependencies=3 otherActiveRequests=4 otherResidentRequests=5";
        residentShown |= line.find("Column -300,400 resident") != String::npos;
        sharedListed |= line.find("Column 101,-200") != String::npos;
    }
    CHECK(remoteShown); CHECK(residentShown); CHECK_FALSE(sharedListed);
}

TEST_CASE("Server Update releases remote machine residency after stop or completion", "[power][server][residency]") {
    Server server(PowerContent()); auto& world = server.GetWorld();
    REQUIRE(server.SetBubbleRadii(0, 0));
    REQUIRE(server.SetPacing({100, false, true}));
    const auto actor = server.GetPlayer(); const auto home = world.GetActor(actor)->position;
    if (const auto enemy = world.ActorAt({5, 5}); enemy.value) REQUIRE(world.DestroyActor(enemy));
    const TileCoord machinePosition{320, 8}, operatorPosition{321, 8}, batteryPosition{322, 8};
    const auto remoteColumn = ChunkOf(machinePosition);
    const auto loading = Take(world.RequestChunks({remoteColumn, 0}, LoadMode::active));
    REQUIRE_FALSE(server.Update(0).storageError);
    for (const auto position : {machinePosition, operatorPosition, batteryPosition}) {
        if (const auto object = world.ObjectAt(position); object.value) REQUIRE(world.DestroyObject(object));
        if (const auto occupant = world.ActorAt(position); occupant.value) REQUIRE(world.DestroyActor(occupant));
        REQUIRE(world.SetTile(position, {PowerContent().FindTerrain("floor")}));
    }
    Take(world.Power().ExtendBounds({machinePosition, batteryPosition}));
    const auto box = Take(world.CreateObject(PowerContent().FindObject("grid_battery_station"), batteryPosition));
    const auto battery = Take(world.CreateItem(PowerItem("aa_rechargeable_battery"), ItemLocation::Stored(box)));
    const auto store = PowerContent().GetItem(PowerItem("aa_rechargeable_battery")).energyStores.front().capability;
    REQUIRE(world.SetItemEnergy(battery, store, 1000)); world.Power().SyncObject(box);
    const auto machine = Take(world.CreateObject(PowerContent().FindObject("grid_scrap_press"), machinePosition));
    const auto machineId = world.GetObject(machine)->id;
    for (int i = 0; i < 20; ++i) Take(world.CreateItem(PowerItem("steel_scrap"), ItemLocation::Carried(actor)));
    REQUIRE(world.MoveActor(actor, operatorPosition));
    REQUIRE(world.UseObject(actor, machine, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    REQUIRE(world.MoveActor(actor, home));
    REQUIRE(world.ReleaseRequest(loading));
    REQUIRE_FALSE(server.Update(0).storageError);
    REQUIRE(world.IsActive(machinePosition));
    PowerDebugSnapshot snapshot; CapturePowerDebug(world, snapshot);
    bool machineOnly = false;
    for (const auto& entry : snapshot.columns) {
        if (entry.column.x == remoteColumn.x && entry.column.z == remoteColumn.z) {
            machineOnly = entry.machineOwners != 0 && !entry.playerActiveRequests && !entry.playerResidentRequests
                && !entry.otherActiveRequests && !entry.otherResidentRequests;
        }
    }
    REQUIRE(machineOnly);

    bool completed = false;
    SECTION("an idle update reconciles explicit stop") {
        REQUIRE(world.MoveActor(actor, operatorPosition));
        REQUIRE(world.UseObject(actor, machine, ObjectAction::Stop).outcome == ObjectUseOutcome::Succeeded);
        REQUIRE(world.MoveActor(actor, home));
        REQUIRE_FALSE(server.Update(0).storageError);
    }
    SECTION("simulation completion is reconciled by the same update") {
        Command wait; wait.waitCost = 400;
        const auto input = server.GetInputContext(server.GetHostPlayerId());
        REQUIRE(server.Submit(server.GetHostPlayerId(), {1, input.opportunity, wait}).outcome == Outcome::queued);
        const auto update = server.Update(0);
        REQUIRE_FALSE(update.storageError);
        CHECK(update.tick >= 300);
        completed = true;
    }
    CHECK_FALSE(world.GetChunk(remoteColumn));
    CapturePowerDebug(world, snapshot);
    for (const auto& entry : snapshot.columns) CHECK_FALSE((entry.column.x == remoteColumn.x && entry.column.z == remoteColumn.z));
    for (const auto& device : world.Power().Capture().devices) {
        if (device.object == machineId) CHECK_FALSE(device.workshopHold);
    }
    // Reload only after proving Update evicted it; inspect persisted gameplay
    // state to distinguish successful completion from an accidental lost hold.
    Take(world.RequestChunks({remoteColumn, 0}, LoadMode::resident));
    REQUIRE_FALSE(server.Update(0).storageError);
    const auto restoredMachine = world.FindObject(machineId);
    const auto state = world.DescribeObjectState(restoredMachine); REQUIRE(state);
    CHECK_FALSE(state->runRequested);
    if (completed) {
        CHECK_FALSE(state->hasJob); CHECK(state->remainingWork == 0);
        const auto* object = world.GetObject(restoredMachine); REQUIRE(object);
        REQUIRE(object->contents.size() == 1);
        CHECK(world.GetItem(object->contents.front())->type == PowerItem("steel_sheet"));
    } else {
        CHECK(state->hasJob); CHECK(state->remainingWork > 0);
    }
    CHECK(world.CheckInvariants());
}
}
