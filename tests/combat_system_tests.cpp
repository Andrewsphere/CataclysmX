#include "combat/combat_system.h"
#include "core/world.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

template<class T>
T Take(WorldResult<T> result) {
    REQUIRE(result);
    return eastl::move(*result);
}

struct Rig {
    World world{TestContent()};
    ActorHandle attacker{};
    ActorHandle target{};
    std::uint32_t rng = 7;

    Rig() {
        Take(world.RequestChunks({{}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        for (int z = 1; z <= 5; ++z) {
            for (int x = 1; x <= 5; ++x) {
                REQUIRE(world.SetTile({x, z}, {TestTerrain("floor"), 0}));
            }
        }
        attacker = Take(world.CreateActor({3, 3}, true));
        target = Take(world.CreateActor({4, 3}, true));
        auto state = MakeCombatState(true);
        state.accuracy = 100;
        REQUIRE(world.Combat().Configure(attacker, state));
        state.faction = CombatFaction::Undead;
        REQUIRE(world.Combat().Configure(target, state));
    }

    void Advance(Tick ticks) {
        world.ElapseTicks(ticks);
        world.Combat().ProcessEvents(rng);
    }
};

TEST_CASE("Heavy windup captures recovery speed and consumes one actor turn", "[combat][timing]") {
    Rig rig;
    REQUIRE(rig.world.SetActorSpeed(rig.attacker, 125));
    REQUIRE(rig.world.Combat().SetStyle(rig.attacker, CombatStyle::Aggressive));
    const auto attack = rig.world.Combat().Attack(rig.attacker, rig.target, rig.rng);
    REQUIRE(attack.outcome == AttackOutcome::Windup);
    CHECK(attack.cost == 100);
    CHECK(rig.world.GetActor(rig.attacker)->turns == 1);
    CHECK(rig.world.GetActor(rig.attacker)->combat.action.recoveryTicks == 150);
    REQUIRE(rig.world.SetActorSpeed(rig.attacker, 200));
    rig.Advance(99);
    CHECK(rig.world.GetActor(rig.attacker)->combat.action.kind == ActorActionKind::MeleeWindup);
    rig.Advance(1);
    CHECK(rig.world.GetActor(rig.attacker)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(rig.world.GetActorRemainingTicks(rig.attacker) == 150);
    CHECK(rig.world.GetActor(rig.attacker)->turns == 1);
    rig.Advance(150);
    CHECK(rig.world.GetActor(rig.attacker)->combat.action.kind == ActorActionKind::None);
    CHECK_FALSE(rig.world.Combat().NextEventIn());
}

TEST_CASE("Neighbor targeting uses hostility and persistent identity ties", "[combat][targeting]") {
    Rig rig;
    // Use a non-player observer to isolate targeting from the lighting profile.
    const auto observer = Take(rig.world.CreateActor({3, 2}));
    auto state = MakeCombatState(false);
    state.faction = CombatFaction::Survivors;
    REQUIRE(rig.world.Combat().Configure(observer, state));
    const auto first = Take(rig.world.CreateActor({2, 2}));
    const auto second = Take(rig.world.CreateActor({4, 2}));
    state.faction = CombatFaction::Undead;
    REQUIRE(rig.world.Combat().Configure(first, state));
    REQUIRE(rig.world.Combat().Configure(second, state));
    CHECK(rig.world.Combat().FindTarget(observer) == first);
    REQUIRE(rig.world.DestroyActor(first));
    CHECK(rig.world.Combat().FindTarget(observer) == second);
    CHECK_FALSE(rig.world.Combat().CanReach(observer, observer));
    CHECK_FALSE(rig.world.Combat().CanReach(observer, rig.target));
}

TEST_CASE("Defensive reaction works during recovery and is bounded by cooldown", "[combat][reaction]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.target, CombatStyle::Defensive));
    const auto initial = rig.world.Combat().Attack(rig.target, rig.attacker, rig.rng);
    REQUIRE(initial.committed);
    const auto deadline = rig.world.GetActorRemainingTicks(rig.target);
    const auto attack = rig.world.Combat().Attack(rig.attacker, rig.target, rig.rng);
    REQUIRE(attack.outcome == AttackOutcome::Parried);
    CHECK(rig.world.GetActor(rig.target)->combat.stamina == 90);
    CHECK(rig.world.GetActor(rig.target)->combat.reactionCooldown == 100);
    CHECK(rig.world.GetActorRemainingTicks(rig.target) == deadline);
    CHECK(rig.world.GetActor(rig.target)->turns == 1);
    CHECK(rig.world.GetActor(rig.attacker)->turns == 1);
    const auto second = Take(rig.world.CreateActor({4, 2}, true));
    auto state = MakeCombatState(true);
    state.accuracy = 100;
    REQUIRE(rig.world.Combat().Configure(second, state));
    const auto repeated = rig.world.Combat().Attack(second, rig.target, rig.rng);
    CHECK(repeated.outcome == AttackOutcome::Hit);
    CHECK(rig.world.GetActor(rig.target)->combat.stamina == 90);
}

TEST_CASE("Damaging windup hits penalize and capability loss cancels without extra turn", "[combat][disruption]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.attacker, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.attacker, rig.target, rig.rng).outcome == AttackOutcome::Windup);
    auto damage = rig.world.Combat().Damage(rig.attacker, rig.target, BodyPart::Torso,
        {DamageType::Blunt, 100, 0});
    CHECK(damage.disruption == DisruptionResponse::AccuracyReduced);
    CHECK(rig.world.GetActorRemainingTicks(rig.attacker) == 80);
    CHECK(rig.world.GetActor(rig.attacker)->combat.action.accuracyPenalty == 15);
    damage = rig.world.Combat().Damage(rig.attacker, rig.target, BodyPart::LeftHand,
        {DamageType::Blunt, 10000, 0});
    CHECK(damage.lostCapability);
    CHECK(damage.disruption == DisruptionResponse::Canceled);
    CHECK(rig.world.GetActorRemainingTicks(rig.attacker) == 20);
    rig.Advance(5);
    rig.world.Combat().Disrupt(rig.attacker, {DisruptionCause::Stagger});
    CHECK(rig.world.GetActorRemainingTicks(rig.attacker) == 15);
    CHECK(rig.world.GetActor(rig.attacker)->turns == 1);
}

TEST_CASE("Armor blocks subwhole injury without disruption or broken part maximum loss", "[combat][armor]") {
    Rig rig;
    auto state = rig.world.GetActor(rig.target)->combat;
    auto& hand = state.body.parts[static_cast<std::size_t>(BodyPart::LeftHand)];
    hand.hp = 0;
    hand.broken = true;
    state.protection[static_cast<std::size_t>(BodyPart::LeftHand)][0] = 101;
    REQUIRE(rig.world.Combat().Configure(rig.target, state));
    REQUIRE(rig.world.Combat().StartAiming(rig.target, rig.attacker, 100));
    const auto blocked = rig.world.Combat().Damage(rig.target, rig.attacker, BodyPart::LeftHand,
        {DamageType::Blunt, 200, 0});
    CHECK(blocked.contact);
    CHECK(blocked.outcome == AttackOutcome::Blocked);
    CHECK(blocked.injury == 0);
    CHECK(rig.world.GetActor(rig.target)->combat.action.kind == ActorActionKind::Aiming);
    CHECK(rig.world.GetActor(rig.target)->combat.body.parts[2].maxLoss == 0);
    const auto damaging = rig.world.Combat().Damage(rig.target, rig.attacker, BodyPart::LeftHand,
        {DamageType::Blunt, 201, 0});
    CHECK(damaging.injury == 1);
    CHECK(damaging.disruption == DisruptionResponse::Canceled);
    CHECK(rig.world.GetActor(rig.target)->combat.body.parts[2].maxLoss == 10);
}

TEST_CASE("Stale heavy targets still leave committed recovery", "[combat][targeting]") {
    Rig rig;
    REQUIRE(rig.world.Combat().SetStyle(rig.attacker, CombatStyle::Aggressive));
    REQUIRE(rig.world.Combat().Attack(rig.attacker, rig.target, rig.rng).outcome == AttackOutcome::Windup);
    REQUIRE(rig.world.DestroyActor(rig.target));
    const auto rngBefore = rig.rng;
    rig.Advance(80);
    CHECK(rig.rng == rngBefore);
    CHECK(rig.world.GetActorRemainingTicks(rig.attacker) == 120);
    CHECK(rig.world.GetActor(rig.attacker)->combat.action.kind == ActorActionKind::Recovery);
}

TEST_CASE("Parked combat state freezes while controllerless bodies recover", "[combat][recovery]") {
    Rig rig;
    auto state = rig.world.GetActor(rig.target)->combat;
    state.stamina = 50;
    state.heavyCooldown = 100;
    state.body.parts[0].hp -= 100;
    state.injury.healingInterval = 10;
    REQUIRE(rig.world.Combat().Configure(rig.target, state));
    REQUIRE(rig.world.SetActorParked(rig.target, true));
    rig.Advance(30);
    CHECK(rig.world.GetActor(rig.target)->combat == state);
    const auto idle = Take(rig.world.CreateActor({2, 2}));
    REQUIRE(rig.world.SetActorController(idle, AiControllerKind::None));
    REQUIRE(rig.world.Combat().Configure(idle, state));
    REQUIRE(rig.world.GetActor(idle)->actionDeadline.suspended);
    rig.Advance(30);
    CHECK(rig.world.GetActor(idle)->combat.stamina == 53);
    CHECK(rig.world.GetActor(idle)->combat.heavyCooldown == 70);
    CHECK(rig.world.GetActor(idle)->combat.body.parts[0].hp == 10000);
    CHECK(rig.world.GetActor(idle)->actionDeadline.suspended);
}

TEST_CASE("Active combat recovery is invariant under elapsed subdivision", "[combat][recovery]") {
    Rig whole;
    Rig split;
    auto state = whole.world.GetActor(whole.target)->combat;
    state.stamina = 97;
    state.staminaElapsed = 6;
    state.reactionCooldown = 27;
    state.heavyCooldown = 45;
    state.body.parts[0].hp -= 300;
    state.injury.healingInterval = 11;
    REQUIRE(whole.world.Combat().Configure(whole.target, state));
    REQUIRE(split.world.Combat().Configure(split.target, state));
    whole.Advance(57);
    for (int i = 0; i < 57; ++i) { split.Advance(1); }
    CHECK(whole.world.GetActor(whole.target)->combat == split.world.GetActor(split.target)->combat);
}

TEST_CASE("Disabled simple apparatus retains bite and disabled locomotion increases its cost", "[combat][moves]") {
    Rig rig;
    auto state = MakeCombatState(false);
    state.faction = CombatFaction::Survivors;
    state.accuracy = 100;
    state.style = CombatStyle::Aggressive;
    state.body.parts[static_cast<std::size_t>(BodyPart::AttackApparatus)].hp = 0;
    state.body.parts[static_cast<std::size_t>(BodyPart::Locomotion)].hp = 0;
    REQUIRE(rig.world.Combat().Configure(rig.attacker, state));
    const auto result = rig.world.Combat().Attack(rig.attacker, rig.target, rig.rng);
    CHECK(result.move == CombatMove::Bite);
    CHECK(result.cost == 150);
    CHECK(result.committed);
    CHECK(rig.world.GetActor(rig.attacker)->combat.stamina == 100);
}

TEST_CASE("Simple core defeat removes NPC after capturing result", "[combat][injury]") {
    Rig rig;
    const auto npc = Take(rig.world.CreateActor({2, 2}));
    const auto id = rig.world.GetActor(npc)->id;
    const auto result = rig.world.Combat().Damage(npc, rig.attacker, BodyPart::Core,
        {DamageType::Blunt, 10000, 0});
    CHECK(result.contact);
    CHECK(result.target == id);
    CHECK(result.injury == 100);
    CHECK(result.outcome == AttackOutcome::Defeated);
    CHECK(rig.world.GetActor(npc) == nullptr);
}

}
