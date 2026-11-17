# Combat runtime integration

`combat_system.cpp` runs catalog-defined actions. `combat_runtime.h/.cpp` are
internal binding, selection, geometry, rating, and equipment helpers. They do not
own entities or introduce another combat definition schema. Bindings use equipment
and persistent item identities; the legacy wielded projection is never a source.

## Build registration

`src/combat/combat_runtime.cpp` is registered with the engine and
`tests/combat_expansion_tests.cpp` with the test target. The existing body, action,
combat-system, and regression-test translation units remain in their targets.
`tests/combat_runtime_fixture.h` constructs an owned, explicitly configured
repertoire before World creation for the old timing/lifetime regressions.

## Execution and presentation

Queries do not draw RNG. Weighted selection groups all bindings of a definition
under one authored weight, in stable definition-name order. Commitments pay once
and capture sources, recovery speed, training art, and work. Execution revalidates
each planned strike and expires invalidated work shares. Immediate counters use
an action-local iterative job queue after all triggering contacts/follow-through;
windup counters enter the normal event scheduler.

Counter commitment overlaps an existing recovery deadline. In ticks captured at
commitment speed, `finalDuration = max(oldRemaining, windup + authoredRecovery)`;
the pending counter stores `finalDuration - windup` as its post-windup recovery.
The absolute final deadline is preflighted before consuming the opening, and the
combat stamina lock covers that same final duration. Thus a 50+90 counter during
200 remaining recovery ends in 200 ticks, not 250 or 140.

`EmitEvent` bounds presentation history to 4096 records, evicting the oldest on
overflow. `DrainEvents` clears/fills caller-owned storage and removes the queued
records. Draining does not affect combat outcomes. World owns the terminal
`ActorDied` notification, so combat must not emit a second death event. When World
publishes death during contact resolution, EmitEvent completes the killing
StrikeResult immediately before that notification in the same atomic call.

`BodyAlive` is the anatomy-only vital query: either detailed Head or Torso zero,
or simple Core zero, is terminal. Body recovery freezes all pools after terminal
injury. Combat calls World marking immediately; corpse transfer/removal occurs
at safe finalization boundaries. Raw BodyPart damage bypasses garments while
retaining intrinsic protection compatibility. ContactRegion damage combines
intrinsic and independently covered garment protection before penetration.

## Stamina and training policy

Stamina is 0..100. Combat commitment/reaction locks prevent regeneration, including
recovery. Rest and Working regenerate 1000 ratio units per tick (one stamina per
10 ticks); Moving regenerates 500. Exertion transitions belong to World/server.
The lock boundary is subtracted before fractional regeneration, and reaching full
stamina discards carry. Inexhaustible actors bypass spending, not time/cooldowns.

Attack work is split across planned strike opportunities, including all three
Sweep tiles. Packet count never affects work. Melee receives the authored share;
handling receives the integer remainder, merging identical skill IDs. Difficulty
is recomputed at each learned-level boundary before calling `Learning().Train`.
Authored-rank monsters are training opponents, not learned-skill XP recipients.
Focus and Leap have no opponent, so do not manufacture combat training rewards.

## Encounter suppression

CombatSystem owns sparse TrainingNotice records keyed by learner/opponent
persistent IDs. Every valid strike interaction refreshes both learned
participants' encounter records, including misses, zero-work actions, and
interactions that do not commit a defensive response. The first difficulty-
suppressed skill or art award emits one TrainingSuppressed event for that pair.
DrainEvents and presentation eviction never reset the notice. Elapse expires
records after 30 active-simulation seconds without interaction; a later encounter
can report suppression again. Authored-rank monsters do not acquire learner
records. This presentation-only state is not stored on actors or persisted.

## Natural sources and art grants

Natural strike binding captures SourceRequirement::requiresNaturalApparatus in
SourceCommitment::naturalApparatus. Authored bite and weary bite requirements can
therefore remain valid after apparatus loss, while apparatus-based attacks fail
source validation. Core life and other authored requirements still apply. There
is no move-name special case or hidden legacy selector.

Baseline styles have no art: ordinary baseline attacks neither acquire an art
nor manufacture art XP/grants. Learned styles capture their actual owned art.
After combat XP changes, RefreshAbilityGrants compares eligible art grants with
stored membership. It refreshes only stale membership through idempotent
GrantArt on an already owned art, preserving innate grants and cooldowns. Combat
ProcessEvents also performs this check at the simulation boundary for skill
changes from other learning paths. Pure availability queries evaluate learned
gates directly, so availability never waits for a mutating refresh.

## Local encumbrance and costs

The internal `HandlingEncumbrance(modifiers, sources, strikeSource)` helper uses
the ceiling average over the selected source's arm/hand contacts or leg/foot
contacts. Two-handed sources include both arms/hands; paired strikes use their
own selected hand. Required two-empty-hand commitments include both arms/hands.
Natural simple attacks use AttackApparatus. It does not sum unrelated garment
contacts or the other paired weapon's contacts into a strike's rating.

`Rating(world, actor, handlingSkill, sources, strikeSource, defense, bonus,
penalty)` uses these rules:

* Attack, parry, block: subtract **3 rating100 per local handling unit**.
* Dodge: subtract **2 * (torso + ceiling-average legs/feet)**.
* Attacks use native attackRating100; every defense uses defenseRating100.
* Simple bodies use Core for torso and Locomotion for legs/feet.
* Head, Eyes, Mouth, Ears remain visible in equipment inspection but do not
  penalize melee accuracy, dodge, stamina, or movement through a global sum.

Move costs apply authored source/grip scaling first. Effective handling skill
then improves each positive windup/recovery base cost by **1% per level, capped
at 10%**: floor the product, clamping positive phases to at least 1. Zero windup
stays zero. This never changes Actor::speed or unrelated actions.

Stamina first uses the existing skill-efficiency discount with ceiling rounding,
then multiplies by `1 + (torso + localHandling)/100`, again rounding up. Paired
actions use a ceiling average of their two source handling values and apply this
modifier once. Dodge uses leg/foot handling. `AbilityCost` applies the same local
rules once to the ability's own authored cost: Sweep uses its committed sources,
Leap uses legs/feet, and Focus has no limb-handling contribution. Sweep never
borrows the move's costs or adds its cost modifiers a second time.

`MovementEncumbranceCost(world, actor, baseCost)` returns
`ceil(baseCost * (100 + ceilingAverageLegsFeet) / 100)` (Locomotion for simple
bodies). It does not apply torso, handling skill, diagonal geometry, or speed.
World owns applying this helper once along its existing movement-cost path.

## Reach and setup validation

Selection, reach queries, and contact sampling intersect the actor's ReachProfile
with each strike's reach height/regions and authored contact weights. Opposing
height restrictions or empty region intersections produce no eligible strike and
consume no selection/contact RNG. Reach checks passability at both endpoints.
Adjacent destination-only corner behavior remains intact; longer reach retains
supercover obstruction checks.

Configure binds the legacy basic-style setup projection and converts legacy
stamina carry in temporary state, then runs catalog-aware ValidateCombatState
before installing anything. Invalid catalog IDs, cooldowns, life/exertion values,
reach, or fractional carry leave actor state and world revision unchanged.
