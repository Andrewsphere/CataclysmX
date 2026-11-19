#include "core/world.h"
#include "core/save_database.h"
#include "crafting/crafting_state.h"
#include "power/power_system.h"
#include "client/power_view.h"
#include "network/wire.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace {
using namespace cataclysmx;
namespace wire = cataclysmx::network::wire;
template<class T, class E> T Take(eastl::expected<T, E> value) {
    REQUIRE(value); return eastl::move(*value);
}

String ForgeSource() {
    return R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'tool_trait',id:'forge_heat',name:'Forge heat'},
      {type:'tool_trait',id:'cut',name:'Cut'},
      {type:'item',id:'kit',name:'Forge kit',mass_g:1,volume_ml:1,placed_object:'forge'},
      {type:'item',id:'charcoal',name:'Charcoal',mass_g:1,volume_ml:1},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:1},
      {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:1},
      {type:'item',id:'local',name:'Local tool',mass_g:1,volume_ml:1,
       crafting_power:{store:'power',energy_per_work:5000},
       capabilities:[{type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'item',id:'cutter',name:'Grid cutter',mass_g:1,volume_ml:1,
       tool_traits:[{id:'cut',tier:1}],crafting_power:{store:'grid',energy_per_work:10000}},
      {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,placed_object:'battery_box',
       capabilities:[{type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:1000,rechargeable:true,battery_class:'cell'}]},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1}],
       tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000,result:{item:'product',count:1},work:250},
      {type:'recipe',id:'fraction',inputs:[{item:'material',count:1}],
       tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:5000,result:{item:'product',count:1},work:4},
      {type:'recipe',id:'conserve',inputs:[{item:'material',count:1}],
       tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:3333,result:{item:'product',count:1},work:250},
      {type:'recipe',id:'drain',inputs:[{item:'material',count:1}],
       tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:500000,result:{item:'product',count:1},work:250},
      {type:'recipe',id:'mixed',inputs:[{item:'material',count:1},{item:'local',count:1,retained:true}],
       tool_traits:[{id:'forge_heat',tier:1},{id:'cut',tier:1}],heat_per_work:10000,result:{item:'product',count:1},work:250},
      {type:'recycling_recipe',id:'recover',target:'product',inputs:[],
       tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000,returns:[{item:'material',count:1}],work:250}
    ],objects:[
      {id:'forge',name:'Forge',behavior:'forge',packing_item:'kit',movable:true,
       tool_traits:[{id:'forge_heat',tier:1}],
       forge:{fuel_capacity:24000,ignition_cost:1000,fuels:[{item:'charcoal',heat:6000}]}},
      {id:'battery_box',name:'Battery box',behavior:'grid_battery'}
    ]})";
}
void Replace(String& text, eastl::string_view from, eastl::string_view to) {
    const auto index = text.find(from.data(), 0, from.size()); REQUIRE(index != String::npos);
    text.replace(index, from.size(), to.data(), to.size());
}
ContentCatalog ForgeContent(const String& text = ForgeSource()) {
    const ContentSource source{"forge-test", {text.data(), text.size()}};
    Diagnostics diagnostics; auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded); return eastl::move(*loaded);
}
struct Rig {
    ContentCatalog content = ForgeContent();
    World world{content};
    LoadRequest request{};
    ActorHandle actor{};
    ItemHandle material{};
    Rig() {
        request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 0; z < 8; ++z) for (int x = 0; x < 8; ++x) {
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor")}));
        }
        actor = Take(world.CreateActor({2, 2}, true));
        material = Add("material");
    }
    ItemHandle Add(const char* id) {
        return Take(world.CreateItem(content.FindItem(id), ItemLocation::Carried(actor)));
    }
    ObjectHandle Forge(TileCoord position = {3, 2}) {
        return Take(world.CreateObject(content.FindObject("forge"), position));
    }
    ForgeState State(ObjectHandle forge) const {
        const auto state = world.DescribeObjectState(forge); REQUIRE(state); return state->forge;
    }
    StationActionResult Use(ObjectHandle forge, ForgeAction action, ItemHandle fuel = {}) {
        return Take(world.UseForge(actor, forge, action, fuel));
    }
    void Ready(ObjectHandle forge) {
        REQUIRE(Use(forge, ForgeAction::LoadFuel, Add("charcoal")).outcome == StationActionOutcome::FuelLoaded);
        REQUIRE(Use(forge, ForgeAction::Ignite).outcome == StationActionOutcome::Ignited);
    }
    CraftingResult Start(const char* recipe = "make") {
        return Take(world.StartCrafting(actor, content.FindCraftingRecipe(recipe)));
    }
    CraftingJobState Job(ItemHandle job) const {
        const auto state = world.CaptureItemState(job);
        const auto* payload = state.Find("crafting_job"); REQUIRE(payload);
        return Take(ReadCraftingState(*payload));
    }
    void Step(Tick elapsed) { world.ElapseTicks(elapsed); world.ProcessCraftingEvents(); }
    void Energy(ItemHandle item, const char* store, std::uint32_t amount) {
        REQUIRE(world.SetItemEnergy(item, FindItemCapability(content.GetItem(world.GetItem(item)->type), store), amount));
    }
    std::uint32_t Energy(ItemHandle item, const char* store) const {
        return world.GetItemEnergy(item, FindItemCapability(content.GetItem(world.GetItem(item)->type), store))->energy;
    }
};
struct SaveFile {
    std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("cataclysmx-forge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string path;
    SaveFile() { REQUIRE(std::filesystem::create_directory(directory)); path = (directory / "world.sqlite").string(); }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

TEST_CASE("Forge content rejects invalid heat providers fuel lists and recipe rates", "[forge][content]") {
    auto text = ForgeSource();
    SECTION("empty fuels") { Replace(text, "fuels:[{item:'charcoal',heat:6000}]", "fuels:[]"); }
    SECTION("duplicate fuel") { Replace(text, "fuels:[{item:'charcoal',heat:6000}]",
        "fuels:[{item:'charcoal',heat:6000},{item:'charcoal',heat:5000}]"); }
    SECTION("unknown fuel") { Replace(text, "item:'charcoal',heat:6000", "item:'missing',heat:6000"); }
    SECTION("internal fuel") { Replace(text, "item:'charcoal',heat:6000", "item:'unfinished_craft',heat:6000"); }
    SECTION("backing item fuel") { Replace(text, "item:'charcoal',heat:6000", "item:'kit',heat:6000"); }
    SECTION("zero fuel yield") { Replace(text, "heat:6000", "heat:0"); }
    SECTION("oversized whole charge") { Replace(text, "heat:6000", "heat:24001"); }
    SECTION("ignition consumes capacity") { Replace(text, "ignition_cost:1000", "ignition_cost:24000"); }
    SECTION("nonforge provider") { Replace(text, "behavior:'forge'", "behavior:'none'"); }
    SECTION("item provider") { Replace(text, "id:'charcoal',name:'Charcoal'",
        "id:'charcoal',name:'Charcoal',tool_traits:[{id:'forge_heat',tier:1}]"); }
    SECTION("missing recipe heat") { Replace(text, "heat_per_work:10000,result", "result"); }
    SECTION("zero recipe heat") { Replace(text, "heat_per_work:10000,result", "heat_per_work:0,result"); }
    SECTION("heat without trait") { Replace(text, "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:5000",
        "tool_traits:[],heat_per_work:5000"); }
    SECTION("recycler requires heat") { Replace(text, "heat_per_work:10000,returns", "heat_per_work:0,returns"); }
    SECTION("recycler heat requires trait") { Replace(text,
        "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000,returns",
        "tool_traits:[],heat_per_work:10000,returns"); }
    const ContentSource source{"invalid-forge", {text.data(), text.size()}};
    Diagnostics diagnostics; CHECK_FALSE(LoadContent({&source, 1}, &diagnostics)); CHECK_FALSE(diagnostics.Empty());
}

TEST_CASE("Forge configuration and independent recipe heat affect content identity", "[forge][content][network]") {
    const auto original = ForgeContent(); auto text = ForgeSource();
    SECTION("capacity") { Replace(text, "fuel_capacity:24000", "fuel_capacity:24001"); }
    SECTION("ignition") { Replace(text, "ignition_cost:1000", "ignition_cost:999"); }
    SECTION("fuel yield") { Replace(text, "heat:6000", "heat:6001"); }
    SECTION("craft heat") { Replace(text, "heat_per_work:5000", "heat_per_work:5001"); }
    SECTION("recycling heat") { Replace(text, "heat_per_work:10000,returns", "heat_per_work:10001,returns"); }
    const auto changed = ForgeContent(text);
    CHECK(network::ContentFingerprint(original) != network::ContentFingerprint(changed));
}

TEST_CASE("Forge canonical expansion retains fuel settings and explicit recycler heat", "[forge][content]") {
    const auto content = ForgeContent();
    const auto expansion = WriteCombatContent(content);
    const auto* objects = expansion.Find("objects"); REQUIRE(objects);
    bool found = false;
    for (std::size_t i = 0; i < objects->Size(); ++i) {
        const auto& entry = objects->At(i);
        if (Take(entry.GetString("id")) != "forge") { continue; }
        const auto* forge = entry.Find("forge"); REQUIRE(forge);
        CHECK(Take(forge->GetUnsigned("fuel_capacity")) == 24000);
        CHECK(Take(forge->GetUnsigned("ignition_cost")) == 1000);
        const auto* fuels = forge->Find("fuels"); REQUIRE(fuels); REQUIRE(fuels->Size() == 1);
        CHECK(Take(fuels->At(0).GetString("item")) == "charcoal");
        CHECK(Take(fuels->At(0).GetUnsigned("heat")) == 6000); found = true;
    }
    CHECK(found);
    const auto* recyclers = expansion.Find("recycling_recipes"); REQUIRE(recyclers); REQUIRE(recyclers->Size() == 1);
    CHECK(Take(recyclers->At(0).GetUnsigned("heat_per_work")) == 10000);
    CHECK(content.GetRecyclingRecipe(content.FindRecyclingRecipe("recover")).heatPerWork == 10000);
}

TEST_CASE("Forge whole fuel transactions retain partial charges and return unscheduled costs", "[forge][world]") {
    Rig r; const auto forge = r.Forge();
    CHECK(r.Use(forge, ForgeAction::Ignite).outcome == StationActionOutcome::InsufficientFuel);
    CHECK(r.Use(forge, ForgeAction::Extinguish).outcome == StationActionOutcome::AlreadyCold);
    CHECK(r.Use(forge, ForgeAction::LoadFuel, r.material).outcome == StationActionOutcome::InvalidFuel);
    REQUIRE(r.world.GetItem(r.material));
    for (int i = 0; i < 4; ++i) {
        const auto fuel = r.Add("charcoal");
        CHECK(r.world.QueryForgeAction(r.actor, forge, ForgeAction::LoadFuel, fuel) == StationActionOutcome::FuelLoaded);
        REQUIRE(r.world.GetItem(fuel)); CHECK(r.State(forge).fuel == static_cast<std::uint64_t>(i) * 6000);
        const auto loaded = r.Use(forge, ForgeAction::LoadFuel, fuel);
        CHECK(loaded.outcome == StationActionOutcome::FuelLoaded); CHECK(loaded.cost == 100);
        CHECK_FALSE(r.world.GetItem(fuel)); CHECK_FALSE(r.State(forge).ready);
    }
    const auto excess = r.Add("charcoal");
    const auto refused = r.Use(forge, ForgeAction::LoadFuel, excess);
    CHECK(refused.outcome == StationActionOutcome::NoCapacity); CHECK(refused.cost == 0);
    CHECK(r.State(forge).fuel == 24000); REQUIRE(r.world.GetItem(excess));
    CHECK(r.world.QueryPackStation(r.actor, forge) == StationActionOutcome::HasFuel);
    const auto ignited = r.Use(forge, ForgeAction::Ignite);
    CHECK(ignited.outcome == StationActionOutcome::Ignited); CHECK(ignited.cost == 400);
    CHECK(r.State(forge).fuel == 23000); CHECK(r.State(forge).ready);
    CHECK(r.Use(forge, ForgeAction::Ignite).outcome == StationActionOutcome::AlreadyReady);
    CHECK(r.Use(forge, ForgeAction::DiscardFuel).outcome == StationActionOutcome::MustExtinguish);
    CHECK(r.world.QueryPackStation(r.actor, forge) == StationActionOutcome::MustExtinguish);
    const auto extinguished = r.Use(forge, ForgeAction::Extinguish);
    CHECK(extinguished.outcome == StationActionOutcome::Extinguished); CHECK(extinguished.cost == 100);
    CHECK(r.State(forge).fuel == 23000); CHECK_FALSE(r.State(forge).ready);
    REQUIRE(r.Use(forge, ForgeAction::Ignite).outcome == StationActionOutcome::Ignited);
    CHECK(r.State(forge).fuel == 22000);
    REQUIRE(r.Use(forge, ForgeAction::Extinguish).outcome == StationActionOutcome::Extinguished);
    const auto discarded = r.Use(forge, ForgeAction::DiscardFuel);
    CHECK(discarded.outcome == StationActionOutcome::FuelDiscarded); CHECK(discarded.cost == 100);
    CHECK(r.State(forge).fuel == 0); CHECK(r.world.QueryPackStation(r.actor, forge) == StationActionOutcome::Packed);
    const auto equipment = r.world.GetObject(forge)->equipment;
    CHECK(Take(r.world.PackStation(r.actor, forge)).outcome == StationActionOutcome::Packed);
    CHECK_FALSE(r.world.GetObject(forge)); CHECK(r.world.GetItem(equipment)->location == ItemLocation::Carried(r.actor));
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0); CHECK(r.world.GetTick() == 0);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Forge action refusals preserve fuel and selected items", "[forge][world]") {
    Rig r; const auto forge = r.Forge(); r.Ready(forge); const auto fuel = r.Add("charcoal");
    auto expected = StationActionOutcome::Disabled;
    SECTION("disabled") { REQUIRE(r.world.SetObjectEnabled(forge, false)); }
    SECTION("broken") {
        REQUIRE(r.world.SetObjectAppearance(forge, ObjectKind::Generic, ObjectFacing::North, true));
        expected = StationActionOutcome::Broken;
    }
    SECTION("out of reach") { REQUIRE(r.world.MoveObject(forge, {7, 7})); expected = StationActionOutcome::OutOfReach; }
    const auto before = r.State(forge);
    CHECK(r.world.QueryForgeAction(r.actor, forge, ForgeAction::LoadFuel, fuel) == expected);
    const auto refused = r.Use(forge, ForgeAction::LoadFuel, fuel);
    CHECK(refused.outcome == expected); CHECK(refused.cost == 0);
    REQUIRE(r.world.GetItem(fuel)); CHECK(r.State(forge) == before);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0); REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Forge ignition requires strictly more than its ignition cost", "[forge][world]") {
    auto text = ForgeSource(); Replace(text, "heat:6000", "heat:1000");
    const auto content = ForgeContent(text); World world(content);
    Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor")}));
    REQUIRE(world.SetTile({3, 2}, {content.FindTerrain("floor")}));
    const auto actor = Take(world.CreateActor({2, 2}, true));
    const auto forge = Take(world.CreateObject(content.FindObject("forge"), {3, 2}));
    const auto fuel = Take(world.CreateItem(content.FindItem("charcoal"), ItemLocation::Carried(actor)));
    REQUIRE(Take(world.UseForge(actor, forge, ForgeAction::LoadFuel, fuel)).outcome == StationActionOutcome::FuelLoaded);
    CHECK(Take(world.UseForge(actor, forge, ForgeAction::Ignite)).outcome == StationActionOutcome::InsufficientFuel);
    CHECK(world.DescribeObjectState(forge)->forge.fuel == 1000);
    CHECK_FALSE(world.DescribeObjectState(forge)->forge.ready);
}

TEST_CASE("Forge idle time and inactivity preserve fuel without residency", "[forge][save]") {
    Rig r; const auto forge = r.Forge(); r.Ready(forge);
    const auto id = r.world.GetObject(forge)->id;
    r.Step(10000); CHECK(r.State(forge).fuel == 5000); CHECK(r.State(forge).ready);
    REQUIRE(r.world.ReleaseRequest(r.request)); REQUIRE(r.world.ReconcileChunks());
    CHECK_FALSE(r.world.FindObject(id).value);
    r.Step(10000);
    r.request = Take(r.world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(r.world.ReconcileChunks());
    CHECK(r.State(r.world.FindObject(id)).fuel == 5000); CHECK(r.State(r.world.FindObject(id)).ready);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Forge heat conserves integer work across speeds and elapsed partitions", "[forge][crafting]") {
    for (const auto speed : {100U, 200U}) for (const Tick partition : {Tick{1}, Tick{5}, Tick{25}}) {
        CAPTURE(speed, partition);
        Rig r; const auto forge = r.Forge(); r.Ready(forge);
        REQUIRE(r.world.SetActorSpeed(r.actor, speed)); REQUIRE(r.world.SetCraftingSettings({1, 200}));
        const auto started = r.Start("conserve"); REQUIRE(started.outcome == CraftingOutcome::Started);
        CHECK(r.world.InspectCrafting(started.unfinished)->fundedTicksRemaining <= 100);
        for (int i = 0; i < 600 && r.world.GetItem(started.unfinished); ++i) { r.Step(partition); }
        CHECK_FALSE(r.world.GetItem(started.unfinished));
        CHECK(r.State(forge).fuel == 5000 - (250 * 3333 / 10000));
        CHECK(r.world.StationWorker(forge) == 0); REQUIRE(r.world.CheckInvariants());
    }
}

TEST_CASE("Forge fractional heat debt follows the job when changing providers", "[forge][crafting][save]") {
    Rig r; const auto first = r.Forge(); r.Ready(first);
    REQUIRE(r.world.SetCraftingSettings({1, 1}));
    const auto started = r.Start("fraction"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.State(first).fuel == 5000); CHECK(r.Job(started.unfinished).heatRemainder == 50000000);
    CHECK(r.Job(started.unfinished).energyRemainder == 0);
    Take(r.world.PauseCrafting(r.actor, started.unfinished)); r.Step(1);
    CHECK(r.world.StationWorker(first) == 0);
    REQUIRE(r.Use(first, ForgeAction::Extinguish).outcome == StationActionOutcome::Extinguished);
    const auto second = r.Forge({2, 3}); r.Ready(second);
    CHECK(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Resumed);
    CHECK(r.State(second).fuel == 4999); CHECK(r.Job(started.unfinished).heatRemainder == 0);
    CHECK(r.State(first).fuel == 5000);
    Take(r.world.PauseCrafting(r.actor, started.unfinished)); r.Step(1);
    CHECK(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Resumed);
    CHECK(r.Job(started.unfinished).heatRemainder == 50000000);
    Take(r.world.PauseCrafting(r.actor, started.unfinished)); r.Step(1);
    CHECK(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Resumed);
    r.Step(1); CHECK_FALSE(r.world.GetItem(started.unfinished));
    CHECK(r.State(second).fuel == 4998); REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Fractionally free forge work still requires a ready positive fuel provider", "[forge][crafting]") {
    Rig r; const auto forge = r.Forge(); REQUIRE(r.world.SetCraftingSettings({1, 1}));
    CHECK(r.Start("fraction").outcome == CraftingOutcome::ColdForge);
    REQUIRE(r.Use(forge, ForgeAction::LoadFuel, r.Add("charcoal")).outcome == StationActionOutcome::FuelLoaded);
    CHECK(r.Start("fraction").outcome == CraftingOutcome::ColdForge);
    CHECK(r.world.GetItem(r.material)->location == ItemLocation::Carried(r.actor));
    REQUIRE(r.Use(forge, ForgeAction::Ignite).outcome == StationActionOutcome::Ignited);
    CHECK(r.Start("fraction").outcome == CraftingOutcome::Started);
    CHECK(r.State(forge).fuel == 5000); REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Combined forge local and grid preflight is atomic on every resource shortage", "[forge][crafting][power]") {
    Rig r; const auto forge = r.Forge(); r.Ready(forge);
    const auto local = r.Add("local"); r.Add("cutter");
    Take(r.world.Power().ExtendBounds({{2, 2}, {2, 3}}));
    const auto box = Take(r.world.CreateObject(r.content.FindObject("battery_box"), {2, 3}));
    const auto battery = Take(r.world.CreateItem(r.content.FindItem("battery"), ItemLocation::Stored(box)));
    r.Energy(battery, "cell", 1000); r.world.Power().SyncObject(box);
    auto expected = CraftingOutcome::InsufficientEnergy;
    SECTION("grid shortage") { r.Energy(battery, "cell", 99); }
    SECTION("local shortage") { r.Energy(local, "power", 49); }
    SECTION("heat shortage") {
        auto& recipe = const_cast<CraftingRecipeDefinition&>(
            r.content.GetCraftingRecipe(r.content.FindCraftingRecipe("mixed")));
        recipe.heatPerWork = 600000; expected = CraftingOutcome::InsufficientFuel;
    }
    const auto inventory = r.world.GetActor(r.actor)->inventory;
    const auto before = r.State(forge); const auto localBefore = r.Energy(local, "power");
    const auto gridBefore = r.Energy(battery, "cell");
    CHECK(r.Start("mixed").outcome == expected);
    CHECK(r.State(forge) == before); CHECK(r.Energy(local, "power") == localBefore);
    CHECK(r.Energy(battery, "cell") == gridBefore);
    CHECK(r.world.GetActor(r.actor)->inventory == inventory);
    CHECK_FALSE(r.world.IsReservedItem(r.material)); CHECK(r.world.StationWorker(forge) == 0);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0); REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Mixed forge slices respect the grid interval boundary and release failed claims", "[forge][crafting][power]") {
    Rig r; const auto forge = r.Forge(); r.Ready(forge);
    const auto local = r.Add("local"); r.Add("cutter");
    Take(r.world.Power().ExtendBounds({{2, 2}, {2, 3}}));
    const auto box = Take(r.world.CreateObject(r.content.FindObject("battery_box"), {2, 3}));
    const auto battery = Take(r.world.CreateItem(r.content.FindItem("battery"), ItemLocation::Stored(box)));
    r.Energy(battery, "cell", 75); r.world.Power().SyncObject(box);
    REQUIRE(r.world.SetCraftingSettings({1, 200})); r.Step(25);
    const auto started = r.Start("mixed"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.world.InspectCrafting(started.unfinished)->fundedTicksRemaining == 75);
    CHECK(r.State(forge).fuel == 4925); CHECK(r.Energy(battery, "cell") == 0);
    const auto localPaid = r.Energy(local, "power");
    r.Step(75);
    CHECK(r.world.InspectCrafting(started.unfinished)->completedWork == 75);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(r.world.StationWorker(forge) == 0); CHECK(r.State(forge).fuel == 4925);
    CHECK(r.Energy(local, "power") == localPaid); REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Forge exhaustion preserves paid work and cancellation refunds originals not heat", "[forge][crafting]") {
    Rig r; const auto forge = r.Forge(); r.Ready(forge);
    const auto materialId = r.world.GetItem(r.material)->id;
    const auto original = r.world.CaptureItemState(r.material);
    const auto started = r.Start("drain"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.State(forge).fuel == 0); CHECK_FALSE(r.State(forge).ready);
    r.Step(100);
    CHECK(r.world.InspectCrafting(started.unfinished)->completedWork == 100);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::ColdForge);
    CHECK(r.world.StationWorker(forge) == 0);
    REQUIRE(r.Use(forge, ForgeAction::LoadFuel, r.Add("charcoal")).outcome == StationActionOutcome::FuelLoaded);
    CHECK(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::ColdForge);
    REQUIRE(r.Use(forge, ForgeAction::Ignite).outcome == StationActionOutcome::Ignited);
    REQUIRE(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Resumed);
    Take(r.world.PauseCrafting(r.actor, started.unfinished)); r.Step(100);
    CHECK(r.world.InspectCrafting(started.unfinished)->completedWork == 200);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::Paused);
    CHECK(r.world.StationWorker(forge) == 0);
    CHECK(Take(r.world.CancelCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Cancelled);
    CHECK(r.world.GetItem(r.material)->id == materialId);
    CHECK(r.world.CaptureItemState(r.material) == original); CHECK_FALSE(r.world.IsReservedItem(r.material));
    CHECK(r.world.GetItem(r.material)->location == ItemLocation::Carried(r.actor));
    CHECK(r.State(forge).fuel == 0); REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Forge readiness and source loss cannot revoke already funded work", "[forge][crafting]") {
    Rig r; const auto forge = r.Forge(); r.Ready(forge);
    const auto started = r.Start(); REQUIRE(started.outcome == CraftingOutcome::Started); r.Step(30);
    auto expected = CraftingStatus::MissingTools;
    SECTION("extinguish") {
        const auto other = Take(r.world.CreateActor({4, 2}, true));
        REQUIRE(Take(r.world.UseForge(other, forge, ForgeAction::Extinguish)).outcome == StationActionOutcome::Extinguished);
        expected = CraftingStatus::ColdForge;
    }
    SECTION("disable") { REQUIRE(r.world.SetObjectEnabled(forge, false)); }
    SECTION("break") { REQUIRE(r.world.SetObjectAppearance(forge, ObjectKind::Generic, ObjectFacing::North, true)); }
    SECTION("destroy") { REQUIRE(r.world.DestroyObject(forge)); }
    SECTION("move away") { REQUIRE(r.world.MoveObject(forge, {7, 7})); }
    CHECK(r.world.InspectCrafting(started.unfinished)->fundedTicksRemaining == 70);
    r.Step(70);
    CHECK(r.world.InspectCrafting(started.unfinished)->completedWork == 100);
    CHECK(r.world.InspectCrafting(started.unfinished)->status == expected);
    CHECK(r.world.StationWorker(forge) == 0); REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Forge disk and unload retain fuel ready state paid work and fractional debt", "[forge][save]") {
    SaveFile file; Rig r; const auto forge = r.Forge(); r.Ready(forge);
    REQUIRE(r.world.SetCraftingSettings({1, 1}));
    const auto started = r.Start("fraction"); REQUIRE(started.outcome == CraftingOutcome::Started);
    const auto forgeId = r.world.GetObject(forge)->id, jobId = r.world.GetItem(started.unfinished)->id;
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    World restored(r.content); Take(restored.LoadSave(file.path.c_str()));
    auto request = Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    auto check = [&] {
        const auto object = restored.DescribeObjectState(restored.FindObject(forgeId)); REQUIRE(object);
        CHECK(object->forge.fuel == 5000); CHECK(object->forge.ready);
        const auto payload = restored.CaptureItemState(restored.FindItem(jobId));
        const auto* job = payload.Find("crafting_job"); REQUIRE(job);
        const auto state = Take(ReadCraftingState(*job));
        CHECK(state.heatRemainder == 50000000); CHECK(state.energyRemainder == 0);
        CHECK(state.remaining == 1); CHECK(state.pendingWork == 10000);
    };
    check();
    REQUIRE(restored.ReleaseRequest(request)); REQUIRE(restored.ReconcileChunks());
    restored.ElapseTicks(10000);
    request = Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    check(); CHECK(restored.StationWorker(restored.FindObject(forgeId)) == 0);
    REQUIRE(restored.CheckInvariants());
}

TEST_CASE("Forge state rejects impossible fuel and ready combinations", "[forge][save]") {
    Rig r; const auto forge = r.Forge(); r.Ready(forge);
    auto state = r.world.CaptureObjectState(forge);
    SECTION("over capacity") { state.SetUnsigned("fuel", 24001); }
    SECTION("ready without fuel") { state.SetUnsigned("fuel", 0); state.SetBoolean("ready", true); }
    SECTION("negative fuel") { state.SetSigned("fuel", -1); }
    SECTION("wrong ready type") { state.SetUnsigned("ready", 1); }
    CHECK_FALSE(ValidateObjectBehaviorState(r.content.GetObject(r.content.FindObject("forge")), state));
}

TEST_CASE("Saved exhausted forge work transfers its paid remainder to a new worker without rebilling", "[forge][crafting][save]") {
    SaveFile file; Rig r; const auto forge = r.Forge(); r.Ready(forge);
    const auto started = r.Start("drain"); REQUIRE(started.outcome == CraftingOutcome::Started);
    r.Step(30);
    const auto forgeId = r.world.GetObject(forge)->id, jobId = r.world.GetItem(started.unfinished)->id;
    const auto actorId = r.world.GetActor(r.actor)->id;
    CHECK(r.State(forge).fuel == 0); CHECK_FALSE(r.State(forge).ready);
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    World restored(r.content); Take(restored.LoadSave(file.path.c_str()));
    Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    const auto restoredForge = restored.FindObject(forgeId);
    const auto job = restored.FindItem(jobId);
    REQUIRE(restoredForge.value); REQUIRE(job.value);
    CHECK(restored.DescribeObjectState(restoredForge)->forge.fuel == 0);
    CHECK_FALSE(restored.DescribeObjectState(restoredForge)->forge.ready);
    REQUIRE(restored.InspectCrafting(job)); CHECK(restored.InspectCrafting(job)->fundedTicksRemaining == 70);
    REQUIRE(restored.DestroyActor(restored.FindActor(actorId)));
    restored.ProcessCraftingEvents();
    CHECK(restored.InspectCrafting(job)->status == CraftingStatus::WorkerUnavailable);
    const auto other = Take(restored.CreateActor({4, 2}, true));
    REQUIRE(restored.TransferItem(job, ItemLocation::Carried(other)));
    CHECK(restored.StationWorker(restoredForge) == 0);
    REQUIRE(Take(restored.ResumeCrafting(other, job)).outcome == CraftingOutcome::Resumed);
    CHECK(restored.InspectCrafting(job)->fundedTicksRemaining == 70);
    CHECK(restored.DescribeObjectState(restoredForge)->forge.fuel == 0);
    restored.ElapseTicks(69); restored.ProcessCraftingEvents();
    CHECK(restored.InspectCrafting(job)->completedWork == 0);
    restored.ElapseTicks(1); restored.ProcessCraftingEvents();
    CHECK(restored.InspectCrafting(job)->completedWork == 100);
    CHECK(restored.InspectCrafting(job)->status == CraftingStatus::ColdForge);
    CHECK(restored.DescribeObjectState(restoredForge)->forge.fuel == 0);
    CHECK(restored.StationWorker(restoredForge) == 0); REQUIRE(restored.CheckInvariants());
}

TEST_CASE("Crafting payload four preserves independent heat debt and migrates older jobs", "[forge][save]") {
    CraftingJobState state; state.recipeName = "make"; state.totalWork = 250;
    state.energyRemainder = 123; state.heatRemainder = craftingHeatDenominator - 1;
    const auto payload = WriteCraftingState(state);
    CHECK(Take(payload.GetUnsigned("version")) == 4);
    const auto restored = Take(ReadCraftingState(payload));
    CHECK(restored.heatRemainder == state.heatRemainder); CHECK(restored.energyRemainder == 123);
    SECTION("legacy version three") {
        DataObject legacy;
        for (std::size_t i = 0; i < payload.Size(); ++i) {
            if (payload.Name(i) != "heat_remainder") { legacy.Set(payload.Name(i), payload.At(i)); }
        }
        legacy.SetUnsigned("version", 3);
        const auto migrated = Take(ReadCraftingState(legacy));
        CHECK(migrated.heatRemainder == 0); CHECK(migrated.energyRemainder == 123);
    }
    SECTION("remainder denominator rejected") {
        auto bad = payload; bad.SetUnsigned("heat_remainder", craftingHeatDenominator);
        CHECK_FALSE(ReadCraftingState(bad));
    }
    SECTION("negative remainder rejected") {
        auto bad = payload; bad.SetSigned("heat_remainder", -1); CHECK_FALSE(ReadCraftingState(bad));
    }
}

TEST_CASE("Forge recycling uses the same heat funding and original target refunds", "[forge][crafting]") {
    Rig r; const auto forge = r.Forge(); const auto target = r.Add("product");
    const auto id = r.world.GetItem(target)->id;
    const auto original = r.world.CaptureItemState(target);
    const auto recipe = r.content.FindRecyclingRecipe("recover");
    CHECK(Take(r.world.StartRecycling(r.actor, target, recipe)).outcome == CraftingOutcome::ColdForge);
    CHECK(r.world.GetItem(target)->location == ItemLocation::Carried(r.actor));
    r.Ready(forge);
    const auto started = Take(r.world.StartRecycling(r.actor, target, recipe));
    REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.State(forge).fuel == 4900); CHECK(r.world.IsReservedItem(target));
    Take(r.world.PauseCrafting(r.actor, started.unfinished)); r.Step(100);
    CHECK(r.world.InspectCrafting(started.unfinished)->completedWork == 100);
    CHECK(r.world.StationWorker(forge) == 0);
    CHECK(Take(r.world.CancelCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Cancelled);
    CHECK(r.world.GetItem(target)->id == id); CHECK(r.world.CaptureItemState(target) == original);
    CHECK_FALSE(r.world.IsReservedItem(target)); CHECK(r.State(forge).fuel == 4900);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Forge shared controls server actions and LAN state round trip", "[forge][network][ui]") {
    // Extend the real forge fixture with only the demo's required bootstrap
    // assets. All station traits and fuel references resolve through the loader.
    auto text = ForgeSource();
    Replace(text, "definitions:[", R"(definitions:[
      {type:'terrain',id:'closed_door',name:'Closed door',movement:{passable:false},
       blocks_sight:true,appearance:{symbol:'+',foreground:'white'}},
      {type:'item',id:'rock',name:'Rock',mass_g:1,volume_ml:1},
      {type:'item',id:'bandage',name:'Bandage',mass_g:1,volume_ml:1},
      {type:'item',id:'backpack',name:'Backpack',mass_g:1,volume_ml:1,
       capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
    )");
    const auto content = ForgeContent(text); const auto type = content.FindObject("forge");
    const auto heat = content.FindToolTrait("forge_heat"); REQUIRE(heat.value != 0);
    REQUIRE(content.GetObject(type).toolTraits.Size() == 1);
    CHECK(content.GetObject(type).toolTraits[0].trait == heat);
    REQUIRE(ValidateServerContent(content)); Server server(content);
    auto& world = server.GetWorld(); const auto actor = server.GetPlayer();
    if (const auto enemy = world.ActorAt({5, 5}); enemy.value) REQUIRE(world.DestroyActor(enemy));
    const auto position = world.GetActor(actor)->position;
    const TileCoord destination{position.x + 1, position.z, position.y};
    if (const auto existing = world.ObjectAt(destination); existing.value) REQUIRE(world.DestroyObject(existing));
    REQUIRE(world.SetTile(destination, {content.FindTerrain("floor")}));
    const auto forge = Take(world.CreateObject(type, destination));
    const auto fuel = Take(world.CreateItem(content.FindItem("charcoal"), ItemLocation::Carried(actor)));
    auto view = server.View(1); PowerWorkshop workshop; workshop.Open(view);
    workshop.Handle({"Right"}, view, content);
    const auto load = workshop.Handle({"l"}, view, content); REQUIRE(load);
    CHECK(load->kind == CommandKind::load_forge); CHECK(load->target == world.GetObject(forge)->id);
    CHECK(load->item == world.GetItem(fuel)->id);
    REQUIRE(world.FindItem(load->item).value); REQUIRE(world.GetItem(fuel));
    wire::Writer commandWriter; REQUIRE(commandWriter.CommandValue(*load));
    Command decoded; wire::Reader commandReader{{commandWriter.bytes.data(), commandWriter.bytes.size()}};
    REQUIRE(commandReader.CommandValue(decoded)); REQUIRE(commandReader.Done());
    CHECK(decoded.kind == load->kind); CHECK(decoded.item == load->item); CHECK(decoded.target == load->target);
    auto invalid = decoded; invalid.otherItem = 17; CHECK_FALSE(ValidCommandShape(invalid));
    invalid = decoded; invalid.target = 0; CHECK_FALSE(ValidCommandShape(invalid));
    invalid = decoded; invalid.position = destination; CHECK_FALSE(ValidCommandShape(invalid));
    const auto loaded = server.Send(decoded); REQUIRE(loaded.outcome == Outcome::station_changed);
    CHECK(loaded.stationOutcome == StationActionOutcome::FuelLoaded); CHECK(loaded.cost == 100); CHECK(loaded.advanced);
    CHECK_FALSE(world.FindItem(load->item).value);
    view = server.View(1);
    const auto ignite = workshop.Handle({"i"}, view, content); REQUIRE(ignite);
    CHECK(ignite->kind == CommandKind::ignite_forge); CHECK(ignite->item == 0);
    invalid = *ignite; invalid.item = world.GetItem(fuel) ? world.GetItem(fuel)->id : 17;
    CHECK_FALSE(ValidCommandShape(invalid));
    wire::Writer invalidCommand; CHECK_FALSE(invalidCommand.CommandValue(invalid));
    CHECK(server.Send(invalid).outcome == Outcome::invalid_command);
    CHECK(world.DescribeObjectState(forge)->forge.fuel == 6000);
    for (const auto kind : {CommandKind::ignite_forge, CommandKind::extinguish_forge, CommandKind::discard_forge_fuel}) {
        Command command; command.kind = kind; command.target = ignite->target;
        wire::Writer writer; REQUIRE(writer.CommandValue(command));
        Command copy; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        REQUIRE(reader.CommandValue(copy)); REQUIRE(reader.Done());
        CHECK(copy.kind == kind); CHECK(copy.target == command.target); CHECK(copy.item == 0);
    }
    const auto ignited = server.Send(*ignite);
    REQUIRE(ignited.outcome == Outcome::station_changed); CHECK(ignited.stationOutcome == StationActionOutcome::Ignited);
    CHECK(ignited.cost == 400); CHECK(world.DescribeObjectState(forge)->forge.fuel == 5000);
    PlayerCommandResult result{server.GetHostPlayerId(), 1, 1, ignited};
    wire::Writer resultWriter; REQUIRE(resultWriter.Result(result));
    PlayerCommandResult resultCopy; wire::Reader resultReader{{resultWriter.bytes.data(), resultWriter.bytes.size()}};
    REQUIRE(resultReader.Result(resultCopy)); REQUIRE(resultReader.Done());
    CHECK(resultCopy.result.stationOutcome == StationActionOutcome::Ignited); CHECK(resultCopy.result.cost == 400);
    view = server.View(1); REQUIRE(view.stations.size() == 1);
    CHECK(view.stations[0].forge.fuel == 5000); CHECK(view.stations[0].forge.ready);
    wire::Writer viewWriter; REQUIRE(viewWriter.View(view));
    WorldView copy; wire::Reader viewReader{{viewWriter.bytes.data(), viewWriter.bytes.size()}};
    REQUIRE(viewReader.View(copy)); REQUIRE(viewReader.Done()); CHECK(copy.stations == view.stations);
    REQUIRE(wire::ValidItemDefinitions(copy, content));
    auto forged = copy; forged.stations[0].forge.fuel = 24001;
    CHECK_FALSE(wire::ValidItemDefinitions(forged, content));
    forged = copy; forged.stations[0].forge.fuel = 0;
    wire::Writer impossible; CHECK_FALSE(impossible.View(forged));
    Vector<String> lines; workshop.Describe(copy, content, lines);
    bool shown = false; for (const auto& line : lines) shown |= line.find("5000/24000") != String::npos;
    CHECK(shown);
    const auto extinguish = workshop.Handle({"x"}, copy, content); REQUIRE(extinguish);
    CHECK(extinguish->kind == CommandKind::extinguish_forge);
    CHECK(server.Send(*extinguish).stationOutcome == StationActionOutcome::Extinguished);
    const auto discard = workshop.Handle({"c"}, server.View(1), content); REQUIRE(discard);
    CHECK(discard->kind == CommandKind::discard_forge_fuel);
    CHECK(server.Send(*discard).stationOutcome == StationActionOutcome::FuelDiscarded);
    CHECK(world.DescribeObjectState(forge)->forge.fuel == 0); REQUIRE(world.CheckInvariants());
}
} // namespace
