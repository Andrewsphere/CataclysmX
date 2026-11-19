#include "core/world.h"
#include "power/power_system.h"
#include "crafting/crafting_state.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) {
    REQUIRE(value); return eastl::move(*value);
}
ContentCatalog Content() {
    const ContentSource source{"power-crafting", R"({format_version:1,objects:[
      {id:'battery_box',name:'Battery box',behavior:'grid_battery'},
      {id:'bench',name:'Bench',behavior:'none',tool_traits:[{id:'cut',tier:1}],
        crafting_power:{store:'grid',energy_per_work:10000}},
      {id:'hammer_bench',name:'Hammer bench',behavior:'none',tool_traits:[{id:'hammering',tier:1}],
        crafting_power:{store:'grid',energy_per_work:10000}},
      {id:'multi_bench',name:'Multi bench',behavior:'none',
        tool_traits:[{id:'cut',tier:1},{id:'hammering',tier:1}],crafting_power:{store:'grid',energy_per_work:10000}},
      {id:'forge',name:'Forge',behavior:'forge',tool_traits:[{id:'forge_heat',tier:1}],
        forge:{fuel_capacity:24000,ignition_cost:1000,fuels:[{item:'charcoal',heat:6000}]}}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'tool_trait',id:'cut',name:'Cut'}, {type:'tool_trait',id:'hammering',name:'Hammer'},
      {type:'tool_trait',id:'forge_heat',name:'Forge heat'},
      {type:'item',id:'charcoal',name:'Charcoal',mass_g:1,volume_ml:1},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:1},
      {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:1},
      {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,placed_object:'battery_box',capabilities:[
        {type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:1000,rechargeable:true,battery_class:'cell'}]},
      {type:'item',id:'cutter',name:'Cutter',mass_g:1,volume_ml:1,tool_traits:[{id:'cut',tier:1}],
        crafting_power:{store:'grid',energy_per_work:10000}},
      {type:'item',id:'hammer',name:'Hammer',mass_g:1,volume_ml:1,tool_traits:[{id:'hammering',tier:1}],
        crafting_power:{store:'grid',energy_per_work:10000}},
      {type:'item',id:'local',name:'Local',mass_g:1,volume_ml:1,
        crafting_power:{store:'power',energy_per_work:5000},capabilities:[
        {type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'recipe',id:'two',inputs:[],tool_traits:[{id:'cut',tier:1},{id:'hammering',tier:1}],result:{item:'product',count:1},work:200},
      {type:'recipe',id:'mixed',inputs:[{item:'local',count:1,retained:true}],tool_traits:[{id:'cut',tier:1}],result:{item:'product',count:1},work:4},
      {type:'recipe',id:'hot',inputs:[{item:'material',count:1},{item:'local',count:1,retained:true}],
        tool_traits:[{id:'cut',tier:1},{id:'forge_heat',tier:1}],heat_per_work:10000,
        result:{item:'product',count:1},work:200},
      {type:'recipe',id:'one',inputs:[],tool_traits:[{id:'cut',tier:1}],result:{item:'product',count:1},work:200}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); return Take(eastl::move(result));
}
struct Rig {
    ContentCatalog content = Content();
    World world{content};
    ActorHandle actor{};
    ItemHandle cutter{}, battery{}, backpackBattery{};
    ObjectHandle box{};
    Rig() {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 6; z <= 10; ++z) { for (int x = 6; x <= 10; ++x) {
            const TileCoord p{x, z};
            if (auto object = world.ObjectAt(p); object.value) { REQUIRE(world.DestroyObject(object)); }
            if (auto occupant = world.ActorAt(p); occupant.value) { REQUIRE(world.DestroyActor(occupant)); }
            REQUIRE(world.SetTile(p, {content.FindTerrain("floor")}));
        } }
        actor = Take(world.CreateActor({8, 8}, true));
        cutter = Add("cutter", ItemLocation::Carried(actor));
        backpackBattery = Add("battery", ItemLocation::Carried(actor));
    }
    ItemHandle Add(const char* id, ItemLocation location) { return Take(world.CreateItem(content.FindItem(id), location)); }
    void Grid(std::uint32_t energy) {
        Take(world.Power().ExtendBounds({{8, 8}, {9, 8}}));
        box = Take(world.CreateObject(content.FindObject("battery_box"), {9, 8}));
        battery = Add("battery", ItemLocation::Stored(box)); SetEnergy(battery, "cell", energy);
        world.Power().SyncObject(box);
    }
    void SetEnergy(ItemHandle item, const char* name, std::uint32_t energy) {
        const auto cap = FindItemCapability(content.GetItem(world.GetItem(item)->type), name);
        REQUIRE(world.SetItemEnergy(item, cap, energy));
    }
    std::uint32_t Energy(ItemHandle item, const char* name) const {
        const auto cap = FindItemCapability(content.GetItem(world.GetItem(item)->type), name);
        return world.GetItemEnergy(item, cap)->energy;
    }
    CraftingResult Start(const char* recipe) { return Take(world.StartCrafting(actor, content.FindCraftingRecipe(recipe))); }
    void Step(Tick ticks) { world.ElapseTicks(ticks); world.ProcessCraftingEvents(); }
    ObjectHandle Bench(const char* type = "bench", TileCoord position = {7, 9}) {
        return Take(world.CreateObject(content.FindObject(type), position));
    }
    ItemHandle Supply(PowerBounds bounds, TileCoord position, std::uint32_t energy) {
        Take(world.Power().ExtendBounds(bounds));
        const auto source = Take(world.CreateObject(content.FindObject("battery_box"), position));
        const auto item = Add("battery", ItemLocation::Stored(source));
        SetEnergy(item, "cell", energy); world.Power().SyncObject(source); return item;
    }
    CraftingJobState Job(ItemHandle item) const {
        const auto state = world.CaptureItemState(item);
        const auto* payload = state.Find("crafting_job"); REQUIRE(payload);
        return Take(ReadCraftingState(*payload));
    }
};

TEST_CASE("Grid crafting aggregates distinct carried tool demands atomically", "[crafting][power]") {
    Rig r; r.Add("hammer", ItemLocation::Carried(r.actor)); r.Grid(150);
    CHECK(r.Start("two").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(r.battery, "cell") == 150); CHECK(r.Energy(r.backpackBattery, "cell") == 1000);
    r.SetEnergy(r.battery, "cell", 200);
    const auto started = r.Start("two"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.Energy(r.battery, "cell") == 0); CHECK(r.Energy(r.backpackBattery, "cell") == 1000);
    r.Step(100);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(r.world.InspectCrafting(started.unfinished)->completedWork == 100);
    r.SetEnergy(r.battery, "cell", 200); r.Step(1);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Resumed);
}

TEST_CASE("Mixed funding rejects missing coverage even for fractional zero charge", "[crafting][power]") {
    Rig r; const auto local = r.Add("local", ItemLocation::Carried(r.actor));
    REQUIRE(r.world.SetCraftingSettings({1, 1})); REQUIRE(r.world.SetActorSpeed(r.actor, 200));
    CHECK(r.Start("mixed").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(local, "power") == 1000);
    r.Grid(0);
    const auto started = r.Start("mixed"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.Energy(local, "power") == 1000); CHECK(r.Energy(r.battery, "cell") == 0);
    r.Step(1);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(r.Energy(local, "power") == 1000);
    r.SetEnergy(r.battery, "cell", 1);
    CHECK(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Resumed);
    CHECK(r.Energy(r.battery, "cell") == 0);
}

TEST_CASE("A local shortage cannot debit a funded grid", "[crafting][power]") {
    Rig r; const auto local = r.Add("local", ItemLocation::Carried(r.actor));
    r.Grid(100); r.SetEnergy(local, "power", 0);
    CHECK(r.Start("mixed").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(r.battery, "cell") == 100); CHECK(r.Energy(local, "power") == 0);
    r.SetEnergy(local, "power", 2);
    REQUIRE(r.Start("mixed").outcome == CraftingOutcome::Started);
    CHECK(r.Energy(r.battery, "cell") == 96); CHECK(r.Energy(local, "power") == 0);
}

TEST_CASE("Grid crafting clamps slices and keeps paid work after disconnection", "[crafting][power]") {
    Rig r; r.Grid(1000); REQUIRE(r.world.SetCraftingSettings({1, 200}));
    r.world.ElapseTicks(25);
    const auto started = r.Start("one"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.world.InspectCrafting(started.unfinished)->fundedTicksRemaining == 75);
    CHECK(r.Energy(r.battery, "cell") == 925);
    REQUIRE(r.world.TransferItem(r.battery, ItemLocation::Carried(r.actor))); r.world.Power().SyncObject(r.box);
    r.Step(75);
    CHECK(r.world.InspectCrafting(started.unfinished)->completedWork == 75);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(r.Energy(r.battery, "cell") == 925);
}

TEST_CASE("Powered stations connect at their own tile even for zero charges", "[crafting][power][workshop]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.cutter));
    const auto bench = r.Bench();
    REQUIRE(r.world.SetCraftingSettings({1, 1})); REQUIRE(r.world.SetActorSpeed(r.actor, 200));
    r.Grid(100);
    const auto inventory = r.world.GetActor(r.actor)->inventory;
    CHECK(r.Start("one").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(r.battery, "cell") == 100);
    CHECK(r.world.GetActor(r.actor)->inventory == inventory);
    CHECK(r.world.StationWorker(bench) == 0);
    // An empty covered endpoint is enough for the initial half-unit slice.
    Take(r.world.Power().ExtendBounds({{7, 9}, {7, 9}}));
    const auto job = r.Start("one"); REQUIRE(job.outcome == CraftingOutcome::Started);
    CHECK(r.Job(job.unfinished).energyRemainder == 50000000);
    CHECK(r.Energy(r.battery, "cell") == 100);
}

TEST_CASE("Powered bench works without worker coverage and bills multitrait providers once", "[crafting][power][workshop]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.cutter));
    const auto bench = r.Bench("multi_bench");
    const auto battery = r.Supply({{6, 9}, {7, 9}}, {6, 9}, 100);
    REQUIRE_FALSE(r.world.Power().GridAt({8, 8}));
    const auto job = r.Start("two"); REQUIRE(job.outcome == CraftingOutcome::Started);
    CHECK(r.Energy(battery, "cell") == 0);
    CHECK(r.world.InspectCrafting(job.unfinished)->stations.Size() == 1);
    CHECK(r.world.StationWorker(bench) == r.world.GetActor(r.actor)->id);
}

TEST_CASE("Station and handheld grids fund one atomic transaction", "[crafting][power][workshop]") {
    Rig r; r.Grid(100);
    const auto bench = r.Bench("hammer_bench");
    const auto remote = r.Supply({{6, 9}, {7, 9}}, {6, 9}, 99);
    REQUIRE(r.world.Power().GridAt({8, 8}) != r.world.Power().GridAt({7, 9}));
    CHECK(r.Start("two").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(r.battery, "cell") == 100); CHECK(r.Energy(remote, "cell") == 99);
    CHECK(r.world.StationWorker(bench) == 0);
    r.SetEnergy(remote, "cell", 100);
    REQUIRE(r.Start("two").outcome == CraftingOutcome::Started);
    CHECK(r.Energy(r.battery, "cell") == 0); CHECK(r.Energy(remote, "cell") == 0);
}

TEST_CASE("Distinct powered stations aggregate shared grids and preflight local stores", "[crafting][power][workshop]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.cutter));
    const auto cut = r.Bench();
    const auto hammer = r.Bench("hammer_bench", {9, 9});
    r.Grid(199); Take(r.world.Power().ExtendBounds({{7, 8}, {10, 9}}));
    CHECK(r.Start("two").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(r.battery, "cell") == 199);
    CHECK(r.world.StationWorker(cut) == 0); CHECK(r.world.StationWorker(hammer) == 0);
    SECTION("two distinct station charges") {
        r.SetEnergy(r.battery, "cell", 200);
        const auto job = r.Start("two"); REQUIRE(job.outcome == CraftingOutcome::Started);
        CHECK(r.Energy(r.battery, "cell") == 0);
        CHECK(r.world.InspectCrafting(job.unfinished)->stations.Size() == 2);
    }
    SECTION("local shortage spends no station energy") {
        const auto local = r.Add("local", ItemLocation::Carried(r.actor));
        r.SetEnergy(local, "power", 1);
        CHECK(r.Start("mixed").outcome == CraftingOutcome::InsufficientEnergy);
        CHECK(r.Energy(r.battery, "cell") == 199); CHECK(r.Energy(local, "power") == 1);
        CHECK(r.world.StationWorker(cut) == 0);
    }
}

TEST_CASE("Fractional station billing follows item IDs then object IDs across grids", "[crafting][power][workshop]") {
    Rig r;
    const auto bench = r.Bench("hammer_bench");
    r.Grid(0);
    const auto remote = r.Supply({{6, 9}, {7, 9}}, {6, 9}, 1);
    REQUIRE(r.world.SetCraftingSettings({1, 1})); REQUIRE(r.world.SetActorSpeed(r.actor, 200));
    const auto job = r.Start("two"); REQUIRE(job.outcome == CraftingOutcome::Started);
    CHECK(r.Energy(r.battery, "cell") == 0); CHECK(r.Energy(remote, "cell") == 0);
    CHECK(r.Job(job.unfinished).energyRemainder == 0);
    r.Step(1);
    CHECK(r.world.InspectCrafting(job.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(r.Job(job.unfinished).energyRemainder == 0);
    CHECK(r.world.StationWorker(bench) == 0);
}

TEST_CASE("Station-only funding honors power boundaries and explicit outage resume", "[crafting][power][workshop]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.cutter));
    const auto bench = r.Bench();
    const auto battery = r.Supply({{6, 9}, {7, 9}}, {6, 9}, 1000);
    REQUIRE(r.world.SetCraftingSettings({1, 200})); r.world.ElapseTicks(25);
    const auto job = r.Start("one"); REQUIRE(job.outcome == CraftingOutcome::Started);
    CHECK(r.world.InspectCrafting(job.unfinished)->fundedTicksRemaining == 75);
    CHECK(r.Energy(battery, "cell") == 925);
    r.SetEnergy(battery, "cell", 0); r.Step(75);
    CHECK(r.world.InspectCrafting(job.unfinished)->completedWork == 75);
    CHECK(r.world.InspectCrafting(job.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(r.world.StationWorker(bench) == 0);
    r.SetEnergy(battery, "cell", 925); r.Step(1);
    CHECK(r.world.InspectCrafting(job.unfinished)->status == CraftingStatus::InsufficientEnergy);
    REQUIRE(Take(r.world.ResumeCrafting(r.actor, job.unfinished)).outcome == CraftingOutcome::Resumed);
    CHECK(r.Energy(battery, "cell") == 826);
}

TEST_CASE("Two station grids bill in persistent object order rather than trait order", "[crafting][power][workshop]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.cutter));
    const auto hammer = r.Bench("hammer_bench", {9, 9});
    const auto cut = r.Bench();
    const auto hammerBattery = r.Supply({{9, 9}, {10, 9}}, {10, 9}, 0);
    const auto cutBattery = r.Supply({{6, 9}, {7, 9}}, {6, 9}, 1);
    REQUIRE(r.world.GetObject(hammer)->id < r.world.GetObject(cut)->id);
    REQUIRE(r.world.Power().GridAt({9, 9}) != r.world.Power().GridAt({7, 9}));
    SECTION("cross-grid fractional balance follows object order") {
        REQUIRE(r.world.SetCraftingSettings({1, 1})); REQUIRE(r.world.SetActorSpeed(r.actor, 200));
        const auto job = r.Start("two"); REQUIRE(job.outcome == CraftingOutcome::Started);
        CHECK(r.Energy(hammerBattery, "cell") == 0); CHECK(r.Energy(cutBattery, "cell") == 0);
        CHECK(r.Job(job.unfinished).energyRemainder == 0);
        CHECK(r.world.InspectCrafting(job.unfinished)->stations.Size() == 2);
    }
    SECTION("shortage in either station grid debits neither") {
        r.SetEnergy(hammerBattery, "cell", 100); r.SetEnergy(cutBattery, "cell", 99);
        CHECK(r.Start("two").outcome == CraftingOutcome::InsufficientEnergy);
        CHECK(r.Energy(hammerBattery, "cell") == 100); CHECK(r.Energy(cutBattery, "cell") == 99);
        CHECK(r.world.StationWorker(hammer) == 0); CHECK(r.world.StationWorker(cut) == 0);
        r.SetEnergy(cutBattery, "cell", 100);
        REQUIRE(r.Start("two").outcome == CraftingOutcome::Started);
        CHECK(r.Energy(hammerBattery, "cell") == 0); CHECK(r.Energy(cutBattery, "cell") == 0);
    }
}

TEST_CASE("Powered station shortages are atomic with forge heat local energy and inputs", "[crafting][power][workshop]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.cutter));
    const auto bench = r.Bench();
    const auto forge = r.Bench("forge", {9, 9});
    const auto charcoal = r.Add("charcoal", ItemLocation::Carried(r.actor));
    REQUIRE(Take(r.world.UseForge(r.actor, forge, ForgeAction::LoadFuel, charcoal)).outcome == StationActionOutcome::FuelLoaded);
    REQUIRE(Take(r.world.UseForge(r.actor, forge, ForgeAction::Ignite)).outcome == StationActionOutcome::Ignited);
    const auto local = r.Add("local", ItemLocation::Carried(r.actor));
    const auto material = r.Add("material", ItemLocation::Carried(r.actor));
    const auto battery = r.Supply({{6, 9}, {7, 9}}, {6, 9}, 99);
    const auto fuel = r.world.DescribeObjectState(forge)->forge.fuel;
    const auto inventory = r.world.GetActor(r.actor)->inventory;
    CHECK(r.Start("hot").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(battery, "cell") == 99); CHECK(r.Energy(local, "power") == 1000);
    CHECK(r.world.DescribeObjectState(forge)->forge.fuel == fuel);
    CHECK(r.world.GetActor(r.actor)->inventory == inventory);
    CHECK(r.world.GetItem(material)->location == ItemLocation::Carried(r.actor));
    CHECK(r.world.StationWorker(bench) == 0); CHECK(r.world.StationWorker(forge) == 0);
    r.SetEnergy(battery, "cell", 100);
    REQUIRE(r.Start("hot").outcome == CraftingOutcome::Started);
    CHECK(r.Energy(battery, "cell") == 0); CHECK(r.Energy(local, "power") == 950);
    CHECK(r.world.DescribeObjectState(forge)->forge.fuel == fuel - 100);
    REQUIRE(r.world.CheckInvariants());
}
}
