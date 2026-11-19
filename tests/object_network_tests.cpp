#include "content/content.h"
#include "network/network.h"
#include "network/wire.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

constexpr ContentSource items{"items.json5", R"({format_version:1,definitions:[
    {type:'item',id:'scrap',name:'Scrap',mass_g:100,volume_ml:100},
    {type:'item',id:'pipe',name:'Pipe',mass_g:300,volume_ml:200}
]})"};

std::uint64_t Fingerprint(eastl::string_view objects) {
    String text = "{format_version:1,objects:[";
    text.append(objects.data(), objects.size());
    text += "]}";
    const ContentSource sources[] = {{"objects.json5", {text.data(), text.size()}}, items};
    Diagnostics diagnostics;
    auto content = LoadContent({sources, 2}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(content);
    return ContentFingerprint(*content);
}

TEST_CASE("Object fingerprints include machine configuration", "[network][object][fingerprint]") {
    constexpr const char* baseline = R"({id:'press',name:'Press',behavior:'crafting_machine',machine:{
        input:'scrap',input_count:3,output:'pipe',output_count:1,
        duration_ticks:300,energy_capacity:3000,energy_per_tick:2}})";
    const auto original = Fingerprint(baseline);
    const char* changes[] = {
        "{id:'other',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Other',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'none'}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'pipe',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:4,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'scrap',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:2,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:301,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3001,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:3}}"
    };
    for (const auto* change : changes) {
        CAPTURE(change);
        CHECK(Fingerprint(change) != original);
    }
    CHECK(Fingerprint("") != original);
}

TEST_CASE("Object fingerprints are stable across source ordering and empty objects", "[network][object][fingerprint]") {
    const ContentSource first{"first", "{format_version:1,objects:[{id:'z',name:'Z',behavior:'none'}]}"};
    const ContentSource second{"second", "{format_version:1,objects:[{id:'a',name:'A',behavior:'none'}]}"};
    const ContentSource forward[] = {items, first, second};
    const ContentSource reverse[] = {second, first, items};
    const auto a = LoadContent({forward, 3});
    const auto b = LoadContent({reverse, 3});
    REQUIRE(a); REQUIRE(b);
    CHECK(ContentFingerprint(*a) == ContentFingerprint(*b));
    const auto noObjects = LoadContent({&items, 1});
    REQUIRE(noObjects);
    CHECK(ContentFingerprint(*noObjects) == Fingerprint(""));
    CHECK(ContentFingerprint(*a) != ContentFingerprint(*noObjects));
}

TEST_CASE("Forge LAN commands and action receipts preserve canonical identities", "[network][forge][codec]") {
    for (const auto kind : {CommandKind::load_forge, CommandKind::ignite_forge,
        CommandKind::extinguish_forge, CommandKind::discard_forge_fuel}) {
        Command command; command.kind = kind; command.target = 20;
        if (kind == CommandKind::load_forge) command.item = 30;
        wire::Writer writer; REQUIRE(writer.CommandValue(command));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        Command decoded; REQUIRE(reader.CommandValue(decoded)); CHECK(reader.Done());
        CHECK(decoded.kind == kind); CHECK(decoded.target == 20); CHECK(decoded.item == command.item);
        command.otherItem = 1;
        CHECK_FALSE(wire::Writer{}.CommandValue(command));
    }
    for (const auto outcome : {StationActionOutcome::FuelLoaded, StationActionOutcome::Ignited,
        StationActionOutcome::Extinguished, StationActionOutcome::FuelDiscarded,
        StationActionOutcome::ColdForge, StationActionOutcome::InsufficientFuel,
        StationActionOutcome::NoCapacity, StationActionOutcome::InvalidFuel,
        StationActionOutcome::Disabled, StationActionOutcome::AlreadyReady,
        StationActionOutcome::AlreadyCold, StationActionOutcome::MustExtinguish, StationActionOutcome::HasFuel}) {
        const bool success = outcome <= StationActionOutcome::FuelDiscarded;
        PlayerCommandResult receipt{1, 2, 3};
        receipt.result.outcome = success ? Outcome::station_changed : Outcome::station_refused;
        receipt.result.stationOutcome = outcome;
        receipt.result.advanced = success;
        receipt.result.cost = success ? 100 : 0;
        wire::Writer writer; REQUIRE(writer.Result(receipt));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        PlayerCommandResult decoded; REQUIRE(reader.Result(decoded)); CHECK(reader.Done());
        CHECK(decoded.result.stationOutcome == outcome);
        receipt.result.advanced = !success;
        CHECK_FALSE(wire::Writer{}.Result(receipt));
    }
}

TEST_CASE("Forge LAN station and inspection state enforce fuel capacity and typed absence", "[network][forge][codec]") {
    const ContentSource source{"forge-network", R"({format_version:1,definitions:[
      {type:'tool_trait',id:'forge_heat',name:'Forge heat'},
      {type:'item',id:'fuel',name:'Fuel',mass_g:1,volume_ml:1}
    ],objects:[
      {id:'forge',name:'Forge',behavior:'forge',tool_traits:[{id:'forge_heat',tier:1}],
       forge:{fuel_capacity:24000,ignition_cost:1000,fuels:[{item:'fuel',heat:6000}]}},
      {id:'table',name:'Table',behavior:'none'}
    ]})"};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    const auto& content = *loaded;
    PlayerInspection inspection; inspection.requestId = 1;
    auto& view = inspection.view;
    view.width = 1; view.height = 1; view.playerId = 1;
    ViewCell cell;
    cell.overlayVisibility = Visibility::Visible; cell.object = 20;
    cell.objectAppearance.kind = ObjectKind::Generic;
    cell.objectAppearance.type = content.FindObject("forge");
    view.cells.push_back(cell);
    StationView station;
    station.id = 20; station.type = content.FindObject("forge"); station.enabled = true;
    station.packing = StationActionOutcome::FixedFixture; station.forge = {12000, true};
    view.stations.push_back(station);
    inspection.mapItems.resize(1);
    auto& details = inspection.details;
    details.object = 20; details.objectType = station.type; details.objectKind = ObjectKind::Generic;
    details.enabled = true; details.forge = station.forge;
    REQUIRE(wire::ValidInspectionDefinitions(inspection, content));
    wire::Writer writer; REQUIRE(writer.Inspection(inspection));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    PlayerInspection decoded; REQUIRE(reader.Inspection(decoded)); CHECK(reader.Done());
    CHECK(decoded.details.forge == details.forge);
    CHECK(decoded.view.stations == view.stations);
    CHECK(wire::ValidInspectionDefinitions(decoded, content));
    SECTION("empty ready station") {
        view.stations[0].forge.fuel = 0;
        CHECK_FALSE(wire::Writer{}.Inspection(inspection));
    }
    SECTION("empty ready inspection") {
        details.forge.fuel = 0;
        CHECK_FALSE(wire::Writer{}.Inspection(inspection));
    }
    SECTION("station capacity") {
        view.stations[0].forge.fuel = 24001;
        CHECK_FALSE(wire::ValidInspectionDefinitions(inspection, content));
    }
    SECTION("inspection capacity") {
        details.forge.fuel = 24001;
        CHECK_FALSE(wire::ValidInspectionDefinitions(inspection, content));
    }
    SECTION("nonforge inspection") {
        details.objectType = content.FindObject("table");
        CHECK_FALSE(wire::ValidInspectionDefinitions(inspection, content));
    }
    SECTION("nonforge station") {
        view.stations[0].type = content.FindObject("table");
        CHECK_FALSE(wire::ValidInspectionDefinitions(inspection, content));
    }
    SECTION("object absent") {
        details.object = 0; details.objectType = {}; details.objectKind = ObjectKind::None; details.enabled = false;
        CHECK_FALSE(wire::Writer{}.Inspection(inspection));
    }
    SECTION("invalid readiness byte") {
        REQUIRE(writer.bytes.size() >= 3);
        writer.bytes[writer.bytes.size() - 3] = 2; // Ready precedes the empty item-list count.
        wire::Reader invalid{{writer.bytes.data(), writer.bytes.size()}};
        CHECK_FALSE(invalid.Inspection(decoded));
    }
}
}
