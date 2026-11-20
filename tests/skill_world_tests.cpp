#include "core/world.h"
#include "core/simulation.h"
#include "skills/learning_system.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }
ContentCatalog LearningContent() {
    const ContentSource source{"learning-world.json5", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'skill',id:'work',name:'Work'}, {type:'skill',id:'unused',name:'Unused',max_level:1},
      {type:'book',id:'capped',name:'Capped',study_work:50,recipes:['make'],benefits:[
        {skill:'unused',first_level:1,last_level:1,direct_xp:20,multiplier:20000}]},
      {type:'book',id:'mixed',name:'Mixed',study_work:50,benefits:[
        {skill:'unused',first_level:1,last_level:1,direct_xp:20,multiplier:20000},
        {skill:'work',first_level:1,last_level:10,direct_xp:20,theory_xp:100,multiplier:20000}]},
      {type:'item',id:'capped_print',name:'Capped',mass_g:1,volume_ml:1,books:['capped']},
      {type:'item',id:'mixed_print',name:'Mixed',mass_g:1,volume_ml:1,books:['mixed']},
      {type:'book',id:'manual',name:'Manual',study_work:250,recipes:['make'],benefits:[
        {skill:'work',first_level:1,last_level:10,direct_xp:100,theory_xp:2000,multiplier:20000}]},
      {type:'book',id:'other',name:'Other',study_work:150,benefits:[
        {skill:'work',first_level:1,last_level:10,direct_xp:20,multiplier:15000}]},
      {type:'item',id:'print',name:'Print',mass_g:1,volume_ml:1,books:['manual']},
      {type:'item',id:'other_print',name:'Other',mass_g:1,volume_ml:1,books:['other']},
      {type:'item',id:'reader',name:'Reader',mass_g:1,volume_ml:1,books:['manual'],reading_power:'power',capabilities:[
        {type:'energy_store',id:'power',capacity_energy:10,initial_energy:10,rechargeable:true}]},
      {type:'item',id:'bag',name:'Bag',mass_g:1,volume_ml:2,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:1},
      {type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:250,knowledge:{known_by_default:false}},
      {type:'recipe',id:'auto',inputs:[],result:{item:'product',count:1},work:1,skills:[{skill:'work',level:1}],knowledge:{autolearn:[{skill:'work',level:1}]}}
    ]})"};
    Diagnostics diagnostics; auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content); return eastl::move(*content);
}
struct Rig {
    ContentCatalog content = LearningContent();
    World world{content};
    ActorHandle actor{};
    LoadRequest request{};
    BookId book = content.FindBook("manual");
    SkillId skill = content.FindSkill("work");
    Rig() {
        request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 0; z < 9; ++z) { for (int x = 0; x < 9; ++x) {
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor"), 0}));
        } }
        actor = Take(world.CreateActor({2, 2}, true));
    }
    ItemHandle Add(const char* name, ItemLocation location) { return Take(world.CreateItem(content.FindItem(name), location)); }
    void Start(BookId id = {}) {
        CHECK(Take(world.Learning().Study(actor, id.value ? id : book)).outcome == StudyOutcome::Started);
    }
    void Step(Tick ticks) { world.ElapseTicks(ticks); world.Learning().ProcessEvents(); }
    const LearningState& State() const { return world.GetActor(actor)->learning; }
};

TEST_CASE("Study commits funded slices before input and benefits only once per character", "[skills][world]") {
    Rig r; r.Add("print", ItemLocation::Carried(r.actor));
    r.Start();
    CHECK_FALSE(r.world.ScheduleActorAction(r.actor));
    CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("make"))).outcome == CraftingOutcome::ActorBusy);
    CHECK(Take(r.world.Learning().Study(r.actor, r.book)).outcome == StudyOutcome::ActorBusy);
    r.Step(99); CHECK(FindBookProgress(r.State(), r.book)->work == 0);
    r.Step(1); CHECK(FindBookProgress(r.State(), r.book)->work == 100 * ratioScale);
    CHECK(FindSkillProgress(r.State(), r.skill) == nullptr);
    std::uint32_t rng = 1;
    CHECK(AdvanceUntilInput(r.world, rng, 1000).stop == SimulationStop::InputRequired);
    CHECK(r.world.GetTick() == 250);
    CHECK(FindBookProgress(r.State(), r.book)->used);
    CHECK(FindSkillProgress(r.State(), r.skill)->xp == 30 * ratioScale);
    CHECK(FindSkillProgress(r.State(), r.skill)->multiplier == 20000);
    const auto completed = r.State();
    CHECK_FALSE(r.world.Learning().ProcessEvents());
    CHECK(Take(r.world.Learning().Study(r.actor, r.book)).outcome == StudyOutcome::AlreadyUsed);
    CHECK(r.State() == completed);
    CHECK(r.world.Learning().HasRecipeReference(r.actor, r.content.FindCraftingRecipe("make")));
    CHECK_FALSE(KnowsRecipe(r.State(), r.content, r.content.FindCraftingRecipe("make")));
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Study pause retains paid work and resumes through an equivalent copy", "[skills][world]") {
    Rig r; const auto copy = r.Add("print", ItemLocation::Carried(r.actor)); r.Start();
    r.Step(30); REQUIRE(r.world.DestroyItem(copy));
    CHECK(Take(r.world.Learning().PauseStudy(r.actor)).outcome == StudyOutcome::PauseRequested);
    CHECK(r.world.Learning().Busy(r.actor)); r.Step(70);
    CHECK_FALSE(r.world.Learning().Busy(r.actor));
    CHECK(FindBookProgress(r.State(), r.book)->work == 100 * ratioScale);
    CHECK(Take(r.world.Learning().Study(r.actor, r.book)).outcome == StudyOutcome::MissingReference);
    r.Add("other_print", ItemLocation::Carried(r.actor));
    r.Start(r.content.FindBook("other")); Take(r.world.Learning().PauseStudy(r.actor)); r.Step(100);
    CHECK(FindBookProgress(r.State(), r.content.FindBook("other"))->work == 100 * ratioScale);
    r.Add("print", ItemLocation::Ground({3, 2})); r.Start(); r.Step(100); r.Step(50);
    CHECK(FindBookProgress(r.State(), r.book)->used);
    CHECK_FALSE(FindBookProgress(r.State(), r.content.FindBook("other"))->used);
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Due study and out-of-reach crafting assignments retain exclusive ownership", "[skills][world]") {
    Rig r; r.Add("print", ItemLocation::Carried(r.actor));
    SECTION("study due but not yet committed") {
        r.Start(); r.world.ElapseTicks(100);
        Vector<ActorHandle> ready; r.world.CollectReadyPlayers(ready); CHECK(ready.empty());
        CHECK_FALSE(r.world.ScheduleActorAction(r.actor));
        CHECK(Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("make"))).outcome == CraftingOutcome::ActorBusy);
    }
    SECTION("craft due with wrapper beyond reach") {
        const auto job = Take(r.world.StartCrafting(r.actor, r.content.FindCraftingRecipe("make")));
        REQUIRE(job.outcome == CraftingOutcome::Started);
        REQUIRE(r.world.TransferItem(job.unfinished, ItemLocation::Ground({7, 7})));
        r.world.ElapseTicks(100);
        CHECK(Take(r.world.Learning().Study(r.actor, r.book)).outcome == StudyOutcome::ActorBusy);
    }
}
TEST_CASE("Lost references finish a funded slice and pause before funding more", "[skills][world]") {
    Rig r; const auto copy = r.Add("print", ItemLocation::Carried(r.actor)); r.Start();
    REQUIRE(r.world.DestroyItem(copy)); r.Step(100);
    CHECK_FALSE(r.world.Learning().Busy(r.actor));
    CHECK(FindBookProgress(r.State(), r.book)->work == 100 * ratioScale);
    r.Add("print", ItemLocation::Carried(r.actor)); r.Start(); r.Step(100);
    const auto last = r.world.GetActor(r.actor)->inventory.back(); REQUIRE(r.world.DestroyItem(last));
    r.Step(50); CHECK(FindBookProgress(r.State(), r.book)->used);
}
TEST_CASE("Electronic study requires usable energy but does not drain it", "[skills][world]") {
    Rig r; const auto reader = r.Add("reader", ItemLocation::Carried(r.actor));
    const auto power = FindItemCapability(r.content.GetItem(r.content.FindItem("reader")), "power");
    REQUIRE(r.world.SetItemEnergy(reader, power, 0));
    CHECK_FALSE(r.world.Learning().HasBookReference(r.actor, r.book));
    CHECK(Take(r.world.Learning().Study(r.actor, r.book)).outcome == StudyOutcome::MissingReference);
    REQUIRE(r.world.SetItemEnergy(reader, power, 1)); r.Start(); r.Step(100);
    CHECK(r.world.GetItemEnergy(reader, power)->energy == 1);
    REQUIRE(r.world.SetItemEnergy(reader, power, 0)); r.Step(100);
    CHECK_FALSE(r.world.Learning().Busy(r.actor));
    CHECK(FindBookProgress(r.State(), r.book)->work == 200 * ratioScale);
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Study references obey nested inventory nearby radius and wall reach", "[skills][world]") {
    Rig r; REQUIRE(r.world.SetCraftingSettings({2, 100}));
    const auto bag = r.Add("bag", ItemLocation::Carried(r.actor));
    const auto copy = r.Add("print", ItemLocation::Inside(bag));
    CHECK(r.world.Learning().HasBookReference(r.actor, r.book));
    const auto other = Take(r.world.CreateActor({3, 2}, true));
    REQUIRE(r.world.TransferItem(bag, ItemLocation::Carried(other)));
    CHECK_FALSE(r.world.Learning().HasBookReference(r.actor, r.book));
    REQUIRE(r.world.TransferItem(copy, ItemLocation::Ground({4, 2})));
    CHECK(r.world.Learning().HasBookReference(r.actor, r.book));
    REQUIRE(r.world.MoveActor(other, {7, 7}));
    for (int z = 0; z <= 4; ++z) { REQUIRE(r.world.SetTile({3, z}, {r.content.FindTerrain("wall"), 0})); }
    CHECK_FALSE(r.world.Learning().HasBookReference(r.actor, r.book));
}
TEST_CASE("Independent characters study a shared book with fractional speed timing", "[skills][world]") {
    Rig r; r.Add("print", ItemLocation::Ground({2, 3}));
    const auto other = Take(r.world.CreateActor({3, 3}, true));
    REQUIRE(r.world.SetActorSpeed(r.actor, 150)); r.Start();
    CHECK(Take(r.world.Learning().Study(other, r.book)).outcome == StudyOutcome::Started);
    r.Step(100); r.Step(100); r.Step(50);
    CHECK(FindBookProgress(r.world.GetActor(other)->learning, r.book)->used);
    CHECK_FALSE(FindBookProgress(r.State(), r.book)->used);
    r.Step(50); r.Step(75);
    CHECK(r.world.GetTick() == 375); CHECK(FindBookProgress(r.State(), r.book)->used);
    CHECK(FindSkillProgress(r.State(), r.skill)->xp == 30 * ratioScale);
    REQUIRE(r.world.CheckInvariants());
}
TEST_CASE("Learning inspection is neutral and previews clipping without mutation", "[skills][world]") {
    Rig r; r.Add("print", ItemLocation::Carried(r.actor));
    const auto before = r.State(); LearningView view;
    r.world.Learning().CollectView(r.actor, view);
    CHECK(r.State() == before); CHECK(view.skills.size() == r.content.SkillCount());
    REQUIRE(view.books.size() == 1); CHECK(view.books[0].cappedDirectXp == 70 * ratioScale);
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.skill, 5));
    r.world.Learning().CollectView(r.actor, view);
    CHECK(view.books[0].cappedTheoryXp == 200 * ratioScale);
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.skill, 1));
    CHECK(KnowsRecipe(r.State(), r.content, r.content.FindCraftingRecipe("auto")));
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.skill, 0));
    CHECK(KnowsRecipe(r.State(), r.content, r.content.FindCraftingRecipe("auto")));
    CHECK_FALSE(r.world.Learning().SetLevel(r.actor, r.skill, 11));
    Take(r.world.Learning().Train(r.actor, {r.skill, 0, ratioScale}, SkillActivity::Craft, 100 * ratioScale));
    CHECK(LearnedSkillLevel(r.State(), r.skill) == 1);
}

TEST_CASE("World learning APIs enforce authored maxima and capped previews", "[skills][world]") {
    Rig r;
    const auto skill = r.content.FindSkill("unused");
    const auto before = r.State();
    CHECK_FALSE(r.world.Learning().SetLevel(r.actor, skill, 2));
    CHECK_FALSE(r.world.Learning().Train(r.actor, {skill, 2, ratioScale}, SkillActivity::Craft, 100));
    CHECK(r.State() == before);
    REQUIRE(r.world.Learning().SetLevel(r.actor, skill, 1));
    Take(r.world.Learning().Train(r.actor, {skill, 1, ratioScale}, SkillActivity::Craft, UINT64_MAX));
    r.Add("capped_print", ItemLocation::Carried(r.actor));
    r.Add("mixed_print", ItemLocation::Carried(r.actor));
    LearningView view;
    r.world.Learning().CollectView(r.actor, view);
    for (const auto& entry : view.skills) {
        if (entry.progress.skill == skill) { CHECK(entry.requiredXp == 0); }
    }
    for (const auto& entry : view.books) {
        if (entry.book == r.content.FindBook("capped")) { CHECK_FALSE(entry.eligible); }
        if (entry.book == r.content.FindBook("mixed")) { CHECK(entry.eligible); }
    }
    r.Start(r.content.FindBook("mixed")); r.Step(50);
    CHECK(FindBookProgress(r.State(), r.content.FindBook("mixed"))->used);
    CHECK(FindSkillProgress(r.State(), skill)->xp == 0);
    CHECK(FindSkillProgress(r.State(), skill)->multiplier == ratioScale);
    CHECK(FindSkillProgress(r.State(), r.skill)->xp == 20 * ratioScale);
}

TEST_CASE("Cap changes preserve paid final study and completed unused references", "[skills][world]") {
    Rig r;
    const auto skill = r.content.FindSkill("unused");
    const auto book = r.content.FindBook("capped");
    r.Add("capped_print", ItemLocation::Carried(r.actor));
    r.Start(book);
    r.Step(20);
    REQUIRE(r.world.Learning().SetLevel(r.actor, skill, 1));
    const auto paid = r.State();
    const auto restored = ReadLearningState(CaptureLearningState(paid, r.content), r.content);
    REQUIRE(restored);
    CHECK(*restored == paid);
    CHECK(r.world.Learning().Busy(r.actor));
    r.Step(30);
    const auto* progress = FindBookProgress(r.State(), book);
    REQUIRE(progress);
    CHECK(progress->work == 50 * ratioScale);
    CHECK_FALSE(progress->used);
    CHECK_FALSE(r.world.Learning().Busy(r.actor));
    CHECK(Take(r.world.Learning().Study(r.actor, book)).outcome == StudyOutcome::OutsideTeachingRange);
    CHECK(r.world.Learning().HasRecipeReference(r.actor, r.content.FindCraftingRecipe("make")));
    REQUIRE(ReadLearningState(CaptureLearningState(r.State(), r.content), r.content));
    REQUIRE(r.world.Learning().SetLevel(r.actor, skill, 0));
    const auto turns = r.world.GetActor(r.actor)->turns;
    r.Start(book);
    CHECK(r.world.GetActor(r.actor)->turns == turns);
    CHECK(FindBookProgress(r.State(), book)->used);
    REQUIRE(r.world.CheckInvariants());
}
}
