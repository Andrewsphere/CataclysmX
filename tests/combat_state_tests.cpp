#include "combat/combat_types.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;

TEST_CASE("Combat creation defaults and hostility are independent of anatomy", "[combat][state]") {
    const auto player = MakeCombatState(true);
    const auto npc = MakeCombatState(false);
    CHECK(player.body.kind == BodyKind::Detailed);
    CHECK(player.faction == CombatFaction::Survivors);
    CHECK(player.injury.regeneration);
    CHECK(npc.body.kind == BodyKind::Simple);
    CHECK(npc.faction == CombatFaction::Neutral);
    CHECK_FALSE(npc.injury.regeneration);
    CHECK(player.style == CombatStyle::Balanced);
    CHECK(npc.style == CombatStyle::Balanced);
    for (const auto a : {CombatFaction::Neutral, CombatFaction::Survivors, CombatFaction::Undead}) {
        for (const auto b : {CombatFaction::Neutral, CombatFaction::Survivors, CombatFaction::Undead}) {
            CHECK(Hostile(a, b) == ((a == CombatFaction::Survivors && b == CombatFaction::Undead)
                || (a == CombatFaction::Undead && b == CombatFaction::Survivors)));
        }
    }
    auto changed = npc;
    changed.body = MakeBody(BodyKind::Detailed);
    changed.faction = CombatFaction::Undead;
    CHECK(ValidateCombatState(changed));
    CHECK(eastl::string_view(CombatStyleName(CombatStyle::Balanced)) == "Balanced");
    CHECK(eastl::string_view(CombatStyleName(CombatStyle::Aggressive)) == "Aggressive");
    CHECK(eastl::string_view(CombatStyleName(CombatStyle::Defensive)) == "Defensive");
}

TEST_CASE("Combat state preserves fractional injuries recovery phases and pending actions", "[combat][state]") {
    auto state = MakeCombatState(true);
    state.style = CombatStyle::Defensive;
    state.faction = CombatFaction::Undead;
    state.injury.mendedHp = 60;
    state.body.parts[0].hp = 9123;
    state.body.parts[0].healingElapsed = 123;
    state.body.parts[2] = {0, 137, 456, 0, 0, true};
    state.body.parts[3] = {9879, 121, 0, 0, 789, false};
    state.stamina = 37;
    state.staminaElapsed = 9;
    state.reactionCooldown = UINT64_MAX;
    state.heavyCooldown = UINT64_MAX - 1;
    state.attackDamage = 100000;
    state.accuracy = 100;
    for (std::size_t i = 0; i < body_part_count; ++i) {
        for (std::size_t j = 0; j < damage_type_count; ++j) {
            state.protection[i][j] = static_cast<std::uint32_t>(i * 100 + j);
        }
    }
    state.protection[0][0] = UINT32_MAX;
    SECTION("heavy windup with stale target") {
        state.action = {ActorActionKind::MeleeWindup, UINT64_MAX,
            static_cast<std::uint32_t>(CombatMove::HeavyAttack), UINT64_MAX, 30};
    }
    SECTION("aiming with stale target") { state.action = {ActorActionKind::Aiming, UINT64_MAX}; }
    SECTION("recovery") { state.action.kind = ActorActionKind::Recovery; }
    SECTION("disruption recovery") { state.action.kind = ActorActionKind::DisruptionRecovery; }
    SECTION("idle") {}
    REQUIRE(ValidateCombatState(state));
    const auto encoded = CaptureCombatState(state);
    const auto decoded = ReadCombatState(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == state);
    const auto json = DecodeDataObject(EncodeDataObject(encoded));
    REQUIRE(json);
    const auto fromJson = ReadCombatState(*json);
    REQUIRE(fromJson);
    CHECK(*fromJson == state);
}

TEST_CASE("Combat state validation rejects invalid runtime invariants", "[combat][state]") {
    auto state = MakeCombatState(true);
    SECTION("body enum") { state.body.kind = static_cast<BodyKind>(255); }
    SECTION("style enum") { state.style = static_cast<CombatStyle>(255); }
    SECTION("faction enum") { state.faction = static_cast<CombatFaction>(255); }
    SECTION("action enum") { state.action.kind = static_cast<ActorActionKind>(255); }
    SECTION("damage bound") { state.attackDamage = 100001; }
    SECTION("accuracy bound") { state.accuracy = 101; }
    SECTION("stamina bound") { state.stamina = 101; }
    SECTION("stamina phase bound") { state.stamina = 99; state.staminaElapsed = 10; }
    SECTION("full stamina cannot bank time") { state.staminaElapsed = 1; }
    SECTION("profile interval") { state.injury.healingInterval = 0; }
    SECTION("profile percentage") { state.injury.maxLossCapPercent = 51; }
    SECTION("absent body part") { state.body.parts[8].hp = 1; }
    SECTION("excess hp") { state.body.parts[0].hp = 10001; }
    SECTION("healthy part cannot bank healing") { state.body.parts[0].healingElapsed = 1; }
    SECTION("aim requires target") { state.action.kind = ActorActionKind::Aiming; }
    SECTION("windup requires heavy move") {
        state.action = {ActorActionKind::MeleeWindup, 99, static_cast<std::uint32_t>(CombatMove::PlainAttack), 1};
    }
    SECTION("windup requires recovery") {
        state.action = {ActorActionKind::MeleeWindup, 99, static_cast<std::uint32_t>(CombatMove::HeavyAttack), 0};
    }
    CHECK_FALSE(ValidateCombatState(state));
}

TEST_CASE("Combat state reader checks exact schemas types ranges and version", "[combat][state]") {
    auto data = CaptureCombatState(MakeCombatState(false));
    StateError expected = StateError::InvalidData;
    SECTION("unsupported version") { data.SetUnsigned("version", 2); expected = StateError::UnsupportedVersion; }
    SECTION("zero version") { data.SetUnsigned("version", 0); expected = StateError::UnsupportedVersion; }
    SECTION("version type") { data.SetSigned("version", 1); }
    SECTION("missing fields") { data = DataObject{}; data.SetUnsigned("version", 1); }
    SECTION("extra field") { data.SetUnsigned("extra", 0); }
    SECTION("wrong root type") { data = DataObject::List(); }
    SECTION("nested extra field") { data.Find("body")->SetUnsigned("extra", 0); }
    SECTION("nested missing field") { data.Set("profile", DataObject{}); }
    SECTION("parts size") { data.Find("body")->Set("parts", DataObject::List()); }
    SECTION("parts type") { data.Find("body")->SetUnsigned("parts", 0); }
    SECTION("enum truncation") { data.SetUnsigned("style", 256); }
    SECTION("body enum truncation") { data.Find("body")->SetUnsigned("kind", 256); }
    SECTION("action enum truncation") { data.Find("action")->SetUnsigned("kind", UINT64_MAX); }
    SECTION("move truncation") { data.Find("action")->SetUnsigned("move", UINT64_MAX); }
    SECTION("penalty truncation") { data.Find("action")->SetUnsigned("accuracyPenalty", 256); }
    SECTION("damage truncation") { data.SetUnsigned("attackDamage", UINT64_MAX); }
    SECTION("accuracy truncation") { data.SetUnsigned("accuracy", 256); }
    SECTION("stamina truncation") { data.SetUnsigned("stamina", UINT64_MAX); }
    SECTION("negative duration") { data.SetSigned("heavyCooldown", -1); }
    SECTION("profile truncation") { data.Find("profile")->SetUnsigned("mendedHp", UINT64_MAX); }
    SECTION("regeneration type") { data.Find("profile")->SetUnsigned("regeneration", 0); }
    SECTION("protection dimensions") { data.Set("protection", DataObject::List()); }
    SECTION("protection overflow") {
        DataObject protection = DataObject::List();
        for (std::size_t i = 0; i < body_part_count; ++i) {
            DataObject row = DataObject::List();
            for (std::size_t j = 0; j < damage_type_count; ++j) { row.Append(DataObject::Unsigned(UINT64_MAX)); }
            protection.Append(eastl::move(row));
        }
        data.Set("protection", eastl::move(protection));
    }
    Diagnostics diagnostics;
    CHECK(ReadCombatState(data, &diagnostics) == eastl::unexpected(expected));
    CHECK_FALSE(diagnostics.Empty());
}

}
