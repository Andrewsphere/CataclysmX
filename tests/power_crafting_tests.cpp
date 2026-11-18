#include "core/world.h"
#include "power/power_system.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) {
    REQUIRE(value); return eastl::move(*value);
}
ContentCatalog Content() {
    const ContentSource source{"power-crafting", R"({format_version:1,objects:[
      {id:'battery_box',name:'Battery box',behavior:'grid_battery'}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'tool_trait',id:'cut',name:'Cut'}, {type:'tool_trait',id:'hammering',name:'Hammer'},
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
}
