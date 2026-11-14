#include "core/server.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentCatalog ServerCraftingContent() {
    const ContentSource source{"crafting-server.json5", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'closed_door',name:'door',movement:{passable:false},blocks_sight:true,appearance:{symbol:'+',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'item',id:'rock',name:'rock',mass_g:10,volume_ml:20},
      {type:'item',id:'bandage',name:'bandage',mass_g:2,volume_ml:5},
      {type:'item',id:'backpack',name:'backpack',mass_g:10,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'recipe',id:'make',inputs:[{item:'rock',count:1}],result:{item:'bandage',count:1},work:250},
      {type:'recipe',id:'missing',inputs:[{item:'rock',count:255}],result:{item:'bandage',count:1},work:250},
      {type:'recycling_recipe',id:'recycle',target:'bandage',inputs:[],returns:[{item:'rock',count:1}],work:150}
    ]})"};
    auto content = LoadContent({&source, 1}); REQUIRE(content);
    return eastl::move(*content);
}
struct Rig {
    ContentCatalog content = ServerCraftingContent();
    Server server{content};
    ItemHandle material{};
    Rig() {
        auto pacing = server.GetPacing(); pacing.rateLimited = true; pacing.ticksPerSecond = 100;
        REQUIRE(server.SetPacing(pacing));
        for (auto item : server.GetWorld().ItemsAt({1, 0})) { REQUIRE(material.value == 0); material = item; }
        REQUIRE(material.value != 0);
        REQUIRE(server.GetWorld().TransferItem(material, ItemLocation::Carried(server.GetPlayer())));
    }
    CommandResult Start() {
        Command command; command.kind = CommandKind::craft; command.recipe = content.FindCraftingRecipe("make");
        return server.Send(command);
    }
    Command Control(CommandKind kind, PersistentId id) { Command c; c.kind = kind; c.item = id; return c; }
};

TEST_CASE("Server starts recipes with a single funded deadline and completes them", "[crafting][server]") {
    Rig r;
    const auto result = r.Start();
    CHECK(result.outcome == Outcome::craft_started); CHECK(result.craftingOutcome == CraftingOutcome::Started);
    CHECK(result.advanced); CHECK(result.cost == 100); CHECK(result.tick == 0);
    auto view = r.server.View(0); REQUIRE(view.craftingJobs.size() == 1);
    const auto job = view.craftingJobs.front(); CHECK(job.fundedTicksRemaining == 100);
    const auto materialId = r.server.GetWorld().GetItem(r.material)->id;
    bool wrapperFound = false;
    for (const auto& entry : view.inventory) {
        CHECK(entry.id != materialId);
        if (entry.id == job.id) {
            wrapperFound = true; CHECK(entry.contained_items == 0); CHECK(entry.capacityVolumeMl == 0);
            CHECK(entry.usedVolumeMl == 0); CHECK(entry.volumeMl == 20); CHECK(entry.massGrams == 10);
        }
    }
    CHECK(wrapperFound);
    r.server.Update(2500000);
    CHECK(r.server.GetTick() == 250); CHECK(r.server.View(0).craftingJobs.empty());
    CHECK_FALSE(r.server.GetWorld().GetItem(r.material));
    unsigned outputs = 0;
    for (auto item : r.server.GetWorld().GetActor(r.server.GetPlayer())->inventory) {
        if (r.server.GetWorld().GetItem(item)->type == r.content.FindItem("bandage")) ++outputs;
    }
    CHECK(outputs == 1); REQUIRE(r.server.GetWorld().CheckInvariants());
}

TEST_CASE("Busy crafting interrupts consume input immediately and clear stale intent", "[crafting][server]") {
    Rig r; REQUIRE(r.Start().outcome == Outcome::craft_started);
    const auto id = r.server.View(0).craftingJobs.front().id;
    r.server.Update(300000); CHECK(r.server.GetTick() == 30);
    const auto player = r.server.GetHostPlayerId();
    auto context = r.server.GetInputContext(player);
    REQUIRE(r.server.Submit(player, {context.lastInputSequence + 1, context.opportunity, {}}).outcome == Outcome::queued);
    context = r.server.GetInputContext(player);
    const auto command = r.Control(CommandKind::pause_craft, id);
    const PlayerInput interrupt{context.lastInputSequence + 1, context.opportunity, command};
    const auto paused = r.server.Submit(player, interrupt);
    CHECK(paused.outcome == Outcome::craft_paused); CHECK(paused.craftingOutcome == CraftingOutcome::PauseRequested);
    CHECK_FALSE(paused.advanced); CHECK(paused.cost == 0); CHECK(paused.tick == 30);
    auto view = r.server.View(0); CHECK_FALSE(view.commandQueued);
    CHECK(view.opportunity == context.opportunity + 1); CHECK(view.lastInputSequence == interrupt.sequence);
    CHECK(view.craftingJobs.front().pauseRequested);
    CHECK(r.server.Submit(player, interrupt).outcome == Outcome::stale_input);
    CHECK(r.server.Submit(player, {interrupt.sequence, view.opportunity, command}).outcome == Outcome::duplicate_input);
    r.server.Update(700000); view = r.server.View(0);
    CHECK(view.tick == 100); CHECK(view.remainingTicks == 0);
    CHECK(view.craftingJobs.front().completedWork == 100); CHECK(view.craftingJobs.front().status == CraftingStatus::Paused);
    const auto resumed = r.server.Send(r.Control(CommandKind::resume_craft, id));
    REQUIRE(resumed.outcome == Outcome::craft_resumed); CHECK(resumed.advanced); CHECK(resumed.cost == 100);
    r.server.Update(200000); const auto tick = r.server.GetTick();
    const auto cancelled = r.server.Send(r.Control(CommandKind::cancel_craft, id));
    CHECK(cancelled.outcome == Outcome::craft_cancelled); CHECK(cancelled.tick == tick);
    CHECK(r.server.View(0).craftingJobs.empty()); CHECK(r.server.View(0).remainingTicks == 0);
    CHECK_FALSE(r.server.GetWorld().IsReservedItem(r.material));
    REQUIRE(r.server.GetWorld().CheckInvariants());
}

TEST_CASE("Crafting refusals preserve state and reserved child commands are rejected", "[crafting][server]") {
    Rig r;
    Command invalid; invalid.kind = CommandKind::craft; invalid.recipe = {UINT32_MAX};
    auto result = r.server.Send(invalid);
    CHECK(result.outcome == Outcome::crafting_refused); CHECK(result.craftingOutcome == CraftingOutcome::InvalidTarget);
    CHECK_FALSE(result.advanced); CHECK(r.server.GetTick() == 0);
    invalid.recipe = r.content.FindCraftingRecipe("missing");
    const auto location = r.server.GetWorld().GetItem(r.material)->location;
    result = r.server.Send(invalid);
    CHECK(result.outcome == Outcome::crafting_refused); CHECK(result.craftingOutcome == CraftingOutcome::MissingInputs);
    CHECK_FALSE(result.advanced); CHECK(result.cost == 0); CHECK(r.server.GetTick() == 0);
    CHECK(r.server.GetWorld().GetItem(r.material)->location == location);
    CHECK(r.server.View(0).craftingJobs.empty());
    REQUIRE(r.Start().outcome == Outcome::craft_started);
    const auto view = r.server.View(0); const auto id = view.craftingJobs.front().id;
    REQUIRE(r.server.Send(r.Control(CommandKind::pause_craft, id)).outcome == Outcome::craft_paused);
    r.server.Update(1000000);
    auto& world = r.server.GetWorld(); const auto before = world.GetItem(r.material)->location;
    result = r.server.Send(r.Control(CommandKind::recycle, world.GetItem(r.material)->id));
    CHECK(result.outcome == Outcome::crafting_refused); CHECK(result.craftingOutcome == CraftingOutcome::InvalidTarget);
    CHECK(world.GetItem(r.material)->location == before); CHECK(r.server.GetTick() == 100);
    CHECK(r.server.View(0).craftingJobs.front().completedWork == 100);
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("Server resolves recycling recipe from the target", "[crafting][server]") {
    Rig r; auto& world = r.server.GetWorld();
    auto target = world.CreateItem(r.content.FindItem("bandage"), ItemLocation::Carried(r.server.GetPlayer())); REQUIRE(target);
    const auto result = r.server.Send(r.Control(CommandKind::recycle, world.GetItem(*target)->id));
    CHECK(result.outcome == Outcome::recycling_started); CHECK(result.advanced); CHECK(result.cost == 100);
    REQUIRE(r.server.View(0).craftingJobs.size() == 1);
    CHECK(r.server.View(0).craftingJobs.front().kind == CraftingKind::Recycle);
    r.server.Update(1500000); CHECK_FALSE(world.GetItem(*target)); CHECK(r.server.View(0).craftingJobs.empty());
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("Crafting control cannot bypass unrelated busy work or another worker assignment", "[crafting][server]") {
    Rig r; auto& world = r.server.GetWorld();
    auto other = world.CreateActor({1, 0}, true); REQUIRE(other);
    auto material = world.CreateItem(r.content.FindItem("rock"), ItemLocation::Carried(*other)); REQUIRE(material);
    const auto started = world.StartCrafting(*other, r.content.FindCraftingRecipe("make")); REQUIRE(started);
    REQUIRE(started->outcome == CraftingOutcome::Started);
    const auto id = world.GetItem(started->unfinished)->id;
    const auto command = r.Control(CommandKind::cancel_craft, id);
    const auto refused = r.server.Send(command);
    CHECK(refused.outcome == Outcome::crafting_refused); CHECK(refused.craftingOutcome == CraftingOutcome::JobBusy);
    REQUIRE(world.InspectCrafting(started->unfinished));
    CHECK(world.InspectCrafting(started->unfinished)->worker == world.GetActor(*other)->id);
    Command wait; wait.waitCost = 100;
    REQUIRE(r.server.Send(wait).outcome == Outcome::waited);
    const auto context = r.server.GetInputContext(r.server.GetHostPlayerId());
    const auto queued = r.server.Submit(r.server.GetHostPlayerId(),
        {context.lastInputSequence + 1, context.opportunity, command});
    CHECK(queued.outcome == Outcome::queued); CHECK(r.server.View(0).remainingTicks == 100);
    CHECK(world.GetItem(started->unfinished)); CHECK(r.server.View(0).commandQueued);
    REQUIRE(world.CheckInvariants());
}
}
