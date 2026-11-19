#include "core/world.h"
#include "crafting/crafting_state.h"
#include "skills/learning_system.h"
#include "skills/skill_state.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> result) { REQUIRE(result); return eastl::move(*result); }

ContentCatalog SkillCraftingContent() {
    const ContentSource source{"skill-crafting.json5", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'skill',id:'making',name:'Making',xp_to_next:[1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000]},
      {type:'item',id:'material',name:'material',mass_g:1,volume_ml:1},
      {type:'item',id:'product',name:'product',mass_g:1,volume_ml:1},
      {type:'item',id:'tool',name:'tool',mass_g:1,volume_ml:1,crafting_power:{store:'power',energy_per_work:10000},
        capabilities:[{type:'energy_store',id:'power',capacity_energy:100,initial_energy:100,rechargeable:true}]},
      {type:'book',id:'manual',name:'Manual',study_work:1,recipes:['make']},
      {type:'book',id:'other_manual',name:'Other manual',study_work:1,recipes:['make']},
      {type:'item',id:'print',name:'Manual copy',mass_g:1,volume_ml:1,books:['manual']},
      {type:'item',id:'other_print',name:'Other manual copy',mass_g:1,volume_ml:1,books:['other_manual']},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1},{item:'tool',count:1,retained:true}],
        result:{item:'product',count:1},work:7,skills:[{skill:'making',level:1}],
        training:[{skill:'making',level:1,xp_per_work:10000}],knowledge:{crafts_to_learn:2,disassemblies_to_learn:3}},
      {type:'recipe',id:'other',inputs:[],result:{item:'product',count:1},work:1,
        knowledge:{crafts_to_learn:3,disassemblies_to_learn:2}},
      {type:'recycling_recipe',id:'unmake',target:'product',work:7,returns:[],skills:[{skill:'making',level:1}],
        training:[{skill:'making',level:1,xp_per_work:10000}],teaches_recipes:['make','other']}
    ]})"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content);
    return eastl::move(*content);
}
struct Rig {
    ContentCatalog content = SkillCraftingContent();
    World world{content};
    ActorHandle actor{};
    ItemHandle material{}, tool{}, book{};
    Rig() {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 0; z < 8; ++z) { for (int x = 0; x < 8; ++x) {
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor"), 0}));
        } }
        actor = Take(world.CreateActor({2, 2}, true));
        material = Add("material", ItemLocation::Carried(actor));
        tool = Add("tool", ItemLocation::Ground({2, 2}));
        book = Add("print", ItemLocation::Ground({2, 2}));
        REQUIRE(world.SetCraftingSettings({1, 1}));
    }
    SkillId Skill() const { return content.FindSkill("making"); }
    CraftingRecipeId Recipe() const { return content.FindCraftingRecipe("make"); }
    ItemHandle Add(const char* id, ItemLocation location) { return Take(world.CreateItem(content.FindItem(id), location)); }
    void Level(ActorHandle who, std::uint8_t level) { REQUIRE(world.Learning().SetLevel(who, Skill(), level)); }
    CraftingResult Start() { return Take(world.StartCrafting(actor, Recipe())); }
    ItemHandle Begin() { const auto result = Start(); REQUIRE(result.outcome == CraftingOutcome::Started); return result.unfinished; }
    void Step(Tick ticks = 1) { world.ElapseTicks(ticks); world.ProcessCraftingEvents(); }
    const LearningState& Learning(ActorHandle who) const { return world.GetActor(who)->learning; }
    std::uint64_t Points(ActorHandle who, CraftingRecipeId recipe) const {
        for (const auto& progress : Learning(who).recipes) { if (progress.recipe == recipe) { return progress.points; } }
        return 0;
    }
    std::uint64_t Xp(ActorHandle who) const {
        const auto* progress = FindSkillProgress(Learning(who), Skill());
        return progress ? progress->xp : 0;
    }
    CraftingJobState State(ItemHandle job) const {
        const auto captured = world.CaptureItemState(job);
        return Take(ReadCraftingState(*captured.Find("crafting_job")));
    }
};

TEST_CASE("Crafting learned skill and reference start refusals are atomic", "[skills][crafting]") {
    Rig r;
    const auto power = r.world.CaptureItemState(r.tool);
    CHECK(r.Start().outcome == CraftingOutcome::MissingSkills); // A book cannot bypass learned skills.
    r.Level(r.actor, 1);
    REQUIRE(r.world.TransferItem(r.book, ItemLocation::Ground({7, 7})));
    CHECK(r.Start().outcome == CraftingOutcome::MissingRecipeReference);
    CHECK(r.world.GetItem(r.material)->location == ItemLocation::Carried(r.actor));
    CHECK(r.world.CaptureItemState(r.tool) == power);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
    Vector<CraftingJobView> jobs; r.world.CollectCraftingJobs(r.actor, jobs); CHECK(jobs.empty());
    r.Add("other_print", ItemLocation::Ground({2, 2}));
    const auto job = r.Begin(); CHECK(r.world.GetItem(job));
}

TEST_CASE("Reference and skill loss stop only the next crafting slice", "[skills][crafting]") {
    Rig r; r.Level(r.actor, 1);
    const auto job = r.Begin();
    SECTION("reference loss") {
        REQUIRE(r.world.TransferItem(r.book, ItemLocation::Ground({7, 7})));
        r.Step();
        CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::MissingRecipeReference);
        CHECK(Take(r.world.ResumeCrafting(r.actor, job)).outcome == CraftingOutcome::MissingRecipeReference);
        r.Add("other_print", ItemLocation::Ground({2, 2}));
    }
    SECTION("learned skill loss") {
        r.Level(r.actor, 0); r.Step();
        CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::MissingSkills);
        CHECK(Take(r.world.ResumeCrafting(r.actor, job)).outcome == CraftingOutcome::MissingSkills);
        r.Level(r.actor, 1);
    }
    CHECK(r.world.InspectCrafting(job)->completedWork == 1);
    CHECK(r.world.InspectCrafting(job)->worker == 0);
    CHECK(r.State(job).contributions[0].work == ratioScale);
    REQUIRE(Take(r.world.ResumeCrafting(r.actor, job)).outcome == CraftingOutcome::Resumed);
    CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::Working);
}

TEST_CASE("Completed slices train once and cancellation never memorizes", "[skills][crafting]") {
    Rig r; r.Level(r.actor, 1);
    const auto job = r.Begin();
    CHECK(r.Xp(r.actor) == 0);
    Take(r.world.PauseCrafting(r.actor, job)); r.Step();
    CHECK(r.Xp(r.actor) == ratioScale);
    CHECK_FALSE(r.world.ProcessCraftingEvents()); CHECK(r.Xp(r.actor) == ratioScale);
    const auto state = r.State(job);
    const auto restored = Take(ReadCraftingState(WriteCraftingState(state)));
    REQUIRE(restored.contributions.size() == 1);
    CHECK(restored.contributions[0].worker == r.world.GetActor(r.actor)->id);
    CHECK(restored.contributions[0].work == ratioScale);
    CHECK(Take(r.world.CancelCrafting(r.actor, job)).outcome == CraftingOutcome::Cancelled);
    CHECK(r.Xp(r.actor) == ratioScale); CHECK(r.Points(r.actor, r.Recipe()) == 0);
}

TEST_CASE("Recipe references retain the funded job sourcing radius", "[skills][crafting]") {
    Rig r; r.Level(r.actor, 1);
    REQUIRE(r.world.SetCraftingSettings({2, 1}));
    REQUIRE(r.world.TransferItem(r.book, ItemLocation::Ground({4, 2})));
    const auto job = r.Begin();
    REQUIRE(r.world.SetCraftingSettings({0, 1}));
    r.Step();
    REQUIRE(r.world.InspectCrafting(job));
    CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::Working);
    CHECK(r.world.InspectCrafting(job)->completedWork == 1);
    CHECK_FALSE(r.world.Learning().HasRecipeReference(r.actor, r.Recipe()));
    CHECK(r.world.Learning().HasRecipeReference(r.actor, r.Recipe(), 2));
}

TEST_CASE("Crafting handoff persists contributions and apportions the whole completion budget", "[skills][crafting]") {
    Rig r; r.Level(r.actor, 1);
    const auto other = Take(r.world.CreateActor({3, 2}, true)); r.Level(other, 1);
    const auto job = r.Begin(); Take(r.world.PauseCrafting(r.actor, job)); r.Step();
    REQUIRE(r.world.TransferItem(job, ItemLocation::Ground({2, 2})));
    REQUIRE(Take(r.world.ResumeCrafting(other, job)).outcome == CraftingOutcome::Resumed);
    r.Step();
    const auto restored = Take(ReadCraftingState(WriteCraftingState(r.State(job))));
    REQUIRE(restored.contributions.size() == 2);
    CHECK(restored.contributions[0].worker == r.world.GetActor(r.actor)->id);
    CHECK(restored.contributions[1].worker == r.world.GetActor(other)->id);
    for (int n = 0; n < 5; ++n) { r.Step(); }
    CHECK_FALSE(r.world.GetItem(job));
    CHECK(r.Xp(r.actor) == ratioScale); CHECK(r.Xp(other) == 6 * ratioScale);
    CHECK(r.Points(r.actor, r.Recipe()) == 4286);
    CHECK(r.Points(other, r.Recipe()) == 25714);
    CHECK(r.Points(r.actor, r.Recipe()) + r.Points(other, r.Recipe()) == 3 * ratioScale);
    CHECK_FALSE(r.world.ProcessCraftingEvents());
}

TEST_CASE("Pending funded work checks replacement knowledge and belongs to the completing worker", "[skills][crafting]") {
    Rig r; r.Level(r.actor, 1);
    REQUIRE(r.world.SetCraftingSettings({1, 2}));
    const auto other = Take(r.world.CreateActor({3, 2}, true));
    const auto job = r.Begin(); r.Step(1);
    REQUIRE(r.world.SetActorParked(other, true));
    REQUIRE(r.world.TransferItem(job, ItemLocation::Carried(other)));
    r.world.ProcessCraftingEvents();
    REQUIRE(r.world.InspectCrafting(job)->status == CraftingStatus::WorkerUnavailable);
    REQUIRE(r.world.SetActorParked(other, false));
    const auto paid = r.world.CaptureItemState(r.tool);
    CHECK(Take(r.world.ResumeCrafting(other, job)).outcome == CraftingOutcome::MissingSkills);
    r.Level(other, 1);
    REQUIRE(r.world.TransferItem(r.book, ItemLocation::Ground({7, 7})));
    CHECK(Take(r.world.ResumeCrafting(other, job)).outcome == CraftingOutcome::MissingRecipeReference);
    CHECK(r.State(job).pendingWork == 2 * ratioScale);
    CHECK(r.State(job).contributions.empty());
    r.Add("other_print", ItemLocation::Carried(other));
    REQUIRE(Take(r.world.ResumeCrafting(other, job)).outcome == CraftingOutcome::Resumed);
    CHECK(r.world.CaptureItemState(r.tool) == paid);
    Take(r.world.PauseCrafting(other, job)); r.Step(1);
    CHECK(r.Xp(r.actor) == 0); CHECK(r.Xp(other) == 2 * ratioScale);
    REQUIRE(r.State(job).contributions.size() == 1);
    CHECK(r.State(job).contributions[0].worker == r.world.GetActor(other)->id);
}

TEST_CASE("Memorization remains eligible above the recipe training ceiling", "[skills][crafting]") {
    Rig r; r.Level(r.actor, 2);
    const auto job = r.Begin();
    for (int n = 0; n < 7; ++n) { r.Step(); }
    CHECK_FALSE(r.world.GetItem(job)); CHECK(r.Xp(r.actor) == 0);
    CHECK(r.Points(r.actor, r.Recipe()) == 3 * ratioScale);
}

TEST_CASE("Recycling uses its own learned gates and teaches its explicit recipe list", "[skills][crafting]") {
    Rig r;
    REQUIRE(r.world.DestroyItem(r.book));
    const auto target = r.Add("product", ItemLocation::Carried(r.actor));
    const auto recycler = r.content.FindRecyclingRecipe("unmake");
    CHECK(Take(r.world.StartRecycling(r.actor, target, recycler)).outcome == CraftingOutcome::MissingSkills);
    CHECK_FALSE(r.world.IsReservedItem(target));
    r.Level(r.actor, 1);
    const auto result = Take(r.world.StartRecycling(r.actor, target, recycler));
    REQUIRE(result.outcome == CraftingOutcome::Started);
    for (int n = 0; n < 7; ++n) { r.Step(); }
    CHECK(r.Xp(r.actor) == 7 * ratioScale);
    CHECK(r.Points(r.actor, r.Recipe()) == 2 * ratioScale);
    CHECK(r.Points(r.actor, r.content.FindCraftingRecipe("other")) == 3 * ratioScale);
    CHECK_FALSE(r.world.GetItem(result.unfinished));
}

TEST_CASE("Crafting save reload retains handoff credit without replaying slice XP", "[skills][crafting][save]") {
    const auto directory = std::filesystem::temp_directory_path()
        / ("cataclysmx-skill-crafting-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(directory));
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } cleanup{directory};
    const auto path = (directory / "world.sqlite").string();
    PersistentId firstId = 0, secondId = 0, jobId = 0;
    DataObject saved;
    {
        Rig r; r.Level(r.actor, 1); firstId = r.world.GetActor(r.actor)->id;
        const auto other = Take(r.world.CreateActor({3, 2})); r.Level(other, 1);
        secondId = r.world.GetActor(other)->id;
        const auto job = r.Begin(); jobId = r.world.GetItem(job)->id;
        Take(r.world.PauseCrafting(r.actor, job)); r.Step();
        REQUIRE(r.world.TransferItem(job, ItemLocation::Ground({2, 2})));
        REQUIRE(Take(r.world.ResumeCrafting(other, job)).outcome == CraftingOutcome::Resumed);
        r.Step(); // Second worker has completed one slice, and funded another.
        saved = r.world.CaptureItemState(job);
        SessionState session; session.player = firstId; session.playerPosition = {2, 2};
        Diagnostics diagnostics;
        const auto result = r.world.CreateSave(path.c_str(), session, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(result);
    }
    const auto content = SkillCraftingContent();
    World world{content}; Take(world.LoadSave(path.c_str()));
    Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
    const auto first = world.FindActor(firstId), second = world.FindActor(secondId);
    const auto job = world.FindItem(jobId); REQUIRE(job.value);
    CHECK(world.CaptureItemState(job) == saved);
    const auto skill = content.FindSkill("making");
    REQUIRE(FindSkillProgress(world.GetActor(first)->learning, skill));
    REQUIRE(FindSkillProgress(world.GetActor(second)->learning, skill));
    CHECK(FindSkillProgress(world.GetActor(first)->learning, skill)->xp == ratioScale);
    CHECK(FindSkillProgress(world.GetActor(second)->learning, skill)->xp == ratioScale);
    CHECK_FALSE(world.ProcessCraftingEvents());
    for (int n = 0; n < 5; ++n) { world.ElapseTicks(1); world.ProcessCraftingEvents(); }
    CHECK_FALSE(world.GetItem(job));
    CHECK(FindSkillProgress(world.GetActor(first)->learning, skill)->xp == ratioScale);
    CHECK(FindSkillProgress(world.GetActor(second)->learning, skill)->xp == 6 * ratioScale);
    REQUIRE(world.GetActor(first)->learning.recipes.size() == 1);
    REQUIRE(world.GetActor(second)->learning.recipes.size() == 1);
    CHECK(world.GetActor(first)->learning.recipes[0].points == 4286);
    CHECK(world.GetActor(second)->learning.recipes[0].points == 25714);
    REQUIRE(world.CheckInvariants());
}

DataObject LegacyPayload(const CraftingJobState& state) {
    const auto current = WriteCraftingState(state);
    DataObject legacy;
    for (std::size_t i = 0; i < current.Size(); ++i) {
        if (current.Name(i) != "contributions" && current.Name(i) != "stations"
            && current.Name(i) != "heat_remainder") { legacy.Set(current.Name(i), current.At(i)); }
    }
    legacy.SetUnsigned("version", 1);
    return legacy;
}

TEST_CASE("Crafting heat remainder is bounded independent debt with strict legacy migration", "[crafting][forge][save]") {
    CraftingJobState state;
    state.recipeName = "make"; state.totalWork = 7;
    state.heatRemainder = craftingHeatDenominator - 1;
    state.energyRemainder = 17;
    const auto payload = WriteCraftingState(state);
    const auto restored = Take(ReadCraftingState(payload));
    CHECK(restored.heatRemainder == state.heatRemainder);
    CHECK(restored.energyRemainder == 17);
    for (unsigned version : {1u, 2u, 3u}) {
        DataObject legacy;
        for (std::size_t i = 0; i < payload.Size(); ++i) {
            const auto& name = payload.Name(i);
            if (name == "heat_remainder" || (version < 3 && name == "stations")
                || (version < 2 && name == "contributions")) { continue; }
            legacy.Set(name, payload.At(i));
        }
        legacy.SetUnsigned("version", version);
        const auto old = Take(ReadCraftingState(legacy));
        CHECK(old.heatRemainder == 0);
        CHECK(old.energyRemainder == 17);
        legacy.SetUnsigned("heat_remainder", 0);
        CHECK_FALSE(ReadCraftingState(legacy));
    }
    for (auto status : {CraftingStatus::ColdForge, CraftingStatus::InsufficientFuel}) {
        state.status = status;
        CHECK(Take(ReadCraftingState(WriteCraftingState(state))).status == status);
    }
    auto invalid = payload;
    invalid.SetUnsigned("heat_remainder", craftingHeatDenominator);
    CHECK_FALSE(ReadCraftingState(invalid));
    invalid.SetUnsigned("heat_remainder", UINT64_MAX);
    CHECK_FALSE(ReadCraftingState(invalid));
    invalid = payload;
    invalid.SetUnsigned("version", 3);
    CHECK_FALSE(ReadCraftingState(invalid));
}

TEST_CASE("Crafting contribution persistence accepts legacy work and rejects malformed credit", "[skills][crafting][save]") {
    CraftingJobState state;
    state.recipeName = "make"; state.totalWork = 7; state.work = 3 * ratioScale;
    state.contributions.push_back({2, ratioScale}); state.contributions.push_back({5, 2 * ratioScale});
    const auto valid = WriteCraftingState(state);
    CHECK(*valid.GetUnsigned("version") == 4);
    REQUIRE(ReadCraftingState(valid));
    const auto legacy = Take(ReadCraftingState(LegacyPayload(state)));
    CHECK(legacy.work == state.work); CHECK(legacy.contributions.empty());
    auto bad = valid;
    SECTION("duplicate worker") { state.contributions[1].worker = 2; bad = WriteCraftingState(state); }
    SECTION("unordered worker") { state.contributions[0].worker = 6; bad = WriteCraftingState(state); }
    SECTION("zero worker") { state.contributions[0].worker = 0; bad = WriteCraftingState(state); }
    SECTION("zero work") { state.contributions[0].work = 0; bad = WriteCraftingState(state); }
    SECTION("attribution exceeds completed work") { ++state.contributions[1].work; bad = WriteCraftingState(state); }
    SECTION("overflow attempt") { state.contributions[1].work = UINT64_MAX; bad = WriteCraftingState(state); }
    SECTION("wrong list type") { bad.SetUnsigned("contributions", 0); }
    SECTION("unknown entry fields") {
        auto entries = DataObject::List(); DataObject entry;
        entry.SetUnsigned("worker", 2); entry.SetUnsigned("work", 1); entry.SetUnsigned("extra", 0);
        entries.Append(eastl::move(entry)); bad.Set("contributions", eastl::move(entries));
    }
    SECTION("strict version one status bound") {
        bad = LegacyPayload(state); bad.SetUnsigned("status", static_cast<unsigned>(CraftingStatus::MissingSkills));
    }
    SECTION("strict version two status bound") {
        bad = DataObject{};
        for (std::size_t i = 0; i < valid.Size(); ++i) {
            if (valid.Name(i) != "stations" && valid.Name(i) != "heat_remainder") { bad.Set(valid.Name(i), valid.At(i)); }
        }
        bad.SetUnsigned("version", 2); bad.SetUnsigned("status", 8);
    }
    CHECK_FALSE(ReadCraftingState(bad));
}
}
