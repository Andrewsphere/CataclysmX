#include "combat/combat_system.h"
#include "core/server.h"
#include "core/simulation.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

template<class T, class Error>
T Take(eastl::expected<T, Error> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

struct SaveFile {
    std::filesystem::path directory;
    std::string path;

    SaveFile() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-combat-integration-" + std::to_string(stamp)
                + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) { break; }
        }
        path = (directory / "world.sqlite").string();
    }

    ~SaveFile() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

LoadRequest Load(World& world, ChunkCoord chunk = {}, LoadMode mode = LoadMode::active) {
    const auto request = Take(world.RequestChunks({chunk, 0}, mode));
    REQUIRE(world.ReconcileChunks());
    return request;
}

void Floor(World& world, TileCoord position) {
    REQUIRE(world.SetTile(position, {TestTerrain("floor"), 0}));
}

CombatState State(bool player, CombatFaction faction) {
    auto state = MakeCombatState(player);
    state.faction = faction;
    state.accuracy = 100;
    // Exact injury assertions must not depend on healing during recovery.
    state.injury.regeneration = false;
    return state;
}

ActorHandle Target(World& world, TileCoord position) {
    Floor(world, position);
    const auto target = Take(world.CreateActor(position));
    REQUIRE(world.Combat().Configure(target, State(false, CombatFaction::Undead)));
    REQUIRE(world.SetActorController(target, AiControllerKind::None));
    return target;
}

struct ServerRig {
    Server server{TestContent(), 7};

    ServerRig() {
        auto& world = server.GetWorld();
        // The demo's Melee enemy must not consume draws or approach the arena.
        REQUIRE(world.DestroyActor(world.ActorAt({5, 5})));
        REQUIRE(server.SetBubbleRadii(1, 1));
        REQUIRE(world.Combat().Configure(server.GetPlayer(), State(true, CombatFaction::Survivors)));
    }
};

struct Rig {
    World world{TestContent()};
    LoadRequest request{};
    ActorHandle player{};
    ActorHandle target{};
    std::uint32_t rng = 7;

    Rig() {
        request = Load(world);
        Floor(world, {3, 3});
        player = Take(world.CreateActor({3, 3}, true));
        REQUIRE(world.Combat().Configure(player, State(true, CombatFaction::Survivors)));
        target = Target(world, {4, 3});
    }

    void Elapse(Tick ticks) {
        world.ElapseTicks(ticks);
        world.Combat().ProcessEvents(rng);
    }
};

void SameActor(const World& a, const World& b, PersistentId id) {
    const auto left = a.FindActor(id);
    const auto right = b.FindActor(id);
    REQUIRE(a.GetActor(left));
    REQUIRE(b.GetActor(right));
    CHECK(a.GetActor(left)->combat == b.GetActor(right)->combat);
    CHECK(a.GetActor(left)->position == b.GetActor(right)->position);
    CHECK(a.GetActor(left)->turns == b.GetActor(right)->turns);
    CHECK(a.GetActorRemainingTicks(left) == b.GetActorRemainingTicks(right));
}

TEST_CASE("Combat integration hostile bump attacks without displacing either actor",
    "[combat][integration][server]") {
    ServerRig rig;
    auto& server = rig.server;
    auto& world = server.GetWorld();
    const auto target = Target(world, {1, 0});
    const auto body = world.GetActor(target)->combat.body;
    const auto result = server.Send({CommandKind::move, 1, 0});
    CHECK(result.outcome == Outcome::attacked);
    CHECK(result.advanced);
    CHECK(result.cost == 100);
    CHECK(result.tick == 100);
    CHECK(world.ActorAt({0, 0}) == server.GetPlayer());
    CHECK(world.ActorAt({1, 0}) == target);
    CHECK_FALSE(world.GetActor(target)->combat.body == body);
    CHECK(world.GetActor(server.GetPlayer())->turns == 1);
    CHECK(world.GetActor(target)->turns == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Combat integration Tab selects perceived adjacent hostiles by identity and refuses unavailable attacks",
    "[combat][integration][server][targeting]") {
    ServerRig rig;
    auto& server = rig.server;
    auto& world = server.GetWorld();
    // West is older, although neighbor enumeration visits east first.
    const auto west = Target(world, {-1, 0});
    const auto east = Target(world, {1, 0});
    const auto eastBody = world.GetActor(east)->combat.body;
    const auto westBody = world.GetActor(west)->combat.body;
    REQUIRE(world.CanPlayerSee(server.GetPlayer(), {-1, 0}));
    REQUIRE(world.CanPlayerSee(server.GetPlayer(), {1, 0}));
    REQUIRE(server.Send({CommandKind::attack}).outcome == Outcome::attacked);
    CHECK_FALSE(world.GetActor(west)->combat.body == westBody);
    CHECK(world.GetActor(east)->combat.body == eastBody);

    const auto vision = world.GetVisionProfile();
    auto blind = vision;
    blind.range = 0;
    blind.dimRange = 0;
    blind.darkRange = 0;
    blind.glareDimRange = 0;
    REQUIRE(world.SetVisionProfile(blind));
    REQUIRE_FALSE(world.CanPlayerSee(server.GetPlayer(), {1, 0}));
    const auto before = world.GetActor(server.GetPlayer())->combat;
    const auto refused = server.Send({CommandKind::attack});
    CHECK(refused.outcome == Outcome::no_target);
    CHECK_FALSE(refused.advanced);
    CHECK(refused.tick == 100);
    CHECK(world.GetActor(server.GetPlayer())->turns == 1);
    CHECK(world.GetActor(server.GetPlayer())->combat == before);
    Command explicitTarget{CommandKind::attack};
    explicitTarget.target = world.GetActor(east)->id;
    CHECK(server.Send(explicitTarget).outcome == Outcome::no_target);
    const auto blindBump = server.Send({CommandKind::move, 1, 0});
    CHECK(blindBump.outcome == Outcome::occupied);
    CHECK_FALSE(blindBump.advanced);
    REQUIRE(world.SetVisionProfile(vision));
    REQUIRE(world.DestroyActor(west));
    REQUIRE(world.DestroyActor(east));
    Target(world, {0, 2}); // Visible but not adjacent.
    CHECK(server.Send({CommandKind::attack}).outcome == Outcome::no_target);
    CHECK(server.GetTick() == 100);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Combat integration neutral bump stays occupied and style commands select heavy timing",
    "[combat][integration][server][timing]") {
    ServerRig rig;
    auto& server = rig.server;
    auto& world = server.GetWorld();
    Floor(world, {1, 0});
    const auto neutral = Take(world.CreateActor({1, 0}));
    REQUIRE(world.SetActorController(neutral, AiControllerKind::None));
    const auto before = world.GetActor(neutral)->combat;
    const auto bump = server.Send({CommandKind::move, 1, 0});
    CHECK(bump.outcome == Outcome::occupied);
    CHECK_FALSE(bump.advanced);
    CHECK(bump.tick == 0);
    CHECK(world.GetActor(neutral)->combat == before);
    CHECK(world.ActorAt({0, 0}) == server.GetPlayer());
    REQUIRE(world.DestroyActor(neutral));
    Target(world, {1, 0});
    REQUIRE(world.SetActorSpeed(server.GetPlayer(), 125));
    Command style{CommandKind::combat_style};
    style.style = CombatStyle::Aggressive;
    const auto selected = server.Send(style);
    CHECK(selected.outcome == Outcome::combat_style_changed);
    CHECK_FALSE(selected.advanced);
    CHECK(server.GetTick() == 0);
    CHECK(world.GetActor(server.GetPlayer())->turns == 0);
    CHECK(world.GetActor(server.GetPlayer())->combat.style == CombatStyle::Aggressive);
    const auto attack = server.Send({CommandKind::attack});
    CHECK(attack.outcome == Outcome::attack_windup);
    CHECK(attack.cost == 100);
    CHECK(attack.tick == 250); // 80 windup + 120 recovery, both captured at 125%.
    CHECK(world.GetActor(server.GetPlayer())->turns == 1);
    CHECK(world.GetActor(server.GetPlayer())->combat.action.kind == ActorActionKind::None);
    style.style = CombatStyle::Defensive;
    CHECK(server.Send(style).outcome == Outcome::combat_style_changed);
    const auto plain = server.Send({CommandKind::attack});
    CHECK(plain.outcome == Outcome::attacked);
    CHECK(plain.cost == 125);
    CHECK(plain.tick == 375);
    CHECK(world.GetActor(server.GetPlayer())->turns == 2);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Combat integration Melee controller uses the shared attacker deadline",
    "[combat][integration][simulation][ai]") {
    Rig rig;
    // Configure before installing the controller or scheduling the player.
    auto enemy = State(false, CombatFaction::Undead);
    enemy.style = CombatStyle::Aggressive;
    REQUIRE(rig.world.Combat().Configure(rig.target, enemy));
    REQUIRE(rig.world.SetActorController(rig.target, AiControllerKind::Melee));
    REQUIRE(rig.world.SetActorSpeed(rig.target, 125));
    REQUIRE(rig.world.ScheduleActorAction(rig.player, 250));
    const auto body = rig.world.GetActor(rig.player)->combat.body;
    const auto first = AdvanceUntilInput(rig.world, rig.rng, 99);
    CHECK(first.stop == SimulationStop::LimitReached);
    CHECK(rig.world.GetActor(rig.target)->turns == 1);
    CHECK(rig.world.GetActor(rig.target)->combat.action.kind == ActorActionKind::MeleeWindup);
    CHECK(rig.world.GetActorRemainingTicks(rig.target) == 1);
    CHECK(rig.world.GetActor(rig.player)->combat.body == body);
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 100).stop == SimulationStop::LimitReached);
    CHECK(rig.world.GetActor(rig.target)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(rig.world.GetActorRemainingTicks(rig.target) == 150);
    CHECK_FALSE(rig.world.GetActor(rig.player)->combat.body == body);
    const auto done = AdvanceUntilInput(rig.world, rig.rng, 250);
    CHECK(done.stop == SimulationStop::InputRequired);
    CHECK(rig.world.GetActor(rig.target)->turns == 1);
    CHECK(rig.world.GetActor(rig.target)->combat.action.kind == ActorActionKind::None);
    CHECK(rig.world.GetActorRemainingTicks(rig.target) == 0);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration pending windup completes before input barrier and consumes one turn",
    "[combat][integration][simulation][timing]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).outcome == AttackOutcome::Windup);
    const auto body = rig.world.GetActor(rig.target)->combat.body;
    const auto rngBefore = rig.rng;
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 79).stop == SimulationStop::LimitReached);
    CHECK(rig.rng == rngBefore);
    CHECK(rig.world.GetActor(rig.target)->combat.body == body);
    const auto impact = AdvanceUntilInput(rig.world, rig.rng, 80);
    CHECK(impact.stop == SimulationStop::LimitReached);
    CHECK(impact.readyPlayers.empty());
    CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 120);
    CHECK_FALSE(rig.world.GetActor(rig.target)->combat.body == body);
    const auto rngAfter = rig.rng;
    const auto injured = rig.world.GetActor(rig.target)->combat.body;
    const auto ready = AdvanceUntilInput(rig.world, rig.rng, 200);
    CHECK(ready.stop == SimulationStop::InputRequired);
    REQUIRE(ready.readyPlayers.size() == 1);
    CHECK(ready.readyPlayers[0] == rig.player);
    CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::None);
    CHECK(rig.world.GetActor(rig.player)->turns == 1);
    CHECK(rig.rng == rngAfter);
    CHECK(rig.world.GetActor(rig.target)->combat.body == injured);
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 200).stop == SimulationStop::InputRequired);
    CHECK(rig.rng == rngAfter);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration canceled aiming becomes ready after twenty ticks despite repeated hits",
    "[combat][integration][simulation][disruption]") {
    Rig rig;
    REQUIRE(rig.world.Combat().StartAiming(rig.player, rig.target, 100));
    const auto hit = rig.world.Combat().Damage(rig.player, rig.target, BodyPart::Torso,
        {DamageType::Blunt, 100, 0});
    REQUIRE(hit.disruption == DisruptionResponse::Canceled);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 20);
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 7).stop == SimulationStop::LimitReached);
    CHECK(rig.world.Combat().Damage(rig.player, rig.target, BodyPart::Torso,
        {DamageType::Blunt, 100, 0}).disruption == DisruptionResponse::Ignored);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 13);
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 19).stop == SimulationStop::LimitReached);
    CHECK(rig.world.Combat().Damage(rig.player, rig.target, BodyPart::Torso,
        {DamageType::Blunt, 100, 0}).disruption == DisruptionResponse::Ignored);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 1);
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 20).stop == SimulationStop::InputRequired);
    CHECK(rig.world.GetTick() == 20);
    CHECK(rig.world.GetActor(rig.player)->turns == 1);
    CHECK(rig.world.GetActor(rig.player)->combat.action == ActorActionState{});
    CHECK(rig.rng == 7);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration moving a heavy target cancels contact but preserves committed recovery",
    "[combat][integration][simulation][targeting]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).committed);
    Floor(rig.world, {5, 3});
    REQUIRE(rig.world.MoveActor(rig.target, {5, 3}));
    const auto body = rig.world.GetActor(rig.target)->combat.body;
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 80).stop == SimulationStop::LimitReached);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 120);
    CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(rig.world.GetActor(rig.target)->combat.body == body);
    CHECK(rig.rng == 7);
    // Returning during recovery must not retry the canceled contact.
    REQUIRE(rig.world.MoveActor(rig.target, {4, 3}));
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 200).stop == SimulationStop::InputRequired);
    CHECK(rig.world.GetActor(rig.target)->combat.body == body);
    CHECK(rig.world.GetActor(rig.player)->turns == 1);
    CHECK(rig.rng == 7);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration stale windup target cannot hit a replacement actor",
    "[combat][integration][simulation][targeting][lifetime]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).committed);
    const auto stale = rig.target;
    const auto targetId = rig.world.GetActor(stale)->id;
    REQUIRE(rig.world.DestroyActor(stale));
    const auto replacement = Target(rig.world, {4, 3});
    REQUIRE(rig.world.GetActor(replacement)->id != targetId);
    const auto body = rig.world.GetActor(replacement)->combat.body;
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 80).stop == SimulationStop::LimitReached);
    CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 120);
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 200).stop == SimulationStop::InputRequired);
    CHECK(rig.world.GetActor(replacement)->combat.body == body);
    CHECK(rig.world.GetActor(rig.player)->turns == 1);
    CHECK(rig.rng == 7);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration checkpoint and RAM reload preserve injury pending action and RNG continuation",
    "[combat][integration][save][streaming][determinism]") {
    SaveFile file;
    Rig rig;
    auto state = State(true, CombatFaction::Survivors);
    state.injury.regeneration = true;
    state.injury.mendingDuration = 1000;
    REQUIRE(rig.world.Combat().Configure(rig.player, state));
    REQUIRE(rig.world.Combat().Damage(rig.player, rig.target, BodyPart::LeftLeg,
        {DamageType::Blunt, 10000, 0}).lostCapability);
    REQUIRE(rig.world.Combat().Damage(rig.player, rig.target, BodyPart::LeftLeg,
        {DamageType::Blunt, 100, 0}).injury == 1);
    rig.Elapse(37);
    REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).outcome == AttackOutcome::Windup);
    rig.Elapse(17);
    const auto playerId = rig.world.GetActor(rig.player)->id;
    const auto targetId = rig.world.GetActor(rig.target)->id;
    const auto saved = rig.world.GetActor(rig.player)->combat;
    const auto& leg = saved.body.parts[static_cast<std::size_t>(BodyPart::LeftLeg)];
    CHECK(leg.broken);
    CHECK(leg.maxLoss == 10); // A fractional 0.10 maximum HP, not a rounded whole HP.
    CHECK(leg.mendingElapsed == 54);
    CHECK(saved.action.target == targetId);
    CHECK(saved.action.recoveryTicks == 120);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 63);
    SessionState session;
    session.player = playerId;
    session.playerPosition = {3, 3};
    session.rngState = rig.rng;
    REQUIRE(rig.world.CreateSave(file.path.c_str(), session));
    World restored(TestContent());
    const auto loadedSession = Take(restored.LoadSave(file.path.c_str()));
    auto restoredRng = loadedSession.rngState;
    CHECK(restoredRng == rig.rng);
    Load(restored);
    SameActor(rig.world, restored, playerId);
    SameActor(rig.world, restored, targetId);

    // Force the live world through its RAM archive before continuing either copy.
    const auto oldPlayer = rig.player;
    REQUIRE(rig.world.ReleaseRequest(rig.request));
    REQUIRE(rig.world.ReconcileChunks());
    REQUIRE_FALSE(rig.world.GetActor(oldPlayer));
    REQUIRE(rig.world.ArchivedChunkCount() > 0);
    rig.request = Load(rig.world);
    rig.player = rig.world.FindActor(playerId);
    rig.target = rig.world.FindActor(targetId);
    REQUIRE(rig.player != oldPlayer);
    CHECK(rig.world.GetActor(rig.player)->combat == saved);
    SameActor(rig.world, restored, playerId);
    for (const Tick tick : {Tick{116}, Tick{117}, Tick{237}}) {
        CAPTURE(tick);
        const auto a = AdvanceUntilInput(rig.world, rig.rng, tick);
        const auto b = AdvanceUntilInput(restored, restoredRng, tick);
        CHECK(a.stop == b.stop);
        CHECK(rig.rng == restoredRng);
        SameActor(rig.world, restored, playerId);
        SameActor(rig.world, restored, targetId);
    }
    CHECK(rig.rng != session.rngState);
    CHECK(rig.world.GetActor(rig.player)->turns == 1);
    CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::None);
    CHECK(rig.world.CheckInvariants());
    CHECK(restored.CheckInvariants());
}

TEST_CASE("Combat integration inactive and parked pending state freezes without catchup",
    "[combat][integration][streaming][parking]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).committed);
    rig.Elapse(13);
    const auto state = rig.world.GetActor(rig.player)->combat;
    bool parked = false;
    SECTION("resident only") {
        REQUIRE(rig.world.UpdateRequest(rig.request, {{}, 0}, LoadMode::resident));
        REQUIRE(rig.world.ReconcileChunks());
    }
    SECTION("parked player") {
        parked = true;
        REQUIRE(rig.world.SetActorParked(rig.player, true));
    }
    rig.Elapse(1000);
    CHECK(rig.world.GetActor(rig.player)->combat == state);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 67);
    CHECK(rig.world.GetActor(rig.player)->turns == 1);
    CHECK(rig.rng == 7);
    if (parked) { REQUIRE(rig.world.SetActorParked(rig.player, false)); }
    else {
        REQUIRE(rig.world.UpdateRequest(rig.request, {{}, 0}, LoadMode::active));
        REQUIRE(rig.world.ReconcileChunks());
    }
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 67);
    const auto now = rig.world.GetTick();
    CHECK(AdvanceUntilInput(rig.world, rig.rng, now + 66).stop == SimulationStop::LimitReached);
    CHECK(rig.rng == 7);
    CHECK(AdvanceUntilInput(rig.world, rig.rng, now + 67).stop == SimulationStop::LimitReached);
    CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(rig.world.GetActorRemainingTicks(rig.player) == 120);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration canceled saved windup never completes against a recycled target slot",
    "[combat][integration][save][disruption][targeting]") {
    SaveFile file;
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).committed);
    REQUIRE(rig.world.Combat().Damage(rig.player, rig.target, BodyPart::LeftHand,
        {DamageType::Blunt, 10000, 0}).disruption == DisruptionResponse::Canceled);
    const auto stale = rig.target;
    const auto oldId = rig.world.GetActor(stale)->id;
    REQUIRE(rig.world.DestroyActor(stale));
    const auto replacement = Target(rig.world, {4, 3});
    const auto replacementId = rig.world.GetActor(replacement)->id;
    REQUIRE(replacementId != oldId);
    REQUIRE_FALSE(rig.world.GetActor(stale));
    const auto body = rig.world.GetActor(replacement)->combat.body;
    SessionState session;
    session.player = rig.world.GetActor(rig.player)->id;
    session.playerPosition = {3, 3};
    session.rngState = rig.rng;
    REQUIRE(rig.world.CreateSave(file.path.c_str(), session));
    World loaded(TestContent());
    auto rng = Take(loaded.LoadSave(file.path.c_str())).rngState;
    Load(loaded);
    const auto player = loaded.FindActor(session.player);
    REQUIRE(loaded.GetActor(player));
    CHECK(loaded.GetActor(player)->combat.action.kind == ActorActionKind::DisruptionRecovery);
    CHECK(loaded.GetActor(player)->combat.action.target == 0);
    CHECK(AdvanceUntilInput(loaded, rng, 20).stop == SimulationStop::InputRequired);
    // Spend enough ordinary wait time to cross the old windup and recovery dates.
    REQUIRE(loaded.ScheduleActorAction(player, 300));
    CHECK(AdvanceUntilInput(loaded, rng, 320).stop == SimulationStop::InputRequired);
    CHECK(loaded.GetActor(loaded.FindActor(replacementId))->combat.body == body);
    CHECK(loaded.GetActor(player)->combat.action == ActorActionState{});
    CHECK(loaded.GetActor(player)->turns == 2);
    CHECK(rng == session.rngState);
    CHECK(loaded.CheckInvariants());
}

TEST_CASE("Combat integration equivalent identities replay across different handle allocation",
    "[combat][integration][determinism][streaming]") {
    World first(TestContent());
    World second(TestContent());
    const auto firstLeft = Load(first);
    const auto firstRight = Load(first, {1, 0});
    const auto secondLeft = Load(second);
    const auto secondRight = Load(second, {1, 0});
    for (auto* world : {&first, &second}) {
        Floor(*world, {15, 3});
        const auto player = Take(world->CreateActor({15, 3}, true));
        auto state = State(true, CombatFaction::Survivors);
        state.style = CombatStyle::Aggressive;
        REQUIRE(world->Combat().Configure(player, state));
        Target(*world, {16, 3});
    }
    const auto playerId = first.GetActor(first.ActorAt({15, 3}))->id;
    const auto targetId = first.GetActor(first.ActorAt({16, 3}))->id;
    REQUIRE(first.ReleaseRequest(firstLeft));
    REQUIRE(first.ReleaseRequest(firstRight));
    REQUIRE(first.ReconcileChunks());
    REQUIRE(second.ReleaseRequest(secondLeft));
    REQUIRE(second.ReleaseRequest(secondRight));
    REQUIRE(second.ReconcileChunks());
    Load(first, {1, 0});
    Load(first);
    Load(second);
    Load(second, {1, 0});
    REQUIRE(first.FindActor(playerId) != second.FindActor(playerId));
    std::uint32_t a = 918;
    std::uint32_t b = a;
    for (int turn = 0; turn < 6; ++turn) {
        CAPTURE(turn);
        const auto attackA = first.Combat().Attack(first.FindActor(playerId), first.FindActor(targetId), a);
        const auto attackB = second.Combat().Attack(second.FindActor(playerId), second.FindActor(targetId), b);
        REQUIRE(attackA.committed);
        CHECK(attackA == attackB);
        const auto until = first.GetTick() + 300;
        CHECK(AdvanceUntilInput(first, a, until).stop == SimulationStop::InputRequired);
        CHECK(AdvanceUntilInput(second, b, until).stop == SimulationStop::InputRequired);
        CHECK(first.GetTick() == second.GetTick());
        CHECK(a == b);
        SameActor(first, second, playerId);
        SameActor(first, second, targetId);
    }
    CHECK(first.CheckInvariants());
    CHECK(second.CheckInvariants());
}

TEST_CASE("Combat integration same tick NPC batch revalidates an actor destroyed before its turn",
    "[combat][integration][simulation][ai][lifetime]") {
    World world(TestContent());
    Load(world);
    for (const TileCoord tile : {TileCoord{3, 3}, TileCoord{4, 3}, TileCoord{8, 8}}) { Floor(world, tile); }
    const auto killer = Take(world.CreateActor({3, 3}));
    const auto victim = Take(world.CreateActor({4, 3}));
    const auto later = Take(world.CreateActor({8, 8}));
    auto state = State(false, CombatFaction::Undead);
    state.attackDamage = 100;
    REQUIRE(world.Combat().Configure(killer, state));
    REQUIRE(world.Combat().Configure(victim, State(false, CombatFaction::Survivors)));
    REQUIRE(world.SetActorController(killer, AiControllerKind::Melee));
    REQUIRE(world.SetActorController(victim, AiControllerKind::Melee));
    // Every possible hit zone now kills: disabled attached zones redirect to Core.
    REQUIRE(world.Combat().Damage(victim, killer, BodyPart::AttackApparatus,
        {DamageType::Blunt, 6000, 0}).lostCapability);
    REQUIRE(world.Combat().Damage(victim, killer, BodyPart::Locomotion,
        {DamageType::Blunt, 6000, 0}).lostCapability);
    const auto victimId = world.GetActor(victim)->id;
    std::uint32_t rng = 7;
    world.ProcessNpcActions(rng);
    CHECK_FALSE(world.GetActor(victim));
    CHECK(world.FindActor(victimId) == ActorHandle{});
    CHECK(world.GetActor(killer)->turns == 1);
    CHECK(world.GetActor(killer)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(world.GetActorRemainingTicks(killer) == 100);
    CHECK(world.GetActor(later)->turns == 1); // The batch continued after the stale entry.
    CHECK(world.GetActorRemainingTicks(later) == 100);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Combat integration retained simple player recovery completes after core failure",
    "[combat][integration][simulation]") {
    Rig rig;
    REQUIRE(rig.world.Combat().Configure(rig.player, State(false, CombatFaction::Survivors)));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).committed);
    rig.world.Combat().Damage(rig.player, rig.target, BodyPart::Core, {DamageType::Blunt, 10000});
    REQUIRE(rig.world.GetActor(rig.player)); // Player defeat is deferred, independent of anatomy.
    CHECK(AdvanceUntilInput(rig.world, rig.rng, 100).stop == SimulationStop::InputRequired);
    CHECK(rig.world.GetTick() == 100);
    CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::None);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration authoritative displacement applies receiver disruption policy",
    "[combat][integration][movement]") {
    Rig rig;
    Floor(rig.world, {3, 4});
    SECTION("Aiming cancels once") {
        REQUIRE(rig.world.Combat().StartAiming(rig.player, rig.target, 100));
        REQUIRE(rig.world.MoveActor(rig.player, {3, 4}));
        CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::DisruptionRecovery);
        CHECK(rig.world.GetActorRemainingTicks(rig.player) == 20);
        rig.world.ElapseTicks(5);
        REQUIRE(rig.world.MoveActor(rig.player, {3, 3}));
        CHECK(rig.world.GetActorRemainingTicks(rig.player) == 15);
    }
    SECTION("Windup loses accuracy without extending commitment") {
        REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
        REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).committed);
        REQUIRE(rig.world.MoveActor(rig.player, {3, 4}));
        CHECK(rig.world.GetActor(rig.player)->combat.action.kind == ActorActionKind::MeleeWindup);
        CHECK(rig.world.GetActor(rig.player)->combat.action.accuracyPenalty == 15);
        CHECK(rig.world.GetActorRemainingTicks(rig.player) == 80);
    }
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Combat integration locomotion penalties start only on zone failure",
    "[combat][integration][server][injury]") {
    ServerRig rig;
    auto& world = rig.server.GetWorld();
    const auto player = rig.server.GetPlayer();
    world.Combat().Damage(player, {}, BodyPart::LeftLeg, {DamageType::Blunt, 9900});
    CHECK(rig.server.Send({CommandKind::move, 1, 0}).cost == 100);
    world.Combat().Damage(player, {}, BodyPart::LeftLeg, {DamageType::Blunt, 100});
    CHECK(rig.server.Send({CommandKind::move, -1, 0}).cost == 150);
    world.Combat().Damage(player, {}, BodyPart::RightLeg, {DamageType::Blunt, 10000});
    CHECK(rig.server.Send({CommandKind::move, 1, 0}).cost == 200);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Combat integration suspended windup preflights both committed phases",
    "[combat][integration][streaming][timing]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.player, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.player, rig.target, rig.rng).committed);
    const auto id = rig.world.GetActor(rig.player)->id;
    bool unloaded = false;
    SECTION("resident activation") {
        REQUIRE(rig.world.ReleaseRequest(rig.request));
        rig.request = Take(rig.world.RequestChunks({{}, 0}, LoadMode::resident));
        REQUIRE(rig.world.ReconcileChunks());
    }
    SECTION("RAM archive restoration") {
        REQUIRE(rig.world.ReleaseRequest(rig.request));
        REQUIRE(rig.world.ReconcileChunks());
        unloaded = true;
    }
    rig.world.ElapseTicks(UINT64_MAX - 100);
    Take(rig.world.RequestChunks({{}, 0}, LoadMode::active));
    const auto result = rig.world.ReconcileChunks();
    REQUIRE_FALSE(result);
    CHECK(result.error() == SaveError::InvalidData);
    if (unloaded) { CHECK(rig.world.FindActor(id) == ActorHandle{}); }
    else {
        CHECK(rig.world.GetActor(rig.player)->actionDeadline.suspended);
        CHECK(rig.world.GetActorRemainingTicks(rig.player) == 80);
    }
    CHECK(rig.world.CheckInvariants());
}

}

TEST_CASE("Diagonal bump attacks and target discovery use destination-only melee reach", "[combat][integration][diagonal]") {
    ServerRig rig; auto& server = rig.server; auto& world = server.GetWorld();
    const auto target = Target(world,{1,1});
    REQUIRE(world.SetTile({1,0},{TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({0,1},{TestTerrain("wall"),0}));
    REQUIRE(world.CanPlayerSee(server.GetPlayer(),{1,1}));
    CHECK(world.Combat().CanReach(server.GetPlayer(),target));
    CHECK(world.Combat().FindTarget(server.GetPlayer()) == target);
    const auto result = server.Send({CommandKind::move,1,1});
    CHECK(result.outcome == Outcome::attacked); CHECK(result.cost == 100);
    CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{});
    CHECK(world.ActorAt({1,1}) == target);
}

TEST_CASE("Locked demo terrain and damage persist through server save", "[server][doors][save]") {
    SaveFile file; Server original(TestContent());
    REQUIRE(original.GetWorld().SetTile({3,0},{TestTerrain("locked_door"),23}));
    REQUIRE(original.CreateSave(file.path.c_str()));
    auto loaded = Server::LoadSave(TestContent(),file.path.c_str()); REQUIRE(loaded);
    const auto* tile = (*loaded)->GetWorld().GetTile({3,0}); REQUIRE(tile);
    CHECK(tile->terrain == TestTerrain("locked_door")); CHECK(tile->damage == 23);
}
