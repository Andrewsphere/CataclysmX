#include "core/world.h"
#include "core/ratio.h"
#include "power/power_system.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace {
using namespace cataclysmx;
constexpr Tick hourTicks = powerDayTicks / 24;
constexpr Tick beforeNoon = 4 * hourTicks - powerQuantumTicks;

template<class T, class E>
T Take(eastl::expected<T, E> result) {
    REQUIRE(result);
    return eastl::move(*result);
}

ContentCatalog SolarContent() {
    const ContentSource source{"solar-runtime", R"({format_version:1,objects:[
      {id:'solar',name:'Solar',behavior:'solar',solar:{peak_output_per_tick:1}},
      {id:'generator',name:'Generator',behavior:'generator',
        generator:{fuel_item:'fuel',fuel_capacity:24000,energy_per_fuel:4000,output_per_tick:4}},
      {id:'battery_box',name:'Battery',behavior:'grid_battery'},
      {id:'machine',name:'Machine',behavior:'crafting_machine',
        machine:{input:'material',input_count:1,output:'product',output_count:1,
          duration_ticks:200,energy_capacity:0,energy_per_tick:1,grid_powered:true}}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
        appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
        appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,
        blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'fuel',name:'Wood',mass_g:1000,volume_ml:1500},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:1},
      {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:1},
      {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,placed_object:'battery_box',
        capabilities:[{type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:0,rechargeable:true}]}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(result));
}

struct SolarRig {
    ContentCatalog content = SolarContent();
    World world{content};
    ActorHandle actor{};
    ObjectHandle solar{}, generator{};

    explicit SolarRig(Tick now = beforeNoon) {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        for (int y = 0; y <= world_max_y; ++y) {
            for (int x = 6; x <= 10; ++x) {
                for (int z = 6; z <= 10; ++z) {
                    const TileCoord p{x, z, y};
                    if (const auto object = world.ObjectAt(p); object.value) { REQUIRE(world.DestroyObject(object)); }
                    if (const auto occupant = world.ActorAt(p); occupant.value) { REQUIRE(world.DestroyActor(occupant)); }
                    REQUIRE(world.SetTile(p, {content.FindTerrain(y == 0 ? "floor" : "air")}));
                }
            }
        }
        actor = Take(world.CreateActor({8, 8}, true));
        Take(world.Power().ExtendBounds({{6, 6}, {10, 10}}));
        world.ElapseTicks(now);
        // Deliberately give the fueled generator an earlier identity than solar.
        generator = Take(world.CreateObject(content.FindObject("generator"), {7, 8}));
        solar = Take(world.CreateObject(content.FindObject("solar"), {8, 7}));
    }
    void Start(ObjectHandle object) {
        REQUIRE(world.UseObject(actor, object, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    }
    void Stop(ObjectHandle object) {
        REQUIRE(world.UseObject(actor, object, ObjectAction::Stop).outcome == ObjectUseOutcome::Succeeded);
    }
    ItemHandle Battery(std::uint32_t charge = 0) {
        const auto box = Take(world.CreateObject(content.FindObject("battery_box"), {9, 8}));
        const auto item = Take(world.CreateItem(content.FindItem("battery"), ItemLocation::Stored(box)));
        REQUIRE(world.SetItemEnergy(item, Store(), charge));
        world.Power().SyncObject(box);
        return item;
    }
    ItemCapabilityId Store() const {
        return content.GetItem(content.FindItem("battery")).energyStores.front().capability;
    }
    std::uint32_t Charge(ItemHandle item) const { return world.GetItemEnergy(item, Store())->energy; }
    PowerOutcome Spend(std::uint64_t amount) {
        const PowerDemand demand{{8, 8}, amount};
        return world.Power().TrySupply({&demand, 1});
    }
};

TEST_CASE("Solar integrates a full daylight triangle without per tick rounding loss", "[power][solar]") {
    CHECK(SolarDaylightRatio(0) == ratioScale / 3);
    CHECK(SolarDaylightRatio(4 * hourTicks) == ratioScale);
    CHECK(SolarDaylightRatio(10 * hourTicks) == 0);
    CHECK(SolarDaylightRatio(22 * hourTicks) == 0);
    CHECK_FALSE(IsSolarDaytime(10 * hourTicks));
    CHECK(IsSolarDaytime(22 * hourTicks));
    CHECK(TicksUntilSolarDawn(10 * hourTicks) == 12 * hourTicks);
    CHECK(TicksUntilSolarDawn(22 * hourTicks) == powerDayTicks);
    CHECK(SolarIntervalAllowance(0, 1) == 33);
    CHECK(SolarIntervalAllowance(beforeNoon, 1) == 100);
    CHECK(SolarIntervalAllowance(beforeNoon + 25, 1) == 75);
    for (const std::uint32_t peak : {1u, 17u, UINT32_MAX}) {
        std::uint64_t total = 0;
        for (Tick now = 0; now < powerDayTicks; now += powerQuantumTicks) {
            total += SolarIntervalAllowance(now, peak);
        }
        CHECK(total == (powerDayTicks / 4) * peak);
    }
    CHECK(SolarIntervalAllowance(UINT64_MAX, UINT32_MAX) == 0);
    CHECK(SolarIntervalAllowance(UINT64_MAX - 7, UINT32_MAX) <= std::uint64_t{7} * UINT32_MAX);
    CHECK(SolarDaylightRatio(UINT64_MAX) <= ratioScale);
}

TEST_CASE("Solar preview is pure and finite payment orders solar fuel and batteries", "[power][solar]") {
    SolarRig r;
    const auto battery = r.Battery(50);
    REQUIRE(r.world.SetObjectEnergy(r.generator, 1000));
    r.Start(r.generator);
    r.Start(r.solar);
    const auto before = r.world.CaptureObjectState(r.solar);
    const PowerDemand demand{{8, 8}, 150};
    CHECK(r.world.Power().CanSupply({&demand, 1}) == PowerOutcome::Supplied);
    CHECK(r.world.CaptureObjectState(r.solar) == before);
    REQUIRE(r.Spend(150) == PowerOutcome::Supplied);
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 0);
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 950);
    CHECK(r.Charge(battery) == 50);
    const auto fuel = r.world.CaptureObjectState(r.generator);
    const auto power = r.world.Power().Capture();
    CHECK(r.Spend(401) == PowerOutcome::InsufficientPower);
    CHECK(r.world.CaptureObjectState(r.generator) == fuel);
    CHECK(r.world.Power().Capture() == power);
    CHECK(r.Charge(battery) == 50);
    REQUIRE(r.Spend(400) == PowerOutcome::Supplied);
    CHECK(r.Charge(battery) == 0);
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 600);
}

TEST_CASE("Solar surplus precedes older fueled sources and charges once per interval", "[power][solar]") {
    SolarRig r;
    const auto battery = r.Battery(950);
    REQUIRE(r.world.SetObjectEnergy(r.generator, 1000));
    r.Start(r.generator);
    r.Start(r.solar);
    r.world.Power().ProcessEvents();
    CHECK(r.Charge(battery) == 1000);
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 1000);
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 50);
    REQUIRE(r.world.SetItemEnergy(battery, r.Store(), 900));
    r.world.Power().ProcessEvents();
    CHECK(r.Charge(battery) == 900);
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 1000);
    CHECK(r.world.Power().Counters().chargedEnergy == 50);
}

TEST_CASE("Solar pays due machine work before charging and paid work survives cover", "[power][solar]") {
    SolarRig r{beforeNoon - powerQuantumTicks};
    const auto battery = r.Battery();
    const auto machine = Take(r.world.CreateObject(r.content.FindObject("machine"), {8, 9}));
    Take(r.world.CreateItem(r.content.FindItem("material"), ItemLocation::Carried(r.actor)));
    r.Start(r.solar);
    r.Start(machine);
    REQUIRE(r.world.Power().ProcessEvents());
    CHECK(r.Charge(battery) == 0);
    CHECK(r.world.DescribeObjectState(machine)->fundedTicks == 100);
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 0);
    REQUIRE(r.world.SetTile({8, 7, 1}, {r.content.FindTerrain("floor")}));
    r.world.ElapseTicks(100);
    CHECK(r.world.DescribeObjectState(machine)->remainingWork == 100);
    CHECK(r.world.DescribeObjectState(machine)->fundedTicks == 0);
    r.world.Power().ProcessEvents();
    CHECK(r.world.DescribeObjectState(machine)->runRequested);
    CHECK(r.world.DescribeObjectState(machine)->fundedTicks == 0);
    CHECK(r.Charge(battery) == 0);
}

TEST_CASE("Mains supplies work and charges batteries before solar or wood gas", "[power][solar]") {
    SolarRig r;
    const auto battery = r.Battery();
    auto saved = r.world.Power().Capture();
    const auto grid = r.world.Power().GridAt({8, 8});
    REQUIRE(grid);
    for (auto& candidate : saved.grids) {
        if (candidate.id == *grid) { candidate.mainsConnected = true; }
    }
    REQUIRE(r.world.Power().Restore(saved, r.world.GetTick()));
    REQUIRE(r.world.SetObjectEnergy(r.generator, 4000));
    r.Start(r.generator);
    r.Start(r.solar);
    REQUIRE(r.Spend(10000) == PowerOutcome::Supplied);
    r.world.Power().ProcessEvents();
    CHECK(r.Charge(battery) == 1000);
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 100);
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 4000);
}

TEST_CASE("Wood gas accepts whole wood portions and spends only delivered nighttime energy", "[power][solar][woodgas]") {
    SolarRig r{10 * hourTicks};
    for (unsigned i = 0; i < 6; ++i) {
        const auto wood = Take(r.world.CreateItem(r.content.FindItem("fuel"), ItemLocation::Carried(r.actor)));
        REQUIRE(r.world.UseObject(r.actor, r.generator, ObjectAction::Refuel).outcome == ObjectUseOutcome::Succeeded);
        CHECK_FALSE(r.world.GetItem(wood));
    }
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 24000);
    const auto excess = Take(r.world.CreateItem(r.content.FindItem("fuel"), ItemLocation::Carried(r.actor)));
    const auto refused = r.world.UseObject(r.actor, r.generator, ObjectAction::Refuel);
    CHECK(refused.outcome == ObjectUseOutcome::Refused);
    CHECK(refused.reason == ObjectUseReason::NoCapacity);
    CHECK(r.world.GetItem(excess));
    CHECK_FALSE(r.world.DescribeObjectState(r.generator)->runRequested);
    r.Start(r.solar);
    r.Start(r.generator);
    REQUIRE(r.Spend(400) == PowerOutcome::Supplied);
    CHECK(r.Spend(1) == PowerOutcome::InsufficientPower);
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 23600);
    r.world.ElapseTicks(1000);
    CHECK(r.world.DescribeObjectState(r.generator)->energy == 23600);
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 0);
}

TEST_CASE("Solar stop enable move and geometry changes cannot reissue an allowance", "[power][solar]") {
    SolarRig r;
    r.Start(r.solar);
    REQUIRE(r.Spend(30) == PowerOutcome::Supplied);
    REQUIRE(r.world.SetObjectEnabled(r.solar, false));
    CHECK(r.Spend(1) == PowerOutcome::InsufficientPower);
    REQUIRE(r.world.SetObjectEnabled(r.solar, true));
    REQUIRE(r.world.MoveObject(r.solar, {9, 7}));
    REQUIRE(r.Spend(20) == PowerOutcome::Supplied);
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 50);
    REQUIRE(r.world.SetTile({9, 7, 1}, {r.content.FindTerrain("floor")}));
    CHECK(r.Spend(1) == PowerOutcome::InsufficientPower);
    REQUIRE(r.world.SetTile({9, 7, 1}, {r.content.FindTerrain("air")}));
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 50);
    r.Stop(r.solar);
    r.Start(r.solar);
    CHECK(r.Spend(1) == PowerOutcome::InsufficientPower);
    r.world.ElapseTicks(100);
    CHECK(r.Spend(1) == PowerOutcome::Supplied);
}

TEST_CASE("Covered solar claims zero until its next interval and ignores rendered sky", "[power][solar]") {
    SolarRig r;
    REQUIRE(r.world.SetTile({8, 7, 1}, {r.content.FindTerrain("floor")}));
    r.Start(r.solar);
    r.world.Power().ProcessEvents();
    CHECK(r.world.DescribeObjectState(r.solar)->generatorIntervalInitialized);
    CHECK(r.world.DescribeObjectState(r.solar)->generatorOutputRemaining == 0);
    REQUIRE(r.world.SetTile({8, 7, 1}, {r.content.FindTerrain("air")}));
    r.world.SetSkyLight(0);
    CHECK(r.Spend(1) == PowerOutcome::InsufficientPower);
    r.world.ElapseTicks(100);
    CHECK(r.Spend(1) == PowerOutcome::Supplied);
}

TEST_CASE("Solar late startup claims only the unelapsed interval", "[power][solar]") {
    SolarRig r;
    r.world.ElapseTicks(25);
    r.Start(r.solar);
    CHECK(r.Spend(76) == PowerOutcome::InsufficientPower);
    CHECK(r.Spend(75) == PowerOutcome::Supplied);
    CHECK(r.Spend(1) == PowerOutcome::InsufficientPower);
}

TEST_CASE("Solar-only residency sleeps to dawn and resumes batched production", "[power][solar][residency]") {
    SolarRig r{10 * hourTicks}; // 18:00.
    const auto battery = r.Battery();
    r.Start(r.solar);
    r.world.Power().ProcessEvents();
    REQUIRE(r.world.Power().NextEventIn());
    CHECK(*r.world.Power().NextEventIn() == 12 * hourTicks);
    Vector<ColumnCoord> held;
    r.world.Power().CollectWorkshopDemand(held);
    CHECK_FALSE(held.empty());
    const auto passes = r.world.Power().Counters().eventPasses;
    r.world.ElapseTicks(12 * hourTicks);
    CHECK(r.world.Power().Counters().eventPasses == passes);
    CHECK(r.Charge(battery) == 0);
    REQUIRE(r.world.Power().NextEventIn());
    CHECK(*r.world.Power().NextEventIn() == 0);
    r.world.ElapseTicks(10000);
    CHECK(r.Charge(battery) == 23);
    CHECK(r.world.Power().Counters().eventPasses - passes == 100);
    r.Stop(r.solar);
    r.world.Power().ProcessEvents();
    CHECK_FALSE(r.world.Power().NextEventIn());
}

TEST_CASE("Solar batched work agrees with subdivided elapsed time", "[power][solar]") {
    SolarRig whole{0}, split{0};
    const auto first = whole.Battery(), second = split.Battery();
    whole.Start(whole.solar);
    split.Start(split.solar);
    whole.world.ElapseTicks(1000);
    for (int i = 0; i < 1000; ++i) { split.world.ElapseTicks(1); }
    CHECK(whole.Charge(first) == split.Charge(second));
    CHECK(whole.world.CaptureObjectState(whole.solar) == split.world.CaptureObjectState(split.solar));
    CHECK(whole.world.Power().Capture() == split.world.Power().Capture());
    CHECK(whole.world.Power().Counters().eventPasses == split.world.Power().Counters().eventPasses);
    CHECK(whole.world.Power().Counters().eventPasses == 10);
}

TEST_CASE("Solar restores strict source state without granting production", "[power][solar][save]") {
    SolarRig r;
    r.Start(r.solar);
    REQUIRE(r.Spend(35) == PowerOutcome::Supplied);
    const auto& definition = r.content.GetObject(r.content.FindObject("solar"));
    auto saved = r.world.CaptureObjectState(r.solar);
    auto restored = Take(MakeObjectBehavior(definition, &saved));
    CHECK(restored->Describe().generatorOutputRemaining == 65);
    REQUIRE(restored->PrepareSolarInterval(r.world.GetTick(), 100));
    CHECK(restored->Describe().generatorOutputRemaining == 65);
    restored->Elapse(powerDayTicks);
    CHECK(restored->Describe().generatorOutputRemaining == 65);
    CHECK_FALSE(restored->NextEventIn());
    saved.SetUnsigned("output_remaining", 101);
    CHECK_FALSE(ValidateObjectBehaviorState(definition, saved));
    saved = r.world.CaptureObjectState(r.solar);
    saved.SetBoolean("run_requested", false);
    CHECK_FALSE(ValidateObjectBehaviorState(definition, saved));
    saved = r.world.CaptureObjectState(r.solar);
    saved.SetUnsigned("unexpected", 0);
    CHECK_FALSE(ValidateObjectBehaviorState(definition, saved));
    auto old = r.world.Power().Capture();
    old.version = 1;
    CHECK_FALSE(ValidPowerState(old, r.world.GetTick()));
    old.devices.clear();
    REQUIRE(r.world.Power().Restore(old, r.world.GetTick()));
    CHECK(r.world.Power().Capture().version == 2);
}
}
