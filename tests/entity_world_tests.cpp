#include "core/world.h"
#include "core/simulation.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/sort.h>
#include <EASTL/utility.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

struct SaveDirectory {
    std::filesystem::path directory;
    std::string database;

    SaveDirectory() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-entity-world-save-" + std::to_string(stamp)
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

ContentCatalog EntityContent() {
    constexpr ContentSource source{"entities", R"({format_version:1,definitions:[
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,open_space:true,
        appearance:{symbol:' ',foreground:'white'}},
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
        appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
        appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'stairs',name:'Stairs',movement:{passable:true},blocks_sight:false,stairs:true,
        appearance:{symbol:'>',foreground:'white'}},
      {type:'item',id:'input',name:'Input',mass_g:10,volume_ml:10,capabilities:[
        {id:'storage',type:'container',capacity_volume_ml:1000}]},
      {type:'item',id:'output',name:'Output',mass_g:100,volume_ml:10}
    ],objects:[
      {id:'idle',name:'Idle',behavior:'none'},
      {id:'press',name:'Press',behavior:'crafting_machine',machine:{
        input:'input',input_count:2,output:'output',output_count:2,
        duration_ticks:10,energy_capacity:100,energy_per_tick:3}}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

template<class T>
T Take(WorldResult<T> result) {
    REQUIRE(result);
    return eastl::move(*result);
}

LoadRequest Load(World& world, ChunkCoord coord = {}) {
    const auto request = Take(world.RequestChunks({coord, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            REQUIRE(world.SetTile({coord.x * chunk_size + x, coord.z * chunk_size + z, coord.y},
                {world.GetContent().FindTerrain("floor"), 0}));
        }
    }
    return request;
}

struct Rig {
    ContentCatalog content = EntityContent();
    World world{content};
    LoadRequest request = Load(world);
    ActorHandle player = Take(world.CreateActor({2, 2}, true));
    ObjectHandle machine = Take(world.CreateObject(content.FindObject("press"), {3, 2}));

    ItemHandle Input(ItemLocation location) { return Take(world.CreateItem(content.FindItem("input"), location)); }
    void Inputs() {
        Input(ItemLocation::Carried(player));
        Input(ItemLocation::Carried(player));
    }
    ObjectStateView State() const {
        const auto state = world.DescribeObjectState(machine);
        REQUIRE(state);
        return *state;
    }
    void Use(ObjectAction action) {
        const auto result = world.UseObject(player, machine, action);
        REQUIRE(result.outcome == ObjectUseOutcome::Succeeded);
        CHECK(result.baseCost == 100);
    }
    void Reload() {
        const auto playerId = world.GetActor(player)->id;
        const auto machineId = world.GetObject(machine)->id;
        REQUIRE(world.ReleaseRequest(request));
        REQUIRE(world.ReconcileChunks());
        CHECK(world.GetActor(player) == nullptr);
        CHECK(world.GetObject(machine) == nullptr);
        world.ElapseTicks(1000);
        request = Take(world.RequestChunks({{}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        player = world.FindActor(playerId);
        machine = world.FindObject(machineId);
        REQUIRE(player.value != 0);
        REQUIRE(machine.value != 0);
        CHECK(world.CheckInvariants());
    }
};

TEST_CASE("World machine inputs are atomic direct leaves selected by persistent ID", "[world][entities][objects]") {
    Rig rig;
    auto& world = rig.world;
    const auto parent = rig.Input(ItemLocation::Carried(rig.player));
    const auto nested = rig.Input(ItemLocation::Inside(parent));
    const auto low = rig.Input(ItemLocation::Carried(rig.player));
    const auto before = world.CaptureObjectState(rig.machine);
    auto result = world.UseObject(rig.player, rig.machine, ObjectAction::Start);
    CHECK(result.outcome == ObjectUseOutcome::Refused);
    CHECK(result.reason == ObjectUseReason::NoInputs);
    CHECK(result.baseCost == 0);
    CHECK(world.GetItem(low) != nullptr);
    CHECK(world.GetItem(nested) != nullptr);
    CHECK(world.CaptureObjectState(rig.machine) == before);
    const auto middle = rig.Input(ItemLocation::Carried(rig.player));
    const auto high = rig.Input(ItemLocation::Carried(rig.player));
    // Change inventory order without changing IDs.
    REQUIRE(world.TransferItem(low, ItemLocation::Ground({2, 2})));
    REQUIRE(world.TransferItem(low, ItemLocation::Carried(rig.player)));
    rig.Use(ObjectAction::Start);
    CHECK(world.GetItem(low) == nullptr);
    CHECK(world.GetItem(middle) == nullptr);
    CHECK(world.GetItem(high) != nullptr);
    CHECK(world.GetItem(parent) != nullptr);
    CHECK(world.GetItem(nested) != nullptr);
    CHECK(world.GetActorRemainingTicks(rig.player) == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Object callbacks keep transactions bound to their own object", "[world][entities][objects]") {
    Rig rig;
    auto& world = rig.world;
    const auto otherActor = Take(world.CreateActor({9, 10}, true));
    const auto otherMachine = Take(world.CreateObject(rig.content.FindObject("press"), {10, 10}));
    rig.Inputs();
    rig.Input(ItemLocation::Carried(otherActor));
    rig.Input(ItemLocation::Carried(otherActor));

    // Invoke in reverse identity order; neither access checks nor item ownership
    // may depend on the object used by the previous callback.
    REQUIRE(world.UseObject(otherActor, otherMachine, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    rig.Use(ObjectAction::Start);
    CHECK(world.GetActor(otherActor)->inventory.empty());
    CHECK(world.GetActor(rig.player)->inventory.empty());
    CHECK(world.UseObject(otherActor, otherMachine, ObjectAction::Start).reason == ObjectUseReason::Busy);
    world.ElapseTicks(10);
    REQUIRE(world.ProcessObjectEvents());
    CHECK_FALSE(world.ProcessObjectEvents());
    REQUIRE(world.GetObject(rig.machine)->contents.size() == 2);
    REQUIRE(world.GetObject(otherMachine)->contents.size() == 2);
    const ItemHandle firstOutput = world.GetObject(rig.machine)->contents.front();
    const ItemHandle secondOutput = world.GetObject(otherMachine)->contents.front();
    CHECK(world.GetItem(firstOutput)->location == ItemLocation::Stored(rig.machine));
    CHECK(world.GetItem(secondOutput)->location == ItemLocation::Stored(otherMachine));
    CHECK(world.GetItem(firstOutput)->id < world.GetItem(secondOutput)->id);
    CHECK(world.UseObject(rig.player, otherMachine, ObjectAction::Collect).outcome == ObjectUseOutcome::Inaccessible);
    REQUIRE(world.UseObject(otherActor, otherMachine, ObjectAction::Collect).outcome == ObjectUseOutcome::Succeeded);
    CHECK(world.GetItem(secondOutput)->location == ItemLocation::Carried(otherActor));
    CHECK(world.GetItem(firstOutput)->location == ItemLocation::Stored(rig.machine));
    rig.Use(ObjectAction::Collect);
    CHECK(world.GetItem(firstOutput)->location == ItemLocation::Carried(rig.player));
    CHECK(world.CheckInvariants());
}

TEST_CASE("World machine pauses resumes and atomically collects within aggregate carry capacity", "[world][entities][objects]") {
    Rig rig;
    rig.Inputs();
    rig.Use(ObjectAction::Start);
    rig.world.ElapseTicks(3);
    CHECK(rig.State().remainingWork == 7);
    CHECK(rig.State().energy == 91);
    rig.Use(ObjectAction::Stop);
    const auto stopped = rig.world.CaptureObjectState(rig.machine);
    rig.world.ElapseTicks(100);
    CHECK(rig.world.CaptureObjectState(rig.machine) == stopped);
    CHECK_FALSE(rig.world.NextObjectEventIn());
    rig.Use(ObjectAction::Start); // Resume needs no second recipe.
    REQUIRE(rig.world.SetObjectEnabled(rig.machine, false));
    rig.world.ElapseTicks(100);
    CHECK(rig.State().running);
    CHECK(rig.State().remainingWork == 7);
    REQUIRE(rig.world.SetObjectEnabled(rig.machine, true));
    rig.world.ElapseTicks(7);
    CHECK(rig.world.NextObjectEventIn() == eastl::optional<Tick>{0});
    REQUIRE(rig.world.ProcessObjectEvents());
    CHECK_FALSE(rig.world.ProcessObjectEvents());
    CHECK_FALSE(rig.State().hasJob);
    CHECK(rig.world.GetObject(rig.machine)->updates == 1);
    CHECK(rig.world.GetObject(rig.machine)->contents.size() == 2);
    REQUIRE(rig.world.SetActorCarryCapacity(rig.player, 150));
    const auto refused = rig.world.UseObject(rig.player, rig.machine, ObjectAction::Collect);
    CHECK(refused.reason == ObjectUseReason::NoCapacity);
    CHECK(refused.baseCost == 0);
    CHECK(rig.world.GetActor(rig.player)->inventory.empty());
    CHECK(rig.world.GetObject(rig.machine)->contents.size() == 2);
    REQUIRE(rig.world.SetActorCarryCapacity(rig.player, 200));
    rig.Use(ObjectAction::Collect);
    CHECK(rig.world.GetObject(rig.machine)->contents.empty());
    CHECK(rig.world.GetCarriedMass(rig.player) == 200);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("World machine energy exhaustion requires explicit reactivation and preserves pending completion", "[world][entities][objects]") {
    Rig rig;
    rig.Inputs();
    REQUIRE(rig.world.SetObjectEnergy(rig.machine, 12));
    rig.Use(ObjectAction::Start);
    rig.world.ElapseTicks(4);
    CHECK(rig.State().remainingWork == 6);
    CHECK_FALSE(rig.State().running);
    CHECK_FALSE(rig.world.NextObjectEventIn());
    rig.Reload();
    CHECK(rig.State().remainingWork == 6);
    REQUIRE(rig.world.SetObjectEnergy(rig.machine, 18));
    CHECK_FALSE(rig.State().running);
    CHECK_FALSE(rig.world.NextObjectEventIn());
    const auto before = rig.world.CaptureObjectState(rig.machine);
    CHECK_FALSE(rig.world.SetObjectEnergy(rig.machine, 101));
    CHECK(rig.world.CaptureObjectState(rig.machine) == before);
    rig.Use(ObjectAction::Start);
    rig.world.ElapseTicks(6);
    CHECK(rig.State().energy == 0);
    CHECK(rig.State().remainingWork == 0);
    REQUIRE(rig.world.SetObjectEnabled(rig.machine, false));
    rig.Reload();
    CHECK_FALSE(rig.world.ProcessObjectEvents());
    CHECK(rig.State().hasJob);
    REQUIRE(rig.world.SetObjectEnabled(rig.machine, true));
    REQUIRE(rig.world.ProcessObjectEvents());
    CHECK_FALSE(rig.world.ProcessObjectEvents());
    rig.Reload();
    CHECK_FALSE(rig.world.ProcessObjectEvents());
    CHECK(rig.world.GetObject(rig.machine)->contents.size() == 2);
    CHECK(rig.world.GetObject(rig.machine)->updates == 1);
}

TEST_CASE("All World object actions revalidate actor residency parking distance and reject vertical reach", "[world][entities][objects]") {
    Rig rig;
    rig.Inputs();
    CHECK(rig.world.UseObject({}, rig.machine, ObjectAction::Start).outcome == ObjectUseOutcome::Inaccessible);
    CHECK(rig.world.UseObject(rig.player, {}, ObjectAction::Start).outcome == ObjectUseOutcome::Inaccessible);
    const auto state = rig.world.CaptureObjectState(rig.machine);
    const auto inaccessible = [&] {
        for (const auto action : {ObjectAction::Start, ObjectAction::Stop, ObjectAction::Collect}) {
            const auto result = rig.world.UseObject(rig.player, rig.machine, action);
            CHECK(result.outcome == ObjectUseOutcome::Inaccessible);
            CHECK(result.baseCost == 0);
        }
        CHECK(rig.world.CaptureObjectState(rig.machine) == state);
        CHECK(rig.world.GetActor(rig.player)->inventory.size() == 2);
    };
    REQUIRE(rig.world.MoveActor(rig.player, {1, 2}));
    inaccessible();
    REQUIRE(rig.world.MoveActor(rig.player, {2, 2}));
    REQUIRE(rig.world.SetActorParked(rig.player, true));
    inaccessible();
    REQUIRE(rig.world.SetActorParked(rig.player, false));
    REQUIRE(rig.world.UpdateRequest(rig.request, {{}, 0}, LoadMode::resident));
    REQUIRE(rig.world.ReconcileChunks());
    inaccessible();
    REQUIRE(rig.world.UpdateRequest(rig.request, {{}, 0}, LoadMode::active));
    REQUIRE(rig.world.ReconcileChunks());
    REQUIRE(rig.world.SetTile({3, 2, 1}, {rig.content.FindTerrain("floor"), 0}));
    REQUIRE(rig.world.MoveActor(rig.player, {3, 2, 1}));
    inaccessible(); // Solid floor separates the vertical neighbors.
    REQUIRE(rig.world.SetTile({3, 2, 0}, {rig.content.FindTerrain("stairs"), 0}));
    REQUIRE(rig.world.SetTile({3, 2, 1}, {rig.content.FindTerrain("stairs"), 0}));
    inaccessible(); // Stairs never permit cross-layer interaction.
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("World object access rejects open vertical faces but excludes resident-only objects", "[world][entities][objects]") {
    Rig rig;
    rig.Inputs();
    REQUIRE(rig.world.SetTile({2, 2, 1}, {rig.content.FindTerrain("air"), 0}));
    const auto upper = Take(rig.world.CreateObject(rig.content.FindObject("press"), {2, 2, 1}));
    CHECK(rig.world.UseObject(rig.player, upper, ObjectAction::Start).outcome == ObjectUseOutcome::Inaccessible);
    const auto request = Take(rig.world.RequestChunks({{1, 0}, 0}, LoadMode::resident));
    REQUIRE(rig.world.ReconcileChunks());
    REQUIRE(rig.world.MoveActor(rig.player, {15, 2}));
    const auto resident = Take(rig.world.CreateObject(rig.content.FindObject("press"), {16, 2}));
    for (const auto action : {ObjectAction::Start, ObjectAction::Stop, ObjectAction::Collect}) {
        CHECK(rig.world.UseObject(rig.player, resident, action).outcome == ObjectUseOutcome::Inaccessible);
    }
    REQUIRE(rig.world.ReleaseRequest(request));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("World controller absence is idle and controller envelopes survive reload", "[world][entities][controllers]") {
    Rig rig;
    const auto npc = Take(rig.world.CreateActor({6, 6}));
    const auto id = rig.world.GetActor(npc)->id;
    REQUIRE(rig.world.SetActorController(npc, AiControllerKind::None));
    REQUIRE(rig.world.SetActorParked(rig.player, true));
    CHECK_FALSE(rig.world.NextActorEventIn());
    Tick tick = 0;
    std::uint32_t rng = 0;
    CHECK(AdvanceUntilInput(rig.world, tick, rng, 100).stop == SimulationStop::NoEvents);
    CHECK(rng == 0);
    const auto none = rig.world.CaptureActorController(npc);
    rig.Reload();
    const auto restored = rig.world.FindActor(id);
    CHECK(rig.world.CaptureActorController(restored) == none);
    REQUIRE(rig.world.SetActorController(restored, AiControllerKind::Wander));
    rig.world.ProcessNpcActions(rng);
    CHECK(rng == 1'013'904'223U);
    CHECK(rig.world.GetActor(restored)->turns == 1);
    CHECK(rig.world.GetActor(restored)->position == TileCoord{7, 6});
    const auto wander = rig.world.CaptureActorController(restored);
    rig.Reload();
    CHECK(rig.world.CaptureActorController(rig.world.FindActor(id)) == wander);
    CHECK_FALSE(rig.world.SetActorController(rig.player, AiControllerKind::Wander));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("World disk checkpoints restore machine progress and controller deadlines without catch-up",
          "[world][entities][save][objects][controllers]") {
    SaveDirectory files;
    Rig rig;
    const auto npc = Take(rig.world.CreateActor({6, 6}));
    AiControllerKind kind = AiControllerKind::None;
    SECTION("None remains idle") { kind = AiControllerKind::None; }
    SECTION("Wander resumes at its saved deadline") { kind = AiControllerKind::Wander; }
    REQUIRE(rig.world.SetActorController(npc,
        kind == AiControllerKind::None ? AiControllerKind::Wander : AiControllerKind::None));
    rig.Inputs();
    rig.Use(ObjectAction::Start);
    const auto playerId = rig.world.GetActor(rig.player)->id;
    const auto machineId = rig.world.GetObject(rig.machine)->id;
    const auto npcId = rig.world.GetActor(npc)->id;
    SessionState session;
    session.player = playerId;
    session.playerPosition = {2, 2};
    REQUIRE(rig.world.CreateSave(files.database.c_str(), session));

    // Mutate a clean checkpoint so persistence must capture dirty runtime state.
    REQUIRE(rig.world.SetObjectEnergy(rig.machine, 45));
    REQUIRE(rig.world.SetActorController(npc, kind));
    REQUIRE(rig.world.ScheduleActorAction(rig.player));
    REQUIRE(rig.world.ScheduleActorAction(npc));
    rig.world.ElapseTicks(3);
    session.tick = 3;
    const auto machineState = rig.world.CaptureObjectState(rig.machine);
    const auto controllerState = rig.world.CaptureActorController(npc);
    const Tick npcDelay = kind == AiControllerKind::None ? 100 : 97;
    CHECK(rig.State().remainingWork == 7);
    CHECK(rig.State().energy == 36);
    REQUIRE(rig.world.Save(session));

    World restored(rig.content);
    const auto loaded = restored.LoadSave(files.database.c_str());
    REQUIRE(loaded);
    CHECK(loaded->tick == session.tick);
    CHECK(loaded->rngState == session.rngState);
    CHECK(loaded->player == playerId);
    CHECK(restored.LoadedChunks().empty());
    restored.ElapseTicks(10000); // Time before residency cannot catch up disk jobs.
    const auto request = Take(restored.RequestChunks({{}, 0}, LoadMode::resident));
    REQUIRE(restored.ReconcileChunks());
    const auto player = restored.FindActor(playerId);
    const auto machine = restored.FindObject(machineId);
    const auto actor = restored.FindActor(npcId);
    REQUIRE(restored.GetActor(player));
    REQUIRE(restored.GetActor(actor));
    REQUIRE(restored.GetObject(machine));
    restored.ElapseTicks(10000); // Resident-only time is frozen as well.
    REQUIRE(restored.UpdateRequest(request, {{}, 0}, LoadMode::active));
    REQUIRE(restored.ReconcileChunks());
    CHECK(restored.CaptureObjectState(machine) == machineState);
    CHECK(restored.CaptureActorController(actor) == controllerState);
    CHECK(restored.GetActorRemainingTicks(player) == 97);
    CHECK(restored.GetActorRemainingTicks(actor) == npcDelay);
    CHECK(restored.GetActor(actor)->position == TileCoord{6, 6});
    CHECK(restored.GetActor(actor)->turns == 1);
    CHECK(restored.GetObject(machine)->updates == 0);
    CHECK(restored.GetObject(machine)->contents.empty());
    CHECK(restored.NextObjectEventIn() == eastl::optional<Tick>{7});
    CHECK_FALSE(restored.ProcessObjectEvents());
    auto rng = loaded->rngState;
    restored.ProcessNpcActions(rng);
    CHECK(rng == session.rngState);

    restored.ElapseTicks(6);
    CHECK_FALSE(restored.ProcessObjectEvents());
    CHECK(restored.GetObject(machine)->contents.empty());
    CHECK(restored.NextObjectEventIn() == eastl::optional<Tick>{1});
    restored.ElapseTicks(1);
    REQUIRE(restored.ProcessObjectEvents());
    CHECK_FALSE(restored.ProcessObjectEvents());
    const auto completed = restored.DescribeObjectState(machine);
    REQUIRE(completed);
    CHECK_FALSE(completed->hasJob);
    CHECK(completed->energy == 15);
    CHECK(restored.GetObject(machine)->updates == 1);
    REQUIRE(restored.GetObject(machine)->contents.size() == 2);
    Vector<PersistentId> outputIds;
    for (const auto item : restored.GetObject(machine)->contents) {
        REQUIRE(restored.GetItem(item));
        CHECK(restored.GetItem(item)->type == rig.content.FindItem("output"));
        CHECK(restored.GetItem(item)->location == ItemLocation::Stored(machine));
        outputIds.push_back(restored.GetItem(item)->id);
    }
    CHECK(outputIds[0] != outputIds[1]);
    CHECK(restored.GetActorRemainingTicks(player) == 90);
    CHECK(restored.GetActorRemainingTicks(actor) == (kind == AiControllerKind::None ? 100 : 90));
    restored.ElapseTicks(90);
    restored.ProcessNpcActions(rng);
    if (kind == AiControllerKind::None) {
        CHECK(rng == 0);
        CHECK(restored.GetActor(actor)->position == TileCoord{6, 6});
        CHECK(restored.GetActor(actor)->turns == 1);
        CHECK(restored.GetActorRemainingTicks(actor) == 100);
    } else {
        CHECK(rng == 1'013'904'223U);
        CHECK(restored.GetActor(actor)->position == TileCoord{7, 6});
        CHECK(restored.GetActor(actor)->turns == 2);
        CHECK(restored.GetActorRemainingTicks(actor) == 100);
    }
    restored.ProcessNpcActions(rng);
    CHECK(restored.GetActor(actor)->turns == (kind == AiControllerKind::None ? 1 : 2));
    CHECK_FALSE(restored.ProcessObjectEvents());
    CHECK(restored.CheckInvariants());

    session.tick += 97;
    session.rngState = rng;
    REQUIRE(restored.Save(session));
    World checkpoint(rig.content);
    REQUIRE(checkpoint.LoadSave(files.database.c_str()));
    Take(checkpoint.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(checkpoint.ReconcileChunks());
    const auto savedMachine = checkpoint.FindObject(machineId);
    REQUIRE(checkpoint.GetObject(savedMachine));
    CHECK(checkpoint.GetObject(savedMachine)->updates == 1);
    REQUIRE(checkpoint.GetObject(savedMachine)->contents.size() == 2);
    for (const auto id : outputIds) {
        const auto item = checkpoint.FindItem(id);
        REQUIRE(checkpoint.GetItem(item));
        CHECK(checkpoint.GetItem(item)->location == ItemLocation::Stored(savedMachine));
    }
    CHECK_FALSE(checkpoint.NextObjectEventIn());
    CHECK_FALSE(checkpoint.ProcessObjectEvents());
    CHECK(checkpoint.CheckInvariants());
}

TEST_CASE("World disk checkpoints preserve disabled zero-work machine completion exactly once",
          "[world][entities][save][objects]") {
    SaveDirectory files;
    Rig rig;
    rig.Inputs();
    rig.Use(ObjectAction::Start);
    const auto machineId = rig.world.GetObject(rig.machine)->id;
    SessionState session;
    session.player = rig.world.GetActor(rig.player)->id;
    session.playerPosition = {2, 2};
    REQUIRE(rig.world.CreateSave(files.database.c_str(), session));
    REQUIRE(rig.world.SetObjectEnergy(rig.machine, 30));
    rig.world.ElapseTicks(10);
    REQUIRE(rig.world.SetObjectEnabled(rig.machine, false));
    CHECK(rig.State().hasJob);
    CHECK(rig.State().remainingWork == 0);
    CHECK(rig.State().energy == 0);
    const auto pending = rig.world.CaptureObjectState(rig.machine);
    session.tick = 10;
    REQUIRE(rig.world.Save(session));

    World restored(rig.content);
    REQUIRE(restored.LoadSave(files.database.c_str()));
    restored.ElapseTicks(10000);
    Take(restored.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(restored.ReconcileChunks());
    const auto machine = restored.FindObject(machineId);
    REQUIRE(restored.GetObject(machine));
    CHECK_FALSE(restored.GetObject(machine)->enabled);
    CHECK(restored.CaptureObjectState(machine) == pending);
    restored.ElapseTicks(10000);
    CHECK_FALSE(restored.NextObjectEventIn());
    CHECK_FALSE(restored.ProcessObjectEvents());
    CHECK(restored.CaptureObjectState(machine) == pending);
    CHECK(restored.GetObject(machine)->contents.empty());
    CHECK(restored.GetObject(machine)->updates == 0);
    REQUIRE(restored.SetObjectEnabled(machine, true));
    CHECK(restored.NextObjectEventIn() == eastl::optional<Tick>{0});
    REQUIRE(restored.ProcessObjectEvents());
    CHECK_FALSE(restored.ProcessObjectEvents());
    const auto completed = restored.DescribeObjectState(machine);
    REQUIRE(completed);
    CHECK_FALSE(completed->hasJob);
    CHECK(completed->energy == 0);
    CHECK(restored.GetObject(machine)->updates == 1);
    REQUIRE(restored.GetObject(machine)->contents.size() == 2);
    for (const auto item : restored.GetObject(machine)->contents) {
        REQUIRE(restored.GetItem(item));
        CHECK(restored.GetItem(item)->type == rig.content.FindItem("output"));
        CHECK(restored.GetItem(item)->location == ItemLocation::Stored(machine));
    }
    restored.ElapseTicks(10000);
    CHECK_FALSE(restored.ProcessObjectEvents());
    CHECK(restored.GetObject(machine)->updates == 1);
    CHECK(restored.GetObject(machine)->contents.size() == 2);
    CHECK(restored.CheckInvariants());
}

TEST_CASE("World running machine jobs freeze in resident and archived chunks", "[world][entities][objects]") {
    Rig rig;
    rig.Inputs();
    rig.Use(ObjectAction::Start);
    rig.world.ElapseTicks(2);
    const auto state = rig.world.CaptureObjectState(rig.machine);
    REQUIRE(rig.world.UpdateRequest(rig.request, {{}, 0}, LoadMode::resident));
    REQUIRE(rig.world.ReconcileChunks());
    rig.world.ElapseTicks(1000);
    CHECK_FALSE(rig.world.ProcessObjectEvents());
    CHECK_FALSE(rig.world.NextObjectEventIn());
    CHECK(rig.world.CaptureObjectState(rig.machine) == state);
    rig.Reload();
    CHECK(rig.world.CaptureObjectState(rig.machine) == state);
    CHECK(rig.world.NextObjectEventIn() == eastl::optional<Tick>{8});
    rig.world.ElapseTicks(8);
    CHECK(rig.world.ProcessObjectEvents());
    CHECK(rig.world.GetObject(rig.machine)->contents.size() == 2);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("World gameplay steps validate movement and ready player collection orders only the barrier", "[world][entities][scheduling]") {
    Rig rig;
    auto& world = rig.world;
    const auto other = Take(world.CreateActor({4, 4}, true));
    Vector<ActorHandle> ready{ActorHandle{}};
    world.CollectReadyPlayers(ready);
    CHECK(ready == Vector<ActorHandle>{rig.player, other});
    REQUIRE(world.ScheduleActorAction(other));
    world.CollectReadyPlayers(ready);
    CHECK(ready == Vector<ActorHandle>{rig.player});
    CHECK(world.TryActorStep(rig.player, {1, 2}) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.TryActorStep(rig.player, {0, 0, 1}) == eastl::unexpected(WorldError::blocked));
    REQUIRE(world.SetTile({3, 2}, {rig.content.FindTerrain("wall"), 0}));
    CHECK(world.TryActorStep(rig.player, {1, 0}) == eastl::unexpected(WorldError::blocked));
    REQUIRE(world.SetTile({3, 2}, {rig.content.FindTerrain("floor"), 0}));
    REQUIRE(world.TryActorStep(rig.player, {1, 0}));
    CHECK(world.GetActor(rig.player)->position == TileCoord{3, 2});
    CHECK(world.GetActor(rig.player)->turns == 0);
    CHECK(world.GetActorRemainingTicks(rig.player) == 0);
    REQUIRE(world.MoveActor(other, {4, 2}));
    CHECK(world.TryActorStep(rig.player, {1, 0}) == eastl::unexpected(WorldError::occupied));
    REQUIRE(world.SetTile({3, 2}, {rig.content.FindTerrain("stairs"), 0}));
    REQUIRE(world.SetTile({3, 2, 1}, {rig.content.FindTerrain("stairs"), 0}));
    REQUIRE(world.TryActorStep(rig.player, {0, 0, 1}));
    CHECK(world.GetActor(rig.player)->position == TileCoord{3, 2, 1});
    CHECK(world.CheckInvariants());
}

TEST_CASE("World scheduler timing is partition invariant across reload order and reusable unordered snapshots", "[world][entities][scheduling]") {
    Rig first, second;
    const auto setup = [](Rig& rig) {
        rig.Inputs();
        rig.Use(ObjectAction::Start);
        Take(rig.world.CreateActor({6, 6}));
        Load(rig.world, {1, 0});
        Take(rig.world.CreateActor({20, 6}));
        REQUIRE(rig.world.SetActorParked(rig.player, true));
    };
    setup(first);
    setup(second);
    second.Reload(); // Reverses the two columns' actor slot insertion order.
    const Tick secondStart = second.world.GetTick();
    Tick a = 0, b = 0;
    std::uint32_t rngA = 17, rngB = 17;
    CHECK(AdvanceUntilInput(first.world, a, rngA, 250).stop == SimulationStop::LimitReached);
    for (Tick limit : {Tick{3}, Tick{10}, Tick{99}, Tick{100}, Tick{201}, Tick{250}}) {
        CHECK(AdvanceUntilInput(second.world, b, rngB, secondStart + limit).stop == SimulationStop::LimitReached);
    }
    CHECK(a == b - secondStart);
    CHECK(rngA == rngB);
    CHECK(first.world.CaptureObjectState(first.machine) == second.world.CaptureObjectState(second.machine));
    Vector<ActorHandle> actors;
    actors.reserve(32);
    const auto capacity = actors.capacity();
    first.world.CollectActiveActors(actors);
    const auto count = actors.size();
    first.world.CollectActiveActors(actors);
    CHECK(actors.capacity() == capacity);
    CHECK(actors.size() == count);
    for (const auto handle : actors) {
        const auto* actor = first.world.GetActor(handle);
        const auto* other = second.world.GetActor(second.world.FindActor(actor->id));
        REQUIRE(other);
        CHECK(actor->position == other->position);
        CHECK(first.world.GetActorRemainingTicks(handle)
            == second.world.GetActorRemainingTicks(second.world.FindActor(actor->id)));
        CHECK(actor->turns == other->turns);
    }
    Vector<ObjectHandle> objects{ObjectHandle{}};
    first.world.CollectActiveObjects(objects);
    CHECK(objects == Vector<ObjectHandle>{first.machine});
    first.world.CollectActiveObjects(objects);
    CHECK(objects.size() == 1);
    CHECK(first.world.CheckInvariants());
    CHECK(second.world.CheckInvariants());
}

TEST_CASE("World typed None objects are idle while generic objects retain legacy timers", "[world][entities][objects]") {
    Rig rig;
    const auto idle = Take(rig.world.CreateObject(rig.content.FindObject("idle"), {4, 2}));
    REQUIRE(rig.world.DescribeObjectState(idle));
    CHECK_FALSE(rig.world.NextObjectEventIn());
    const auto generic = Take(rig.world.CreateObject({5, 2}, true));
    CHECK_FALSE(rig.world.DescribeObjectState(generic));
    CHECK_FALSE(rig.world.DescribeObjectState({}));
    rig.world.ElapseTicks(100);
    CHECK(rig.world.ProcessObjectEvents());
    CHECK(rig.world.GetObject(generic)->updates == 1);
    CHECK(rig.world.GetObject(idle)->updates == 0);
    CHECK(rig.world.NextObjectEventIn() == eastl::optional<Tick>{100});
    CHECK_FALSE(rig.world.ProcessObjectEvents());
}

}
