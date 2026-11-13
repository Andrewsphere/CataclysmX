#include "actors/ai_controller.h"
#include "combat/combat_system.h"
#include "core/world.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <limits>

namespace {
using namespace cataclysmx;

template<class T, class Error>
T Take(eastl::expected<T, Error> result) {
    REQUIRE(result);
    return eastl::move(*result);
}

ContentCatalog SightedContent() {
    const ContentSource source{"sighted-fixture", R"({format_version:1,
      definitions:[
        {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,
          open_space:true,appearance:{symbol:' ',foreground:'white'}},
        {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
          appearance:{symbol:'.',foreground:'white'}},
        {type:'terrain',id:'stairs',name:'Stairs',movement:{passable:true},blocks_sight:false,
          stairs:true,appearance:{symbol:'>',foreground:'white'}},
        {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
          appearance:{symbol:'#',foreground:'white'}}],
      ai_profiles:[{id:'patient',candidate_budget:1,bucket_budget:2},
        {id:'near',sight_range:1,dim_range:1,dark_range:1},
        {id:'blind',dark_range:0}]
    })"};
    return Take(LoadContent({&source, 1}));
}

struct Rig {
    ContentCatalog content = SightedContent();
    World world{content};
    LoadRequest request;

    Rig() {
        request = Take(world.RequestChunks({{}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        // Clear generated roofs explicitly: skylight is an input to detection.
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = 0; x < chunk_size; ++x) {
                REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor"), 0}));
                for (int y = 1; y <= world_max_y; ++y) {
                    REQUIRE(world.SetTile({x, z, y}, {content.FindTerrain("air"), 0}));
                }
            }
        }
        world.SetSkyLight(255);
    }

    ActorHandle Create(TileCoord position, CombatFaction faction, bool player = false) {
        const auto handle = Take(world.CreateActor(position, player));
        REQUIRE(world.SetActorController(handle, AiControllerKind::None));
        auto combat = MakeCombatState(player);
        combat.faction = faction;
        REQUIRE(world.Combat().Configure(handle, combat));
        return handle;
    }

    void Wall(int x) {
        for (int z = 0; z < chunk_size; ++z) {
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("wall"), 0}));
        }
    }

    void Advance(AiController& controller, Tick elapsed) {
        world.ElapseTicks(elapsed);
        controller.Elapse(elapsed);
    }
};

eastl::unique_ptr<AiController> Controller(eastl::string_view profile = {}) {
    auto controller = Take(MakeAiController(AiControllerKind::Sighted));
    REQUIRE(controller);
    if (!profile.empty()) {
        auto state = controller->SaveState();
        state.SetString("profile", profile);
        controller = Take(MakeAiController(AiControllerKind::Sighted, &state));
    }
    return controller;
}

int Distance(TileCoord a, TileCoord b) {
    return std::abs(a.x - b.x) + std::abs(a.z - b.z) + std::abs(a.y - b.y);
}

void ExecuteMove(Rig& rig, ActorHandle self, const ActorIntent& intent) {
    REQUIRE(intent.kind == ActorIntentKind::Move);
    const auto position = rig.world.GetActor(self)->position;
    REQUIRE(rig.world.MoveActor(self, {position.x + intent.direction.x,
        position.z + intent.direction.z, position.y + intent.direction.y}));
}

TEST_CASE("Sighted detects a distant hostile and pursues without executing its intent", "[ai][sighted][controller]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto target = rig.Create({10, 2}, CombatFaction::Survivors);
    auto controller = Controller();
    const ActorReadContext context(rig.world, self);
    rig.Advance(*controller, 17);
    std::uint32_t rng = 42;
    const auto revision = rig.world.GetRevision();
    const auto intent = controller->ChooseAction(context, rng);
    CHECK(controller->Kind() == AiControllerKind::Sighted);
    CHECK(intent.kind == ActorIntentKind::Move);
    CHECK(intent.direction == TileCoord{1, 0});
    CHECK(rig.world.GetActor(self)->position == TileCoord{2, 2});
    CHECK(rig.world.GetRevision() == revision);
    const auto view = controller->Inspect();
    CHECK(view.behavior == AiBehavior::Pursue);
    CHECK(view.target == rig.world.GetActor(target)->id);
    CHECK(view.lastKnownPosition == TileCoord{10, 2});
    CHECK(view.observedTick == 17);
    CHECK(view.memoryRemaining == 1200);
    CHECK(view.targetVisible);
    ExecuteMove(rig, self, intent);
    rig.Advance(*controller, 100);
    CHECK(controller->ChooseAction(context, rng).kind == ActorIntentKind::Move);
    CHECK(controller->Inspect().observedTick == 117);
    CHECK(controller->Inspect().memoryRemaining == 1200);
}

TEST_CASE("Sighted pursues all eight bearings independently of identity tie phase", "[ai][sighted][movement]") {
    Rig rig;
    for (int identity = 0; identity < 20; ++identity) {
        for (int i = 0; i < 8; ++i) {
            CAPTURE(identity, i);
            const auto direction = actorStepDirections[i];
            const auto self = rig.Create({7, 7}, CombatFaction::Undead);
            const TileCoord goal{7 + 4 * direction.x, 7 + 4 * direction.z};
            const auto target = rig.Create(goal, CombatFaction::Survivors);
            auto controller = Controller();
            std::uint32_t rng = 42;
            const auto intent = controller->ChooseAction(ActorReadContext(rig.world, self), rng);
            REQUIRE(intent.kind == ActorIntentKind::Move);
            CHECK(intent.direction == direction);
            CHECK(intent.baseCost == 100);
            const auto decision = controller->Inspect().decision;
            CHECK(decision.valid);
            CHECK(decision.origin == TileCoord{7, 7});
            CHECK(decision.goal == goal);
            CHECK(decision.hasGoal);
            CHECK(decision.selectedMove == i);
            CHECK(decision.intent == intent.kind);
            CHECK(decision.moves[i] == AiMoveStatus::Available);
            REQUIRE(rig.world.DestroyActor(target));
            REQUIRE(rig.world.DestroyActor(self));
        }
        // An odd identity stride exercises both parities of the ten-way tie phase.
        const auto padding = rig.Create({7, 7}, CombatFaction::Neutral);
        REQUIRE(rig.world.DestroyActor(padding));
    }
}

TEST_CASE("Sighted immediately attacks every visible neighbor even in darkness", "[ai][sighted][combat][light]") {
    Rig rig;
    for (const auto light : {0, 255}) {
        rig.world.SetSkyLight(static_cast<std::uint8_t>(light));
        for (int i = 0; i < 8; ++i) {
            CAPTURE(light, i);
            const auto self = rig.Create({7, 7}, CombatFaction::Undead);
            // Exhaust background discovery's one candidate before the target.
            const auto target = rig.Create({7 + actorStepDirections[i].x,
                7 + actorStepDirections[i].z}, CombatFaction::Survivors);
            for (const auto profile : {"patient", "near", "blind"}) {
                CAPTURE(profile);
                auto controller = Controller(profile);
                std::uint32_t rng = 42;
                const auto intent = controller->ChooseAction(ActorReadContext(rig.world, self), rng);
                const bool visible = light != 0 || eastl::string_view(profile) != "blind";
                CHECK(controller->Inspect().targetVisible == visible);
                CHECK((intent.kind == ActorIntentKind::Attack) == visible);
                if (visible) { CHECK(intent.target == target); }
                const auto decision = controller->Inspect().decision;
                CHECK(decision.intent == intent.kind);
                CHECK(decision.moves[i] == AiMoveStatus::Occupied);
                if (visible) {
                    CHECK(decision.behavior == AiBehavior::Attack);
                    CHECK(decision.selectedMove == -1);
                }
            }
            REQUIRE(rig.world.DestroyActor(target));
            REQUIRE(rig.world.DestroyActor(self));
        }
    }
}

TEST_CASE("Sighted closes the forward gap against sideways and alternating targets", "[ai][sighted][movement]") {
    Rig rig;
    for (int identity = 0; identity < 20; ++identity) {
        for (int bearing = 0; bearing < 4; ++bearing) {
            for (const bool alternating : {false, true}) {
                CAPTURE(identity, bearing, alternating);
                const auto forward = actorStepDirections[bearing];
                const TileCoord sideways{-forward.z, forward.x};
                const auto self = rig.Create({7, 7}, CombatFaction::Undead);
                const auto target = rig.Create({7 + 5 * forward.x, 7 + 5 * forward.z}, CombatFaction::Survivors);
                auto controller = Controller();
                std::uint32_t rng = 42;
                for (int step = 0; step < 3; ++step) {
                    const int lateral = alternating ? (step % 2 == 0 ? 1 : -1) : step + 1;
                    const TileCoord goal{7 + 5 * forward.x + lateral * sideways.x,
                        7 + 5 * forward.z + lateral * sideways.z};
                    REQUIRE(rig.world.MoveActor(target, goal));
                    const auto before = rig.world.GetActor(self)->position;
                    const auto intent = controller->ChooseAction(ActorReadContext(rig.world, self), rng);
                    REQUIRE(intent.kind == ActorIntentKind::Move);
                    // A Manhattan tie used to allow mirroring lateral movement
                    // forever; every step must also close the forward gap.
                    CHECK(intent.direction.x * forward.x + intent.direction.z * forward.z == 1);
                    ExecuteMove(rig, self, intent);
                    CHECK(AiMovementScore(rig.world.GetActor(self)->position, goal) < AiMovementScore(before, goal));
                    rig.Advance(*controller, 100);
                }
                REQUIRE(rig.world.DestroyActor(target));
                REQUIRE(rig.world.DestroyActor(self));
            }
        }
        const auto padding = rig.Create({7, 7}, CombatFaction::Neutral);
        REQUIRE(rig.world.DestroyActor(padding));
    }
}

TEST_CASE("Sighted diagonal interaction still requires successful geometric detection", "[ai][sighted][combat][light]") {
    Rig rig;
    const auto self = rig.Create({7, 7}, CombatFaction::Undead);
    TileCoord position{8, 8};
    SECTION("opaque corner blocks an immediate dark diagonal") {
        rig.world.SetSkyLight(0);
        REQUIRE(rig.world.SetTile({8, 7}, {rig.content.FindTerrain("wall"), 0}));
    }
    SECTION("positive neighbor exception does not extend across layers") {
        position = {8, 7, 1};
        REQUIRE(rig.world.SetTile(position, {rig.content.FindTerrain("floor"), 0}));
    }
    rig.Create(position, CombatFaction::Survivors);
    auto controller = Controller("near");
    std::uint32_t rng = 42;
    const auto intent = controller->ChooseAction(ActorReadContext(rig.world, self), rng);
    CHECK(intent.kind != ActorIntentKind::Attack);
    CHECK_FALSE(controller->Inspect().targetVisible);
    CHECK(controller->Inspect().target == 0);
    if (position.y != 0) { CHECK(controller->Inspect().rays == 0); }
}

TEST_CASE("Sighted captured probes explain a blocked wait and release backtracking", "[ai][sighted][inspection]") {
    Rig rig;
    const auto self = rig.Create({7, 7}, CombatFaction::Undead);
    for (int i = 0; i < 8; ++i) {
        const auto direction = actorStepDirections[i];
        REQUIRE(rig.world.SetTile({7 + direction.x, 7 + direction.z},
            {rig.content.FindTerrain(i == 4 ? "floor" : "wall"), 0}));
    }
    auto controller = Controller();
    std::uint32_t rng = 42;
    const ActorReadContext context(rig.world, self);
    const auto first = controller->ChooseAction(context, rng);
    REQUIRE(first.direction == TileCoord{1, 1});
    ExecuteMove(rig, self, first);
    for (int i = 0; i < 8; ++i) {
        const auto direction = actorStepDirections[i];
        const TileCoord position{8 + direction.x, 8 + direction.z};
        if (position != TileCoord{7, 7}) {
            REQUIRE(rig.world.SetTile(position, {rig.content.FindTerrain("wall"), 0}));
        }
    }
    rig.Advance(*controller, 100);
    const auto wait = controller->ChooseAction(context, rng);
    REQUIRE(wait.kind == ActorIntentKind::Wait);
    const auto snapshot = CaptureAiController(controller.get());
    const auto evidence = controller->Inspect().decision;
    CHECK(evidence.tick == 100);
    CHECK(evidence.origin == TileCoord{8, 8});
    CHECK(evidence.previousPosition == TileCoord{7, 7});
    CHECK(evidence.hasPrevious);
    CHECK_FALSE(evidence.hasGoal);
    CHECK(evidence.intent == ActorIntentKind::Wait);
    CHECK(evidence.selectedMove == -1);
    CHECK(evidence.moves[6] == AiMoveStatus::Backtracking);
    CHECK(evidence.moves[0] == AiMoveStatus::Impassable);
    CHECK(evidence.moves[8] == AiMoveStatus::Unsupported);
    // Inspection cannot refresh probes after the world or clock changes.
    REQUIRE(rig.world.SetTile({9, 8}, {rig.content.FindTerrain("floor"), 0}));
    rig.Advance(*controller, 1);
    CHECK(controller->Inspect().decision.moves == evidence.moves);
    CHECK(controller->Inspect().decision.tick == evidence.tick);
    const auto afterElapsed = CaptureAiController(controller.get());
    controller->Inspect();
    CHECK(CaptureAiController(controller.get()) == afterElapsed);
    CHECK(snapshot != afterElapsed);
    REQUIRE(rig.world.SetTile({9, 8}, {rig.content.FindTerrain("wall"), 0}));
    const auto reverse = controller->ChooseAction(context, rng);
    CHECK(reverse.kind == ActorIntentKind::Move);
    CHECK(reverse.direction == TileCoord{-1, -1});
    CHECK_FALSE(controller->Inspect().decision.hasPrevious);
    CHECK(controller->Inspect().decision.moves[6] == AiMoveStatus::Available);
}

TEST_CASE("Sighted search records its remembered boundary without resensing on inspection", "[ai][sighted][inspection][memory]") {
    Rig rig;
    const auto self = rig.Create({7, 7}, CombatFaction::Undead);
    const auto target = rig.Create({8, 7}, CombatFaction::Survivors);
    auto controller = Controller();
    std::uint32_t rng = 42;
    const ActorReadContext context(rig.world, self);
    REQUIRE(controller->ChooseAction(context, rng).kind == ActorIntentKind::Attack);
    REQUIRE(rig.world.DestroyActor(target));
    REQUIRE(rig.world.MoveActor(self, {8, 7}));
    controller->ChooseAction(context, rng);
    REQUIRE(controller->Inspect().behavior == AiBehavior::Search);
    REQUIRE(rig.world.MoveActor(self, {10, 7}));
    const auto intent = controller->ChooseAction(context, rng);
    REQUIRE(intent.kind == ActorIntentKind::Move);
    const auto decision = controller->Inspect().decision;
    CHECK(decision.behavior == AiBehavior::Search);
    CHECK(decision.goal == TileCoord{8, 7});
    CHECK(decision.moves[0] == AiMoveStatus::OutsideSearch);
    CHECK(decision.moves[1] == AiMoveStatus::OutsideSearch);
    CHECK(decision.moves[4] == AiMoveStatus::OutsideSearch);
    CHECK(decision.moves[decision.selectedMove] == AiMoveStatus::Available);
    CHECK(Distance({10 + intent.direction.x, 7 + intent.direction.z}, decision.goal) <= 2);
    const auto saved = CaptureAiController(controller.get());
    controller->Inspect();
    controller->Inspect();
    CHECK(CaptureAiController(controller.get()) == saved);
}

TEST_CASE("Sighted pursuit retains paired stairs and records missing stair support", "[ai][sighted][movement][inspection]") {
    Rig rig;
    const auto self = rig.Create({7, 7}, CombatFaction::Undead);
    REQUIRE(rig.world.SetTile({7, 7}, {rig.content.FindTerrain("stairs"), 0}));
    REQUIRE(rig.world.SetTile({7, 7, 1}, {rig.content.FindTerrain("stairs"), 0}));
    const auto target = rig.Create({7, 7, 1}, CombatFaction::Survivors);
    auto controller = Controller();
    std::uint32_t rng = 42;
    const ActorReadContext context(rig.world, self);
    CHECK(controller->ChooseAction(context, rng).kind != ActorIntentKind::Attack);
    REQUIRE(controller->Inspect().targetVisible);
    CHECK(controller->Inspect().decision.moves[8] == AiMoveStatus::Occupied);
    REQUIRE(rig.world.DestroyActor(target));
    const auto up = controller->ChooseAction(context, rng);
    CHECK(up.kind == ActorIntentKind::Move);
    CHECK(up.direction == TileCoord{0, 0, 1});
    CHECK(controller->Inspect().decision.selectedMove == 8);
    REQUIRE(rig.world.SetTile({7, 7, 1}, {rig.content.FindTerrain("floor"), 0}));
    controller->ChooseAction(context, rng);
    CHECK(controller->Inspect().decision.moves[8] == AiMoveStatus::StairsRequired);
}

TEST_CASE("World executes sighted pursuit and commits an attack on an NPC", "[ai][sighted][world]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto target = rig.Create({6, 2}, CombatFaction::Survivors);
    auto combat = rig.world.GetActor(self)->combat;
    combat.accuracy = 100;
    REQUIRE(rig.world.Combat().Configure(self, combat));
    REQUIRE(rig.world.SetActorController(self, AiControllerKind::Sighted));
    const auto targetId = rig.world.GetActor(target)->id;
    const auto initialBody = rig.world.GetActor(target)->combat.body;
    std::uint32_t rng = 42;

    for (int step = 0; step < 3; ++step) {
        CAPTURE(step);
        const auto before = rig.world.GetActor(self)->position;
        rig.world.Combat().ProcessEvents(rng);
        rig.world.ProcessNpcActions(rng);
        const auto after = rig.world.GetActor(self)->position;
        CHECK(Distance(after, {6, 2}) == Distance(before, {6, 2}) - 1);
        CHECK(rig.world.GetActor(target)->position == TileCoord{6, 2});
        CHECK(rig.world.GetActor(target)->combat.body == initialBody);
        const auto view = rig.world.InspectActorAi(self);
        CHECK(view.target == targetId);
        CHECK(view.targetVisible);
        CHECK(view.lastKnownPosition == TileCoord{6, 2});
        CHECK(view.observedTick == rig.world.GetTick());
        CHECK(view.memoryRemaining == 1200);
        CHECK(view.behavior == AiBehavior::Pursue);
        CHECK(rig.world.GetActor(self)->actionDeadline.Remaining(rig.world.GetTick()) == 100);
        rig.world.ElapseTicks(100);
    }
    REQUIRE(Distance(rig.world.GetActor(self)->position, {6, 2}) == 1);
    REQUIRE(rig.world.GetActor(target)->combat.body == initialBody);
    rig.world.Combat().ProcessEvents(rng);
    rig.world.ProcessNpcActions(rng);
    const auto view = rig.world.InspectActorAi(self);
    CHECK(view.behavior == AiBehavior::Attack);
    CHECK(view.target == targetId);
    CHECK(view.targetVisible);
    CHECK(view.observedTick == rig.world.GetTick());
    CHECK(view.memoryRemaining == 1200);
    const auto feedback = rig.world.Combat().Describe(self).feedback;
    CHECK_FALSE(feedback.incoming);
    CHECK(feedback.result.committed);
    CHECK(feedback.result.outcome == AttackOutcome::Hit);
    CHECK(feedback.result.target == targetId);
    CHECK(feedback.result.injury > 0);
    CHECK(feedback.result.cost > 0);
    CHECK(rig.world.GetActor(self)->actionDeadline.Remaining(rig.world.GetTick()) == feedback.result.cost);
    REQUIRE(rig.world.GetActor(target));
    CHECK(rig.world.GetActor(target)->combat.body != initialBody);
    const auto received = rig.world.Combat().Describe(target).feedback;
    CHECK(received.incoming);
    CHECK(received.result.target == targetId);
    CHECK(received.result.injury > 0);
    rig.world.ElapseTicks(feedback.result.cost);
    rig.world.Combat().ProcessEvents(rng);
    CHECK(rig.world.GetActor(self)->actionDeadline.Remaining(rig.world.GetTick()) == 0);
    CHECK(rig.world.GetActor(self)->combat.action.kind == ActorActionKind::None);
}

TEST_CASE("Sighted locally detours around an occluding obstacle using remembered position", "[ai][sighted][controller]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto target = rig.Create({7, 2}, CombatFaction::Survivors);
    auto first = Controller();
    const ActorReadContext context(rig.world, self);
    std::uint32_t rng = 43;
    rig.Advance(*first, 10);
    REQUIRE(first->ChooseAction(context, rng).direction == TileCoord{1, 0});
    const auto known = first->Inspect();
    REQUIRE(known.targetVisible);
    const auto snapshot = CaptureAiController(first.get());
    auto second = Take(ReadAiController(snapshot));
    auto secondRng = rng;
    REQUIRE(rig.world.SetTile({3, 2}, {rig.content.FindTerrain("wall"), 0}));
    rig.Advance(*first, 100);
    second->Elapse(100);
    REQUIRE(rig.world.MoveActor(target, {8, 2}));
    const auto firstIntent = first->ChooseAction(context, rng);
    REQUIRE(rig.world.MoveActor(target, {9, 2}));
    const auto secondIntent = second->ChooseAction(context, secondRng);
    CHECK(firstIntent == secondIntent);
    CHECK(rng == secondRng);
    CHECK(CaptureAiController(first.get()) == CaptureAiController(second.get()));
    for (const auto* controller : {first.get(), second.get()}) {
        const auto view = controller->Inspect();
        CHECK_FALSE(view.targetVisible);
        CHECK(view.behavior == AiBehavior::Pursue);
        CHECK(view.target == known.target);
        CHECK(view.lastKnownPosition == known.lastKnownPosition);
        CHECK(view.observedTick == known.observedTick);
        CHECK(view.memoryRemaining == known.memoryRemaining - 100);
    }
    REQUIRE(firstIntent.kind == ActorIntentKind::Move);
    CHECK(firstIntent.direction != TileCoord{1, 0});
    CHECK(firstIntent.direction.x == 1);
    CHECK(std::abs(firstIntent.direction.z) == 1);
    ExecuteMove(rig, self, firstIntent);
    CHECK(Distance(rig.world.GetActor(self)->position, known.lastKnownPosition) <= 6);
}

TEST_CASE("Hidden movement and stale handles cannot refresh sighted memory", "[ai][sighted][memory]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto target = rig.Create({10, 2}, CombatFaction::Survivors);
    auto controller = Controller();
    const ActorReadContext context(rig.world, self);
    std::uint32_t rng = 61;
    rig.Advance(*controller, 23);
    controller->ChooseAction(context, rng);
    const auto known = controller->Inspect();
    REQUIRE(known.targetVisible);
    rig.Wall(6);
    SECTION("live target moves out of sight") {
        REQUIRE(rig.world.MoveActor(target, {11, 4}));
    }
    SECTION("destroyed target and reused storage") {
        REQUIRE(rig.world.DestroyActor(target));
        const auto replacement = rig.Create({11, 4}, CombatFaction::Survivors);
        CHECK(rig.world.GetActor(replacement)->id != known.target);
        CHECK(rig.world.GetActor(target) == nullptr);
    }
    for (int i = 1; i <= 4; ++i) {
        rig.Advance(*controller, 100);
        controller->ChooseAction(context, rng);
        const auto view = controller->Inspect();
        CHECK_FALSE(view.targetVisible);
        CHECK(view.target == known.target);
        CHECK(view.lastKnownPosition == known.lastKnownPosition);
        CHECK(view.observedTick == known.observedTick);
        CHECK(view.memoryRemaining == 1200 - Tick{100} * i);
        CHECK(view.behavior == AiBehavior::Pursue);
    }
    rig.Advance(*controller, 800);
    CHECK(controller->Inspect().target == 0);
    CHECK(controller->Inspect().behavior == AiBehavior::Idle);
    CHECK(controller->Inspect().memoryRemaining == 0);
}

TEST_CASE("Reaching an empty remembered location starts bounded search then forgets", "[ai][sighted][memory]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto target = rig.Create({5, 2}, CombatFaction::Survivors);
    auto controller = Controller();
    const ActorReadContext context(rig.world, self);
    std::uint32_t rng = 71;
    controller->ChooseAction(context, rng);
    const auto id = rig.world.GetActor(target)->id;
    REQUIRE(controller->Inspect().target == id);
    REQUIRE(rig.world.DestroyActor(target));
    // Only these explicit steps execute; sensing/Elapse cannot move the actor.
    for (int i = 0; i < 3; ++i) {
        const auto intent = controller->ChooseAction(context, rng);
        ExecuteMove(rig, self, intent);
        rig.Advance(*controller, 100);
    }
    REQUIRE(rig.world.GetActor(self)->position == TileCoord{5, 2});
    for (int i = 0; i < 3; ++i) {
        const auto intent = controller->ChooseAction(context, rng);
        CHECK(controller->Inspect().behavior == AiBehavior::Search);
        CHECK(controller->Inspect().target == id);
        CHECK_FALSE(controller->Inspect().targetVisible);
        ExecuteMove(rig, self, intent);
        CHECK(Distance(rig.world.GetActor(self)->position, {5, 2}) <= 2);
        rig.Advance(*controller, 100);
    }
    CHECK(controller->Inspect().behavior == AiBehavior::Idle);
    CHECK(controller->Inspect().target == 0);
    CHECK(controller->Inspect().memoryRemaining == 0);
}

TEST_CASE("Sighted allegiance selects NPC hostiles instead of privileging the player", "[ai][sighted][faction]") {
    Rig rig;
    CombatFaction own = CombatFaction::Survivors;
    CombatFaction enemy = CombatFaction::Undead;
    SECTION("survivor seeks undead") {}
    SECTION("undead seeks survivor") {
        own = CombatFaction::Undead;
        enemy = CombatFaction::Survivors;
    }
    const auto self = rig.Create({2, 2}, own);
    rig.Create({3, 2}, own, true);
    rig.Create({2, 3}, CombatFaction::Neutral);
    const auto target = rig.Create({7, 2}, enemy);
    auto controller = Controller();
    std::uint32_t rng = 83;
    const auto intent = controller->ChooseAction(ActorReadContext(rig.world, self), rng);
    CHECK(intent.kind != ActorIntentKind::Attack);
    CHECK(controller->Inspect().target == rig.world.GetActor(target)->id);
    CHECK(controller->Inspect().targetVisible);
}

TEST_CASE("Sighted detection uses canonical target light bands and their range edges", "[ai][sighted][light]") {
    Rig rig;
    const auto self = rig.Create({1, 2}, CombatFaction::Undead);
    // At each light boundary use a new controller, avoiding remembered contacts.
    struct LightCase { std::uint8_t level; LightBand band; int range; };
    const LightCase cases[] = {{0, LightBand::Dark, 1}, {15, LightBand::Dark, 1},
        {16, LightBand::Dim, 6}, {63, LightBand::Dim, 6},
        {64, LightBand::Ordinary, 12}, {191, LightBand::Ordinary, 12},
        {192, LightBand::Bright, 12}, {255, LightBand::Bright, 12}};
    const auto target = rig.Create({2, 2}, CombatFaction::Survivors);
    for (const auto& test : cases) {
        CAPTURE(test.level, test.range);
        rig.world.SetSkyLight(test.level);
        for (int distance : {test.range, test.range + 1}) {
            CAPTURE(distance);
            const TileCoord position{1 + distance, 2};
            REQUIRE(rig.world.MoveActor(target, position));
            REQUIRE(ClassifyLight(rig.world.GetLightLevel(position)) == test.band);
            auto controller = Controller();
            std::uint32_t rng = 97;
            controller->ChooseAction(ActorReadContext(rig.world, self), rng);
            CHECK(controller->Inspect().targetVisible == (distance <= test.range));
            CHECK(controller->Inspect().target == (distance <= test.range ? rig.world.GetActor(target)->id : 0));
        }
    }
}

TEST_CASE("Tracking leaves a bounded discovery allowance that reaches new contacts", "[ai][sighted][budget]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto distant = rig.Create({12, 2}, CombatFaction::Survivors);
    // Neutral bodies consume candidate work, but do not block optical rays.
    for (int x = 3; x <= 10; ++x) { rig.Create({x, 6}, CombatFaction::Neutral); }
    const auto close = rig.Create({5, 2}, CombatFaction::Survivors);
    auto first = Controller("patient");
    auto second = Controller("patient");
    const ActorReadContext context(rig.world, self);
    std::uint32_t rng = 103;
    std::uint32_t secondRng = rng;
    bool trackedDistant = false;
    bool foundClose = false;
    for (int scan = 0; scan < 14; ++scan) {
        CAPTURE(scan);
        CHECK(first->ChooseAction(context, rng) == second->ChooseAction(context, secondRng));
        const auto view = first->Inspect();
        CHECK(rng == secondRng);
        CHECK(CaptureAiController(first.get()) == CaptureAiController(second.get()));
        CHECK(view.candidates <= 1);
        CHECK(view.buckets <= 2);
        // Discovery plus tracking, eight local neighbors and remembered location.
        CHECK(view.rays <= 1 + 1 + 8 + 1);
        if (view.target == rig.world.GetActor(distant)->id) {
            trackedDistant = true;
            CHECK(view.targetVisible);
        }
        if (view.target == rig.world.GetActor(close)->id) { foundClose = true; }
        rig.Advance(*first, 100);
        second->Elapse(100);
    }
    CHECK(trackedDistant);
    CHECK(foundClose);
    CHECK(first->Inspect().target == rig.world.GetActor(close)->id);
    CHECK(rig.world.GetActor(self)->position == TileCoord{2, 2});
}

TEST_CASE("Sighted snapshots resume intents RNG and state across runtime handle changes", "[ai][sighted][save]") {
    Rig rig;
    auto self = rig.Create({2, 2}, CombatFaction::Undead);
    auto target = rig.Create({3, 2}, CombatFaction::Survivors);
    const auto selfId = rig.world.GetActor(self)->id;
    const auto targetId = rig.world.GetActor(target)->id;
    auto original = Controller("patient");
    std::uint32_t rng = 109;
    REQUIRE(original->ChooseAction(ActorReadContext(rig.world, self), rng).kind == ActorIntentKind::Attack);
    original->Elapse(37);
    const auto snapshot = CaptureAiController(original.get());
    REQUIRE(*snapshot.GetString("type") == "sighted");
    CHECK(ValidateAiControllerContent(snapshot, rig.content));
    auto restored = Take(ReadAiController(snapshot));
    CHECK(CaptureAiController(restored.get()) == snapshot);
    std::uint32_t restoredRng = rng;

    // Capture the next decision before residency changes, then replay it after.
    const auto expected = original->ChooseAction(ActorReadContext(rig.world, self), rng);
    const auto expectedState = CaptureAiController(original.get());
    const auto oldSelf = self;
    const auto oldTarget = target;
    REQUIRE(rig.world.ReleaseRequest(rig.request));
    REQUIRE(rig.world.ReconcileChunks());
    rig.request = Take(rig.world.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(rig.world.ReconcileChunks());
    self = rig.world.FindActor(selfId);
    target = rig.world.FindActor(targetId);
    REQUIRE(self != ActorHandle{});
    REQUIRE(target != ActorHandle{});
    REQUIRE(self != oldSelf);
    REQUIRE(target != oldTarget);
    const auto actual = restored->ChooseAction(ActorReadContext(rig.world, self), restoredRng);
    CHECK(actual.kind == expected.kind);
    CHECK(actual.direction == expected.direction);
    CHECK(actual.baseCost == expected.baseCost);
    REQUIRE(actual.target == target);
    CHECK(rig.world.GetActor(actual.target)->id == targetId);
    CHECK(rng == restoredRng);
    CHECK(CaptureAiController(restored.get()) == expectedState);

    // Continue through searching and expiry so resumption also checks RNG draws,
    // not just an attack path that can leave RNG unchanged.
    REQUIRE(rig.world.DestroyActor(target));
    REQUIRE(rig.world.MoveActor(self, {3, 2}));
    const ActorReadContext context(rig.world, self);
    for (int i = 0; i < 8; ++i) {
        CHECK(original->ChooseAction(context, rng) == restored->ChooseAction(context, restoredRng));
        CHECK(rng == restoredRng);
        CHECK(CaptureAiController(original.get()) == CaptureAiController(restored.get()));
        rig.Advance(*original, 100);
        restored->Elapse(100);
    }
    CHECK(original->Inspect().behavior == AiBehavior::Idle);
    CHECK(rng != 109);
}

TEST_CASE("Host elapsed subdivisions preserve sighted decisions at equal action boundaries", "[ai][sighted][timing]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto target = rig.Create({6, 2}, CombatFaction::Survivors);
    auto whole = Controller();
    auto split = Controller();
    const ActorReadContext context(rig.world, self);
    std::uint32_t rng = 127;
    std::uint32_t splitRng = rng;
    CHECK(whole->ChooseAction(context, rng) == split->ChooseAction(context, splitRng));
    SECTION("occluded pursuit and memory expiry") { rig.Wall(4); }
    SECTION("search and search expiry") {
        REQUIRE(rig.world.DestroyActor(target));
        REQUIRE(rig.world.MoveActor(self, {6, 2}));
        CHECK(whole->ChooseAction(context, rng) == split->ChooseAction(context, splitRng));
        REQUIRE(whole->Inspect().behavior == AiBehavior::Search);
    }
    const auto stationaryPosition = rig.world.GetActor(self)->position;
    for (const Tick elapsed : {Tick{99}, Tick{1}, Tick{137}, Tick{963}, Tick{60001}}) {
        CAPTURE(elapsed);
        // World advances only logical time, never ProcessNpcActions.
        rig.Advance(*whole, elapsed);
        split->Elapse(0);
        const Tick first = elapsed / 3;
        split->Elapse(first);
        split->Elapse(elapsed - first);
        CHECK(CaptureAiController(whole.get()) == CaptureAiController(split.get()));
        CHECK(whole->ChooseAction(context, rng) == split->ChooseAction(context, splitRng));
        CHECK(rng == splitRng);
        CHECK(CaptureAiController(whole.get()) == CaptureAiController(split.get()));
    }
    CHECK(whole->Inspect().target == 0);
    CHECK(rig.world.GetActor(self)->position == stationaryPosition);
}

TEST_CASE("Sighted attack intents respect self capability without losing the contact", "[ai][sighted][combat]") {
    Rig rig;
    const auto self = rig.Create({2, 2}, CombatFaction::Undead);
    const auto target = rig.Create({3, 3}, CombatFaction::Survivors);
    auto combat = MakeCombatState(false);
    combat.faction = CombatFaction::Undead;
    bool capable = true;
    SECTION("disabled simple apparatus can still bite") {
        combat.body.parts[static_cast<std::size_t>(BodyPart::AttackApparatus)].hp = 0;
        combat.body.parts[static_cast<std::size_t>(BodyPart::Locomotion)].hp = 0;
    }
    SECTION("detailed body with no functional attacking limbs") {
        combat.body = MakeBody(BodyKind::Detailed);
        for (const auto part : {BodyPart::LeftHand, BodyPart::RightHand, BodyPart::LeftLeg, BodyPart::RightLeg}) {
            ApplyInjury(combat.body, part, 100, combat.injury);
        }
        capable = false;
    }
    SECTION("detailed body retains a kick without hands") {
        combat.body = MakeBody(BodyKind::Detailed);
        ApplyInjury(combat.body, BodyPart::LeftHand, 100, combat.injury);
        ApplyInjury(combat.body, BodyPart::RightHand, 100, combat.injury);
    }
    REQUIRE(rig.world.Combat().Configure(self, combat));
    const auto before = rig.world.GetActor(self)->combat;
    CHECK_FALSE(rig.world.Combat().CanAttack({}));
    CHECK(rig.world.Combat().CanAttack(self) == capable);
    auto controller = Controller();
    std::uint32_t rng = 131;
    const auto intent = controller->ChooseAction(ActorReadContext(rig.world, self), rng);
    CHECK((intent.kind == ActorIntentKind::Attack) == capable);
    if (capable) { CHECK(intent.target == target); }
    CHECK(controller->Inspect().target == rig.world.GetActor(target)->id);
    CHECK(controller->Inspect().targetVisible);
    CHECK(rig.world.GetActor(self)->combat == before);
}

void CheckRejectedState(const DataObject& state, StateError expected = StateError::InvalidData) {
    Diagnostics diagnostics;
    const auto valid = ValidateAiControllerState(AiControllerKind::Sighted, state, &diagnostics);
    REQUIRE_FALSE(valid);
    CHECK(valid.error() == expected);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().back().location);
    CHECK(diagnostics.Frames().back().location->path.find("actors.controller.state") == 0);
    const auto made = MakeAiController(AiControllerKind::Sighted, &state);
    REQUIRE_FALSE(made);
    CHECK(made.error() == expected);
    DataObject envelope;
    envelope.SetString("type", "sighted");
    envelope.Set("state", state);
    const auto read = ReadAiController(envelope);
    REQUIRE_FALSE(read);
    CHECK(read.error() == expected);
}

TEST_CASE("Sighted saved state rejects malformed schema timers and inconsistent memory", "[ai][sighted][save]") {
    auto controller = Controller();
    auto state = controller->SaveState();
    StateError expected = StateError::InvalidData;
    SECTION("missing fields") { state = DataObject{}; }
    SECTION("wrong root") { state = DataObject::List(); }
    SECTION("extra field") { state.SetBoolean("extra", false); }
    SECTION("signed version") { state.SetSigned("version", 1); }
    SECTION("unsupported version") { state.SetUnsigned("version", 2); expected = StateError::UnsupportedVersion; }
    SECTION("zero version") { state.SetUnsigned("version", 0); expected = StateError::UnsupportedVersion; }
    SECTION("wrong boolean type") { state.SetUnsigned("started", 0); }
    SECTION("invalid profile identifier") { state.SetString("profile", "Bad Profile"); }
    SECTION("embedded null profile") { state.SetString("profile", eastl::string_view("a\0b", 3)); }
    SECTION("negative timer") { state.SetSigned("memory_remaining", -1); }
    SECTION("excess memory") { state.SetUnsigned("memory_remaining", 60001); }
    SECTION("excess scan interval") { state.SetUnsigned("scan_remaining", 60001); }
    SECTION("timer overflow") { state.SetUnsigned("scan_remaining", std::numeric_limits<std::uint64_t>::max()); }
    SECTION("search exceeds memory") { state.SetUnsigned("search_remaining", 1); }
    SECTION("unknown behavior") { state.SetUnsigned("behavior", 255); }
    SECTION("cursor out of bounds") { state.SetUnsigned("bucket", std::numeric_limits<std::uint64_t>::max()); }
    SECTION("unstarted cursor has continuation") { state.SetUnsigned("after", 1); }
    SECTION("unstarted cursor has bucket") { state.SetUnsigned("bucket", 1); }
    SECTION("target without memory") { state.SetUnsigned("target", 7); }
    SECTION("memory without target") { state.SetUnsigned("memory_remaining", 1); }
    SECTION("idle memory has observation tick") { state.SetUnsigned("observed_tick", 1); }
    SECTION("pursuit without target") { state.SetUnsigned("behavior", static_cast<unsigned>(AiBehavior::Pursue)); }
    SECTION("search without a search timer") {
        state.SetUnsigned("target", 7);
        state.SetUnsigned("memory_remaining", 100);
        state.SetUnsigned("behavior", static_cast<unsigned>(AiBehavior::Search));
    }
    SECTION("pursuit with a search timer") {
        state.SetUnsigned("target", 7);
        state.SetUnsigned("memory_remaining", 100);
        state.SetUnsigned("search_remaining", 10);
        state.SetUnsigned("behavior", static_cast<unsigned>(AiBehavior::Pursue));
    }
    CheckRejectedState(state, expected);
}

TEST_CASE("Sighted saved coordinates are strict signed bounded triples", "[ai][sighted][save]") {
    const auto controller = Controller();
    for (const auto field : {"last_position", "anchor", "previous_position"}) {
        for (int invalid = 0; invalid < 10; ++invalid) {
            CAPTURE(field, invalid);
            auto state = controller->SaveState();
            auto* position = state.Find(field);
            REQUIRE(position);
            switch (invalid) {
            case 0: *position = DataObject::List(); break;
            case 1: *position = DataObject{}; break;
            case 2: position->SetUnsigned("x", 0); break;
            case 3: position->SetSigned("w", 0); break;
            case 4: position->SetSigned("x", std::int64_t{world_coordinate_limit} + 1); break;
            case 5: position->SetSigned("x", -std::int64_t{world_coordinate_limit} - 1); break;
            case 6: position->SetSigned("z", std::int64_t{world_coordinate_limit} + 1); break;
            case 7: position->SetSigned("z", -std::int64_t{world_coordinate_limit} - 1); break;
            case 8: position->SetSigned("y", world_max_y + 1); break;
            case 9: position->SetSigned("y", world_min_y - 1); break;
            }
            CheckRejectedState(state);
        }
    }
}

TEST_CASE("Sighted saved profile references validate against the receiving content", "[ai][sighted][save]") {
    const auto content = SightedContent();
    auto controller = Controller();
    CHECK(ValidateAiControllerContent(CaptureAiController(controller.get()), content));
    controller = Controller("patient");
    auto envelope = CaptureAiController(controller.get());
    CHECK(ValidateAiControllerContent(envelope, content));
    auto* state = envelope.Find("state");
    REQUIRE(state);
    state->SetString("profile", "missing_profile");
    // Structural restoration is separate from resolving content references.
    CHECK(ReadAiController(envelope));
    Diagnostics diagnostics;
    const auto valid = ValidateAiControllerContent(envelope, content, &diagnostics);
    REQUIRE_FALSE(valid);
    CHECK(valid.error() == StateError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().back().location);
    CHECK(diagnostics.Frames().back().location->path == "actors.controller.state.profile");
}

}
