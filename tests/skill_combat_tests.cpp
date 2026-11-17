#include "combat/combat_system.h"
#include "core/world.h"
#include "skills/learning_system.h"
#include "skills/skill_state.h"
#include "combat_runtime_fixture.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }

ContentCatalog CombatContent() {
    const ContentSource source{"skill-combat.json5", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'skill',id:'melee',name:'Melee',category:'combat'},
      {type:'skill',id:'unarmed',name:'Unarmed',category:'combat'},
      {type:'skill',id:'dodge',name:'Dodge',category:'combat'},
      {type:'skill',id:'reading',name:'Reading'},
      {type:'book',id:'manual',name:'Manual',study_work:250,benefits:[{skill:'reading',first_level:1,last_level:10,direct_xp:100}]},
      {type:'item',id:'print',name:'Print',mass_g:1,volume_ml:1,books:['manual']},
      {type:'item',id:'weapon',name:'Weapon',mass_g:20,volume_ml:40,
       melee:{damage:[{type:'blunt',amount:450},{type:'slashing',amount:450}]}}
    ]})"};
    Diagnostics diagnostics; auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content);
    ConfigureCombatRuntimeTestContent(*content);
    auto& style = const_cast<CombatStyleDefinition&>(content->GetCombatStyle(content->FindCombatStyle("basic_balanced")));
    style.moves.push_back({content->FindCombatMove("basic_weapon"), 1});
    auto& fist = const_cast<CombatMoveDefinition&>(content->GetCombatMove(content->FindCombatMove("basic_fist")));
    fist.requirements.primary.freeHands = HandMask::Both;
    auto& weapon = const_cast<CombatMoveDefinition&>(content->GetCombatMove(content->FindCombatMove("basic_weapon")));
    weapon.strikes[0].intrinsicMiss = 0;
    return eastl::move(*content);
}

struct Rig {
    ContentCatalog content = CombatContent();
    World world{content};
    ActorHandle attacker{}, defender{};
    std::uint32_t rng = 7;
    Rig() {
        Take(world.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 1; z <= 5; ++z) for (int x = 1; x <= 6; ++x) {
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor"), 0}));
        }
        attacker = Take(world.CreateActor({3, 3}, true));
        defender = Take(world.CreateActor({4, 3}, true));
        auto state = MakeCombatState(true); state.accuracy = 100;
        REQUIRE(world.Combat().Configure(attacker, state));
        state.faction = CombatFaction::Undead;
        REQUIRE(world.Combat().Configure(defender, state));
    }
    std::uint64_t Xp(ActorHandle actor, const char* name) const {
        const auto* progress = FindSkillProgress(world.GetActor(actor)->learning, content.FindSkill(name));
        return progress ? progress->xp : 0;
    }
};

TEST_CASE("Missed strikes split attack training and pay only committed defense training", "[skills][combat]") {
    Rig r;
    auto state = r.world.GetActor(r.attacker)->combat;
    state.legacyAccuracyOverride = true; state.intrinsicMissOverride = ratioScale;
    REQUIRE(r.world.Combat().Configure(r.attacker, state));
    REQUIRE(r.world.Combat().SetStyle(r.defender, CombatStyle::Defensive));
    REQUIRE(r.world.Combat().Attack(r.attacker, r.defender, r.rng).outcome == AttackOutcome::Miss);
    CHECK(r.Xp(r.attacker, "melee") == 4000);
    CHECK(r.Xp(r.attacker, "unarmed") == 6000);
    CHECK(r.Xp(r.defender, "dodge") == 0);
    CHECK(r.Xp(r.defender, "melee") == 4000);
}

TEST_CASE("Multiple weapon packets train melee once and no unarmed", "[skills][combat]") {
    Rig r;
    const auto weapon = Take(r.world.CreateItem(r.content.FindItem("weapon"), ItemLocation::Carried(r.attacker)));
    REQUIRE(r.world.WieldItem(r.attacker, weapon).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.Combat().Attack(r.attacker, r.defender, r.rng).outcome == AttackOutcome::Hit);
    CHECK(r.Xp(r.attacker, "melee") == ratioScale);
    CHECK(r.Xp(r.attacker, "unarmed") == 0);
    CHECK(r.Xp(r.defender, "dodge") == 0);
}

TEST_CASE("Parrying trains defender melee instead of dodge", "[skills][combat]") {
    Rig r; REQUIRE(r.world.Combat().SetStyle(r.defender, CombatStyle::Defensive));
    REQUIRE(r.world.Combat().Attack(r.attacker, r.defender, r.rng).committed);
    CHECK(r.Xp(r.attacker, "melee") == 4000);
    CHECK(r.Xp(r.defender, "melee") == 4000);
    CHECK(r.Xp(r.defender, "dodge") == 0);
    CHECK(r.Xp(r.defender, "unarmed") == 6000);
}

TEST_CASE("Heavy attacks train only when the windup reaches a valid target", "[skills][combat]") {
    Rig r; REQUIRE(r.world.Combat().SetStyle(r.attacker, CombatStyle::Aggressive));
    REQUIRE(r.world.Combat().Attack(r.attacker, r.defender, r.rng).outcome == AttackOutcome::Windup);
    CHECK(r.Xp(r.attacker, "melee") == 0);
    SECTION("target remains in reach") {
        r.world.ElapseTicks(80); r.world.Combat().ProcessEvents(r.rng);
        CHECK(r.Xp(r.attacker, "melee") == 4000);
        CHECK(r.Xp(r.attacker, "unarmed") == 6000);
        CHECK(r.Xp(r.defender, "dodge") == 0);
    }
    SECTION("target leaves reach") {
        REQUIRE(r.world.MoveActor(r.defender, {5, 3}));
        r.world.ElapseTicks(80); r.world.Combat().ProcessEvents(r.rng);
        CHECK(r.Xp(r.attacker, "melee") == 0);
        CHECK(r.Xp(r.defender, "dodge") == 0);
    }
}

TEST_CASE("Refused combat and raw damage do not train skills", "[skills][combat]") {
    Rig r;
    CHECK(r.world.Combat().CanAttack(r.attacker));
    r.world.Combat().FindTarget(r.attacker);
    CHECK(r.world.Combat().Attack(r.attacker, {}, r.rng).outcome == AttackOutcome::NoTarget);
    REQUIRE(r.world.MoveActor(r.defender, {5, 3}));
    CHECK(r.world.Combat().Attack(r.attacker, r.defender, r.rng).outcome == AttackOutcome::OutOfReach);
    r.world.Combat().Damage(r.defender, r.attacker, BodyPart::Torso, {DamageType::Blunt, 100, 0});
    for (const auto actor : {r.attacker, r.defender}) {
        CHECK(r.Xp(actor, "melee") == 0); CHECK(r.Xp(actor, "unarmed") == 0); CHECK(r.Xp(actor, "dodge") == 0);
    }
}

TEST_CASE("Funded study remains combat busy at its unprocessed deadline", "[skills][combat]") {
    Rig r;
    Take(r.world.CreateItem(r.content.FindItem("print"), ItemLocation::Carried(r.attacker)));
    REQUIRE(Take(r.world.Learning().Study(r.attacker, r.content.FindBook("manual"))).outcome == StudyOutcome::Started);
    r.world.ElapseTicks(100);
    REQUIRE(r.world.GetActorRemainingTicks(r.attacker) == 0);
    REQUIRE(r.world.Learning().Busy(r.attacker));
    CHECK_FALSE(r.world.Combat().CanAttack(r.attacker));
    CHECK(r.world.Combat().Attack(r.attacker, r.defender, r.rng).outcome == AttackOutcome::NotReady);
    CHECK_FALSE(r.world.Combat().StartAiming(r.attacker, r.defender, 100));
    CHECK(r.Xp(r.attacker, "melee") == 0);
    CHECK(r.Xp(r.defender, "dodge") == 0);
}
}
