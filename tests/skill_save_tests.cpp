#include "core/world.h"
#include "core/world_archive.h"
#include "core/save_database.h"
#include "core/simulation.h"
#include "crafting/crafting_state.h"
#include "skills/learning_system.h"
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }
ContentCatalog LearningSaveContent(bool expanded = false) {
    String text = R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'skill',id:'work',name:'Work'},
      {type:'book',id:'manual',name:'Manual',study_work:250,recipes:['learn'],benefits:[{skill:'work',first_level:1,last_level:10,direct_xp:20,multiplier:15000}]},
      {type:'item',id:'print',name:'Print',mass_g:1,volume_ml:1,books:['manual']},
      {type:'recipe',id:'make',inputs:[],result:{item:'print',count:1},work:250},
      {type:'recipe',id:'learn',inputs:[],result:{item:'print',count:1},work:250,
       knowledge:{known_by_default:false,crafts_to_learn:1,disassemblies_to_learn:1}}
    )";
    if (expanded) { text += R"(,{type:'skill',id:'aaa_skill',name:'Earlier'}, {type:'book',id:'aaa_book',name:'Earlier',study_work:1,benefits:[{skill:'aaa_skill',first_level:1,last_level:1,direct_xp:1}]})"; }
    text += "]}";
    const ContentSource source{"learning-save.json5", {text.data(), text.size()}};
    Diagnostics diagnostics; auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content); return eastl::move(*content);
}
struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        directory = std::filesystem::temp_directory_path() / ("cataclysmx-learning-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(std::filesystem::create_directory(directory)); path = (directory / "world.sqlite").string();
    }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
    void Sql(const char* sql) {
        sqlite3* db = nullptr; REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
        const auto code = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        INFO(sqlite3_errmsg(db)); CHECK(code == SQLITE_OK); CHECK(sqlite3_close(db) == SQLITE_OK);
    }
};
LoadRequest Load(World& world, LoadMode mode = LoadMode::active) {
    const auto request = Take(world.RequestChunks({{0, 0}, 0}, mode)); REQUIRE(world.ReconcileChunks()); return request;
}
ActorHandle Reader(World& world, bool player = true) {
    REQUIRE(world.SetTile({2, 2}, {world.GetContent().FindTerrain("floor"), 0}));
    const auto actor = Take(world.CreateActor({2, 2}, player));
    Take(world.CreateItem(world.GetContent().FindItem("print"), ItemLocation::Carried(actor)));
    return actor;
}
SessionState Session(const World& world, ActorHandle actor) {
    SessionState session; session.player = world.GetActor(actor)->id; session.playerPosition = {2, 2}; return session;
}
void Start(World& world, ActorHandle actor) {
    REQUIRE(Take(world.Learning().Study(actor, world.GetContent().FindBook("manual"))).outcome == StudyOutcome::Started);
}

TEST_CASE("Study save roundtrip keeps funded work and stable content identities", "[skills][save]") {
    SaveFile file; PersistentId id = 0; DataObject captured;
    {
        const auto content = LearningSaveContent(); World world(content); Load(world);
        const auto actor = Reader(world); id = world.GetActor(actor)->id;
        REQUIRE(world.SetActorSpeed(actor, 150)); Start(world, actor);
        world.ElapseTicks(100); REQUIRE(world.Learning().ProcessEvents()); world.ElapseTicks(37);
        captured = CaptureLearningState(world.GetActor(actor)->learning, content);
        REQUIRE(world.CreateSave(file.path.c_str(), Session(world, actor))); REQUIRE(world.CheckInvariants());
    }
    {
        const auto content = LearningSaveContent(true); World world(content);
        const auto session = Take(world.LoadSave(file.path.c_str())); Load(world);
        const auto actor = world.FindActor(id); REQUIRE(actor.value);
        CHECK(CaptureLearningState(world.GetActor(actor)->learning, content) == captured);
        CHECK(world.GetActor(actor)->learning.study.remaining == 63);
        std::uint32_t rng = 1;
        CHECK(AdvanceUntilInput(world, rng, 1000).stop == SimulationStop::InputRequired);
        CHECK(world.GetTick() == 375);
        CHECK(FindBookProgress(world.GetActor(actor)->learning, content.FindBook("manual"))->used);
        CHECK(FindSkillProgress(world.GetActor(actor)->learning, content.FindSkill("work"))->xp == 20 * ratioScale);
        REQUIRE(world.Save(session)); REQUIRE(world.CheckInvariants());
    }
    {
        const auto content = LearningSaveContent(true); World world(content);
        Take(world.LoadSave(file.path.c_str())); Load(world); const auto actor = world.FindActor(id);
        CHECK_FALSE(world.Learning().ProcessEvents());
        CHECK(Take(world.Learning().Study(actor, content.FindBook("manual"))).outcome == StudyOutcome::AlreadyUsed);
        CHECK(FindSkillProgress(world.GetActor(actor)->learning, content.FindSkill("work"))->xp == 20 * ratioScale);
    }
}
TEST_CASE("Inactive parked and archived study timers freeze through persistence", "[skills][save]") {
    const auto content = LearningSaveContent(); SaveFile file; PersistentId id = 0; LearningState captured;
    {
        World world(content); auto request = Load(world); const auto actor = Reader(world); id = world.GetActor(actor)->id;
        Start(world, actor); world.ElapseTicks(30); Take(world.Learning().PauseStudy(actor));
        captured = world.GetActor(actor)->learning;
        SECTION("parked") {
            REQUIRE(world.ParkActors({&actor, 1})); world.ElapseTicks(1000);
            CHECK_FALSE(world.Learning().ProcessEvents()); CHECK(world.GetActor(actor)->learning == captured);
        }
        SECTION("resident inactive") {
            Load(world, LoadMode::resident); REQUIRE(world.ReleaseRequest(request)); REQUIRE(world.ReconcileChunks());
            world.ElapseTicks(1000); CHECK_FALSE(world.Learning().ProcessEvents()); CHECK(world.GetActor(actor)->learning == captured);
        }
        SECTION("unloaded archive") {
            REQUIRE(world.ReleaseRequest(request)); REQUIRE(world.ReconcileChunks()); world.ElapseTicks(1000);
            CHECK_FALSE(world.Learning().ProcessEvents()); CHECK_FALSE(world.FindActor(id).value);
        }
        SessionState session; session.playerPosition = {2, 2};
        REQUIRE(world.CreateSave(file.path.c_str(), session)); REQUIRE(world.CheckInvariants());
    }
    World world(content); Take(world.LoadSave(file.path.c_str())); Load(world); const auto actor = world.FindActor(id);
    REQUIRE(actor.value); CHECK(world.GetActor(actor)->learning == captured);
    if (world.GetActor(actor)->parked) { REQUIRE(world.SetActorParked(actor, false)); }
    world.ElapseTicks(70); REQUIRE(world.Learning().ProcessEvents());
    CHECK_FALSE(world.Learning().Busy(actor));
    CHECK(FindBookProgress(world.GetActor(actor)->learning, content.FindBook("manual"))->work == 100 * ratioScale);
    REQUIRE(world.CheckInvariants());
}
TEST_CASE("Paused books roundtrip and resume only their remaining work", "[skills][save]") {
    const auto content = LearningSaveContent(); SaveFile file; PersistentId id = 0;
    {
        World world(content); Load(world); const auto actor = Reader(world); id = world.GetActor(actor)->id;
        Start(world, actor); Take(world.Learning().PauseStudy(actor)); world.ElapseTicks(100); world.Learning().ProcessEvents();
        REQUIRE(world.CreateSave(file.path.c_str(), Session(world, actor)));
    }
    World world(content); Take(world.LoadSave(file.path.c_str())); Load(world); const auto actor = world.FindActor(id);
    CHECK_FALSE(world.Learning().Busy(actor)); Start(world, actor);
    std::uint32_t rng = 1; AdvanceUntilInput(world, rng, 1000);
    CHECK(world.GetTick() == 250); CHECK(FindBookProgress(world.GetActor(actor)->learning, content.FindBook("manual"))->used);
}
TEST_CASE("Learning save rejects corrupt envelopes content identities and pending work", "[skills][save]") {
    const auto content = LearningSaveContent(); SaveFile file;
    {
        World world(content); Load(world); const auto actor = Reader(world); Start(world, actor);
        REQUIRE(world.CreateSave(file.path.c_str(), Session(world, actor)));
    }
    SECTION("missing envelope") { file.Sql("UPDATE chunks SET payload=json_remove(payload,'$.actors[0].learning') WHERE json_array_length(payload,'$.actors')>0"); }
    SECTION("empty envelope") { file.Sql("UPDATE chunks SET payload=json_set(payload,'$.actors[0].learning',json('{}')) WHERE json_array_length(payload,'$.actors')>0"); }
    SECTION("unknown book") { file.Sql("UPDATE chunks SET payload=json_set(payload,'$.actors[0].learning.study.book','missing') WHERE json_array_length(payload,'$.actors')>0"); }
    SECTION("excess pending work") { file.Sql("UPDATE chunks SET payload=json_set(payload,'$.actors[0].learning.study.pendingWork',999999999) WHERE json_array_length(payload,'$.actors')>0"); }
    SECTION("deadline disagreement") { file.Sql("UPDATE chunks SET payload=json_set(payload,'$.actors[0].remaining_ticks','99') WHERE json_array_length(payload,'$.actors')>0"); }
    World world(content); CHECK_FALSE(world.LoadSave(file.path.c_str()));
}
TEST_CASE("Version twelve saves migrate to explicit neutral learning envelopes", "[skills][save]") {
    const auto content = LearningSaveContent(); SaveFile file; PersistentId id = 0;
    {
        World world(content); Load(world); const auto actor = Reader(world); id = world.GetActor(actor)->id;
        REQUIRE(world.CreateSave(file.path.c_str(), Session(world, actor)));
    }
    file.Sql("UPDATE chunks SET payload=json_remove(payload,'$.actors[0].learning') WHERE json_array_length(payload,'$.actors')>0; UPDATE metadata SET format_version=12");
    {
        World world(content); const auto session = Take(world.LoadSave(file.path.c_str())); Load(world);
        CHECK(world.GetActor(world.FindActor(id))->learning == LearningState{});
        REQUIRE(world.Save(session));
    }
    // A v13 decoder requires the envelope, so reopening also verifies migration.
    World world(content); Take(world.LoadSave(file.path.c_str())); Load(world);
    CHECK(world.GetActor(world.FindActor(id))->learning == LearningState{});
}
TEST_CASE("Persistence rejects simultaneous study and crafting assignments", "[skills][save]") {
    const auto content = LearningSaveContent(); SaveFile file; SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str())); REQUIRE(database.Begin());
    ChunkArchive chunk{}; chunk.tiles.fill({content.FindTerrain("floor"), 0});
    SavedActor actor{1, {}, true, 0}; actor.remainingTicks = 100;
    LearningState learning; EnsureBookProgress(learning, content.FindBook("manual"));
    learning.study.book = content.FindBook("manual"); learning.study.active = true;
    learning.study.remaining = 100; learning.study.pendingWork = 100 * ratioScale;
    actor.learning = CaptureLearningState(learning, content); chunk.actors.push_back(actor);
    SavedItem wrapper{}; wrapper.id = 2; wrapper.type = content.UnfinishedCraftItem();
    wrapper.location = LocationKind::actor; wrapper.owner = 1;
    CraftingJobState job; job.recipeName = "make"; job.totalWork = 250; job.worker = 1;
    job.status = CraftingStatus::Working; job.remaining = 100; job.pendingWork = 100 * ratioScale;
    wrapper.state.Set("crafting_job", WriteCraftingState(job)); chunk.items.push_back(eastl::move(wrapper));
    CHECK_FALSE(database.WriteChunk(chunk)); database.Rollback();
}
TEST_CASE("Unloaded NPC contributors receive completion credit without simulation activation", "[skills][save]") {
    bool diskOnly = false;
    SECTION("authoritative archive") {}
    SECTION("saved disk after archive flush") { diskOnly = true; }
    const auto content = LearningSaveContent(); SaveFile file;
    const auto recipe = content.FindCraftingRecipe("learn"); PersistentId contributor = 0;
    {
        World world(content); const auto origin = Load(world);
        const auto first = Reader(world, false); contributor = world.GetActor(first)->id;
        world.LearnRecipe(contributor, recipe, 6000);
        const auto job = Take(world.StartCrafting(first, recipe)); REQUIRE(job.outcome == CraftingOutcome::Started);
        Take(world.PauseCrafting(first, job.unfinished)); world.ElapseTicks(100); REQUIRE(world.ProcessCraftingEvents());
        const auto destination = Take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({18, 2}, {content.FindTerrain("floor"), 0}));
        const auto second = Take(world.CreateActor({18, 2}, true));
        Take(world.CreateItem(content.FindItem("print"), ItemLocation::Carried(second)));
        REQUIRE(world.TransferItem(job.unfinished, ItemLocation::Carried(second)));
        REQUIRE(world.ReleaseRequest(origin)); REQUIRE(world.ReconcileChunks());
        CHECK_FALSE(world.FindActor(contributor).value);
        SessionState session;
        REQUIRE(world.CreateSave(file.path.c_str(), session));
        if (!diskOnly) {
            // Reload and unload once to create an authoritative unsaved archive.
            const auto temporary = Load(world); REQUIRE(world.ReleaseRequest(temporary)); REQUIRE(world.ReconcileChunks());
        } else { CHECK(world.ArchivedChunkCount() == 0); }
        CHECK(Take(world.ResumeCrafting(second, job.unfinished)).outcome == CraftingOutcome::Resumed);
        world.ElapseTicks(100); REQUIRE(world.ProcessCraftingEvents());
        world.ElapseTicks(50); REQUIRE(world.ProcessCraftingEvents());
        CHECK_FALSE(world.FindActor(contributor).value); // Awarding never loads the actor.
        CHECK_FALSE(world.GetItem(job.unfinished));
        REQUIRE(world.Save(session));
        Load(world);
        const auto restored = world.FindActor(contributor); REQUIRE(restored.value);
        CHECK(KnowsRecipe(world.GetActor(restored)->learning, content, recipe));
        REQUIRE(world.GetActor(restored)->learning.recipes.size() == 1);
        CHECK(world.GetActor(restored)->learning.recipes[0].points == 10000);
        REQUIRE(world.CheckInvariants());
        (void)destination;
    }
    World world(content); Take(world.LoadSave(file.path.c_str())); Load(world);
    const auto restored = world.FindActor(contributor); REQUIRE(restored.value);
    CHECK(KnowsRecipe(world.GetActor(restored)->learning, content, recipe));
}
TEST_CASE("Recipe credit disk errors are surfaced and retried exactly once", "[skills][save]") {
    const auto content = LearningSaveContent(); SaveFile file; World world(content);
    const auto request = Load(world); const auto actor = Reader(world, false);
    const auto id = world.GetActor(actor)->id; const auto recipe = content.FindCraftingRecipe("learn");
    REQUIRE(world.ReleaseRequest(request)); REQUIRE(world.ReconcileChunks());
    SessionState session; REQUIRE(world.CreateSave(file.path.c_str(), session));
    sqlite3* blocker = nullptr; REQUIRE(sqlite3_open(file.path.c_str(), &blocker) == SQLITE_OK);
    REQUIRE(sqlite3_exec(blocker, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);
    world.LearnRecipe(id, recipe, 500);
    const auto reconcile = world.ReconcileChunks(); REQUIRE_FALSE(reconcile); CHECK(reconcile.error() == SaveError::Busy);
    const auto save = world.Save(session); REQUIRE_FALSE(save); CHECK(save.error() == SaveError::Busy);
    CHECK_FALSE(world.FindActor(id).value);
    REQUIRE(sqlite3_exec(blocker, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_close(blocker) == SQLITE_OK);
    REQUIRE(world.Save(session)); REQUIRE(world.Save(session)); Load(world);
    const auto restored = world.FindActor(id); REQUIRE(restored.value);
    REQUIRE(world.GetActor(restored)->learning.recipes.size() == 1);
    CHECK(world.GetActor(restored)->learning.recipes[0].points == 500);
}
TEST_CASE("Recipe credits ignore destroyed contributors despite stale disk source rows", "[skills][save]") {
    const auto content = LearningSaveContent(); SaveFile file; World world(content);
    const auto request = Load(world); const auto actor = Reader(world, false);
    const auto id = world.GetActor(actor)->id; const auto recipe = content.FindCraftingRecipe("learn");
    SessionState session; REQUIRE(world.CreateSave(file.path.c_str(), session));
    REQUIRE(world.DestroyActor(actor));
    world.LearnRecipe(id, recipe, 1000); CHECK_FALSE(world.FindActor(id).value);
    REQUIRE(world.ReleaseRequest(request)); REQUIRE(world.ReconcileChunks());
    world.LearnRecipe(id, recipe, 1000); REQUIRE(world.Save(session));
    world.LearnRecipe(id, recipe, 1000); Load(world);
    CHECK_FALSE(world.FindActor(id).value); REQUIRE(world.CheckInvariants());
}
}
