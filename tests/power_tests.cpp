#include "power/power_system.h"
#include "core/world.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> result) {
    REQUIRE(result); return eastl::move(*result);
}
ContentCatalog GeometryContent() {
    const ContentSource source{"power-geometry", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'}
    ]})"};
    return Take(LoadContent({&source, 1}));
}

TEST_CASE("Power geometry connects tile faces across all axes but not edges", "[power][geometry]") {
    const PowerBounds box{{0, 0, 0}, {2, 2, 2}};
    CHECK(PowerContains(box, {2, 2, 2}));
    CHECK_FALSE(PowerContains(box, {3, 2, 2}));
    CHECK(PowerConnects(box, {{3, 1, 1}, {5, 2, 2}}));
    CHECK(PowerConnects(box, {{1, 3, 1}, {2, 5, 2}}));
    CHECK(PowerConnects(box, {{1, 1, 3}, {2, 2, 5}}));
    CHECK(PowerConnects(box, {{-2, 1, 1}, {-1, 2, 2}}));
    CHECK_FALSE(PowerConnects(box, {{3, 3, 0}, {4, 4, 1}}));
    CHECK_FALSE(PowerConnects(box, {{3, 3, 3}, {4, 4, 4}}));
    CHECK_FALSE(PowerConnects(box, {{4, 0, 0}, {5, 1, 1}}));
    CHECK_FALSE(ValidPowerBounds({{0, 0, 0}, {-1, 0, 0}}));
    CHECK_FALSE(ValidPowerBounds({{0, 0, world_min_y - 1}, {0, 0, 0}}));
}

TEST_CASE("Generated electrical grids stay distinct until the extension itself joins them", "[power][geometry]") {
    auto content = GeometryContent(); World world(content); PowerSystem power(world, 42);
    const auto first = Take(power.RegisterBuilding({0, 0}, {{0, 0, -1}, {1, 1, 2}}));
    const auto second = Take(power.RegisterBuilding({2, 0}, {{2, 0, -1}, {3, 1, 2}}));
    const auto third = Take(power.RegisterBuilding({4, 0}, {{4, 0, -1}, {5, 1, 2}}));
    REQUIRE(first != second); REQUIRE(second != third);
    CHECK(power.GridAt({0, 0, -1}) == eastl::optional<PowerGridId>{first});
    CHECK(power.GridAt({0, 0, 2}) == eastl::optional<PowerGridId>{first});
    const PowerBounds extension{{1, 2, 0}, {2, 2, 0}};
    const auto quote = Take(power.Quote(extension));
    CHECK(quote.newTiles == 2); CHECK(quote.wiringUnits == 2); CHECK(quote.mergedGrids == 2);
    CHECK(Take(power.ExtendBounds(extension)) == first);
    CHECK(power.GridAt({2, 0}) == eastl::optional<PowerGridId>{first});
    CHECK(power.GridAt({4, 0}) == eastl::optional<PowerGridId>{third});
    CHECK(Take(power.RegisterBuilding({2, 0}, {{2, 0, -1}, {3, 1, 2}})) == first);
    CHECK(Take(power.Quote(extension)).newTiles == 0);
    CHECK_FALSE(power.RegisterBuilding({10, 0}, {{0, 0}, {0, 0}}));
}

TEST_CASE("Wiring preserves union holes and configured single-layer limits", "[power][geometry]") {
    auto content = GeometryContent(); World world(content); PowerSystem power(world, 42);
    const auto id = Take(power.ExtendBounds({{0, 0}, {2, 0}}));
    CHECK(Take(power.ExtendBounds({{0, 1}, {0, 2}})) == id);
    CHECK_FALSE(power.GridAt({2, 2}));
    CHECK(Take(power.Quote({{0, 0}, {2, 2}})).newTiles == 4);
    CHECK_FALSE(power.Quote({{0, 0}, {16, 0}}));
    CHECK_FALSE(power.Quote({{0, 0, 0}, {0, 0, 1}}));
    REQUIRE(power.SetSettings({2, 3}));
    CHECK_FALSE(power.Quote({{0, 0}, {2, 0}}));
    CHECK(power.SetSettings({17, 3}));
    CHECK_FALSE(power.SetSettings({maxPowerWiringExtent + 1, 3}));
    CHECK(power.Settings() == PowerSettings{17, 3});
}

TEST_CASE("Global power DTO restores remote holds and rejects ambiguous geometry atomically", "[power][save]") {
    auto content = GeometryContent(); World world(content); PowerSystem power(world, 42);
    Take(power.RegisterBuilding({0, 0}, {{0, 0, -1}, {40, 1, 2}}));
    auto saved = power.Capture();
    saved.devices.push_back({100, {1, 0}, PowerDeviceKind::Machine, true});
    saved.devices.push_back({101, {33, 0}, PowerDeviceKind::Battery, false});
    saved.devices.push_back({102, {70, 0}, PowerDeviceKind::Machine, false});
    saved.grids.front().fairnessCursor = 100;
    saved.grids.front().intervalProcessed = true;
    saved.grids.front().processedInterval = 100;
    REQUIRE(power.Restore(saved, 150));
    CHECK(power.Capture() == saved);
    Vector<ColumnCoord> columns; power.CollectWorkshopDemand(columns);
    REQUIRE(columns.size() == 2);
    CHECK(columns[0] == ColumnCoord{0, 0}); CHECK(columns[1] == ColumnCoord{2, 0});
    Vector<PowerColumnDebug> debug; power.CollectWorkshopDebug(debug);
    REQUIRE(debug.size() == 2);
    CHECK(debug[0].ownerHolds == 1); CHECK(debug[1].sourceDependencies == 1);
    auto invalid = saved;
    invalid.grids.push_back(invalid.grids.front());
    invalid.grids.back().id = {invalid.nextGridId++};
    CHECK_FALSE(power.Restore(invalid, 150));
    CHECK(power.Capture() == saved);
    invalid = saved; invalid.devices.front().object = 0;
    CHECK_FALSE(ValidPowerState(invalid, 150));
    invalid = saved; invalid.grids.front().processedInterval = 200;
    CHECK_FALSE(ValidPowerState(invalid, 150));
    invalid = saved; invalid.devices[1].chargeProcessed = true; invalid.devices[1].chargeInterval = 100;
    CHECK_FALSE(ValidPowerState(invalid, 150));
    power.UnregisterObject(100); power.CollectWorkshopDemand(columns);
    CHECK(columns.empty());
}

TEST_CASE("Power cutoff is seed stable and independent of wall clock pacing", "[power]") {
    for (std::uint32_t seed = 0; seed < 100; ++seed) {
        const auto cutoff = SeededMainsCutoff(seed);
        CHECK(cutoff == SeededMainsCutoff(seed));
        CHECK(cutoff >= 7 * powerDayTicks); CHECK(cutoff <= 14 * powerDayTicks);
        CHECK(cutoff % powerDayTicks == 0);
    }
    CHECK(PowerIntervalStart(199) == 100);
    CHECK(PowerIntervalRemaining(199) == 1);
    CHECK(PowerIntervalRemaining(200) == 100);
    CHECK(PowerIntervalRemaining(std::numeric_limits<Tick>::max()) == 0);
}

TEST_CASE("Mains preview is pure and player grids do not inherit mains by proximity", "[power]") {
    auto content = GeometryContent(); World world(content); PowerSystem power(world, 42);
    Take(power.RegisterBuilding({0, 0}, {{0, 0}, {1, 1}}));
    Take(power.ExtendBounds({{5, 0}, {5, 0}}));
    const PowerDemand demands[] = {{{0, 0}, UINT64_MAX}, {{5, 0}, 1}};
    const auto before = power.Capture();
    CHECK(power.CanSupply(demands) == PowerOutcome::InsufficientPower);
    CHECK(power.TrySupply(demands) == PowerOutcome::InsufficientPower);
    CHECK(power.Capture() == before);
    CHECK(power.TrySupply({demands, 1}) == PowerOutcome::Supplied);
    CHECK(power.Capture() == before);
    const PowerDemand missing{{100, 100}, 1};
    CHECK(power.TrySupply({&missing, 1}) == PowerOutcome::NoGrid);
}

TEST_CASE("Whole building registration preflight checks pending overlaps and grid ID headroom", "[power][geometry]") {
    auto content = GeometryContent(); World world(content); PowerSystem power(world, 42);
    const BuildingPowerRegistration buildings[] = {
        {{0, 0}, {{0, 0, -1}, {3, 3, 1}}},
        {{4, 0}, {{4, 0, -1}, {7, 3, 1}}},
        {{0, 0}, {{0, 0, -1}, {3, 3, 1}}}
    };
    const auto before = power.Capture();
    REQUIRE(power.ValidateBuildings(buildings));
    CHECK(power.Capture() == before);
    for (const auto& building : buildings) { Take(power.RegisterBuilding(building.origin, building.bounds)); }
    CHECK(power.Capture().grids.size() == 2);
    const auto after = power.Capture();
    REQUIRE(power.ValidateBuildings(buildings)); CHECK(power.Capture() == after);
    const BuildingPowerRegistration overlaps[] = {
        {{20, 0}, {{20, 0}, {23, 3}}}, {{22, 0}, {{22, 0}, {25, 3}}}
    };
    CHECK_FALSE(power.ValidateBuildings(overlaps)); CHECK(power.Capture() == after);
    auto nearlyFull = power.Capture(); nearlyFull.nextGridId = UINT64_MAX - 1;
    REQUIRE(power.Restore(nearlyFull, 0));
    const BuildingPowerRegistration last[] = {
        {{20, 0}, {{20, 0}, {23, 3}}}, {{24, 0}, {{24, 0}, {27, 3}}}
    };
    CHECK_FALSE(power.ValidateBuildings(last)); CHECK(power.Capture() == nearlyFull);
    REQUIRE(power.ValidateBuildings({last, 1}));
    Take(power.RegisterBuilding(last[0].origin, last[0].bounds));
    REQUIRE(power.ValidateBuildings({last, 1})); // Existing origin needs no ID.
    CHECK_FALSE(power.ValidateBuildings({last + 1, 1}));
}

ContentCatalog EquipmentContent() {
    const ContentSource source{"power-equipment", R"({format_version:1,objects:[
      {id:'generator',name:'Generator',behavior:'generator',generator:{fuel_item:'fuel',fuel_capacity:10000,energy_per_fuel:1000,output_per_tick:1}},
      {id:'battery_box',name:'Battery box',behavior:'grid_battery'},
      {id:'machine',name:'Machine',behavior:'crafting_machine',machine:{input:'material',input_count:1,output:'material',output_count:1,
        duration_ticks:300,energy_capacity:0,energy_per_tick:1,grid_powered:true}}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'fuel',name:'Fuel',mass_g:1,volume_ml:1},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:1},
      {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,placed_object:'battery_box',capabilities:[
        {type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:0,rechargeable:true,battery_class:'cell'}]}
    ]})"};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(loaded));
}
struct EquipmentRig {
    ContentCatalog content = EquipmentContent();
    World world{content};
    ActorHandle actor{};
    ObjectHandle generator{};
    explicit EquipmentRig(bool startGenerator = true) {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        for (int x = 6; x <= 10; ++x) { for (int z = 6; z <= 10; ++z) {
            const TileCoord position{x, z};
            if (auto object = world.ObjectAt(position); object.value) { REQUIRE(world.DestroyObject(object)); }
            if (auto occupant = world.ActorAt(position); occupant.value) { REQUIRE(world.DestroyActor(occupant)); }
            REQUIRE(world.SetTile(position, {content.FindTerrain("floor")}));
        } }
        actor = Take(world.CreateActor({8, 8}, true));
        Take(world.Power().ExtendBounds({{6, 6}, {10, 10}}));
        generator = Take(world.CreateObject(content.FindObject("generator"), {7, 8}));
        REQUIRE(world.SetObjectEnergy(generator, 1000));
        if (startGenerator) {
            REQUIRE(world.UseObject(actor, generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
        }
        world.Power().SyncObject(generator);
    }
    ObjectHandle Machine(TileCoord position) {
        const auto machine = Take(world.CreateObject(content.FindObject("machine"), position));
        Take(world.CreateItem(content.FindItem("material"), ItemLocation::Carried(actor)));
        REQUIRE(world.UseObject(actor, machine, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
        world.Power().SyncObject(machine);
        return machine;
    }
    ItemHandle Battery(TileCoord position, std::uint32_t charge = 0) {
        const auto box = Take(world.CreateObject(content.FindObject("battery_box"), position));
        const auto item = Take(world.CreateItem(content.FindItem("battery"), ItemLocation::Stored(box)));
        const auto store = content.GetItem(content.FindItem("battery")).energyStores.front().capability;
        REQUIRE(world.SetItemEnergy(item, store, charge));
        world.Power().SyncObject(box);
        return item;
    }
    std::uint32_t Charge(ItemHandle item) const {
        const auto store = content.GetItem(content.FindItem("battery")).energyStores.front().capability;
        return world.GetItemEnergy(item, store)->energy;
    }
};

TEST_CASE("Finite generator supplies directly and a failed batch never debits fuel", "[power][accounting]") {
    EquipmentRig rig;
    const PowerDemand tooMuch[] = {{{8, 8}, 99}, {{8, 8}, 2}};
    const auto before = rig.world.CaptureObjectState(rig.generator);
    CHECK(rig.world.Power().CanSupply(tooMuch) == PowerOutcome::InsufficientPower);
    CHECK(rig.world.Power().TrySupply(tooMuch) == PowerOutcome::InsufficientPower);
    CHECK(rig.world.CaptureObjectState(rig.generator) == before);
    PowerDemand demand{{8, 8}, 60};
    REQUIRE(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::Supplied);
    auto view = rig.world.DescribeObjectState(rig.generator);
    REQUIRE(view); CHECK(view->energy == 940); CHECK(view->generatorOutputRemaining == 40);
    const auto saved = rig.world.Power().Capture();
    REQUIRE(rig.world.Power().Restore(saved, rig.world.GetTick()));
    rig.world.Power().ProcessEvents();
    demand.energy = 41;
    CHECK(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::InsufficientPower);
    demand.energy = 40;
    REQUIRE(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::Supplied);
    CHECK(rig.world.DescribeObjectState(rig.generator)->energy == 900);
    REQUIRE(rig.world.UseObject(rig.actor, rig.generator, ObjectAction::Stop).outcome == ObjectUseOutcome::Succeeded);
    REQUIRE(rig.world.UseObject(rig.actor, rig.generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    demand.energy = 1;
    CHECK(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::InsufficientPower);
    rig.world.ElapseTicks(powerQuantumTicks);
    rig.world.Power().ProcessEvents();
    demand.energy = 100;
    REQUIRE(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::Supplied);
    CHECK(rig.world.DescribeObjectState(rig.generator)->energy == 800);
}

TEST_CASE("Generator surplus charges the original item once and never duplicates chemical energy", "[power][accounting]") {
    EquipmentRig rig;
    rig.world.Power().ProcessEvents();
    const auto box = Take(rig.world.CreateObject(rig.content.FindObject("battery_box"), {9, 8}));
    const auto battery = Take(rig.world.CreateItem(rig.content.FindItem("battery"), ItemLocation::Stored(box)));
    const auto store = rig.content.GetItem(rig.content.FindItem("battery")).energyStores.front().capability;
    rig.world.Power().SyncObject(box);
    // Installation may follow the start command's already-consumed surplus pass.
    // A fresh interval gives this newly installed store its first charging turn.
    rig.world.ElapseTicks(powerQuantumTicks);
    rig.world.Power().ProcessEvents();
    CHECK(rig.world.GetItemEnergy(battery, store)->energy == 100);
    CHECK(rig.world.DescribeObjectState(rig.generator)->energy == 900);
    const auto saved = rig.world.Power().Capture();
    REQUIRE(rig.world.Power().Restore(saved, rig.world.GetTick()));
    rig.world.Power().ProcessEvents();
    CHECK(rig.world.GetItemEnergy(battery, store)->energy == 100);
    PowerDemand demand{{8, 8}, 60};
    REQUIRE(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::Supplied);
    CHECK(rig.world.GetItemEnergy(battery, store)->energy == 40);
    rig.world.Power().Invalidate(); rig.world.Power().ProcessEvents();
    CHECK(rig.world.GetItemEnergy(battery, store)->energy == 40);
    CHECK(rig.world.DescribeObjectState(rig.generator)->energy == 900);
    REQUIRE(rig.world.TransferItem(battery, ItemLocation::Carried(rig.actor)));
    rig.world.Power().SyncObject(box);
    demand.energy = 1;
    CHECK(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::InsufficientPower);
    CHECK(rig.world.GetItemEnergy(battery, store)->energy == 40);
}

TEST_CASE("Grid machines rotate equal first opportunity and reload does not rotate twice", "[power][fairness]") {
    EquipmentRig rig;
    const auto first = rig.Machine({8, 7});
    const auto second = rig.Machine({9, 8});
    rig.world.Power().ProcessEvents();
    CHECK(rig.world.DescribeObjectState(first)->fundedTicks == 100);
    CHECK(rig.world.DescribeObjectState(second)->fundedTicks == 0);
    const auto saved = rig.world.Power().Capture();
    REQUIRE(rig.world.Power().Restore(saved, rig.world.GetTick()));
    rig.world.Power().ProcessEvents();
    CHECK(rig.world.Power().Capture().grids.front().fairnessCursor == saved.grids.front().fairnessCursor);
    CHECK(rig.world.DescribeObjectState(first)->fundedTicks == 100);
    CHECK(rig.world.DescribeObjectState(second)->fundedTicks == 0);
    rig.world.ElapseTicks(powerQuantumTicks);
    rig.world.Power().ProcessEvents();
    CHECK(rig.world.DescribeObjectState(first)->remainingWork == 200);
    CHECK(rig.world.DescribeObjectState(first)->fundedTicks == 0);
    CHECK(rig.world.DescribeObjectState(second)->fundedTicks == 100);
    CHECK(rig.world.Power().Capture().grids.front().fairnessCursor == rig.world.GetObject(second)->id);
}

TEST_CASE("Player grid dependencies include unloaded source columns without any machine hold", "[power][residency]") {
    auto content = GeometryContent(); World world(content); PowerSystem power(world, 42);
    Take(power.RegisterBuilding({-32, 0}, {{-32, 0, -1}, {47, 1, 2}}));
    Take(power.RegisterBuilding({80, 0}, {{80, 0, 0}, {95, 1, 2}}));
    auto saved = power.Capture();
    saved.devices.push_back({100, {-17, 0, -1}, PowerDeviceKind::Battery, false});
    saved.devices.push_back({101, {33, 0, 2}, PowerDeviceKind::Generator, false});
    saved.devices.push_back({102, {34, 0, 0}, PowerDeviceKind::Battery, false});
    saved.devices.push_back({103, {17, 0}, PowerDeviceKind::Machine, false});
    saved.devices.push_back({104, {81, 0}, PowerDeviceKind::Battery, false});
    REQUIRE(power.Restore(saved, 0));
    Vector<ColumnCoord> columns;
    power.CollectWorkshopDemand(columns); CHECK(columns.empty());
    const PowerBounds areas[] = {{{0, 0}, {0, 0}}, {{1, 0, 2}, {1, 0, 2}}, {{500, 500}, {500, 500}}};
    power.CollectConsumerDemand(areas, columns);
    REQUIRE(columns.size() == 2);
    CHECK(columns[0] == ColumnCoord{-2, 0});
    CHECK(columns[1] == ColumnCoord{2, 0});
    power.CollectConsumerDemand({}, columns); CHECK(columns.empty());
    CHECK(power.Capture() == saved);
}

TEST_CASE("Consumer areas intersect exact grid unions including inclusive edges and layers", "[power][residency][geometry]") {
    auto content = GeometryContent(); World world(content); PowerSystem power(world, 42);
    Take(power.ExtendBounds({{0, 0}, {15, 0}}));
    Take(power.ExtendBounds({{0, 1}, {0, 15}})); // L-shaped union, not its bounding box.
    Take(power.RegisterBuilding({20, 0}, {{20, 0, 1}, {40, 15, 1}}));
    auto saved = power.Capture();
    saved.devices.push_back({100, {0, 0}, PowerDeviceKind::Battery, false});
    saved.devices.push_back({101, {0, 1}, PowerDeviceKind::Generator, false});
    saved.devices.push_back({102, {33, 0, 1}, PowerDeviceKind::Battery, false});
    saved.devices.push_back({103, {14, 0}, PowerDeviceKind::Machine, false});
    REQUIRE(power.Restore(saved, 0));
    Vector<ColumnCoord> columns{{99, 99}};
    const PowerBounds hole{{1, 1}, {15, 15}};
    power.CollectConsumerDemand({&hole, 1}, columns); CHECK(columns.empty());
    const PowerBounds wrongLayer{{1, 1, -1}, {15, 15, -1}};
    power.CollectConsumerDemand({&wrongLayer, 1}, columns); CHECK(columns.empty());
    const PowerBounds areas[] = {
        {{15, 0}, {16, 1}}, // Last covered tile counts.
        {{0, 15}, {0, 15}}, // Duplicate source columns are deduplicated.
        {{40, 15, 1}, {40, 15, 1}}
    };
    power.CollectConsumerDemand(areas, columns);
    REQUIRE(columns.size() == 2);
    CHECK(columns[0] == ColumnCoord{0, 0}); CHECK(columns[1] == ColumnCoord{2, 0});
    CHECK(power.Capture() == saved); // Query neither funds nor activates anything.
}

TEST_CASE("Disabled and broken equipment releases holds while preserving run intent", "[power][residency]") {
    EquipmentRig rig;
    const auto machine = rig.Machine({8, 7});
    const auto held = [&rig](ObjectHandle object) {
        const auto id = rig.world.GetObject(object)->id;
        for (const auto& device : rig.world.Power().Capture().devices) {
            if (device.object == id) { return device.workshopHold; }
        }
        return false;
    };
    REQUIRE(held(machine)); REQUIRE(held(rig.generator));
    REQUIRE(rig.world.SetObjectEnabled(machine, false)); rig.world.Power().SyncObject(machine);
    CHECK_FALSE(held(machine)); CHECK(rig.world.DescribeObjectState(machine)->runRequested);
    REQUIRE(rig.world.SetObjectEnabled(rig.generator, false)); rig.world.Power().SyncObject(rig.generator);
    CHECK_FALSE(held(rig.generator));
    Vector<ColumnCoord> columns; rig.world.Power().CollectWorkshopDemand(columns); CHECK(columns.empty());
    REQUIRE(rig.world.SetObjectEnabled(machine, true)); rig.world.Power().SyncObject(machine);
    REQUIRE(held(machine));
    const auto* object = rig.world.GetObject(machine);
    const auto kind = object->kind; const auto facing = object->facing;
    REQUIRE(rig.world.SetObjectAppearance(machine, kind, facing, true)); rig.world.Power().SyncObject(machine);
    CHECK_FALSE(held(machine));
    REQUIRE(rig.world.SetObjectAppearance(machine, kind, facing, false)); rig.world.Power().SyncObject(machine);
    CHECK(held(machine));
}

TEST_CASE("Surplus allocation shares a finite generator allowance across batteries and respects fuel", "[power][accounting]") {
    EquipmentRig rig(false);
    const auto first = rig.Battery({9, 8}, 970);
    const auto second = rig.Battery({8, 9}, 900);
    const auto extra = Take(rig.world.CreateObject(rig.content.FindObject("generator"), {7, 7}));
    REQUIRE(rig.world.SetObjectEnergy(extra, 15));
    REQUIRE(rig.world.UseObject(rig.actor, rig.generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    REQUIRE(rig.world.UseObject(rig.actor, extra, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    rig.world.Power().SyncObject(extra);
    rig.world.Power().ProcessEvents();
    CHECK(rig.Charge(first) == 1000); CHECK(rig.Charge(second) == 985);
    CHECK(rig.world.DescribeObjectState(rig.generator)->energy == 900);
    CHECK(rig.world.DescribeObjectState(rig.generator)->generatorOutputRemaining == 0);
    CHECK(rig.world.DescribeObjectState(extra)->energy == 0);
    CHECK(rig.world.DescribeObjectState(extra)->generatorOutputRemaining == 85);
    CHECK(rig.world.Power().Counters().chargedEnergy == 115);
    // Refueling does not reopen this source's already-used surplus pass.
    REQUIRE(rig.world.SetObjectEnergy(extra, 100)); rig.world.Power().SyncObject(extra);
    rig.world.Power().ProcessEvents();
    CHECK(rig.Charge(second) == 985);
    const PowerDemand direct{{8, 8}, 85};
    REQUIRE(rig.world.Power().TrySupply({&direct, 1}) == PowerOutcome::Supplied);
    CHECK(rig.world.DescribeObjectState(extra)->energy == 15);
    CHECK(rig.Charge(second) == 985);
}

TEST_CASE("Due machines are funded before surplus battery charging", "[power][accounting]") {
    EquipmentRig rig(false);
    const auto battery = rig.Battery({9, 8});
    const auto machine = rig.Machine({8, 7});
    REQUIRE(rig.world.UseObject(rig.actor, rig.generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    rig.world.Power().SyncObject(rig.generator); rig.world.Power().ProcessEvents();
    CHECK(rig.world.DescribeObjectState(machine)->fundedTicks == 100);
    CHECK(rig.Charge(battery) == 0);
    CHECK(rig.world.DescribeObjectState(rig.generator)->energy == 900);
    rig.world.Power().Invalidate(); rig.world.Power().ProcessEvents();
    CHECK(rig.Charge(battery) == 0);
    CHECK(rig.world.DescribeObjectState(rig.generator)->energy == 900);
}

TEST_CASE("An unsuccessful batch cannot partially spend generator and installed battery energy", "[power][accounting]") {
    EquipmentRig rig;
    const auto battery = rig.Battery({9, 8}, 20);
    const auto generatorBefore = rig.world.CaptureObjectState(rig.generator);
    const auto batteryBefore = rig.world.CaptureItemState(battery);
    const PowerDemand demands[] = {{{8, 8}, 80}, {{8, 8}, 41}};
    CHECK(rig.world.Power().CanSupply(demands) == PowerOutcome::InsufficientPower);
    CHECK(rig.world.Power().TrySupply(demands) == PowerOutcome::InsufficientPower);
    CHECK(rig.world.CaptureObjectState(rig.generator) == generatorBefore);
    CHECK(rig.world.CaptureItemState(battery) == batteryBefore);
}

TEST_CASE("Stopped generator archive retains its spent interval after reconstruction", "[power][save]") {
    EquipmentRig rig;
    const PowerDemand demand{{8, 8}, 60};
    REQUIRE(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::Supplied);
    REQUIRE(rig.world.UseObject(rig.actor, rig.generator, ObjectAction::Stop).outcome == ObjectUseOutcome::Succeeded);
    const auto stopped = rig.world.CaptureObjectState(rig.generator);
    const auto& definition = rig.content.GetObject(rig.content.FindObject("generator"));
    auto behavior = Take(MakeObjectBehavior(definition, &stopped));
    REQUIRE(behavior->PrepareGeneratorInterval(rig.world.GetTick()));
    CHECK(behavior->Describe().generatorIntervalInitialized);
    CHECK(behavior->Describe().generatorOutputRemaining == 0);
    CHECK(behavior->Describe().energy == 940);
    CHECK(behavior->SaveState() == stopped);
    REQUIRE(rig.world.UseObject(rig.actor, rig.generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    const auto restarted = rig.world.CaptureObjectState(rig.generator);
    behavior = Take(MakeObjectBehavior(definition, &restarted));
    REQUIRE(behavior->PrepareGeneratorInterval(rig.world.GetTick()));
    CHECK_FALSE(behavior->SpendGeneratorEnergy(1));
    REQUIRE(behavior->PrepareGeneratorInterval(powerQuantumTicks));
    REQUIRE(behavior->SpendGeneratorEnergy(100));
    CHECK(behavior->Describe().energy == 840);
}

TEST_CASE("Power event processing terminates at an empty grid and a total outage", "[power][scheduling]") {
    EquipmentRig rig(false);
    rig.world.Power().ProcessEvents();
    CHECK_FALSE(rig.world.Power().NextEventIn());
    rig.Machine({8, 7}); rig.world.Power().ProcessEvents();
    REQUIRE(rig.world.Power().NextEventIn());
    CHECK(*rig.world.Power().NextEventIn() == powerQuantumTicks);
    CHECK_FALSE(rig.world.Power().ProcessEvents());
    const auto saved = rig.world.Power().Capture();
    REQUIRE(rig.world.Power().Restore(saved, 0));
    rig.world.Power().ProcessEvents();
    CHECK(*rig.world.Power().NextEventIn() == powerQuantumTicks);
}

TEST_CASE("Paid machine progress and fuel accounting do not depend on elapsed slice partition", "[power][scheduling]") {
    EquipmentRig whole;
    EquipmentRig split;
    const auto wholeMachine = whole.Machine({8, 7});
    const auto splitMachine = split.Machine({8, 7});
    whole.world.Power().ProcessEvents(); split.world.Power().ProcessEvents();
    whole.world.ElapseTicks(200); whole.world.Power().ProcessEvents();
    for (Tick duration : {Tick{17}, Tick{83}, Tick{41}, Tick{59}}) {
        split.world.ElapseTicks(duration); split.world.Power().ProcessEvents();
    }
    CHECK(whole.world.GetTick() == split.world.GetTick());
    CHECK(whole.world.CaptureObjectState(whole.generator) == split.world.CaptureObjectState(split.generator));
    CHECK(whole.world.CaptureObjectState(wholeMachine) == split.world.CaptureObjectState(splitMachine));
    CHECK(whole.world.DescribeObjectState(wholeMachine)->remainingWork == 100);
    CHECK(whole.world.DescribeObjectState(whole.generator)->energy == 700);
}

TEST_CASE("Mains stops exactly at the persisted cutoff and never returns", "[power][scheduling]") {
    auto content = GeometryContent(); World world(content);
    Take(world.Power().RegisterBuilding({0, 0}, {{0, 0}, {1, 1}}));
    const auto cutoff = world.Power().Capture().mainsCutoffTick;
    const PowerDemand demand{{0, 0}, UINT64_MAX};
    world.ElapseTicks(cutoff - 1);
    CHECK(world.Power().SliceLimit(world.GetTick()) == 1);
    CHECK(world.Power().TrySupply({&demand, 1}) == PowerOutcome::Supplied);
    world.ElapseTicks(1);
    CHECK(world.Power().TrySupply({&demand, 1}) == PowerOutcome::InsufficientPower);
    const auto saved = world.Power().Capture();
    REQUIRE(world.Power().Restore(saved, world.GetTick()));
    world.Power().ProcessEvents();
    world.ElapseTicks(powerQuantumTicks);
    CHECK(world.Power().TrySupply({&demand, 1}) == PowerOutcome::InsufficientPower);
}
}
