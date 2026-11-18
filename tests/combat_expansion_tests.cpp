#include "combat/combat_system.h"
#include "combat/combat_runtime.h"
#include "combat_runtime_fixture.h"
#include "skills/learning_system.h"
#include <EASTL/algorithm.h>
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> result) { REQUIRE(result); return eastl::move(*result); }

CombatMoveDefinition& Move(ContentCatalog& content, const char* name) {
    return const_cast<CombatMoveDefinition&>(content.GetCombatMove(content.FindCombatMove(name)));
}
CombatStyleDefinition& Style(ContentCatalog& content, const char* name) {
    return const_cast<CombatStyleDefinition&>(content.GetCombatStyle(content.FindCombatStyle(name)));
}
ItemDefinition& ItemType(ContentCatalog& content, const char* name) {
    return const_cast<ItemDefinition&>(content.GetItem(content.FindItem(name)));
}
AbilityDefinition& Ability(ContentCatalog& content, const char* name) {
    return const_cast<AbilityDefinition&>(content.GetAbility(content.FindAbility(name)));
}
const ContentCatalog& ExpansionContent() {
    // Expansion equipment lives in the main pack, not the smaller demo pack
    // used by timing/server regression fixtures.
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
        REQUIRE(loaded->FindItem("brass_knuckles").value);
        REQUIRE(loaded->FindItem("cloth_face_wrap").value);
        return eastl::move(*loaded);
    }();
    return content;
}
ContentCatalog ArenaContent() {
    auto content = ExpansionContent();
    ConfigureCombatRuntimeTestContent(content);
    auto& fist = Move(content, "basic_fist");
    fist.requirements.primary.hand = SourceHand::Left;
    fist.strikes[0].contacts = {{ContactRegion::Torso, 1}};
    fist.strikes[0].damage = {{DamageType::Blunt, 100, 0}};
    Style(content, "basic_balanced").moves = {{content.FindCombatMove("basic_fist"), 1}};
    auto& parry = Move(content, "basic_parry");
    parry.requirements.primary.hand = SourceHand::Left;
    Style(content, "basic_defensive").moves = {{content.FindCombatMove("basic_parry"), 1}};
    return content;
}
struct Arena {
    ContentCatalog content;
    World world;
    ActorHandle attacker{}, defender{};
    std::uint32_t rng = 7;
    explicit Arena(ContentCatalog catalog = ArenaContent()) : content(eastl::move(catalog)), world(content) {
        Take(world.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 1; z <= 8; ++z) { for (int x = 1; x <= 8; ++x) {
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor"), 0}));
        } }
        attacker = Take(world.CreateActor({3, 3}, true)); defender = Take(world.CreateActor({4, 3}, true));
        auto state = MakeCombatState(true); state.injury.regeneration = false;
        REQUIRE(world.Combat().Configure(attacker, state)); state.faction = CombatFaction::Undead;
        REQUIRE(world.Combat().Configure(defender, state));
    }
    void Advance(Tick ticks) { world.ElapseTicks(ticks); world.Combat().ProcessEvents(rng); }
    ItemHandle Carry(ActorHandle actor, const char* name) {
        return Take(world.CreateItem(content.FindItem(name), ItemLocation::Carried(actor)));
    }
    Vector<CombatEvent> Events() { Vector<CombatEvent> events; world.Combat().DrainEvents(events); return events; }
};

TEST_CASE("Configure rejects expanded state invariants atomically", "[combat][expansion][configuration]") {
    Arena arena;
    const auto original = arena.world.GetActor(arena.attacker)->combat;
    const auto revision = arena.world.GetRevision();
    auto state = original;
    SECTION("unknown cooldown definition") { state.moveCooldowns.push_back({CombatMoveId{UINT32_MAX}, 1}); }
    SECTION("zero cooldown") { state.moveCooldowns.push_back({arena.content.FindCombatMove("basic_fist"), 0}); }
    SECTION("duplicate cooldown") {
        state.moveCooldowns.push_back({arena.content.FindCombatMove("basic_fist"), 1});
        state.moveCooldowns.push_back(state.moveCooldowns.front());
    }
    SECTION("unknown style") { state.styleId = {UINT32_MAX}; }
    SECTION("invalid exertion") { state.exertion = static_cast<ExertionKind>(255); }
    SECTION("invalid life") { state.life = static_cast<ActorLifeState>(255); }
    SECTION("invalid reach") { state.reach.height = static_cast<ReachHeight>(255); }
    SECTION("out-of-range fraction") { state.stamina = 50; state.staminaRegenerationRemainder = ratioScale; }
    SECTION("fraction conversion cannot wrap") { state.stamina = 50; state.staminaRegenerationRemainder = UINT32_MAX; state.staminaElapsed = 9; }
    SECTION("combined legacy and expanded fractions exceed one") { state.stamina = 50; state.staminaRegenerationRemainder = 9000; state.staminaElapsed = 1; }
    SECTION("full stamina cannot bank a fraction") { state.staminaRegenerationRemainder = 1; }
    SECTION("alive state cannot contain a zero vital") { state.body.parts[static_cast<unsigned>(BodyPart::Head)].hp = 0; }
    const auto result = arena.world.Combat().Configure(arena.attacker, state);
    REQUIRE_FALSE(result); CHECK(result.error() == WorldError::invalid_request);
    CHECK(arena.world.GetActor(arena.attacker)->combat == original);
    CHECK(arena.world.GetRevision() == revision);
}

TEST_CASE("Configure binds legacy setup and converts its fraction before expanded validation", "[combat][expansion][configuration]") {
    Arena arena;
    auto state = MakeCombatState(true); state.style = CombatStyle::Aggressive; state.stamina = 50;
    state.staminaElapsed = 6; state.staminaRegenerationRemainder = 1000;
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    const auto& configured = arena.world.GetActor(arena.attacker)->combat;
    CHECK(configured.styleId == arena.content.FindCombatStyle("basic_aggressive"));
    CHECK(configured.staminaElapsed == 0); CHECK(configured.staminaRegenerationRemainder == 7000);
    CHECK(ValidateCombatState(configured, arena.content));
}

TEST_CASE("Configure routes a valid terminal setup through World's death transition", "[combat][expansion][configuration][death]") {
    Arena arena;
    auto state = arena.world.GetActor(arena.attacker)->combat;
    state.body.parts[static_cast<unsigned>(BodyPart::Head)].hp = 0; state.life = ActorLifeState::Dead;
    REQUIRE(ValidateCombatState(state, arena.content));
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    CHECK_FALSE(arena.world.IsActorAlive(arena.attacker));
    CHECK(arena.world.GetActor(arena.attacker)->combat.life == ActorLifeState::Dead);
    CHECK(arena.world.GetActor(arena.attacker)->actionDeadline.suspended);
    CHECK_FALSE(arena.world.ActorAt({3, 3}).value);
    const auto events = arena.Events(); unsigned deaths = 0;
    for (const auto& event : events) { if (event.kind == CombatEventKind::ActorDied) { ++deaths; } }
    CHECK(deaths == 1);
    arena.world.FinalizeActorDeaths(); CHECK(arena.world.CheckInvariants());
}

TEST_CASE("Handling encumbrance averages only the committed strike's contacts", "[combat][expansion][encumbrance]") {
    EquipmentModifiers modifiers;
    const auto set = [&](ContactRegion region, std::uint32_t value) { modifiers.encumbrance[static_cast<unsigned>(region)] = value; };
    set(ContactRegion::LeftArm, 1); set(ContactRegion::LeftHand, 2);
    set(ContactRegion::RightArm, 9); set(ContactRegion::RightHand, 10);
    set(ContactRegion::LeftLeg, 3); set(ContactRegion::LeftFoot, 4);
    set(ContactRegion::RightLeg, 5); set(ContactRegion::RightFoot, 6);
    set(ContactRegion::Eyes, 1000); set(ContactRegion::Ears, 1000); set(ContactRegion::Torso, 1000);
    set(ContactRegion::AttackApparatus, 7);
    SourceCommitment sources; sources.hands[0].required = true;
    CHECK(combat_runtime::HandlingEncumbrance(modifiers, sources) == 2);
    sources.paired = true; sources.secondaryKind = AttackSourceKind::EmptyHand; sources.hands[1].required = true;
    CHECK(combat_runtime::HandlingEncumbrance(modifiers, sources) == 2);
    CHECK(combat_runtime::HandlingEncumbrance(modifiers, sources, StrikeSource::Secondary) == 10);
    sources.paired = false; sources.primaryKind = AttackSourceKind::HeldItem; sources.hands[0].grip = GripKind::TwoHanded;
    CHECK(combat_runtime::HandlingEncumbrance(modifiers, sources) == 6);
    sources.primaryKind = AttackSourceKind::Kick; sources.functionalLegs = LegMask::Left;
    CHECK(combat_runtime::HandlingEncumbrance(modifiers, sources) == 4);
    sources.functionalLegs = LegMask::Both;
    CHECK(combat_runtime::HandlingEncumbrance(modifiers, sources) == 5);
    sources.primaryKind = AttackSourceKind::Natural;
    CHECK(combat_runtime::HandlingEncumbrance(modifiers, sources) == 7);
}

TEST_CASE("Local combat encumbrance separates punching dodging movement and sensory contacts", "[combat][expansion][encumbrance]") {
    auto content = ArenaContent();
    auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Torso, ratioScale, 10, false}, {ContactRegion::LeftArm, ratioScale, 1, false},
        {ContactRegion::LeftHand, ratioScale, 2, false}, {ContactRegion::Eyes, ratioScale, 1000, false},
        {ContactRegion::Ears, ratioScale, 500, false}, {ContactRegion::RightFoot, ratioScale, 8, false}};
    auto& move = Move(content, "basic_fist"); move.cost = {100, 201, 10};
    Arena arena(eastl::move(content));
    auto state = arena.world.GetActor(arena.attacker)->combat; state.attackRating100 = 20; state.defenseRating100 = 70;
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, arena.content.GetCombatSkills().unarmed, 5));
    const auto item = arena.Carry(arena.attacker, "cloth_face_wrap");
    REQUIRE(arena.world.WearItem(arena.attacker, item).outcome == InventoryOutcome::Succeeded);
    const auto* actor = arena.world.GetActor(arena.attacker);
    Vector<SourceCommitment> bindings; combat_runtime::Bind(arena.world, *actor, arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist")), bindings);
    REQUIRE(bindings.size() == 1); const auto sources = bindings.front(); const auto skill = arena.content.GetCombatSkills().unarmed;
    CHECK(combat_runtime::Rating(arena.world, *actor, skill, sources, StrikeSource::Primary, DefenseKind::None) == 314);
    CHECK(combat_runtime::Rating(arena.world, *actor, skill, sources, StrikeSource::Primary, DefenseKind::Parry) == 364);
    CHECK(combat_runtime::Rating(arena.world, *actor, skill, sources, StrikeSource::Primary, DefenseKind::Block) == 364);
    CHECK(combat_runtime::Rating(arena.world, *actor, skill, sources, StrikeSource::Primary, DefenseKind::Dodge) == 46);
    const auto cost = combat_runtime::Cost(arena.world, *actor, arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist")), sources);
    CHECK(cost.windupCost == 95); CHECK(cost.recoveryCost == 190); CHECK(cost.staminaCost == 10); // 9 base + round(9 * 12%)
    CHECK(combat_runtime::MovementEncumbranceCost(arena.world, *actor, 100) == 102);
    CHECK(combat_runtime::MovementEncumbranceCost(arena.world, *actor, 141) == 144);
    CHECK(combat_runtime::MovementEncumbranceCost(arena.world, *actor, 150) == 153);
    REQUIRE(arena.world.SetActorSpeed(arena.attacker, 125)); actor = arena.world.GetActor(arena.attacker);
    CHECK(combat_runtime::Cost(arena.world, *actor, arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist")), sources) == cost);
    CHECK(actor->speed == 125);
}

TEST_CASE("Sensory-only encumbrance never changes melee accuracy cost or movement", "[combat][expansion][encumbrance]") {
    auto content = ArenaContent(); auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Head, ratioScale, 20, false}, {ContactRegion::Eyes, ratioScale, 21, false},
        {ContactRegion::Mouth, ratioScale, 22, false}, {ContactRegion::Ears, ratioScale, 23, false}};
    Arena arena(eastl::move(content));
    Vector<SourceCommitment> bindings; const auto& move = arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist"));
    combat_runtime::Bind(arena.world, *arena.world.GetActor(arena.attacker), move, bindings); REQUIRE(bindings.size() == 1);
    const auto sources = bindings.front(); const auto skill = arena.content.GetCombatSkills().unarmed;
    const auto before = combat_runtime::Cost(arena.world, *arena.world.GetActor(arena.attacker), move, sources);
    const auto item = arena.Carry(arena.attacker, "cloth_face_wrap"); REQUIRE(arena.world.WearItem(arena.attacker, item).outcome == InventoryOutcome::Succeeded);
    const auto* actor = arena.world.GetActor(arena.attacker);
    for (const auto defense : {DefenseKind::None, DefenseKind::Dodge, DefenseKind::Parry, DefenseKind::Block}) {
        CHECK(combat_runtime::Rating(arena.world, *actor, skill, sources, StrikeSource::Primary, defense) == 0);
    }
    CHECK(combat_runtime::Cost(arena.world, *actor, move, sources) == before);
    CHECK(combat_runtime::MovementEncumbranceCost(arena.world, *actor, 100) == 100);
    CombatInfoView info; arena.world.Combat().CollectInfo(arena.attacker, info);
    CHECK(info.equipment.encumbrance[static_cast<unsigned>(ContactRegion::Eyes)] == 21);
    CHECK(info.equipment.encumbrance[static_cast<unsigned>(ContactRegion::Ears)] == 23);
}

TEST_CASE("Handling time improvement is bounded and preserves positive phases", "[combat][expansion][encumbrance]") {
    auto content = ArenaContent(); Move(content, "basic_fist").cost = {100, 201, 10};
    Arena arena(eastl::move(content)); const auto skill = arena.content.GetCombatSkills().unarmed;
    const auto& move = arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist"));
    Vector<SourceCommitment> bindings; combat_runtime::Bind(arena.world, *arena.world.GetActor(arena.attacker), move, bindings);
    REQUIRE(bindings.size() == 1);
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, skill, 10));
    const auto* actor = arena.world.GetActor(arena.attacker);
    auto cost = combat_runtime::Cost(arena.world, *actor, move, bindings.front());
    CHECK(cost.windupCost == 90); CHECK(cost.recoveryCost == 180); CHECK(cost.staminaCost == 8);
    auto tiny = move; tiny.cost = {1, 1, 0};
    cost = combat_runtime::Cost(arena.world, *actor, tiny, bindings.front());
    CHECK(cost.windupCost == 1); CHECK(cost.recoveryCost == 1); CHECK(cost.staminaCost == 0);
    tiny.cost.windupCost = 0;
    CHECK(combat_runtime::Cost(arena.world, *actor, tiny, bindings.front()).windupCost == 0);
}

TEST_CASE("Simple combat encumbrance uses apparatus locomotion and core zones", "[combat][expansion][encumbrance]") {
    auto content = ArenaContent(); auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Core, ratioScale, 13, false}, {ContactRegion::AttackApparatus, ratioScale, 11, false},
        {ContactRegion::Locomotion, ratioScale, 7, false}};
    Arena arena(eastl::move(content));
    auto state = MakeCombatState(false); state.faction = CombatFaction::Survivors; state.authoredTrainingRanks = true;
    state.attackRating100 = 100; state.defenseRating100 = 200;
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    const auto item = arena.Carry(arena.attacker, "cloth_face_wrap");
    REQUIRE(arena.world.WearItem(arena.attacker, item).outcome == InventoryOutcome::Succeeded);
    const auto* actor = arena.world.GetActor(arena.attacker);
    SourceCommitment sources; sources.primaryKind = AttackSourceKind::Natural; sources.naturalApparatus = true;
    CHECK(combat_runtime::Rating(arena.world, *actor, {}, sources, StrikeSource::Primary, DefenseKind::None) == 67);
    CHECK(combat_runtime::Rating(arena.world, *actor, {}, sources, StrikeSource::Primary, DefenseKind::Parry) == 167);
    CHECK(combat_runtime::Rating(arena.world, *actor, {}, sources, StrikeSource::Primary, DefenseKind::Dodge) == 160);
    CHECK(combat_runtime::MovementEncumbranceCost(arena.world, *actor, 100) == 107);
    auto move = arena.content.GetCombatMove(arena.content.FindCombatMove("basic_natural")); move.cost.staminaCost = 10;
    CHECK(combat_runtime::Cost(arena.world, *actor, move, sources).staminaCost == 12);
}

TEST_CASE("Stamina encumbrance rounds the surcharge without changing movement", "[combat][expansion][encumbrance][stamina]") {
    auto content = ArenaContent(); auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Torso, ratioScale, 1, false},
        {ContactRegion::LeftLeg, ratioScale, 1, false}};
    Arena arena(eastl::move(content));
    const auto item = arena.Carry(arena.attacker, "cloth_face_wrap");
    REQUIRE(arena.world.WearItem(arena.attacker, item).outcome == InventoryOutcome::Succeeded);
    const auto* actor = arena.world.GetActor(arena.attacker);
    SourceCommitment sources; sources.hands[0].required = true;
    auto move = arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist"));
    move.cost.staminaCost = 0;
    CHECK(combat_runtime::Cost(arena.world, *actor, move, sources).staminaCost == 0);
    move.cost.staminaCost = 1;
    CHECK(combat_runtime::Cost(arena.world, *actor, move, sources).staminaCost == 1);
    move.cost.staminaCost = 49;
    CHECK(combat_runtime::Cost(arena.world, *actor, move, sources).staminaCost == 49);
    move.cost.staminaCost = 50;
    CHECK(combat_runtime::Cost(arena.world, *actor, move, sources).staminaCost == 51);
    CHECK(combat_runtime::MovementEncumbranceCost(arena.world, *actor, 1) == 2);
}

TEST_CASE("Heavy and stacked encumbrance still increases stamina costs", "[combat][expansion][encumbrance][stamina]") {
    auto content = ArenaContent(); auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Torso, ratioScale, 25, false}};
    Arena arena(eastl::move(content));
    const auto first = arena.Carry(arena.attacker, "cloth_face_wrap");
    const auto second = arena.Carry(arena.attacker, "cloth_face_wrap");
    REQUIRE(arena.world.WearItem(arena.attacker, first).outcome == InventoryOutcome::Succeeded);
    auto move = arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist"));
    SourceCommitment sources; sources.hands[0].required = true;
    move.cost.staminaCost = 10;
    CHECK(combat_runtime::Cost(arena.world, *arena.world.GetActor(arena.attacker), move, sources).staminaCost == 13);
    REQUIRE(arena.world.WearItem(arena.attacker, second).outcome == InventoryOutcome::Succeeded);
    CHECK(combat_runtime::Cost(arena.world, *arena.world.GetActor(arena.attacker), move, sources).staminaCost == 16);
    move.cost.staminaCost = 0;
    CHECK(combat_runtime::Cost(arena.world, *arena.world.GetActor(arena.attacker), move, sources).staminaCost == 0);
}

TEST_CASE("Sweep applies ability-local modifiers once rather than borrowing move costs", "[combat][expansion][encumbrance][abilities]") {
    auto content = ArenaContent(); auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Torso, ratioScale, 10, false}, {ContactRegion::LeftArm, ratioScale, 10, false},
        {ContactRegion::LeftHand, ratioScale, 10, false}};
    auto& sweep = Ability(content, "sweep"); sweep.cost = {50, 100, 10};
    auto& move = Move(content, "weapon_sweep_strike"); move.requirements.primary = {AttackSourceKind::EmptyHand, SourceHand::Left};
    move.cost = {900, 900, 90}; // Never borrowed by Sweep.
    auto& art = const_cast<CombatArtDefinition&>(content.GetCombatArt(content.FindCombatArt("weapon_forms")));
    for (auto& grant : art.abilities) { grant.unlock = {}; }
    Arena arena(eastl::move(content));
    REQUIRE(arena.world.Combat().GrantArt(arena.attacker, arena.content.FindCombatArt("weapon_forms"), ArtGrantReason::Administration));
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, arena.content.GetCombatSkills().unarmed, 5));
    const auto item = arena.Carry(arena.attacker, "cloth_face_wrap");
    REQUIRE(arena.world.WearItem(arena.attacker, item).outcome == InventoryOutcome::Succeeded);
    AbilityRequest request{arena.content.FindAbility("sweep"), {AbilityGrantSourceKind::Art, arena.content.FindCombatArt("weapon_forms")},
        {AbilityTargetKind::CardinalDirection, 0, {}, CardinalDirection::North}};
    const auto query = arena.world.Combat().QueryAbility(arena.attacker, request); REQUIRE(query.Available());
    CHECK(query.cost.windupCost == 47); CHECK(query.cost.recoveryCost == 95); CHECK(query.cost.staminaCost == 11);
    const auto used = arena.world.Combat().UseAbility(arena.attacker, request, arena.rng); REQUIRE(used.pending);
    CHECK(used.cost == 47); CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 89);
    CHECK(arena.world.GetActor(arena.attacker)->combat.action.recoveryTicks == 95);
}

TEST_CASE("Actor reach restricts move contacts during query selection and resolution", "[combat][expansion][reach]") {
    auto content = ArenaContent();
    Move(content, "basic_fist").strikes[0].contacts = {{ContactRegion::Torso, 100}, {ContactRegion::LeftLeg, 1}};
    Arena arena(eastl::move(content));
    auto state = arena.world.GetActor(arena.attacker)->combat;
    state.reach.height = ReachHeight::Lower; state.reach.regions = ContactBit(ContactRegion::LeftLeg);
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    const auto& strike = arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist")).strikes.front();
    const auto before = arena.rng;
    CHECK(combat_runtime::Contact(*arena.world.GetActor(arena.defender), strike, state.reach, arena.rng) == ContactRegion::LeftLeg);
    CHECK(arena.rng == before);
    REQUIRE(arena.world.Combat().QueryAttack(arena.attacker, arena.defender).Available());
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    const auto events = arena.Events(); bool found = false;
    for (const auto& event : events) { if (event.kind == CombatEventKind::Strike) { found = true; CHECK(event.strike.region == ContactRegion::LeftLeg); } }
    CHECK(found);
}

TEST_CASE("Disjoint actor and move reach consumes no selection or contact draws", "[combat][expansion][reach]") {
    Arena arena;
    auto state = arena.world.GetActor(arena.attacker)->combat; state.reach.height = ReachHeight::Lower;
    state.reach.regions = ContactBit(ContactRegion::LeftLeg);
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    const auto& strike = arena.content.GetCombatMove(arena.content.FindCombatMove("basic_fist")).strikes.front();
    const auto before = arena.rng;
    CHECK_FALSE(arena.world.Combat().CanAttack(arena.attacker));
    CHECK_FALSE(combat_runtime::StrikeReach(arena.world, *arena.world.GetActor(arena.attacker), *arena.world.GetActor(arena.defender), strike));
    CHECK(arena.world.Combat().QueryAttack(arena.attacker, arena.defender).reason == CombatRefusal::OutOfReach);
    CHECK_FALSE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    CHECK(combat_runtime::Contact(*arena.world.GetActor(arena.defender), strike, state.reach, arena.rng) == ContactRegion::Count);
    CHECK(arena.rng == before);
    auto upper = strike; upper.reach.height = ReachHeight::Upper; upper.contacts.clear();
    CHECK(combat_runtime::Contact(*arena.world.GetActor(arena.defender), upper, state.reach, arena.rng) == ContactRegion::Count);
    CHECK(arena.rng == before);
}

TEST_CASE("Melee reach rejects an obstructed source tile", "[combat][expansion][reach]") {
    Arena arena; REQUIRE(arena.world.Combat().CanReach(arena.attacker, arena.defender));
    const auto source = arena.world.GetActor(arena.attacker)->position;
    REQUIRE(arena.world.SetTile(source, {arena.content.FindTerrain("wall"), 0}));
    CHECK_FALSE(arena.world.Combat().CanReach(arena.attacker, arena.defender));
}

TEST_CASE("Reaction probabilities use precise ratings and one partitioned outcome draw", "[combat][expansion][rules]") {
    CHECK(AttackRating100(2, 3) == 260);
    CHECK(DodgeRating100(3) == 300);
    CHECK(DefenseChance(DefenseKind::Dodge, 260, 300) == 4200);
    CHECK(DefenseChance(DefenseKind::Dodge, 300, 300) == 4000);
    CHECK(DefenseChance(DefenseKind::Parry, 300, 300) == 4000);
    CHECK(DefenseChance(DefenseKind::Block, 300, 300) == 6000);
    CHECK(DefenseChance(DefenseKind::None, -100000, 100000) == 0);
    CHECK(DefenseChance(DefenseKind::Parry, 100000, -100000) == 500);
    CHECK(DefenseChance(DefenseKind::Block, -100000, 100000) == 9000);

    CHECK(ResolveStrikeDraw(499, 500, 4000) == StrikeDrawOutcome::Miss);
    CHECK(ResolveStrikeDraw(500, 500, 4000) == StrikeDrawOutcome::DefenseSucceeded);
    CHECK(ResolveStrikeDraw(4299, 500, 4000) == StrikeDrawOutcome::DefenseSucceeded);
    CHECK(ResolveStrikeDraw(4300, 500, 4000) == StrikeDrawOutcome::Contact);
    unsigned misses = 0, responses = 0, contacts = 0;
    for (std::uint32_t draw = 0; draw < ratioScale; ++draw) {
        switch (ResolveStrikeDraw(draw, 500, 4000)) {
        case StrikeDrawOutcome::Miss: ++misses; break;
        case StrikeDrawOutcome::DefenseSucceeded: ++responses; break;
        case StrikeDrawOutcome::Contact: ++contacts; break;
        }
    }
    CHECK(misses == 500); CHECK(responses == 3800); CHECK(contacts == 5700);
}

TEST_CASE("Shared block budget conserves absorption with stable damage-type remainder ties", "[combat][expansion][rules]") {
    const DamagePacket packets[] = {{DamageType::Blunt, 600, 0}, {DamageType::Piercing, 400, 0}};
    eastl::array<std::uint32_t, 2> absorbed{};
    CHECK(AllocateBlockAbsorption({packets, 2}, 501, {absorbed.data(), absorbed.size()}) == 501);
    CHECK(absorbed[0] == 301); CHECK(absorbed[1] == 200);
    CHECK(AllocateBlockAbsorption({packets, 2}, 5000, {absorbed.data(), absorbed.size()}) == 1000);
    CHECK(absorbed[0] == 600); CHECK(absorbed[1] == 400);
    CHECK(AllocateBlockAbsorption({packets, 2}, 0, {absorbed.data(), absorbed.size()}) == 0);
    CHECK(absorbed[0] == 0); CHECK(absorbed[1] == 0);

    const DamagePacket tied[] = {{DamageType::Piercing, 1, 0}, {DamageType::Blunt, 1, 0}, {DamageType::Slashing, 1, 0}};
    const DamagePacket reordered[] = {tied[2], tied[0], tied[1]};
    eastl::array<std::uint32_t, 3> first{}, second{};
    CHECK(AllocateBlockAbsorption({tied, 3}, 2, {first.data(), first.size()}) == 2);
    CHECK(AllocateBlockAbsorption({reordered, 3}, 2, {second.data(), second.size()}) == 2);
    CHECK(first[0] == 0); CHECK(first[1] == 1); CHECK(first[2] == 1);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (tied[i].type == reordered[j].type) { CHECK(first[i] == second[j]); }
        }
    }
}

TEST_CASE("Weighted move selection chooses a definition before source bindings", "[combat][expansion][selection]") {
    auto content = ArenaContent(); World world(content);
    const auto fist = content.FindCombatMove("basic_fist"), kick = content.FindCombatMove("basic_kick");
    Vector<combat_runtime::Candidate> one{{fist, {}, {}, 3}, {kick, {}, {}, 7}};
    auto two = one; two.insert(two.begin() + 1, one.front()); two[1].sources.primaryHand = Hand::Right;
    for (std::uint32_t seed = 0; seed < 256; ++seed) {
        auto a = seed, b = seed;
        CHECK(combat_runtime::Select(world, one, a).move == combat_runtime::Select(world, two, b).move);
    }
}

TEST_CASE("Attack queries and neighbor discovery consume no resources or RNG", "[combat][expansion][query]") {
    Arena arena;
    const auto before = arena.world.GetActor(arena.attacker)->combat;
    const auto rng = arena.rng;
    REQUIRE(arena.world.Combat().QueryAttack(arena.attacker, arena.defender).Available());
    Vector<ActorHandle> targets; arena.world.Combat().CollectAdjacentTargets(arena.attacker, targets);
    REQUIRE(targets.size() == 1); CHECK(targets[0] == arena.defender);
    CHECK(arena.world.Combat().ChooseAdjacentTarget(arena.attacker, arena.rng) == arena.defender);
    CHECK(arena.rng == rng); // DrawBelow(1) is deliberately free.
    CHECK(arena.world.GetActor(arena.attacker)->combat == before);
    CHECK(arena.world.GetActor(arena.attacker)->turns == 0);
}

TEST_CASE("An unusable two-handed weapon does not veto an independent kick", "[combat][expansion][sources]") {
    auto content = ArenaContent();
    auto& profile = *ItemType(content, "brass_knuckles").melee;
    profile.grips = {{GripKind::TwoHanded}}; profile.hands = 2;
    Style(content, "basic_balanced").moves = {{content.FindCombatMove("basic_weapon"), 1}, {content.FindCombatMove("basic_kick"), 1}};
    Arena arena(eastl::move(content));
    const auto weapon = arena.Carry(arena.attacker, "brass_knuckles");
    REQUIRE(arena.world.HoldItem(arena.attacker, weapon, Hand::Left, GripKind::TwoHanded).outcome == InventoryOutcome::Succeeded);
    arena.world.Combat().Damage(arena.attacker, {}, BodyPart::LeftHand, {DamageType::Blunt, 10000, 0});
    const auto attack = arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng);
    REQUIRE(attack.committed); CHECK(attack.moveId == arena.content.FindCombatMove("basic_kick"));
    CHECK(arena.world.GetActor(arena.attacker)->equipment.hands[1].item == weapon);
}

TEST_CASE("Authored bite remains available without an attack apparatus", "[combat][expansion][sources]") {
    auto content = TestContent();
    auto& bite = Move(content, "basic_bite");
    REQUIRE_FALSE(bite.requirements.primary.requiresNaturalApparatus);
    REQUIRE(Move(content, "basic_natural").requirements.primary.requiresNaturalApparatus);
    bite.cost.windupCost = 20;
    bite.strikes[0].intrinsicMiss = ratioScale;
    Arena arena(eastl::move(content));
    auto state = MakeCombatState(false); state.faction = CombatFaction::Survivors;
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    arena.world.Combat().Damage(arena.attacker, {}, BodyPart::AttackApparatus, {DamageType::Blunt, 6000, 0});
    const auto result = arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng);
    REQUIRE(result.committed); REQUIRE(result.outcome == AttackOutcome::Windup);
    CHECK(result.moveId == arena.content.FindCombatMove("basic_bite"));
    CHECK_FALSE(arena.world.GetActor(arena.attacker)->combat.action.sources.naturalApparatus);
    arena.Advance(result.cost);
    CHECK(arena.world.GetActor(arena.attacker)->combat.action.kind == ActorActionKind::Recovery);
    CHECK(arena.world.GetActorRemainingTicks(arena.attacker) == 130);
}

TEST_CASE("Knuckles occupy a hand but retain Unarmed handling", "[combat][expansion][sources][skills]") {
    auto content = ArenaContent();
    Style(content, "basic_balanced").moves = {{content.FindCombatMove("basic_weapon"), 1}};
    Move(content, "basic_weapon").strikes[0].intrinsicMiss = ratioScale;
    Arena arena(eastl::move(content));
    const auto weapon = arena.Carry(arena.attacker, "brass_knuckles");
    REQUIRE(arena.world.HoldItem(arena.attacker, weapon, Hand::Left, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    const auto& skills = arena.world.GetActor(arena.attacker)->learning;
    const auto* melee = FindSkillProgress(skills, arena.content.GetCombatSkills().melee);
    const auto* unarmed = FindSkillProgress(skills, arena.content.GetCombatSkills().unarmed);
    REQUIRE(melee); REQUIRE(unarmed); CHECK(melee->xp == 4000); CHECK(unarmed->xp == 6000);
}

TEST_CASE("Paired windup loses its captured source without substituting another hand", "[combat][expansion][commitment]") {
    auto content = ArenaContent();
    auto& paired = Move(content, "basic_paired"); paired.cost = {20, 100, 10};
    Style(content, "basic_balanced").moves = {{content.FindCombatMove("basic_paired"), 1}};
    Arena arena(eastl::move(content));
    for (const auto hand : {Hand::Left, Hand::Right}) {
        const auto item = arena.Carry(arena.attacker, "brass_knuckles");
        REQUIRE(arena.world.HoldItem(arena.attacker, item, hand, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    }
    const auto attack = arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng);
    REQUIRE(attack.outcome == AttackOutcome::Windup);
    const auto sources = arena.world.GetActor(arena.attacker)->combat.action.sources;
    REQUIRE(sources.paired); REQUIRE(sources.hands[1].required);
    REQUIRE(arena.world.ReleaseHand(arena.attacker, Hand::Right).outcome == InventoryOutcome::Busy);
    // An ordinary equipment command cannot interrupt a paid windup. Force a
    // disarm through World ownership mutation and check its cancellation policy.
    const auto item = arena.world.GetActor(arena.attacker)->equipment.hands[1].item;
    REQUIRE(arena.world.TransferItem(item, ItemLocation::Ground(arena.world.GetActor(arena.attacker)->position)));
    CHECK_FALSE(combat_runtime::SourcesValid(arena.world, *arena.world.GetActor(arena.attacker), sources));
    REQUIRE(arena.world.GetActor(arena.attacker)->combat.action.kind == ActorActionKind::DisruptionRecovery);
    const auto cancellation = arena.world.GetActorRemainingTicks(arena.attacker); REQUIRE(cancellation > 0);
    const auto before = arena.rng; const auto body = arena.world.GetActor(arena.defender)->combat.body;
    arena.Advance(cancellation);
    CHECK(arena.rng == before); CHECK(arena.world.GetActor(arena.defender)->combat.body == body);
    CHECK(arena.world.GetActor(arena.attacker)->combat.action.kind == ActorActionKind::None);
    CHECK(arena.world.GetActor(arena.attacker)->equipment.hands[0].item.value != 0);
    CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 90);
}

TEST_CASE("Intrinsic misses still pay the single committed reaction", "[combat][expansion][reactions]") {
    auto content = ArenaContent(); Move(content, "basic_fist").strikes[0].intrinsicMiss = ratioScale;
    Arena arena(eastl::move(content)); REQUIRE(arena.world.Combat().SetStyle(arena.defender, CombatStyle::Defensive));
    const auto result = arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng);
    REQUIRE(result.outcome == AttackOutcome::Miss);
    CHECK(arena.world.GetActor(arena.defender)->combat.stamina == 90);
    const auto events = arena.Events(); unsigned responses = 0;
    for (const auto& event : events) {
        if (event.kind == CombatEventKind::ActionCommitted && event.actor == arena.world.GetActor(arena.defender)->id) { ++responses; }
        if (event.kind == CombatEventKind::Strike) { CHECK(event.strike.response == DefenseKind::Parry); CHECK_FALSE(event.strike.defenseSucceeded); }
    }
    CHECK(responses == 1);
    CHECK_FALSE(FindSkillProgress(arena.world.GetActor(arena.defender)->learning, arena.content.GetCombatSkills().dodge));
}

TEST_CASE("A counter consumes its opening after all incoming strikes without recursive counters", "[combat][expansion][counter]") {
    auto content = ArenaContent();
    auto& attack = Move(content, "basic_fist"); attack.strikes.push_back(attack.strikes.front());
    auto& parry = Move(content, "basic_parry"); parry.followThrough.condition = EffectCondition::DefenseSucceeded;
    parry.followThrough.counterOpening = 100;
    auto& counter = Move(content, "basic_counter"); counter.requirements.primary = {AttackSourceKind::EmptyHand, SourceHand::Left};
    counter.strikes[0].intrinsicMiss = 0; counter.strikes[0].contacts = {{ContactRegion::Torso, 1}};
    counter.strikes[0].damage = {{DamageType::Blunt, 100, 0}};
    Style(content, "basic_defensive").moves.push_back({content.FindCombatMove("basic_counter"), 1});
    Arena arena(eastl::move(content));
    REQUIRE(arena.world.Combat().Attack(arena.defender, arena.attacker, arena.rng).committed);
    REQUIRE(arena.world.Combat().SetStyle(arena.defender, CombatStyle::Defensive));
    arena.Events();
    const auto priorRecovery = arena.world.GetActorRemainingTicks(arena.defender);
    for (arena.rng = 0; ; ++arena.rng) { auto draw = arena.rng; if (DrawBelow(draw, ratioScale) < 4000) { break; } }
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    const auto events = arena.Events(); Vector<StrikeResult> strikes;
    for (const auto& event : events) { if (event.kind == CombatEventKind::Strike) { strikes.push_back(event.strike); } }
    REQUIRE(strikes.size() == 3); CHECK_FALSE(strikes[0].counter); CHECK_FALSE(strikes[1].counter); CHECK(strikes[2].counter);
    CHECK(strikes[2].attacker == arena.world.GetActor(arena.defender)->id);
    CHECK(arena.world.GetActor(arena.defender)->combat.openings.empty());
    CHECK(arena.world.GetActorRemainingTicks(arena.defender) >= priorRecovery);
}

TEST_CASE("Counter windup overlaps prior recovery with one matching stamina lock", "[combat][expansion][counter][stamina]") {
    for (const std::uint32_t priorRecovery : {200U, 100U}) {
        CAPTURE(priorRecovery);
        auto content = ArenaContent(); Move(content, "basic_fist").cost.recoveryCost = priorRecovery;
        auto& parry = Move(content, "basic_parry"); parry.cost = {0, 30, 0};
        parry.followThrough.condition = EffectCondition::DefenseSucceeded; parry.followThrough.counterOpening = 300;
        auto& counter = Move(content, "basic_counter"); counter.cost = {50, 90, 5};
        counter.requirements.primary = {AttackSourceKind::EmptyHand, SourceHand::Left};
        counter.strikes[0].intrinsicMiss = ratioScale;
        Style(content, "basic_defensive").moves.push_back({content.FindCombatMove("basic_counter"), 1});
        Arena arena(eastl::move(content));
        REQUIRE(arena.world.Combat().Attack(arena.defender, arena.attacker, arena.rng).committed);
        REQUIRE(arena.world.GetActorRemainingTicks(arena.defender) == priorRecovery);
        REQUIRE(arena.world.Combat().SetStyle(arena.defender, CombatStyle::Defensive));
        for (arena.rng = 0; ; ++arena.rng) { auto draw = arena.rng; if (DrawBelow(draw, ratioScale) < 4000) { break; } }
        REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
        const Tick duration = priorRecovery > 140 ? priorRecovery : 140;
        const auto* actor = arena.world.GetActor(arena.defender);
        REQUIRE(actor->combat.action.counter); CHECK(actor->combat.action.kind == ActorActionKind::MeleeWindup);
        CHECK(actor->combat.action.recoveryTicks == duration - 50);
        CHECK(arena.world.GetActorRemainingTicks(arena.defender) == 50);
        CHECK(actor->combat.combatLockRemaining == duration); CHECK(actor->combat.stamina == 95);
        arena.Advance(50); actor = arena.world.GetActor(arena.defender);
        CHECK(actor->combat.action.kind == ActorActionKind::Recovery);
        CHECK(arena.world.GetActorRemainingTicks(arena.defender) == duration - 50);
        CHECK(actor->combat.combatLockRemaining == duration - 50); CHECK(actor->combat.stamina == 95);
        arena.Advance(duration - 51); CHECK(arena.world.GetActor(arena.defender)->combat.stamina == 95);
        CHECK(arena.world.GetActorRemainingTicks(arena.defender) == 1);
        arena.Advance(1); actor = arena.world.GetActor(arena.defender);
        CHECK(actor->combat.action.kind == ActorActionKind::None); CHECK(actor->combat.combatLockRemaining == 0);
        CHECK(actor->combat.stamina == 95);
        arena.Advance(10); CHECK(arena.world.GetActor(arena.defender)->combat.stamina == 96);
    }
}

TEST_CASE("Shared block precedes combined armor and penetration with one injury", "[combat][expansion][armor]") {
    auto content = ArenaContent();
    Move(content, "basic_fist").strikes[0].damage = {{DamageType::Blunt, 600, 50}, {DamageType::Piercing, 400, 50}};
    auto& block = Move(content, "basic_block"); block.requirements.primary = {AttackSourceKind::EmptyHand, SourceHand::Left};
    block.defense.blockCapacity = 500; block.defense.usesSourceCapacity = false; block.followThrough.counterOpening = 0;
    Style(content, "basic_defensive").moves = {{content.FindCombatMove("basic_block"), 1}};
    Arena arena(eastl::move(content));
    auto state = arena.world.GetActor(arena.defender)->combat; state.style = CombatStyle::Defensive;
    state.styleId = arena.content.FindCombatStyle("basic_defensive"); state.defenseRating100 = 100000;
    state.protection[static_cast<unsigned>(BodyPart::Torso)][static_cast<unsigned>(DamageType::Blunt)] = 100;
    state.protection[static_cast<unsigned>(BodyPart::Torso)][static_cast<unsigned>(DamageType::Piercing)] = 100;
    REQUIRE(arena.world.Combat().Configure(arena.defender, state));
    // No move/binding/contact draw in this fixture. Choose a successful block band.
    for (arena.rng = 0; ; ++arena.rng) { auto draw = arena.rng; if (DrawBelow(draw, ratioScale) < 9000) { break; } }
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    const auto events = arena.Events(); bool found = false;
    for (const auto& event : events) { if (event.kind == CombatEventKind::Strike) {
        found = true; CHECK(event.strike.incomingDamage == 1000); CHECK(event.strike.blockAbsorbed == 500);
        CHECK(event.strike.armorAbsorbed == 100); CHECK(event.strike.postProtectionDamage == 400); CHECK(event.strike.hpLoss == 400);
    } }
    CHECK(found);
}

TEST_CASE("Garment coverage draws are independent and persistent-ID ordered", "[combat][expansion][armor][rng]") {
    auto content = ArenaContent();
    auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Torso, 5000, 0, false}};
    armor.protection = {}; armor.protection[0] = 200;
    Arena arena(eastl::move(content));
    const auto first = arena.Carry(arena.defender, "cloth_face_wrap"), second = arena.Carry(arena.defender, "cloth_face_wrap");
    REQUIRE(arena.world.WearItem(arena.defender, second).outcome == InventoryOutcome::Succeeded);
    REQUIRE(arena.world.WearItem(arena.defender, first).outcome == InventoryOutcome::Succeeded);
    auto expectedRng = arena.rng;
    const bool firstCovered = DrawBelow(expectedRng, ratioScale) < 5000;
    const bool secondCovered = DrawBelow(expectedRng, ratioScale) < 5000;
    const unsigned covered = unsigned(firstCovered) + unsigned(secondCovered);
    const DamagePacket packet{DamageType::Blunt, 1000, 100};
    const auto result = arena.world.Combat().DamageContact(arena.defender, arena.attacker, ContactRegion::Torso, {&packet, 1}, arena.rng);
    CHECK(arena.rng == expectedRng);
    CHECK(result.armorAbsorbed == (covered ? covered * 200 - 100 : 0));
}

TEST_CASE("Mouth clothing protects only its face contact despite shared Head HP", "[combat][expansion][armor][contacts]") {
    auto content = ArenaContent();
    auto& armor = *ItemType(content, "cloth_face_wrap").armor;
    armor.regions = {{ContactRegion::Mouth, ratioScale, 2, false}};
    armor.protection = {}; armor.protection[static_cast<unsigned>(DamageType::Blunt)] = 1000;
    Arena arena(eastl::move(content));
    auto state = arena.world.GetActor(arena.defender)->combat;
    state.protection[static_cast<unsigned>(BodyPart::Head)][static_cast<unsigned>(DamageType::Blunt)] = 100;
    REQUIRE(arena.world.Combat().Configure(arena.defender, state));
    const auto garment = arena.Carry(arena.defender, "cloth_face_wrap");
    REQUIRE(arena.world.WearItem(arena.defender, garment).outcome == InventoryOutcome::Succeeded);
    const auto beforeRng = arena.rng;
    const DamagePacket damage{DamageType::Blunt, 1100, 0};
    for (const auto region : {ContactRegion::Head, ContactRegion::Eyes, ContactRegion::Mouth, ContactRegion::Ears}) {
        const auto result = arena.world.Combat().DamageContact(arena.defender, arena.attacker, region, {&damage, 1}, arena.rng);
        CHECK(result.injuryPart == BodyPart::Head);
        CHECK(result.hpLoss == (region == ContactRegion::Mouth ? 0U : 1000U));
        CHECK(result.armorAbsorbed == (region == ContactRegion::Mouth ? 1100U : 100U));
    }
    CHECK(arena.rng == beforeRng);
    CHECK(arena.world.GetActor(arena.defender)->combat.body.parts[static_cast<unsigned>(BodyPart::Head)].hp == 7000);
    CombatInfoView view; arena.world.Combat().CollectInfo(arena.defender, view);
    CHECK(view.equipment.encumbrance[static_cast<unsigned>(ContactRegion::Mouth)] == 2);
    CHECK(view.equipment.encumbrance[static_cast<unsigned>(ContactRegion::Head)] == 0);
    CHECK(view.equipment.encumbrance[static_cast<unsigned>(ContactRegion::Eyes)] == 0);
    CHECK(view.equipment.encumbrance[static_cast<unsigned>(ContactRegion::Ears)] == 0);
}

TEST_CASE("Combat stamina lock accounting is subdivision-independent", "[combat][expansion][stamina]") {
    Arena whole, split;
    for (auto* arena : {&whole, &split}) {
        auto state = arena->world.GetActor(arena->attacker)->combat;
        state.stamina = 50; state.combatLockRemaining = 17;
        REQUIRE(arena->world.Combat().Configure(arena->attacker, state));
    }
    whole.Advance(57); for (int i = 0; i < 57; ++i) { split.Advance(1); }
    CHECK(whole.world.GetActor(whole.attacker)->combat == split.world.GetActor(split.attacker)->combat);
    CHECK(whole.world.GetActor(whole.attacker)->combat.stamina == 54);
}

TEST_CASE("Windup and its full recovery do not regenerate committed stamina", "[combat][expansion][stamina]") {
    Arena arena; REQUIRE(arena.world.Combat().SetStyle(arena.attacker, CombatStyle::Aggressive));
    const auto used = arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng);
    REQUIRE(used.outcome == AttackOutcome::Windup); CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 80);
    arena.Advance(80); CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 80);
    arena.Advance(120); CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 80);
    arena.Advance(10); CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 81);
}

TEST_CASE("Exhausted fallback is used only when ordinary moves cannot be afforded", "[combat][expansion][stamina]") {
    auto content = ArenaContent(); Move(content, "basic_fist").cost.staminaCost = 5;
    Style(content, "basic_balanced").moves.push_back({content.FindCombatMove("basic_exhausted_fist"), 1000});
    Arena arena(eastl::move(content));
    auto state = arena.world.GetActor(arena.attacker)->combat; state.stamina = 1;
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    const auto result = arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng);
    REQUIRE(result.committed); CHECK(result.moveId == arena.content.FindCombatMove("basic_exhausted_fist"));
    CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 1);
    arena.Advance(result.cost);
    state = arena.world.GetActor(arena.attacker)->combat; state.stamina = 100;
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    const auto normal = arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng);
    REQUIRE(normal.committed); CHECK(normal.moveId == arena.content.FindCombatMove("basic_fist"));
}

TEST_CASE("Two-level combat difficulty gap still trains the fixed 40/60 budget", "[combat][expansion][skills]") {
    auto content = ArenaContent(); Move(content, "basic_fist").strikes[0].intrinsicMiss = ratioScale;
    Arena arena(eastl::move(content)); const auto skills = arena.content.GetCombatSkills();
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, skills.melee, 4));
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, skills.unarmed, 4));
    auto state = arena.world.GetActor(arena.defender)->combat; state.authoredTrainingRanks = true; state.defensiveTrainingRank = 2;
    REQUIRE(arena.world.Combat().Configure(arena.defender, state));
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    const auto& learning = arena.world.GetActor(arena.attacker)->learning;
    CHECK(FindSkillProgress(learning, skills.melee)->xp == 400);
    CHECK(FindSkillProgress(learning, skills.unarmed)->xp == 600);
    CHECK_FALSE(FindSkillProgress(arena.world.GetActor(arena.defender)->learning, skills.dodge));
}

TEST_CASE("Encounter suppression survives drains and refreshes until inactivity expiry", "[combat][expansion][skills][events]") {
    auto content = ArenaContent(); Move(content, "basic_fist").strikes[0].intrinsicMiss = ratioScale;
    Arena arena(eastl::move(content)); const auto skills = arena.content.GetCombatSkills();
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, skills.melee, 3));
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, skills.unarmed, 3));
    auto state = arena.world.GetActor(arena.defender)->combat; state.authoredTrainingRanks = true; state.defensiveTrainingRank = 0;
    REQUIRE(arena.world.Combat().Configure(arena.defender, state));
    const auto notices = [&](ActorHandle target) {
        REQUIRE(arena.world.Combat().Attack(arena.attacker, target, arena.rng).committed);
        const auto events = arena.Events(); unsigned count = 0;
        for (const auto& event : events) { if (event.kind == CombatEventKind::TrainingSuppressed) { ++count; } }
        return count;
    };
    CHECK(notices(arena.defender) == 1);
    arena.Advance(2500); CHECK(notices(arena.defender) == 0);
    arena.Advance(2500); CHECK(notices(arena.defender) == 0); // Original expiry passed; interaction refreshed it.
    const auto second = Take(arena.world.CreateActor({3, 4}, true));
    REQUIRE(arena.world.Combat().Configure(second, state));
    arena.Advance(100); CHECK(notices(second) == 1); // Suppression is opponent-specific.
    arena.Advance(30 * ticks_per_turn); CHECK(notices(arena.defender) == 1);
}

TEST_CASE("Physical strike metrics report actual loss rather than overkill", "[combat][expansion][events][death]") {
    Arena arena;
    const DamagePacket packet{DamageType::Blunt, 100000, 0};
    const auto result = arena.world.Combat().DamageContact(arena.defender, arena.attacker, ContactRegion::Torso, {&packet, 1}, arena.rng);
    CHECK(result.hpLoss == 10000); CHECK(result.postProtectionDamage == 100000); CHECK(result.killed);
    CHECK_FALSE(arena.world.IsActorAlive(arena.defender));
    CHECK_FALSE(arena.world.Combat().QueryAttack(arena.defender, arena.attacker).Available());
    arena.world.FinalizeActorDeaths(); CHECK_FALSE(arena.world.ActorAt({4, 3}).value);
}

TEST_CASE("A killing action publishes one strike before one terminal death", "[combat][expansion][events][death]") {
    auto content = ArenaContent(); Move(content, "basic_fist").strikes[0].damage = {{DamageType::Blunt, 100000, 0}};
    Arena arena(eastl::move(content));
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).outcome == AttackOutcome::Defeated);
    arena.world.MarkActorDead(arena.defender, arena.world.GetActor(arena.attacker)->id);
    arena.world.FinalizeActorDeaths(); arena.world.FinalizeActorDeaths();
    const auto events = arena.Events(); unsigned strikes = 0, deaths = 0;
    std::size_t strikeIndex = events.size(), deathIndex = events.size();
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].kind == CombatEventKind::Strike) { ++strikes; strikeIndex = i; CHECK(events[i].strike.hpLoss == 10000); }
        if (events[i].kind == CombatEventKind::ActorDied) { ++deaths; deathIndex = i; }
    }
    CHECK(strikes == 1); CHECK(deaths == 1); CHECK(strikeIndex < deathIndex);
}

TEST_CASE("Combat event draining is destructive and producer history is bounded", "[combat][expansion][events]") {
    Arena arena;
    for (unsigned i = 0; i < 4200; ++i) { CombatEvent event; event.actor = i + 1; arena.world.Combat().EmitEvent(event); }
    auto events = arena.Events(); REQUIRE(events.size() == 4096); CHECK(events.front().actor == 105); CHECK(events.back().actor == 4200);
    arena.world.Combat().DrainEvents(events); CHECK(events.empty());
}

TEST_CASE("Focus refreshes rather than stacking and cooldown is per art source", "[combat][expansion][abilities]") {
    auto content = ArenaContent(); auto& focus = Ability(content, "focus"); focus.cooldown = 0; focus.cost = {0, 10, 0}; focus.focusDuration = 100;
    Arena arena(eastl::move(content)); const auto art = arena.content.FindCombatArt("boxing");
    REQUIRE(arena.world.Combat().GrantArt(arena.attacker, art, ArtGrantReason::Administration));
    AbilityRequest request{arena.content.FindAbility("focus"), {AbilityGrantSourceKind::Art, art}, {AbilityTargetKind::Self}};
    REQUIRE(arena.world.Combat().UseAbility(arena.attacker, request, arena.rng).committed); arena.Advance(10);
    REQUIRE(arena.world.Combat().UseAbility(arena.attacker, request, arena.rng).committed);
    CHECK(arena.world.GetActor(arena.attacker)->combat.focusRemaining == 100);
    CHECK(arena.world.GetActor(arena.attacker)->combat.focusRatingBonus100 == 100);
}

TEST_CASE("Baseline styles grant no art or art ability implicitly", "[combat][expansion][abilities]") {
    Arena arena;
    const auto* actor = arena.world.GetActor(arena.attacker);
    CHECK_FALSE(arena.content.GetCombatStyle(actor->combat.styleId).art.value);
    CHECK((!actor->characterCombat || actor->characterCombat->arts.empty()));
    AbilityRequest request{arena.content.FindAbility("focus"),
        {AbilityGrantSourceKind::Art, arena.content.FindCombatArt("boxing")}, {AbilityTargetKind::Self}};
    CHECK(arena.world.Combat().QueryAbility(arena.attacker, request).reason == CombatRefusal::MissingGrant);
}

TEST_CASE("Disabling combat training preserves execution while freezing skill and art awards", "[combat][expansion][skills]") {
    for (const bool learnedStyle : {false, true}) {
        CAPTURE(learnedStyle);
        auto content = ArenaContent();
        Move(content, "basic_fist").cost.staminaCost = 5;
        Move(content, "basic_fist").strikes[0].intrinsicMiss = ratioScale;
        Style(content, "boxing_guard").moves = {{content.FindCombatMove("basic_fist"), 1}};
        Arena disabled(content), enabled(content);
        for (auto* arena : {&disabled, &enabled}) {
            if (learnedStyle) {
                REQUIRE(arena->world.Combat().GrantArt(arena->attacker, arena->content.FindCombatArt("boxing"), ArtGrantReason::Administration));
                auto state = arena->world.GetActor(arena->attacker)->combat;
                state.styleId = arena->content.FindCombatStyle("boxing_guard");
                REQUIRE(arena->world.Combat().Configure(arena->attacker, state));
            }
            REQUIRE(arena->world.Combat().SetStyle(arena->defender, CombatStyle::Defensive));
        }
        const auto attackLearning = disabled.world.GetActor(disabled.attacker)->learning;
        const auto defenseLearning = disabled.world.GetActor(disabled.defender)->learning;
        const auto character = CloneCharacterCombat(disabled.world.GetActor(disabled.attacker)->characterCombat.get());
        disabled.world.Combat().SetTrainingEnabled(false);
        const auto noTraining = disabled.world.Combat().Attack(disabled.attacker, disabled.defender, disabled.rng);
        const auto withTraining = enabled.world.Combat().Attack(enabled.attacker, enabled.defender, enabled.rng);
        REQUIRE(noTraining.committed); CHECK(noTraining == withTraining); CHECK(disabled.rng == enabled.rng);
        CHECK(disabled.world.GetActor(disabled.attacker)->combat == enabled.world.GetActor(enabled.attacker)->combat);
        CHECK(disabled.world.GetActor(disabled.defender)->combat == enabled.world.GetActor(enabled.defender)->combat);
        CHECK(disabled.world.GetActor(disabled.attacker)->combat.stamina == 95);
        CHECK(disabled.world.GetActor(disabled.defender)->combat.stamina == 90);
        CHECK(disabled.world.GetActor(disabled.attacker)->learning == attackLearning);
        CHECK(disabled.world.GetActor(disabled.defender)->learning == defenseLearning);
        CHECK(EqualCharacterCombat(character.get(), disabled.world.GetActor(disabled.attacker)->characterCombat.get()));
        const auto skills = content.GetCombatSkills();
        const auto* offense = FindSkillProgress(enabled.world.GetActor(enabled.attacker)->learning, skills.melee);
        const auto* defense = FindSkillProgress(enabled.world.GetActor(enabled.defender)->learning, skills.melee);
        REQUIRE(offense); REQUIRE(defense); CHECK(offense->xp == 4000); CHECK(defense->xp == 4000);
        if (learnedStyle) { CHECK(enabled.world.GetActor(enabled.attacker)->characterCombat->arts.front().xp == ratioScale); }

        disabled.Advance(noTraining.cost);
        disabled.world.Combat().SetTrainingEnabled(true);
        REQUIRE(disabled.world.Combat().Attack(disabled.attacker, disabled.defender, disabled.rng).committed);
        CHECK(disabled.world.GetActor(disabled.attacker)->learning == enabled.world.GetActor(enabled.attacker)->learning);
        CHECK(disabled.world.GetActor(disabled.defender)->learning == enabled.world.GetActor(enabled.defender)->learning);
        CHECK(EqualCharacterCombat(disabled.world.GetActor(disabled.attacker)->characterCombat.get(),
            enabled.world.GetActor(enabled.attacker)->characterCombat.get()));
    }
}

TEST_CASE("Disabled training neither emits suppression nor consumes its encounter notice", "[combat][expansion][skills][events]") {
    auto content = ArenaContent(); Move(content, "basic_fist").strikes[0].intrinsicMiss = ratioScale;
    Arena arena(eastl::move(content)); const auto skills = arena.content.GetCombatSkills();
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, skills.melee, 3));
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, skills.unarmed, 3));
    auto state = arena.world.GetActor(arena.defender)->combat; state.authoredTrainingRanks = true; state.defensiveTrainingRank = 0;
    REQUIRE(arena.world.Combat().Configure(arena.defender, state));
    const auto countNotices = [&]() {
        const auto events = arena.Events(); unsigned count = 0;
        for (const auto& event : events) { if (event.kind == CombatEventKind::TrainingSuppressed) { ++count; } }
        return count;
    };
    arena.world.Combat().SetTrainingEnabled(false);
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    CHECK(countNotices() == 0);
    arena.Advance(100); arena.world.Combat().SetTrainingEnabled(true);
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    CHECK(countNotices() == 1);
}

TEST_CASE("Combat art XP refreshes newly unlocked stored ability grants", "[combat][expansion][abilities][skills]") {
    auto content = ArenaContent(); const auto artId = content.FindCombatArt("boxing"); const auto leap = content.FindAbility("leap");
    auto& art = const_cast<CombatArtDefinition&>(content.GetCombatArt(artId));
    for (auto& grant : art.abilities) { if (grant.ability == leap) { grant.unlock = {}; grant.unlock.artXp = 5000; } }
    Style(content, "boxing_guard").moves = {{content.FindCombatMove("basic_fist"), 1}};
    Move(content, "basic_fist").strikes[0].intrinsicMiss = ratioScale;
    Arena arena(eastl::move(content));
    REQUIRE(arena.world.Combat().GrantArt(arena.attacker, artId, ArtGrantReason::Administration));
    auto state = arena.world.GetActor(arena.attacker)->combat; state.styleId = arena.content.FindCombatStyle("boxing_guard");
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    const AbilityGrant expected{leap, {AbilityGrantSourceKind::Art, artId}};
    CHECK_FALSE(combat_runtime::Granted(arena.world, *arena.world.GetActor(arena.attacker), leap, expected.source));
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    const auto& character = *arena.world.GetActor(arena.attacker)->characterCombat;
    CHECK(character.arts.size() == 1); CHECK(character.arts.front().art == artId); CHECK(character.arts.front().xp == ratioScale);
    CHECK(eastl::find(character.abilityGrants.begin(), character.abilityGrants.end(), expected) != character.abilityGrants.end());
}

TEST_CASE("Skill gate changes refresh grants without reacquiring arts or resetting cooldowns", "[combat][expansion][abilities][skills]") {
    auto content = ArenaContent(); const auto artId = content.FindCombatArt("boxing"); const auto leap = content.FindAbility("leap");
    auto& art = const_cast<CombatArtDefinition&>(content.GetCombatArt(artId));
    for (auto& grant : art.abilities) {
        if (grant.ability == leap) { grant.unlock = {}; grant.unlock.skills = {{content.GetCombatSkills().unarmed, 2}}; }
    }
    Arena arena(eastl::move(content));
    REQUIRE(arena.world.Combat().GrantArt(arena.attacker, artId, ArtGrantReason::Administration));
    const AbilityGrantKey source{AbilityGrantSourceKind::Art, artId};
    AbilityRequest focus{arena.content.FindAbility("focus"), source, {AbilityTargetKind::Self}};
    REQUIRE(arena.world.Combat().UseAbility(arena.attacker, focus, arena.rng).committed); arena.Advance(60);
    const auto cooldown = combat_runtime::AbilityCooldownRemaining(*arena.world.GetActor(arena.attacker), focus.ability, source);
    REQUIRE(cooldown > 0);
    AbilityRequest request{leap, source, {AbilityTargetKind::Tile, 0, {3, 5}}};
    CHECK(arena.world.Combat().QueryAbility(arena.attacker, request).reason == CombatRefusal::MissingGrant);
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, arena.content.GetCombatSkills().unarmed, 2));
    REQUIRE(arena.world.Combat().QueryAbility(arena.attacker, request).Available());
    arena.world.Combat().ProcessEvents(arena.rng);
    const AbilityGrant expected{leap, source};
    const auto hasStoredGrant = [&]() {
        const auto& grants = arena.world.GetActor(arena.attacker)->characterCombat->abilityGrants;
        return eastl::find(grants.begin(), grants.end(), expected) != grants.end();
    };
    CHECK(hasStoredGrant());
    REQUIRE(arena.world.Learning().SetLevel(arena.attacker, arena.content.GetCombatSkills().unarmed, 1));
    CHECK(arena.world.Combat().QueryAbility(arena.attacker, request).reason == CombatRefusal::MissingGrant);
    arena.world.Combat().ProcessEvents(arena.rng); CHECK_FALSE(hasStoredGrant());
    const auto* actor = arena.world.GetActor(arena.attacker);
    CHECK(actor->characterCombat->arts.size() == 1);
    CHECK(combat_runtime::AbilityCooldownRemaining(*actor, focus.ability, source) == cooldown);
}

TEST_CASE("Delayed Leap revalidates landing without attacking its new occupant", "[combat][expansion][abilities]") {
    auto content = ArenaContent();
    auto& art = const_cast<CombatArtDefinition&>(content.GetCombatArt(content.FindCombatArt("boxing")));
    for (auto& grant : art.abilities) { grant.unlock = {}; }
    Arena arena(eastl::move(content)); const auto artId = arena.content.FindCombatArt("boxing");
    REQUIRE(arena.world.Combat().GrantArt(arena.attacker, artId, ArtGrantReason::Administration));
    AbilityRequest request{arena.content.FindAbility("leap"), {AbilityGrantSourceKind::Art, artId}, {AbilityTargetKind::Tile, 0, {3, 5}}};
    const auto used = arena.world.Combat().UseAbility(arena.attacker, request, arena.rng); REQUIRE(used.pending);
    REQUIRE(arena.world.MoveActor(arena.defender, {3, 5})); const auto body = arena.world.GetActor(arena.defender)->combat.body;
    const auto before = arena.rng; arena.Advance(used.cost);
    CHECK(arena.world.GetActor(arena.attacker)->position == TileCoord{3, 3});
    CHECK(arena.world.GetActor(arena.defender)->combat.body == body); CHECK(arena.rng == before);
    CHECK(arena.world.GetActorRemainingTicks(arena.attacker) == 80);
}

TEST_CASE("Sweep snapshots three adjacent victims including friends and pays once", "[combat][expansion][abilities]") {
    auto content = ArenaContent();
    auto& art = const_cast<CombatArtDefinition&>(content.GetCombatArt(content.FindCombatArt("weapon_forms")));
    for (auto& grant : art.abilities) { grant.unlock = {}; }
    auto& move = Move(content, "weapon_sweep_strike");
    move.requirements.primary = {AttackSourceKind::EmptyHand, SourceHand::Left};
    move.strikes[0].intrinsicMiss = 0; move.strikes[0].damageRatio = ratioScale;
    move.strikes[0].contacts = {{ContactRegion::Torso, 1}};
    move.strikes[0].damage = {{DamageType::Blunt, 1000, 0}};
    Arena arena(eastl::move(content));
    const auto left = Take(arena.world.CreateActor({4, 2}, true));
    const auto right = Take(arena.world.CreateActor({4, 4}, true));
    const auto artId = arena.content.FindCombatArt("weapon_forms");
    REQUIRE(arena.world.Combat().GrantArt(arena.attacker, artId, ArtGrantReason::Administration));
    AbilityRequest request{arena.content.FindAbility("sweep"), {AbilityGrantSourceKind::Art, artId},
        {AbilityTargetKind::CardinalDirection, 0, {}, CardinalDirection::East}};
    const auto used = arena.world.Combat().UseAbility(arena.attacker, request, arena.rng); REQUIRE(used.pending);
    arena.Advance(used.cost);
    for (const auto target : {left, arena.defender, right}) {
        CHECK(arena.world.GetActor(target)->combat.body.parts[static_cast<unsigned>(BodyPart::Torso)].hp == 9000);
    }
    CHECK(arena.world.GetActor(arena.attacker)->combat.stamina == 82);
    const auto events = arena.Events(); unsigned strikes = 0;
    for (const auto& event : events) { if (event.kind == CombatEventKind::Strike) { ++strikes; } }
    CHECK(strikes == 3); CHECK(arena.world.GetActorRemainingTicks(arena.attacker) == 140);
}

TEST_CASE("Combat difficulty is recomputed at each crossed skill level", "[combat][expansion][skills]") {
    auto content = ArenaContent();
    auto& move = Move(content, "basic_fist"); move.training.work = 100000; move.strikes[0].intrinsicMiss = ratioScale;
    for (const auto skill : {content.GetCombatSkills().melee, content.GetCombatSkills().unarmed}) {
        auto& definition = const_cast<SkillDefinition&>(content.GetSkill(skill));
        definition.xpToNext[0] = 1; definition.xpToNext[1] = 1; definition.xpToNext[2] = 1;
    }
    Arena arena(eastl::move(content));
    auto state = arena.world.GetActor(arena.defender)->combat; state.authoredTrainingRanks = true; state.defensiveTrainingRank = 0;
    REQUIRE(arena.world.Combat().Configure(arena.defender, state));
    REQUIRE(arena.world.Combat().Attack(arena.attacker, arena.defender, arena.rng).committed);
    const auto& learning = arena.world.GetActor(arena.attacker)->learning;
    const auto* melee = FindSkillProgress(learning, arena.content.GetCombatSkills().melee);
    const auto* unarmed = FindSkillProgress(learning, arena.content.GetCombatSkills().unarmed);
    REQUIRE(melee); REQUIRE(unarmed); CHECK(melee->level == 2); CHECK(unarmed->level == 2);
    CHECK(melee->xp == 1000); CHECK(unarmed->xp == 3000);
}

TEST_CASE("NPC reach two checks supercover actors while player ordinary reach stays one", "[combat][expansion][reach]") {
    Arena arena; auto state = MakeCombatState(false); state.reach.distance = 2; state.faction = CombatFaction::Survivors;
    REQUIRE(arena.world.Combat().Configure(arena.attacker, state));
    // Player identity is independent of anatomy: ordinary player reach remains one.
    REQUIRE(arena.world.MoveActor(arena.defender, {5, 3})); CHECK_FALSE(arena.world.Combat().CanReach(arena.attacker, arena.defender));
    const auto npc = Take(arena.world.CreateActor({3, 4})); REQUIRE(arena.world.Combat().Configure(npc, state));
    REQUIRE(arena.world.MoveActor(arena.defender, {5, 4})); CHECK(arena.world.Combat().CanReach(npc, arena.defender));
    const auto blocker = Take(arena.world.CreateActor({4, 4})); CHECK_FALSE(arena.world.Combat().CanReach(npc, arena.defender));
    REQUIRE(arena.world.DestroyActor(blocker)); CHECK(arena.world.Combat().CanReach(npc, arena.defender));
}
}
