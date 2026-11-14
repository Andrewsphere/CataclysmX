#include "core/world.h"
#include "core/simulation.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
template<class T> T Take(WorldResult<T> value) { REQUIRE(value); return eastl::move(*value); }
ContentCatalog CraftingContent() {
    const ContentSource source{"crafting-world.json5", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'tool_trait',id:'cut',name:'Cut'}, {type:'tool_trait',id:'hammer',name:'Hammer'},
      {type:'item',id:'material',name:'material',mass_g:10,volume_ml:20,capabilities:[
        {type:'energy_store',id:'power',capacity_energy:100,initial_energy:30,rechargeable:true,battery_class:'cell'}]},
      {type:'item',id:'product',name:'product',mass_g:5,volume_ml:10,capabilities:[
        {type:'energy_store',id:'power',capacity_energy:40,initial_energy:40,rechargeable:true,battery_class:'cell'}]},
      {type:'item',id:'tool',name:'tool',mass_g:10,volume_ml:10,
        tool_traits:[{id:'cut',tier:2},{id:'hammer',tier:1}],crafting_power:{store:'power',energy_per_work:10000},
        capabilities:[{type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'item',id:'bag',name:'bag',mass_g:1,volume_ml:1,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'item',id:'tiny_bag',name:'tiny bag',mass_g:1,volume_ml:1,capabilities:[{type:'container',id:'storage',capacity_volume_ml:19}]},
      {type:'item',id:'cut_only',name:'cutter',mass_g:1,volume_ml:1,tool_traits:[{id:'cut',tier:1}],
        crafting_power:{store:'power',energy_per_work:10000},capabilities:[{type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'item',id:'hammer_only',name:'hammer',mass_g:1,volume_ml:1,tool_traits:[{id:'hammer',tier:1}],
        crafting_power:{store:'power',energy_per_work:10000},capabilities:[{type:'energy_store',id:'power',capacity_energy:1000,initial_energy:50,rechargeable:true}]},
      {type:'item',id:'fraction_multi',name:'multitool',mass_g:1,volume_ml:1,tool_traits:[{id:'cut',tier:1},{id:'hammer',tier:1}],
        crafting_power:{store:'power',energy_per_work:5000},capabilities:[{type:'energy_store',id:'power',capacity_energy:10,initial_energy:10,rechargeable:true}]},
      {type:'item',id:'fraction_cut',name:'cutter',mass_g:1,volume_ml:1,tool_traits:[{id:'cut',tier:1}],
        crafting_power:{store:'power',energy_per_work:5000},capabilities:[{type:'energy_store',id:'power',capacity_energy:10,initial_energy:10,rechargeable:true}]},
      {type:'item',id:'fraction_hammer',name:'hammer',mass_g:1,volume_ml:1,tool_traits:[{id:'hammer',tier:1}],
        crafting_power:{store:'power',energy_per_work:5000},capabilities:[{type:'energy_store',id:'power',capacity_energy:10,initial_energy:10,rechargeable:true}]},
      {type:'item',id:'internal',name:'device',mass_g:1,volume_ml:1,capabilities:[
        {type:'energy_store',id:'power',capacity_energy:20,initial_energy:20,rechargeable:false},
        {type:'energy_store',id:'aux',capacity_energy:5,initial_energy:5,rechargeable:false}]},
      {type:'item',id:'other_cell',name:'other battery',mass_g:1,volume_ml:1,capabilities:[
        {type:'energy_store',id:'power',capacity_energy:40,initial_energy:40,rechargeable:true,battery_class:'other'}]},
      {type:'item',id:'recycle_target',name:'recycling target',mass_g:1,volume_ml:1,capabilities:[
        {type:'energy_store',id:'power',capacity_energy:100,initial_energy:100,rechargeable:true,battery_class:'cell'}]},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1},{item:'tool',count:1,retained:true}],
        tool_traits:[{id:'cut',tier:1},{id:'hammer',tier:1}],result:{item:'product',count:1},work:250},
      {type:'recipe',id:'short',inputs:[{item:'material',count:1}],tool_traits:[{id:'cut',tier:1}],result:{item:'product',count:1},work:3},
      {type:'recipe',id:'two_tools',inputs:[{item:'material',count:1}],tool_traits:[{id:'cut',tier:1},{id:'hammer',tier:1}],result:{item:'product',count:1},work:100},
      {type:'recipe',id:'consume_tool',inputs:[{item:'tool',count:1}],tool_traits:[{id:'cut',tier:1}],result:{item:'product',count:1},work:1},
      {type:'recipe',id:'retained_only',inputs:[{item:'tool',count:1,retained:true}],result:{item:'product',count:1},work:3},
      {type:'recipe',id:'retained_two',inputs:[{item:'tool',count:2,retained:true}],result:{item:'product',count:1},work:3},
      {type:'recipe',id:'fraction_retained',inputs:[{item:'fraction_multi',count:1,retained:true}],result:{item:'product',count:1},work:4},
      {type:'recipe',id:'fraction_overlap',inputs:[{item:'fraction_multi',count:1,retained:true}],tool_traits:[{id:'cut',tier:1},{id:'hammer',tier:1}],result:{item:'product',count:1},work:4},
      {type:'recipe',id:'fraction_traits',inputs:[],tool_traits:[{id:'cut',tier:1},{id:'hammer',tier:1}],result:{item:'product',count:1},work:4},
      {type:'recipe',id:'make_internal',inputs:[{item:'material',count:1}],result:{item:'internal',count:1},work:1},
      {type:'recipe',id:'mismatched_cell',inputs:[{item:'material',count:1}],result:{item:'other_cell',count:1},work:1},
      {type:'recipe',id:'disabled',crafting_enabled:false,inputs:[],result:{item:'product',count:1},work:1},
      {type:'recycling_recipe',id:'recycle',target:'product',inputs:[],returns:[{item:'material',count:1}],work:1},
      {type:'recycling_recipe',id:'recycle_internal',target:'internal',inputs:[],returns:[{item:'material',count:1}],work:1},
      {type:'recycling_recipe',id:'recycle_with_material',target:'recycle_target',inputs:[{item:'recycle_target',count:1}],returns:[{item:'material',count:1}],work:3}
    ]})"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content);
    return eastl::move(*content);
}
struct Rig {
    ContentCatalog content = CraftingContent();
    World world{content};
    LoadRequest request{};
    ActorHandle actor{};
    ItemHandle material{}, tool{};
    Rig() {
        request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 0; z < 8; ++z) { for (int x = 0; x < 8; ++x) { REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor"), 0})); } }
        actor = Take(world.CreateActor({2, 2}, true));
        material = Add("material", ItemLocation::Carried(actor)); tool = Add("tool", ItemLocation::Carried(actor));
    }
    ItemHandle Add(const char* name, ItemLocation location) { return Take(world.CreateItem(content.FindItem(name), location)); }
    ItemHandle Start(const char* recipe = "make") {
        const auto result = Take(world.StartCrafting(actor, content.FindCraftingRecipe(recipe)));
        REQUIRE(result.outcome == CraftingOutcome::Started); return result.unfinished;
    }
    std::uint32_t Energy(ItemHandle item) const {
        const auto cap = FindItemCapability(content.GetItem(world.GetItem(item)->type), "power");
        return world.GetItemEnergy(item, cap)->energy;
    }
    void Power(ItemHandle item, std::uint32_t amount) {
        REQUIRE(world.SetItemEnergy(item, FindItemCapability(content.GetItem(world.GetItem(item)->type), "power"), amount));
    }
    void Step(Tick ticks) { world.ElapseTicks(ticks); world.ProcessCraftingEvents(); }
};

TEST_CASE("Crafting reserves actual leaves and wrapper has dynamic dimensions", "[crafting][world]") {
    Rig r;
    const auto id = r.world.GetItem(r.material)->id;
    const auto wrapper = r.Start();
    CHECK(r.world.GetItemMass(wrapper) == 10); CHECK(r.world.GetItemVolume(wrapper) == 20);
    CHECK(r.world.IsReservedItem(r.material)); CHECK_FALSE(r.world.IsReservedItem(wrapper));
    CHECK_FALSE(r.world.CanAccessItem(r.actor, r.material)); CHECK(r.world.CanAccessItem(r.actor, r.tool));
    CHECK_FALSE(r.world.TransferItem(r.material, ItemLocation::Carried(r.actor)));
    CHECK_FALSE(r.world.CreateItem(r.content.FindItem("material"), ItemLocation::Inside(wrapper)));
    CHECK(r.Energy(r.tool) == 900); // One actual tool, two traits and an exact tool entry.
    CHECK(r.world.InspectCrafting(wrapper)->completedWork == 0);
    REQUIRE(r.world.TransferItem(wrapper, ItemLocation::Ground({3, 2})));
    CHECK(r.world.GetItem(r.material)->id == id);
    CHECK(r.world.GetItem(r.material)->location == ItemLocation::Inside(wrapper));
    REQUIRE(r.world.CheckInvariants());
    const auto cancelled = Take(r.world.CancelCrafting(r.actor, wrapper));
    CHECK(cancelled.outcome == CraftingOutcome::Cancelled);
    CHECK(r.world.GetItem(r.material)->id == id); CHECK_FALSE(r.world.IsReservedItem(r.material));
    CHECK(r.Energy(r.tool) == 900); CHECK_FALSE(r.world.GetItem(wrapper)); REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Crafting start refusals are atomic and ordinary gameplay outcomes", "[crafting][world]") {
    Rig r;
    r.Power(r.tool, 99);
    const auto state = r.world.CaptureItemState(r.tool);
    const auto location = r.world.GetItem(r.material)->location;
    const auto refused = Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("make")));
    CHECK(refused.outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.world.GetItem(r.material)->location == location); CHECK(r.world.CaptureItemState(r.tool) == state);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
    Vector<CraftingJobView> jobs; r.world.CollectCraftingJobs(r.actor, jobs); CHECK(jobs.empty());
    CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("disabled"))).outcome == CraftingOutcome::RecipeDisabled);
    CHECK_FALSE(r.world.SetCraftingSettings({9, 100})); CHECK_FALSE(r.world.SetCraftingSettings({1, 0}));
    CHECK_FALSE(r.world.SetCraftingSettings({1, 1001}));
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Crafting funded work commits before input and completion cannot duplicate outputs", "[crafting][world]") {
    Rig r;
    const auto wrapper = r.Start();
    r.Step(99); CHECK(r.world.InspectCrafting(wrapper)->completedWork == 0);
    r.Step(1); CHECK(r.world.InspectCrafting(wrapper)->completedWork == 100);
    CHECK(r.Energy(r.tool) == 800);
    std::uint32_t rng = 1;
    const auto result = AdvanceUntilInput(r.world, rng, 1000);
    CHECK(result.stop == SimulationStop::InputRequired); CHECK(r.world.GetTick() == 250);
    CHECK_FALSE(r.world.GetItem(wrapper)); CHECK_FALSE(r.world.GetItem(r.material));
    unsigned count = 0;
    for (auto item : r.world.GetActor(r.actor)->inventory) {
        if (r.world.GetItem(item)->type == r.content.FindItem("product")) { ++count; CHECK(r.Energy(item) == 30); }
    }
    CHECK(count == 1); CHECK_FALSE(r.world.ProcessCraftingEvents()); CHECK(r.Energy(r.tool) == 750);
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Final funded slice produces at the wrapper root rather than teleporting to its worker", "[crafting][world]") {
    Rig r;
    const auto job = r.Start("short"); r.Step(1);
    const TileCoord destination{7, 7};
    SECTION("wrapper dropped out of reach") {
        REQUIRE(r.world.TransferItem(job, ItemLocation::Ground(destination)));
    }
    SECTION("another actor carries a nested wrapper away") {
        const auto other = Take(r.world.CreateActor({3, 2}, true));
        const auto bag = r.Add("bag", ItemLocation::Carried(other));
        REQUIRE(r.world.TransferItem(job, ItemLocation::Inside(bag)));
        REQUIRE(r.world.MoveActor(other, destination));
    }
    CHECK(r.world.InspectCrafting(job)->completedWork == 0);
    CHECK(r.world.InspectCrafting(job)->fundedTicksRemaining == 2);
    r.Step(2);
    CHECK_FALSE(r.world.GetItem(job)); CHECK_FALSE(r.world.GetItem(r.material));
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0); CHECK(r.Energy(r.tool) == 997);
    unsigned products = 0;
    for (auto item : r.world.ItemsAt(destination)) {
        if (r.world.GetItem(item)->type != r.content.FindItem("product")) { continue; }
        ++products; CHECK(r.Energy(item) == 30);
        CHECK(r.world.GetItem(item)->location == ItemLocation::Ground(destination));
    }
    CHECK(products == 1);
    for (auto item : r.world.GetActor(r.actor)->inventory) {
        CHECK(r.world.GetItem(item)->type != r.content.FindItem("product"));
    }
    CHECK_FALSE(r.world.ProcessCraftingEvents()); CHECK(r.world.CountItemsAt(destination) == 1);
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Pause finishes funded slice and loss of tool does not undo payment", "[crafting][world]") {
    Rig r;
    const auto wrapper = r.Start();
    r.Step(30);
    REQUIRE(r.world.TransferItem(r.tool, ItemLocation::Ground({7, 7})));
    CHECK(Take(r.world.PauseCrafting(r.actor, wrapper)).outcome == CraftingOutcome::PauseRequested);
    r.Step(70);
    CHECK(r.world.InspectCrafting(wrapper)->completedWork == 100);
    CHECK(r.world.InspectCrafting(wrapper)->status == CraftingStatus::Paused);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
    CHECK(Take(r.world.ResumeCrafting(r.actor, wrapper)).outcome == CraftingOutcome::MissingTools);
    REQUIRE(r.world.TransferItem(r.tool, ItemLocation::Carried(r.actor)));
    r.Power(r.tool, 99);
    CHECK(Take(r.world.ResumeCrafting(r.actor, wrapper)).outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(r.tool) == 99);
    r.Power(r.tool, 150);
    CHECK(Take(r.world.ResumeCrafting(r.actor, wrapper)).outcome == CraftingOutcome::Resumed);
    r.Step(100); r.Step(50);
    CHECK_FALSE(r.world.GetItem(wrapper)); CHECK(r.Energy(r.tool) == 0);
}
TEST_CASE("Crafting uses nearby accessible inventory trees and respects walls and actor ownership", "[crafting][world]") {
    Rig r;
    const auto bag = r.Add("bag", ItemLocation::Ground({3, 2}));
    REQUIRE(r.world.TransferItem(r.material, ItemLocation::Inside(bag)));
    const auto other = Take(r.world.CreateActor({2, 3}, true));
    REQUIRE(r.world.TransferItem(r.tool, ItemLocation::Carried(other)));
    CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("make"))).outcome == CraftingOutcome::MissingTools);
    REQUIRE(r.world.TransferItem(r.tool, ItemLocation::Carried(r.actor)));
    REQUIRE(r.world.SetCraftingSettings({2, 100}));
    REQUIRE(r.world.TransferItem(bag, ItemLocation::Ground({4, 2})));
    for (int z = 0; z <= 4; ++z) { REQUIRE(r.world.SetTile({3, z}, {r.content.FindTerrain("wall"), 0})); }
    CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("make"))).outcome == CraftingOutcome::MissingInputs);
    REQUIRE(r.world.SetTile({3, 2}, {r.content.FindTerrain("floor"), 0}));
    const auto wrapper = r.Start(); CHECK(r.world.IsReservedItem(r.material));
    CHECK(r.world.GetItem(bag)->contents.empty());
    CHECK(Take(r.world.CancelCrafting(r.actor, wrapper)).outcome == CraftingOutcome::Cancelled);
}
TEST_CASE("Crafting freezes parked and resident workers without catch-up", "[crafting][world]") {
    Rig r;
    const auto wrapper = r.Start(); r.Step(30);
    REQUIRE(r.world.SetActorParked(r.actor, true)); r.Step(1000);
    CHECK(r.world.InspectCrafting(wrapper)->fundedTicksRemaining == 70);
    REQUIRE(r.world.SetActorParked(r.actor, false));
    REQUIRE(r.world.UpdateRequest(r.request, {{0, 0}, 0}, LoadMode::resident)); REQUIRE(r.world.ReconcileChunks());
    r.Step(1000); CHECK(r.world.InspectCrafting(wrapper)->fundedTicksRemaining == 70);
    REQUIRE(r.world.UpdateRequest(r.request, {{0, 0}, 0}, LoadMode::active)); REQUIRE(r.world.ReconcileChunks());
    r.Step(69); CHECK(r.world.InspectCrafting(wrapper)->completedWork == 0);
    r.Step(1); CHECK(r.world.InspectCrafting(wrapper)->completedWork == 100);
}
TEST_CASE("Ineligible wrappers release active crafting workers without losing their paid slice", "[crafting][world][crafting-unavailable]") {
    Rig r;
    const auto job = r.Start(); r.Step(30);
    REQUIRE(Take(r.world.PauseCrafting(r.actor, job)).outcome == CraftingOutcome::PauseRequested);
    SECTION("wrapper moves to a resident inactive chunk") {
        Take(r.world.RequestChunks({{1, 0}, 0}, LoadMode::resident)); REQUIRE(r.world.ReconcileChunks());
        const TileCoord destination{chunk_size, 2};
        REQUIRE(r.world.SetTile(destination, {r.content.FindTerrain("floor"), 0}));
        REQUIRE(r.world.TransferItem(job, ItemLocation::Ground(destination)));
        CHECK_FALSE(r.world.IsActive(destination));
    }
    SECTION("wrapper is carried by another parked actor") {
        const auto other = Take(r.world.CreateActor({3, 2}, true));
        REQUIRE(r.world.TransferItem(job, ItemLocation::Carried(other)));
        REQUIRE(r.world.SetActorParked(other, true));
    }
    std::uint32_t rng = 1;
    const auto result = AdvanceUntilInput(r.world, rng, r.world.GetTick() + 100);
    CHECK(result.stop == SimulationStop::InputRequired);
    REQUIRE(result.readyPlayers.size() == 1); CHECK(result.readyPlayers[0] == r.actor);
    CHECK(r.world.GetTick() == 30);
    const auto state = r.world.InspectCrafting(job); REQUIRE(state);
    CHECK(state->status == CraftingStatus::WorkerUnavailable); CHECK(state->worker == 0); CHECK_FALSE(state->pauseRequested);
    CHECK(state->completedWork == 0); CHECK(state->fundedTicksRemaining == 70);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0); CHECK(r.Energy(r.tool) == 900);
    REQUIRE(r.world.CheckInvariants());
    REQUIRE(r.world.ScheduleActorAction(r.actor, 1)); REQUIRE(r.world.MoveActor(r.actor, {2, 3})); r.Step(1);
    CHECK(r.world.InspectCrafting(job)->fundedTicksRemaining == 70);
    REQUIRE(r.world.TransferItem(job, ItemLocation::Carried(r.actor)));
    CHECK(Take(r.world.ResumeCrafting(r.actor, job)).outcome == CraftingOutcome::Resumed);
    CHECK(r.Energy(r.tool) == 900); CHECK(r.world.GetActorRemainingTicks(r.actor) == 70);
    Take(r.world.PauseCrafting(r.actor, job)); r.Step(70);
    CHECK(r.world.InspectCrafting(job)->completedWork == 100); CHECK(r.Energy(r.tool) == 900);
    CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::Paused); REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Reattached conflicting crafting claims clear pause without replacing the actor action", "[crafting][world][crafting-unavailable]") {
    Rig r;
    const auto request = Take(r.world.RequestChunks({{1, 0}, 0}, LoadMode::active)); REQUIRE(r.world.ReconcileChunks());
    const TileCoord position{chunk_size, 2}; REQUIRE(r.world.SetTile(position, {r.content.FindTerrain("floor"), 0}));
    const auto job = r.Start(); const auto id = r.world.GetItem(job)->id; r.Step(30);
    Take(r.world.PauseCrafting(r.actor, job)); REQUIRE(r.world.TransferItem(job, ItemLocation::Ground(position)));
    REQUIRE(r.world.ReleaseRequest(request)); REQUIRE(r.world.ReconcileChunks());
    REQUIRE(r.world.ScheduleActorAction(r.actor, 3));
    Take(r.world.RequestChunks({{1, 0}, 0}, LoadMode::active)); REQUIRE(r.world.ReconcileChunks());
    const auto restored = r.world.FindItem(id); REQUIRE(restored.value);
    const auto state = r.world.InspectCrafting(restored); REQUIRE(state);
    CHECK(state->status == CraftingStatus::WorkerUnavailable); CHECK(state->worker == 0); CHECK_FALSE(state->pauseRequested);
    CHECK(state->fundedTicksRemaining == 70); CHECK(r.world.GetActorRemainingTicks(r.actor) == 3);
    CHECK(r.Energy(r.tool) == 900); REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Short fractional slices preserve speed and energy accounting", "[crafting][world]") {
    Rig r;
    REQUIRE(r.world.SetCraftingSettings({1, 1})); REQUIRE(r.world.SetActorSpeed(r.actor, 150));
    const auto wrapper = r.Start("short");
    for (int n = 0; n < 4; ++n) { r.Step(1); REQUIRE(r.world.GetItem(wrapper)); }
    r.Step(1); CHECK_FALSE(r.world.GetItem(wrapper)); CHECK(r.Energy(r.tool) == 997);
    Rig fast;
    REQUIRE(fast.world.SetActorSpeed(fast.actor, 50));
    const auto quick = fast.Start("short"); CHECK(fast.world.InspectCrafting(quick)->fundedTicksRemaining == 2);
    fast.Step(2); CHECK_FALSE(fast.world.GetItem(quick)); CHECK(fast.Energy(fast.tool) == 997);
}
TEST_CASE("Recycling transfers actual target energy and never initializes free output power", "[crafting][world]") {
    Rig r;
    const auto product = r.Add("product", ItemLocation::Carried(r.actor)); r.Power(product, 7);
    const auto result = Take(r.world.StartRecycling(r.actor, product, r.content.FindRecyclingRecipe("recycle")));
    REQUIRE(result.outcome == CraftingOutcome::Started); CHECK(r.world.IsReservedItem(product));
    r.Step(1); CHECK_FALSE(r.world.GetItem(product)); CHECK_FALSE(r.world.GetItem(result.unfinished));
    unsigned outputs = 0;
    for (auto item : r.world.GetActor(r.actor)->inventory) {
        if (item != r.material && r.world.GetItem(item)->type == r.content.FindItem("material")) { ++outputs; CHECK(r.Energy(item) == 7); }
    }
    CHECK(outputs == 1); REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Shared crafting tools pay in persistent job order", "[crafting][world]") {
    Rig r;
    const auto other = Take(r.world.CreateActor({3, 2}, true));
    r.Add("material", ItemLocation::Carried(other));
    REQUIRE(r.world.TransferItem(r.tool, ItemLocation::Ground({2, 2})));
    r.Power(r.tool, 300);
    const auto first = r.Start();
    const auto second = Take(r.world.StartCrafting(other, r.content.FindCraftingRecipe("make")));
    REQUIRE(second.outcome == CraftingOutcome::Started); CHECK(r.Energy(r.tool) == 100);
    r.Step(100);
    CHECK(r.world.InspectCrafting(first)->status == CraftingStatus::Working);
    CHECK(r.world.InspectCrafting(second.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(r.world.InspectCrafting(second.unfinished)->completedWork == 100); CHECK(r.Energy(r.tool) == 0);
}
TEST_CASE("Recycling recovers energy only from the actual target and cancellation preserves both originals", "[crafting][world]") {
    Rig r;
    // The charged consumable has the same type and a lower persistent ID than
    // the target, so neither type matching nor first-child ordering identifies it.
    const auto extra = r.Add("recycle_target", ItemLocation::Carried(r.actor)); r.Power(extra, 90);
    const auto target = r.Add("recycle_target", ItemLocation::Carried(r.actor));
    const auto targetId = r.world.GetItem(target)->id, extraId = r.world.GetItem(extra)->id;
    std::uint32_t targetEnergy = 0;
    SECTION("empty target cannot recover additional material energy") { targetEnergy = 0; }
    SECTION("charged target bounds recovery despite charged additional material") { targetEnergy = 7; }
    SECTION("cancellation releases the two original objects with their original energy") {
        r.Power(target, 7);
        const auto started = Take(r.world.StartRecycling(r.actor, target, r.content.FindRecyclingRecipe("recycle_with_material")));
        REQUIRE(started.outcome == CraftingOutcome::Started); r.Step(1);
        CHECK(Take(r.world.CancelCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Cancelled);
        REQUIRE(r.world.FindItem(targetId) == target); REQUIRE(r.world.FindItem(extraId) == extra);
        CHECK(r.Energy(target) == 7); CHECK(r.Energy(extra) == 90);
        CHECK(r.world.GetItem(target)->location == ItemLocation::Carried(r.actor));
        CHECK(r.world.GetItem(extra)->location == ItemLocation::Carried(r.actor));
        CHECK_FALSE(r.world.IsReservedItem(target)); CHECK_FALSE(r.world.IsReservedItem(extra));
        REQUIRE(r.world.CheckInvariants()); return;
    }
    r.Power(target, targetEnergy);
    const auto started = Take(r.world.StartRecycling(r.actor, target, r.content.FindRecyclingRecipe("recycle_with_material")));
    REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.world.IsReservedItem(target)); CHECK(r.world.IsReservedItem(extra));
    const auto state = r.world.CaptureItemState(started.unfinished);
    CHECK(*state.Find("crafting_job")->GetUnsigned("target") == targetId);
    REQUIRE(r.world.CheckInvariants()); r.Step(3);
    CHECK_FALSE(r.world.GetItem(target)); CHECK_FALSE(r.world.GetItem(extra)); CHECK_FALSE(r.world.GetItem(started.unfinished));
    unsigned recovered = 0;
    for (auto item : r.world.GetActor(r.actor)->inventory) {
        if (item == r.material || r.world.GetItem(item)->type != r.content.FindItem("material")) { continue; }
        ++recovered; CHECK(r.Energy(item) == targetEnergy);
    }
    CHECK(recovered == 1); REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Stale crafting workers preserve funded work for explicit reassignment", "[crafting][world]") {
    Rig r;
    const auto wrapper = r.Start(); r.Step(20);
    REQUIRE(r.world.DestroyActor(r.actor));
    r.world.ProcessCraftingEvents();
    const auto next = Take(r.world.CreateActor({2, 2}, true));
    CHECK(r.world.InspectCrafting(wrapper)->status == CraftingStatus::WorkerUnavailable);
    CHECK(Take(r.world.ResumeCrafting(next, wrapper)).outcome == CraftingOutcome::Resumed);
    CHECK(r.world.InspectCrafting(wrapper)->fundedTicksRemaining == 80);
    r.Step(80); CHECK(r.world.InspectCrafting(wrapper)->completedWork == 100);
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("All distinct crafting tool charges preflight before any payment", "[crafting][world]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.tool));
    const auto cutter = r.Add("cut_only", ItemLocation::Carried(r.actor));
    const auto hammer = r.Add("hammer_only", ItemLocation::Carried(r.actor));
    CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("two_tools"))).outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.Energy(cutter) == 1000); CHECK(r.Energy(hammer) == 50); CHECK_FALSE(r.world.IsReservedItem(r.material));
    r.Power(hammer, 100);
    const auto wrapper = r.Start("two_tools"); CHECK(r.Energy(cutter) == 900); CHECK(r.Energy(hammer) == 0);
    r.Step(100); CHECK_FALSE(r.world.GetItem(wrapper));
}
TEST_CASE("Crafting consumed tools and trait tools are disjoint", "[crafting][world]") {
    Rig r;
    CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("consume_tool"))).outcome == CraftingOutcome::MissingTools);
    const auto spare = r.Add("tool", ItemLocation::Carried(r.actor));
    const auto wrapper = r.Start("consume_tool");
    CHECK_FALSE(r.world.IsReservedItem(r.tool)); CHECK(r.world.IsReservedItem(spare));
    CHECK(r.Energy(r.tool) == 999); CHECK(r.Energy(spare) == 1000);
    CHECK(Take(r.world.CancelCrafting(r.actor, wrapper)).outcome == CraftingOutcome::Cancelled);
}
TEST_CASE("Wrapper external volume participates in parent capacity and cancellation ground fallback", "[crafting][world]") {
    Rig r;
    const auto wrapper = r.Start();
    const auto tiny = r.Add("tiny_bag", ItemLocation::Ground({2, 2}));
    CHECK_FALSE(r.world.TransferItem(wrapper, ItemLocation::Inside(tiny)));
    const auto bag = r.Add("bag", ItemLocation::Ground({2, 2}));
    REQUIRE(r.world.TransferItem(wrapper, ItemLocation::Inside(bag)));
    CHECK(r.world.GetContainerUsedVolume(bag) == 20);
    REQUIRE(r.world.SetActorCarryCapacity(r.actor, 10));
    CHECK(Take(r.world.CancelCrafting(r.actor, wrapper)).outcome == CraftingOutcome::Cancelled);
    CHECK(r.world.GetItem(r.material)->location == ItemLocation::Ground({2, 2}));
    CHECK(r.world.GetContainerUsedVolume(bag) == 0); REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Wrapper destruction discards pending work and releases its worker", "[crafting][world]") {
    Rig r; const auto wrapper = r.Start();
    REQUIRE(r.world.DestroyItem(wrapper));
    CHECK_FALSE(r.world.GetItem(r.material)); CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
    CHECK_FALSE(r.world.ProcessCraftingEvents()); CHECK(r.Energy(r.tool) == 900);
    REQUIRE(r.world.ScheduleActorAction(r.actor)); REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Powered retained-only tools are billed and preflight includes every retained count", "[crafting][world]") {
    Rig r;
    SECTION("one powered exact tool without traits") {
        r.Power(r.tool, 2);
        CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("retained_only"))).outcome == CraftingOutcome::InsufficientEnergy);
        CHECK(r.Energy(r.tool) == 2); CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
        r.Power(r.tool, 3);
        const auto job = r.Start("retained_only"); CHECK(r.Energy(r.tool) == 0);
        r.Step(3); CHECK_FALSE(r.world.GetItem(job)); REQUIRE(r.world.GetItem(r.tool));
    }
    SECTION("two instances of the same retained entry") {
        const auto second = r.Add("tool", ItemLocation::Carried(r.actor));
        r.Power(r.tool, 3); r.Power(second, 2);
        CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("retained_two"))).outcome == CraftingOutcome::InsufficientEnergy);
        CHECK(r.Energy(r.tool) == 3); CHECK(r.Energy(second) == 2);
        r.Power(second, 3);
        const auto job = r.Start("retained_two"); CHECK(r.Energy(r.tool) == 0); CHECK(r.Energy(second) == 0);
        r.Step(3); CHECK_FALSE(r.world.GetItem(job)); REQUIRE(r.world.GetItem(r.tool)); REQUIRE(r.world.GetItem(second));
    }
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Fractional exact and multi-trait overlap is charged once per actual tool", "[crafting][world]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.tool)); REQUIRE(r.world.SetCraftingSettings({1, 1}));
    const auto tool = r.Add("fraction_multi", ItemLocation::Carried(r.actor));
    const auto job = r.Start("fraction_overlap"); CHECK(r.Energy(tool) == 10);
    r.Step(1); CHECK(r.Energy(tool) == 9);
    r.Step(1); CHECK(r.Energy(tool) == 9);
    r.Step(1); CHECK(r.Energy(tool) == 8);
    r.Step(1); CHECK_FALSE(r.world.GetItem(job)); REQUIRE(r.world.GetItem(tool));
}
TEST_CASE("Fractional energy survives multitool split and merge without requirement slot remapping", "[crafting][world]") {
    Rig r; REQUIRE(r.world.DestroyItem(r.tool)); REQUIRE(r.world.SetCraftingSettings({1, 1}));
    const auto multi = r.Add("fraction_multi", ItemLocation::Carried(r.actor));
    const auto job = r.Start("fraction_traits");
    Take(r.world.PauseCrafting(r.actor, job)); r.Step(1); CHECK(r.Energy(multi) == 10);
    REQUIRE(r.world.TransferItem(multi, ItemLocation::Ground({7, 7})));
    const auto cutter = r.Add("fraction_cut", ItemLocation::Carried(r.actor));
    const auto hammer = r.Add("fraction_hammer", ItemLocation::Carried(r.actor));
    CHECK(Take(r.world.ResumeCrafting(r.actor, job)).outcome == CraftingOutcome::Resumed);
    // Previous .5 + cutter .5 bills one; hammer contributes another .5.
    CHECK(r.Energy(cutter) == 9); CHECK(r.Energy(hammer) == 10);
    Take(r.world.PauseCrafting(r.actor, job)); r.Step(1);
    REQUIRE(r.world.TransferItem(cutter, ItemLocation::Ground({7, 7})));
    REQUIRE(r.world.TransferItem(hammer, ItemLocation::Ground({7, 7})));
    REQUIRE(r.world.TransferItem(multi, ItemLocation::Carried(r.actor)));
    CHECK(Take(r.world.ResumeCrafting(r.actor, job)).outcome == CraftingOutcome::Resumed);
    CHECK(r.Energy(multi) == 9); // The former second trait's .5 is not orphaned.
    r.Step(1); r.Step(1); CHECK_FALSE(r.world.GetItem(job));
    CHECK(r.Energy(multi) + r.Energy(cutter) + r.Energy(hammer) == 28);
}
TEST_CASE("Shared retained tools preserve each job fractional debt when later funding fails", "[crafting][world]") {
    Rig r; REQUIRE(r.world.SetCraftingSettings({1, 1}));
    const auto tool = r.Add("fraction_multi", ItemLocation::Ground({2, 2})); r.Power(tool, 1);
    const auto other = Take(r.world.CreateActor({3, 2}, true));
    const auto first = r.Start("fraction_retained");
    const auto second = Take(r.world.StartCrafting(other, r.content.FindCraftingRecipe("fraction_retained")));
    REQUIRE(second.outcome == CraftingOutcome::Started); CHECK(r.Energy(tool) == 1);
    r.Step(1);
    CHECK(r.Energy(tool) == 0); CHECK(r.world.InspectCrafting(first)->status == CraftingStatus::Working);
    CHECK(r.world.InspectCrafting(second.unfinished)->status == CraftingStatus::InsufficientEnergy);
    r.Power(tool, 1);
    CHECK(Take(r.world.ResumeCrafting(other, second.unfinished)).outcome == CraftingOutcome::Resumed);
    CHECK(r.Energy(tool) == 0); // Refused funding did not reset or advance that job's .5.
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Crafting transfer bridges battery and internal stores but rejects distinct named classes", "[crafting][world]") {
    Rig r;
    SECTION("battery into multiple internal stores is capacity bounded") {
        r.Power(r.material, 60); const auto job = r.Start("make_internal"); r.Step(1); CHECK_FALSE(r.world.GetItem(job));
        unsigned products = 0;
        for (auto item : r.world.GetActor(r.actor)->inventory) {
            if (r.world.GetItem(item)->type != r.content.FindItem("internal")) { continue; }
            ++products; CHECK(r.Energy(item) == 20);
            const auto aux = FindItemCapability(r.content.GetItem(r.world.GetItem(item)->type), "aux");
            CHECK(r.world.GetItemEnergy(item, aux)->energy == 5);
        }
        CHECK(products == 1);
    }
    SECTION("empty battery produces zero energy despite output defaults") {
        r.Power(r.material, 0); r.Start("make_internal"); r.Step(1);
        unsigned products = 0;
        for (auto item : r.world.GetActor(r.actor)->inventory) {
            if (r.world.GetItem(item)->type != r.content.FindItem("internal")) { continue; }
            ++products;
            for (const auto& store : r.content.GetItem(r.world.GetItem(item)->type).energyStores) {
                CHECK(r.world.GetItemEnergy(item, store.capability)->energy == 0);
            }
        }
        CHECK(products == 1);
    }
    SECTION("internal stores back to a battery transfer available energy only") {
        const auto device = r.Add("internal", ItemLocation::Carried(r.actor)); r.Power(device, 7);
        const auto aux = FindItemCapability(r.content.GetItem(r.world.GetItem(device)->type), "aux");
        REQUIRE(r.world.SetItemEnergy(device, aux, 3));
        const auto result = Take(r.world.StartRecycling(r.actor, device, r.content.FindRecyclingRecipe("recycle_internal")));
        REQUIRE(result.outcome == CraftingOutcome::Started); r.Step(1);
        unsigned products = 0;
        for (auto item : r.world.GetActor(r.actor)->inventory) {
            if (item == r.material || r.world.GetItem(item)->type != r.content.FindItem("material")) { continue; }
            ++products; CHECK(r.Energy(item) == 10);
        }
        CHECK(products == 1);
    }
    SECTION("two explicitly different named classes do not transfer") {
        r.Start("mismatched_cell"); r.Step(1);
        unsigned products = 0;
        for (auto item : r.world.GetActor(r.actor)->inventory) {
            if (r.world.GetItem(item)->type != r.content.FindItem("other_cell")) { continue; }
            ++products; CHECK(r.Energy(item) == 0);
        }
        CHECK(products == 1);
    }
    REQUIRE(r.world.CheckInvariants());
}
}
