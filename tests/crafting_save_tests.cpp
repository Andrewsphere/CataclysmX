#include "core/world.h"
#include "core/world_archive.h"
#include "items/item_snapshot.h"
#include "crafting/crafting_state.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }
ContentCatalog SaveContent(bool expanded = false) {
    String text = R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'tool_trait',id:'cut',name:'Cut'},
      {type:'item',id:'material',name:'material',mass_g:10,volume_ml:20},
      {type:'item',id:'product',name:'product',mass_g:5,volume_ml:10},
      {type:'item',id:'recovered',name:'recovered cell',mass_g:1,volume_ml:1,capabilities:[
        {type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'item',id:'tool',name:'tool',mass_g:10,volume_ml:10,tool_traits:[{id:'cut',tier:1}],
        crafting_power:{store:'power',energy_per_work:3333},
        capabilities:[{type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1}],tool_traits:[{id:'cut',tier:1}],result:{item:'product',count:1},work:7},
      {type:'recipe',id:'retained',inputs:[{item:'tool',count:2,retained:true}],result:{item:'product',count:1},work:4},
      {type:'recycling_recipe',id:'recycle_tool',target:'tool',inputs:[{item:'tool',count:1}],returns:[{item:'recovered',count:1}],work:3}
    )";
    if (expanded) { text += ",{type:'recipe',id:'aaa',inputs:[],result:{item:'product',count:1},work:1}"; }
    text += "]}";
    const ContentSource source{"crafting-save.json5", {text.data(), text.size()}};
    Diagnostics diagnostics; auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content); return eastl::move(*content);
}
struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("cataclysmx-crafting-" + std::to_string(stamp));
        REQUIRE(std::filesystem::create_directory(directory)); path = (directory / "world.sqlite").string();
    }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
LoadRequest Load(World& world) {
    const auto request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks()); return request;
}
TEST_CASE("Crafting funded slices and fractional charges survive world save", "[crafting][save]") {
    const auto content = SaveContent(); SaveFile file;
    PersistentId actorId = 0, jobId = 0, materialId = 0, toolId = 0;
    DataObject savedState;
    {
        World world(content); Load(world);
        REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
        const auto actor = Take(world.CreateActor({2, 2}, true)); actorId = world.GetActor(actor)->id;
        const auto material = Take(world.CreateItem(content.FindItem("material"), ItemLocation::Carried(actor)));
        materialId = world.GetItem(material)->id;
        const auto tool = Take(world.CreateItem(content.FindItem("tool"), ItemLocation::Carried(actor))); toolId = world.GetItem(tool)->id;
        REQUIRE(world.SetCraftingSettings({2, 2})); REQUIRE(world.SetActorSpeed(actor, 150));
        const auto started = Take(world.StartCrafting(actor, content.FindCraftingRecipe("make")));
        REQUIRE(started.outcome == CraftingOutcome::Started); jobId = world.GetItem(started.unfinished)->id;
        world.ElapseTicks(2); REQUIRE(world.ProcessCraftingEvents()); world.ElapseTicks(1);
        CHECK(world.InspectCrafting(started.unfinished)->completedWork == 1);
        savedState = world.CaptureItemState(started.unfinished);
        SessionState session; session.player = actorId; session.playerPosition = {2, 2};
        REQUIRE(world.CreateSave(file.path.c_str(), session)); REQUIRE(world.CheckInvariants());
    }
    {
        World world(content); const auto session = Take(world.LoadSave(file.path.c_str())); Load(world);
        const auto job = world.FindItem(jobId); const auto actor = world.FindActor(actorId);
        REQUIRE(job.value); REQUIRE(actor.value);
        CHECK(world.CaptureItemState(job) == savedState);
        CHECK(world.GetItem(world.FindItem(materialId))->location == ItemLocation::Inside(job));
        CHECK(world.IsReservedItem(world.FindItem(materialId)));
        CHECK(world.GetCraftingSettings().maxSliceTicks == 100); // Administrative default is not saved.
        world.ElapseTicks(1); world.ProcessCraftingEvents();
        CHECK(world.InspectCrafting(job)->fundedTicksRemaining == 2); // Job policy is saved.
        while (world.GetItem(job)) { world.ElapseTicks(1); world.ProcessCraftingEvents(); }
        CHECK(world.GetTick() == 11); CHECK_FALSE(world.FindItem(materialId).value);
        const auto tool = world.FindItem(toolId);
        const auto power = FindItemCapability(content.GetItem(world.GetItem(tool)->type), "power");
        CHECK(world.GetItemEnergy(tool, power)->energy == 998);
        REQUIRE(world.CheckInvariants()); REQUIRE(world.Save(session));
    }
}
TEST_CASE("Crafting archive detachment freezes pending work", "[crafting][save]") {
    const auto content = SaveContent(); World world(content); auto request = Load(world);
    REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
    const auto actor = Take(world.CreateActor({2, 2}, true));
    Take(world.CreateItem(content.FindItem("material"), ItemLocation::Carried(actor)));
    Take(world.CreateItem(content.FindItem("tool"), ItemLocation::Carried(actor)));
    const auto started = Take(world.StartCrafting(actor, content.FindCraftingRecipe("make")));
    REQUIRE(started.outcome == CraftingOutcome::Started);
    const auto id = world.GetItem(started.unfinished)->id;
    world.ElapseTicks(2); const auto state = world.CaptureItemState(started.unfinished);
    REQUIRE(world.ReleaseRequest(request)); REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(1000); world.ProcessCraftingEvents(); CHECK_FALSE(world.FindItem(id).value);
    request = Load(world); const auto restored = world.FindItem(id); REQUIRE(restored.value);
    CHECK(world.CaptureItemState(restored) == state); CHECK(world.InspectCrafting(restored)->fundedTicksRemaining == 5);
    world.ElapseTicks(5); world.ProcessCraftingEvents(); CHECK_FALSE(world.GetItem(restored)); REQUIRE(world.CheckInvariants());
}
TEST_CASE("Destroyed pause-requested workers leave valid savable paid crafting work", "[crafting][save][crafting-unavailable]") {
    const auto content = SaveContent(); SaveFile file;
    PersistentId replacementId = 0, jobId = 0, toolId = 0;
    const auto toolType = content.FindItem("tool"), productType = content.FindItem("product");
    const auto power = FindItemCapability(content.GetItem(toolType), "power");
    {
        World world(content); Load(world);
        REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
        REQUIRE(world.SetTile({3, 2}, {content.FindTerrain("floor"), 0}));
        const auto actor = Take(world.CreateActor({2, 2}, true));
        const auto replacement = Take(world.CreateActor({3, 2}, true)); replacementId = world.GetActor(replacement)->id;
        Take(world.CreateItem(content.FindItem("material"), ItemLocation::Carried(actor)));
        const auto tool = Take(world.CreateItem(toolType, ItemLocation::Carried(actor))); toolId = world.GetItem(tool)->id;
        const auto started = Take(world.StartCrafting(actor, content.FindCraftingRecipe("make")));
        REQUIRE(started.outcome == CraftingOutcome::Started); jobId = world.GetItem(started.unfinished)->id;
        world.ElapseTicks(2); Take(world.PauseCrafting(actor, started.unfinished)); REQUIRE(world.DestroyActor(actor));
        REQUIRE(world.ProcessCraftingEvents());
        const auto view = world.InspectCrafting(started.unfinished); REQUIRE(view);
        CHECK(view->status == CraftingStatus::WorkerUnavailable); CHECK(view->worker == 0); CHECK_FALSE(view->pauseRequested);
        CHECK(view->completedWork == 0); CHECK(view->fundedTicksRemaining == 5);
        CHECK(world.GetItemEnergy(tool, power)->energy == 998);
        SessionState session; session.player = replacementId; session.playerPosition = {3, 2};
        REQUIRE(world.CreateSave(file.path.c_str(), session)); REQUIRE(world.CheckInvariants());
    }
    {
        World world(content); Take(world.LoadSave(file.path.c_str())); Load(world);
        const auto job = world.FindItem(jobId), tool = world.FindItem(toolId); const auto actor = world.FindActor(replacementId);
        REQUIRE(job.value); const auto view = world.InspectCrafting(job); REQUIRE(view);
        CHECK(view->status == CraftingStatus::WorkerUnavailable); CHECK_FALSE(view->pauseRequested);
        CHECK(view->fundedTicksRemaining == 5);
        CHECK(Take(world.ResumeCrafting(actor, job)).outcome == CraftingOutcome::Resumed);
        CHECK(world.GetItemEnergy(tool, power)->energy == 998); CHECK(world.GetActorRemainingTicks(actor) == 5);
        world.ElapseTicks(5); REQUIRE(world.ProcessCraftingEvents()); CHECK_FALSE(world.GetItem(job));
        CHECK(world.GetItemEnergy(tool, power)->energy == 998);
        unsigned products = 0;
        for (auto item : world.GetActor(actor)->inventory) { products += world.GetItem(item)->type == productType; }
        CHECK(products == 1); REQUIRE(world.CheckInvariants());
    }
}
TEST_CASE("Retained tool replacement after save preserves aggregate fractional billing", "[crafting][save]") {
    const auto content = SaveContent(); SaveFile file;
    PersistentId worker = 0, jobId = 0, firstId = 0, secondId = 0;
    const auto type = content.FindItem("tool"); const auto power = FindItemCapability(content.GetItem(type), "power");
    {
        World world(content); Load(world);
        REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
        REQUIRE(world.SetTile({7, 7}, {content.FindTerrain("floor"), 0}));
        const auto actor = Take(world.CreateActor({2, 2}, true)); worker = world.GetActor(actor)->id;
        const auto first = Take(world.CreateItem(type, ItemLocation::Carried(actor))); firstId = world.GetItem(first)->id;
        const auto second = Take(world.CreateItem(type, ItemLocation::Carried(actor))); secondId = world.GetItem(second)->id;
        REQUIRE(world.SetCraftingSettings({1, 1}));
        const auto started = Take(world.StartCrafting(actor, content.FindCraftingRecipe("retained")));
        REQUIRE(started.outcome == CraftingOutcome::Started); jobId = world.GetItem(started.unfinished)->id;
        Take(world.PauseCrafting(actor, started.unfinished)); world.ElapseTicks(1); world.ProcessCraftingEvents();
        const auto state = world.CaptureItemState(started.unfinished);
        CHECK(*state.Find("crafting_job")->GetUnsigned("energy_remainder") == 66660000);
        CHECK(world.GetItemEnergy(first, power)->energy == 1000); CHECK(world.GetItemEnergy(second, power)->energy == 1000);
        SessionState session; session.player = worker; session.playerPosition = {2, 2}; REQUIRE(world.CreateSave(file.path.c_str(), session));
    }
    {
        World world(content); Take(world.LoadSave(file.path.c_str())); Load(world);
        const auto actor = world.FindActor(worker);
        const auto job = world.FindItem(jobId), first = world.FindItem(firstId), second = world.FindItem(secondId);
        REQUIRE(world.TransferItem(first, ItemLocation::Ground({7, 7})));
        const auto replacement = Take(world.CreateItem(type, ItemLocation::Carried(actor)));
        REQUIRE(world.SetCraftingSettings({1, 1}));
        CHECK(Take(world.ResumeCrafting(actor, job)).outcome == CraftingOutcome::Resumed);
        CHECK(world.GetItemEnergy(second, power)->energy == 1000);
        CHECK(world.GetItemEnergy(replacement, power)->energy == 999); // Carries the old first tool's fraction too.
        for (int n = 0; n < 4 && world.GetItem(job); ++n) { world.ElapseTicks(1); world.ProcessCraftingEvents(); }
        CHECK_FALSE(world.GetItem(job));
        CHECK(world.GetItemEnergy(first, power)->energy == 1000);
        CHECK(world.GetItemEnergy(second, power)->energy + world.GetItemEnergy(replacement, power)->energy == 1998);
        REQUIRE(world.CheckInvariants());
    }
}
ChunkArchive Snapshot(const ContentCatalog& content) {
    ChunkArchive chunk{}; chunk.tiles.fill({content.FindTerrain("floor"), 0});
    SavedActor actor{1, {}, true, 0}; chunk.actors.push_back(actor);
    SavedItem wrapper{}; wrapper.id = 2; wrapper.type = content.UnfinishedCraftItem(); wrapper.location = LocationKind::actor; wrapper.owner = 1;
    CraftingJobState state; state.recipeName = "make"; state.totalWork = 7; state.worker = 1;
    state.pendingWork = 20000; state.remaining = 2; state.status = CraftingStatus::Working;
    wrapper.state.Set("crafting_job", WriteCraftingState(state)); chunk.items.push_back(eastl::move(wrapper));
    SavedItem child{}; child.id = 3; child.type = content.FindItem("material"); child.location = LocationKind::item; child.owner = 2;
    WriteItemState(content.GetItem(child.type), MakeItemState(content.GetItem(child.type)), child.state);
    chunk.items.push_back(eastl::move(child)); return chunk;
}
TEST_CASE("Recycling target identity survives saving alongside a same-type charged consumable", "[crafting][save]") {
    const auto content = SaveContent(); SaveFile file;
    PersistentId worker = 0, jobId = 0, targetId = 0, extraId = 0;
    const auto type = content.FindItem("tool"), resultType = content.FindItem("recovered");
    const auto power = FindItemCapability(content.GetItem(type), "power");
    {
        World world(content); Load(world); REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
        const auto actor = Take(world.CreateActor({2, 2}, true)); worker = world.GetActor(actor)->id;
        const auto extra = Take(world.CreateItem(type, ItemLocation::Carried(actor))); extraId = world.GetItem(extra)->id;
        const auto target = Take(world.CreateItem(type, ItemLocation::Carried(actor))); targetId = world.GetItem(target)->id;
        REQUIRE(world.SetItemEnergy(extra, power, 900)); REQUIRE(world.SetItemEnergy(target, power, 4));
        const auto started = Take(world.StartRecycling(actor, target, content.FindRecyclingRecipe("recycle_tool")));
        REQUIRE(started.outcome == CraftingOutcome::Started); jobId = world.GetItem(started.unfinished)->id;
        world.ElapseTicks(1);
        SessionState session; session.player = worker; session.playerPosition = {2, 2};
        REQUIRE(world.CreateSave(file.path.c_str(), session)); REQUIRE(world.CheckInvariants());
    }
    {
        World world(content); Take(world.LoadSave(file.path.c_str())); Load(world);
        const auto actor = world.FindActor(worker); const auto job = world.FindItem(jobId);
        REQUIRE(job.value); const auto state = world.CaptureItemState(job);
        CHECK(*state.Find("crafting_job")->GetUnsigned("target") == targetId);
        REQUIRE(world.FindItem(targetId).value); REQUIRE(world.FindItem(extraId).value);
        world.ElapseTicks(2); world.ProcessCraftingEvents();
        CHECK_FALSE(world.GetItem(job)); CHECK_FALSE(world.FindItem(targetId).value); CHECK_FALSE(world.FindItem(extraId).value);
        unsigned recovered = 0;
        for (auto item : world.GetActor(actor)->inventory) {
            if (world.GetItem(item)->type != resultType) { continue; }
            ++recovered;
            CHECK(world.GetItemEnergy(item, FindItemCapability(content.GetItem(resultType), "power"))->energy == 4);
        }
        CHECK(recovered == 1); REQUIRE(world.CheckInvariants());
    }
}
TEST_CASE("Recycling snapshot target must identify one reserved child of the correct type", "[crafting][save]") {
    const auto content = SaveContent(); auto saved = Snapshot(content);
    CraftingJobState state; state.kind = CraftingKind::Recycle; state.recipeName = "recycle_tool";
    state.totalWork = 3; state.worker = 1; state.status = CraftingStatus::Working;
    state.remaining = 2; state.pendingWork = 30000; state.recyclingTarget = 5;
    saved.items[0].state.Set("crafting_job", WriteCraftingState(state));
    saved.items[1].type = content.FindItem("tool");
    WriteItemState(content.GetItem(saved.items[1].type), MakeItemState(content.GetItem(saved.items[1].type)), saved.items[1].state);
    SavedItem target = saved.items[1]; target.id = 5; saved.items.push_back(eastl::move(target));
    REQUIRE(ValidateItemSnapshot(content, saved));
    auto* payload = saved.items[0].state.Find("crafting_job");
    SECTION("missing target identity") { payload->SetUnsigned("target", 0); }
    SECTION("target references a different object") { payload->SetUnsigned("target", 1); }
    SECTION("target is no longer reserved") { saved.items.back().location = LocationKind::actor; saved.items.back().owner = 1; }
    SECTION("target type does not match recipe") { saved.items.back().type = content.FindItem("recovered"); }
    SECTION("target identity is duplicated") { saved.items[1].id = 5; }
    CHECK_FALSE(ValidateItemSnapshot(content, saved));
}
TEST_CASE("Crafting snapshots validate stable recipe names reserved children and bounded accounting", "[crafting][save]") {
    const auto content = SaveContent(); const auto valid = Snapshot(content); REQUIRE(ValidateItemSnapshot(content, valid));
    SECTION("missing child") { auto bad = valid; bad.items.pop_back(); CHECK_FALSE(ValidateItemSnapshot(content, bad)); }
    SECTION("wrong child") { auto bad = valid; bad.items.back().type = content.FindItem("product"); CHECK_FALSE(ValidateItemSnapshot(content, bad)); }
    SECTION("over-credit") { auto bad = valid; bad.items[0].state.Find("crafting_job")->SetUnsigned("pending", 70001); CHECK_FALSE(ValidateItemSnapshot(content, bad)); }
    SECTION("missing recipe") { auto bad = valid; bad.items[0].state.Find("crafting_job")->SetString("recipe", "missing"); CHECK_FALSE(ValidateItemSnapshot(content, bad)); }
    SECTION("bad residue") { auto bad = valid; bad.items[0].state.Find("crafting_job")->SetUnsigned("timing_remainder", 100); CHECK_FALSE(ValidateItemSnapshot(content, bad)); }
    SECTION("crafting cannot claim a recycling target") {
        auto bad = valid; bad.items[0].state.Find("crafting_job")->SetUnsigned("target", 3);
        CHECK_FALSE(ValidateItemSnapshot(content, bad));
    }
    SECTION("energy remainder is strictly fractional") {
        auto bad = valid; bad.items[0].state.Find("crafting_job")->SetUnsigned("energy_remainder", craftingEnergyDenominator);
        CHECK_FALSE(ValidateItemSnapshot(content, bad));
    }
    SECTION("working job must have a worker") {
        auto bad = valid; bad.items[0].state.Find("crafting_job")->SetUnsigned("worker", 0);
        CHECK_FALSE(ValidateItemSnapshot(content, bad));
    }
    SECTION("paused job cannot retain a worker or pending slice") {
        auto bad = valid; auto* job = bad.items[0].state.Find("crafting_job");
        job->SetUnsigned("status", static_cast<unsigned>(CraftingStatus::Paused));
        CHECK_FALSE(ValidateItemSnapshot(content, bad));
        job->SetUnsigned("worker", 0); CHECK_FALSE(ValidateItemSnapshot(content, bad));
        job->SetUnsigned("remaining", 0); job->SetUnsigned("pending", 0);
        CHECK(ValidateItemSnapshot(content, bad));
    }
    SECTION("pause flag and status agree") {
        auto bad = valid; auto* job = bad.items[0].state.Find("crafting_job");
        job->SetUnsigned("status", static_cast<unsigned>(CraftingStatus::PauseRequested));
        CHECK_FALSE(ValidateItemSnapshot(content, bad));
        job->SetBoolean("pause", true); CHECK(ValidateItemSnapshot(content, bad));
        job->SetUnsigned("status", static_cast<unsigned>(CraftingStatus::Working));
        CHECK_FALSE(ValidateItemSnapshot(content, bad));
    }
    SECTION("unavailable worker can leave funded work for reassignment") {
        auto saved = valid; auto* job = saved.items[0].state.Find("crafting_job");
        job->SetUnsigned("status", static_cast<unsigned>(CraftingStatus::WorkerUnavailable));
        job->SetUnsigned("worker", 0); CHECK(ValidateItemSnapshot(content, saved));
        job->SetBoolean("pause", true); CHECK_FALSE(ValidateItemSnapshot(content, saved));
    }
    SECTION("recipe IDs can change") {
        const auto expanded = SaveContent(true);
        auto state = Take(ReadCraftingState(*valid.items[0].state.Find("crafting_job")));
        REQUIRE(ResolveCraftingState(expanded, state));
        CHECK(state.craftingRecipe == expanded.FindCraftingRecipe("make")); CHECK(state.craftingRecipe != content.FindCraftingRecipe("make"));
    }
}
}
