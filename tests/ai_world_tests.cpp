#include "core/world.h"
#include "core/save_database.h"
#include "combat/combat_system.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

template<class T, class Error>
T Take(eastl::expected<T, Error> result) {
    REQUIRE(result);
    return eastl::move(*result);
}

LoadRequest Load(World& world, int radius = 0) {
    const auto request = Take(world.RequestChunks({{}, radius}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    return request;
}

ActorHandle Create(World& world, TileCoord position, bool player = false) {
    REQUIRE(world.SetTile(position, {world.GetContent().FindTerrain("floor"), 0}));
    return Take(world.CreateActor(position, player));
}

void CheckOrder(const World& world, ChunkCoord position) {
    const auto* chunk = world.GetChunk(position);
    REQUIRE(chunk);
    PersistentId after = 0;
    for (const auto handle : chunk->actors) {
        CHECK(world.NextActorInChunk(position, after) == handle);
        REQUIRE(world.GetActor(handle)->id > after);
        after = world.GetActor(handle)->id;
    }
    CHECK(world.NextActorInChunk(position, after) == ActorHandle{});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Chunk actor cursors survive insertions moves erasures and restoration", "[ai][world]") {
    World world(TestContent());
    const auto request = Load(world, 1);
    const auto first = Create(world, {1, 1});
    const auto second = Create(world, {2, 1});
    const auto third = Create(world, {chunk_size + 1, 1});
    const auto firstId = world.GetActor(first)->id;
    const auto secondId = world.GetActor(second)->id;
    const auto thirdId = world.GetActor(third)->id;
    CHECK(world.NextActorInChunk({}, 0) == first);
    CHECK(world.NextActorInChunk({}, firstId) == second);
    REQUIRE(world.SetTile({chunk_size + 2, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(first, {chunk_size + 2, 1}));
    CHECK(world.NextActorInChunk({1, 0}, 0) == first);
    CHECK(world.NextActorInChunk({1, 0}, firstId) == third);
    const auto fourth = Create(world, {3, 1});
    CHECK(world.NextActorInChunk({}, secondId) == fourth);
    REQUIRE(world.MoveActor(first, {1, 1}));
    REQUIRE(world.DestroyActor(second));
    CHECK(world.FindActor(secondId) == ActorHandle{});
    CHECK(world.NextActorInChunk({}, secondId) == fourth);
    CheckOrder(world, {});
    CheckOrder(world, {1, 0});
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.FindActor(firstId) == ActorHandle{});
    CHECK(world.NextActorInChunk({}, 0) == ActorHandle{});
    Load(world, 1);
    REQUIRE(world.FindActor(firstId) != ActorHandle{});
    REQUIRE(world.FindActor(thirdId) != ActorHandle{});
    CHECK(world.GetActor(first) == nullptr);
    CheckOrder(world, {});
    CheckOrder(world, {1, 0});
}

TEST_CASE("Actor occupancy excludes shared parked players across edits and restoration", "[ai][world][parking]") {
    World world(TestContent());
    const auto request = Load(world, 1);
    const TileCoord shared{1, 1};
    const TileCoord sameChunk{2, 1};
    const TileCoord otherChunk{-1, 1};
    Vector<ActorHandle> parked;
    for (int index = 0; index < 128; ++index) {
        const auto player = Create(world, shared, true);
        REQUIRE(world.SetActorParked(player, true));
        parked.push_back(player);
        CHECK(world.ActorAt(shared) == ActorHandle{});
    }
    const auto live = Create(world, shared);
    const auto liveId = world.GetActor(live)->id;
    const auto parkedId = world.GetActor(parked.front())->id;
    CHECK(world.ActorAt(shared) == live);
    CHECK(world.CreateActor(shared) == eastl::unexpected(WorldError::occupied));
    CHECK(world.SetActorParked(parked.front(), false) == eastl::unexpected(WorldError::occupied));
    REQUIRE(world.DestroyActor(parked.back()));
    parked.pop_back();
    CHECK(world.ActorAt(shared) == live);
    REQUIRE(world.SetTile(sameChunk, {TestTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(live, sameChunk));
    CHECK(world.ActorAt(shared) == ActorHandle{});
    CHECK(world.ActorAt(sameChunk) == live);
    // A parked displacement onto an occupied tile must not replace its occupant.
    REQUIRE(world.MoveActor(parked.back(), sameChunk));
    CHECK(world.ActorAt(sameChunk) == live);
    REQUIRE(world.MoveActor(parked.back(), shared));
    REQUIRE(world.SetActorParked(parked.front(), false));
    CHECK(world.ActorAt(shared) == parked.front());
    REQUIRE(world.SetActorParked(parked.front(), false));
    CHECK(world.ActorAt(shared) == parked.front());
    REQUIRE(world.SetTile(otherChunk, {TestTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(live, otherChunk));
    CHECK(world.ActorAt(sameChunk) == ActorHandle{});
    CHECK(world.ActorAt(otherChunk) == live);
    CHECK(world.ActorAt({world_coordinate_limit + 1, 0}) == ActorHandle{});
    CHECK(world.ActorAt({chunk_size * 10, 0}) == ActorHandle{});
    CheckOrder(world, {});
    CheckOrder(world, {-1, 0});

    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.ActorAt(shared) == ActorHandle{});
    CHECK(world.ActorAt(otherChunk) == ActorHandle{});
    Load(world, 1);
    const auto restoredLive = world.FindActor(liveId);
    const auto restoredPlayer = world.FindActor(parkedId);
    REQUIRE(restoredLive != ActorHandle{});
    REQUIRE(restoredPlayer != ActorHandle{});
    CHECK(world.ActorAt(otherChunk) == restoredLive);
    CHECK(world.ActorAt(shared) == restoredPlayer);
    CHECK(world.ActorAt(sameChunk) == ActorHandle{});
    CHECK(world.GetChunk({})->actors.size() == parked.size());
    CHECK(world.CheckInvariants());
    REQUIRE(world.SetActorParked(restoredPlayer, true));
    CHECK(world.ActorAt(shared) == ActorHandle{});
    REQUIRE(world.DestroyActor(restoredPlayer));
    CHECK(world.ActorAt(shared) == ActorHandle{});
    REQUIRE(world.DestroyActor(restoredLive));
    CHECK(world.ActorAt(otherChunk) == ActorHandle{});
    const auto replacement = Create(world, otherChunk);
    CHECK(world.ActorAt(otherChunk) == replacement);
    CheckOrder(world, {});
    CheckOrder(world, {-1, 0});
}

TEST_CASE("Gravity updates actor occupancy between layers while parked players stay frozen", "[ai][world][parking][gravity]") {
    World world(TestContent());
    Load(world);
    const TileCoord upper{2, 2, 1};
    const TileCoord lower{2, 2, 0};
    REQUIRE(world.SetTile(lower, {TestTerrain("floor"), 0}));
    const auto parked = Create(world, upper, true);
    REQUIRE(world.SetActorParked(parked, true));
    const auto falling = Create(world, upper);
    REQUIRE(world.SetTile(upper, {TestTerrain("air"), 0}));
    CHECK(world.GetActor(falling)->position == lower);
    CHECK(world.GetActor(parked)->position == upper);
    CHECK(world.ActorAt(upper) == ActorHandle{});
    CHECK(world.ActorAt(lower) == falling);
    REQUIRE(world.SetActorParked(parked, false));
    CHECK(world.ActorAt(upper) == parked); // The lower occupant stops the fall.
    REQUIRE(world.DestroyActor(falling));
    CHECK(world.GetActor(parked)->position == lower);
    CHECK(world.ActorAt(upper) == ActorHandle{});
    CHECK(world.ActorAt(lower) == parked);
    CheckOrder(world, {});
    CheckOrder(world, {0, 0, 1});
}

TEST_CASE("World AI inspection and elapsed time respect resident and unloaded suspension", "[ai][world]") {
    World world(TestContent());
    const auto request = Load(world);
    const auto npc = Create(world, {2, 2});
    const auto player = Create(world, {4, 2}, true);
    REQUIRE(world.SetTile({3, 2}, {TestTerrain("floor"), 0}));
    const auto id = world.GetActor(npc)->id;
    CHECK(world.InspectActorAi({}).behavior == AiBehavior::Idle);
    CHECK(world.InspectActorAi(player).target == 0);
    REQUIRE(world.SetActorController(npc, AiControllerKind::None));
    CHECK(world.InspectActorAi(npc).memoryRemaining == 0);
    CHECK(world.InspectActorAi(npc).scanRemaining == 0);
    auto combat = MakeCombatState(false);
    combat.faction = CombatFaction::Undead;
    REQUIRE(world.Combat().Configure(npc, combat));
    REQUIRE(world.SetActorController(npc, AiControllerKind::Sighted));
    world.SetSkyLight(255);
    std::uint32_t rng = 42;
    world.ProcessNpcActions(rng);
    const auto before = world.InspectActorAi(npc);
    REQUIRE(before.scanRemaining > 0);
    REQUIRE(before.memoryRemaining > 0);
    world.ElapseTicks(1);
    const auto elapsed = world.InspectActorAi(npc);
    CHECK(elapsed.scanRemaining == before.scanRemaining - 1);
    CHECK(elapsed.memoryRemaining == before.memoryRemaining - 1);
    const auto frozen = world.CaptureActorController(npc);
    REQUIRE(world.UpdateRequest(request, {{}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(100);
    CHECK(world.CaptureActorController(npc) == frozen);
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(100);
    CHECK(world.InspectActorAi(npc).target == 0);
    Load(world);
    const auto restored = world.FindActor(id);
    REQUIRE(restored != ActorHandle{});
    CHECK(world.CaptureActorController(restored) == frozen);
    CHECK(world.InspectActorAi(restored).scanRemaining == elapsed.scanRemaining);
    CHECK(world.InspectActorAi(restored).memoryRemaining == elapsed.memoryRemaining);
    CHECK(world.CheckInvariants());
}

ContentCatalog ProfileContent(bool named) {
    const ContentSource sources[] = {
        {"terrain", R"({format_version:1,definitions:[
          {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,open_space:true,
            appearance:{symbol:' ',foreground:'white'}},
          {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
            appearance:{symbol:'.',foreground:'white'}},
          {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
            appearance:{symbol:'#',foreground:'white'}}]})"},
        {"profiles", named ? "{format_version:1,ai_profiles:[{id:'test_sighted'}]}"
            : "{format_version:1,ai_profiles:[]}"}
    };
    return Take(LoadContent({sources, 2}));
}

struct SaveDirectory {
    std::filesystem::path directory;
    std::string database;
    SaveDirectory() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-ai-world-" + std::to_string(stamp)
                + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) { break; }
        }
        database = (directory / "world.sqlite").string();
    }
    ~SaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

TEST_CASE("Named sighted profiles round trip and missing content rejects load atomically", "[ai][world][save]") {
    const auto content = ProfileContent(true);
    const auto missing = ProfileContent(false);
    SaveDirectory files;
    World world(content);
    const auto request = Load(world);
    const auto npc = Create(world, {1, 1});
    const auto player = Create(world, {2, 1}, true);
    const auto id = world.GetActor(npc)->id;
    CHECK(world.SetActorAiProfile({}, "test_sighted") == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.SetActorAiProfile(player, "test_sighted") == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.SetActorController(npc, AiControllerKind::Sighted));
    CHECK(ValidateAiControllerContent(world.CaptureActorController(npc), missing));
    REQUIRE(world.SetActorAiProfile(npc, "test_sighted"));
    const auto envelope = world.CaptureActorController(npc);
    REQUIRE(envelope.Find("state"));
    REQUIRE(envelope.Find("state")->GetString("profile"));
    CHECK(*envelope.Find("state")->GetString("profile") == "test_sighted");
    CHECK_FALSE(world.SetActorAiProfile(npc, "missing"));
    CHECK_FALSE(world.SetActorAiProfile(npc, ""));
    CHECK(world.CaptureActorController(npc) == envelope);
    CHECK(ValidateAiControllerContent(envelope, content));
    CHECK_FALSE(ValidateAiControllerContent(envelope, missing));
    SessionState session;
    session.player = world.GetActor(player)->id;
    session.playerPosition = world.GetActor(player)->position;
    REQUIRE(world.CreateSave(files.database.c_str(), session));
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    Load(world);
    CHECK(world.CaptureActorController(world.FindActor(id)) == envelope);
    World restored(content);
    REQUIRE(restored.LoadSave(files.database.c_str()));
    Load(restored);
    CHECK(restored.CaptureActorController(restored.FindActor(id)) == envelope);
    World rejected(missing);
    CHECK(rejected.LoadSave(files.database.c_str()) == eastl::unexpected(SaveError::InvalidData));
    CHECK_FALSE(rejected.HasSave());
    CHECK(rejected.LoadedChunks().empty());
    CHECK(rejected.CheckInvariants());
    SaveDatabase database(missing);
    CHECK(database.Open(files.database.c_str()) == eastl::unexpected(SaveError::InvalidData));
    CHECK(world.CheckInvariants());
}
}
