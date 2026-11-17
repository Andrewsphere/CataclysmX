# Combat expansion integration contract

## Status and ownership

This page records the C++23 contracts and integration decisions used to implement
the melee expansion. Combat, content loading, scheduling, persistence, networking,
and the terminal/SDL/headless frontends are integrated. The ownership notes below
describe development boundaries rather than outstanding work. See
[combat API](combat_api.md) for the public overview and
[verification](combat_verification.md) for the completed checks.

The foundation owns `combat_ids.h`, `damage.h`, `contact.h/.cpp`,
`equipment_types.h`, `action_types.h`, `character_combat.h`, `combat_rules.h/.cpp`,
`content/combat_definition.h`, and `content/armor_definition.h`. Existing actor,
action, combat state/system, and melee profile headers expose additive contracts.
The subsystem implementations consume these contracts through the existing World
coordinator and authoritative command path.

## Dependency direction and definition identity

`combat_ids.h` contains four distinct 32-bit catalog IDs: `CombatStyleId`,
`CombatMoveId`, `CombatArtId`, `AbilityId`. Zero is absent. Definition names are
the stable identities for persistence, selection ordering, and hotkeys. Catalog
indices and handles must never be persisted as definition/entity identities.

`damage.h` is independent of actor/action/content state. `melee_profile.h` now
uses leaf damage/equipment/skill vocabulary. Definitions must not include
`combat_types.h`, `Actor`, or `World`. Catalog-aware implementations include
`content.h` in their `.cpp`, not in these leaf contracts. There is no runtime
registration or generic condition/effect framework.

Content integration adds `FindCombatStyle`, `TryGetCombatStyle`, `GetCombatStyle`,
`CombatStyleCount`, and the analogous CombatMove/CombatArt/Ability methods. It owns
`CombatSkillIds` binding and validation. Skill names are `melee`, `unarmed`,
`knives`, `swords`, `clubs_hammers`, `axes`, `polearms`, `shields`, `dodge`.

When a fixture contains no combat definitions, the content loader installs an
explicit typed baseline repertoire and required default skill bindings. Main
pack JSON supplies the real definitions. Partial combat catalogs must either
resolve all required baseline roles or fail with diagnostics; do not create
per-actor hidden hardcoded fallbacks or invoke the old selector when a new
definition lookup fails. Distinguish fixture defaults from arbitrary missing
definition names in saves, which are errors.

## Authoritative fields and legacy adapters

All existing `ActorActionState` members retain order and types; new members are
appended. Existing aggregate initializers remain source-compatible. Existing
CombatState fields, `CombatMove`, `CombatStyle`, and old methods remain available.

After expanded integration:

* `CombatState::styleId` is authoritative. `style` is a lossy legacy projection
  (named basic styles map exactly; learned styles project Balanced). A zero style
  ID is an unbound setup/migration state, not permission to run legacy selection.
* `Actor::equipment` is authoritative. `wieldedItem` is left-first, else right,
  else null, refreshed by every equipment mutation and restore. It must never be
  used to discover expanded attack sources or count mass.
* `ActorActionState::definitionDriven` marks expanded commitments. `moveId`,
  `committedStyle`, `trainingArt`, `sources`, `ability`, `abilitySource`,
  `abilityTarget`, `ratingPenalty100`, `trainingWork`, and `counter` are authoritative.
  `target` remains the persistent melee target. `move`, `weapon`, and
  `accuracyPenalty` are legacy projections only. They cannot represent all moves.
* `CombatState::stamina` stays the numeric authority; do not add a duplicate
  current-stamina field. `staminaRegenerationRemainder` is the expansion fraction;
  old `staminaElapsed` is migration-only after conversion.
* `moveCooldowns` supersedes `reactionCooldown`/`heavyCooldown`. Legacy cooldown
  fields are projections for the matching basic move only, never extra gates.
* `life`, `inexhaustible`, `reach`, stagger/focus fields, exertion/lock fields,
  and learned-versus-authored training-rank policy are authoritative.
* `attackRating100`/`defenseRating100` are base ratings for actors with authored
  ranks; on learned-skill actors they are explicit additive native bonuses.
  They are not training ranks. `attackDamage` remains native attack damage in HP.
* `legacyAccuracyOverride` enables `intrinsicMissOverride` only for migrated
  accuracy state. New actors use each strike's intrinsic miss (default 500).

Do not enable expanded gameplay piecemeal with a silent old-selector bypass.
Existing old methods may remain for setup/tests during migration, but integrated
`Attack`, Tab, bump, NPCs, and abilities must all use the new definition pipeline.
`SetStyle(CombatStyle)` maps a basic definition in compatibility mode; gameplay
uses paid `ChangeStyle(CombatStyleId)` while ready.

## Equipment and commitment

`EquipmentState::hands[0]` is left; `[1]` is right. A two-handed grip repeats one
reference with TwoHanded in both slots. One-handed grips cannot repeat an item.
Held/worn items are direct inventory children and owned once. Held and worn are
exclusive in V1. Transfers/destruction clear all affected references. Disability
retains occupancy. Furniture grabbing ignores occupancy but grants no free hands.

The integration owner adds World APIs (ordinary refusal via InventoryResult,
invalid handles/requests following existing World conventions):

```cpp
EquipmentQuery QueryEquipment(ActorHandle, const EquipmentRequest&) const;
InventoryResult HoldItem(ActorHandle, ItemHandle, Hand, GripKind);
InventoryResult ReleaseHand(ActorHandle, Hand);
InventoryResult RegripItem(ActorHandle, Hand, GripKind);
InventoryResult WearItem(ActorHandle, ItemHandle);
InventoryResult RemoveWornItem(ActorHandle, ItemHandle);
```

These validate atomically and return base costs without scheduling, matching
current inventory mutations. Server schedules exactly once. Legacy WieldItem
maps to explicit HoldItem policy and must not silently evict occupied equipment.
The loader adds optional `ArmorDefinition armor` to ItemDefinition and maps
legacy MeleeProfile::hands to exactly one authored GripDefinition. `handlingSkill`
is explicit (knuckles use Unarmed while occupying a hand); missing legacy family
uses the validated improvised/Melee binding, never damage-type inference.

Moves contain SourceRequirement primary/secondary and a paired flag. Each
candidate binds independently; an unusable held weapon cannot veto an eligible
kick. Primary/secondary handling filters apply to each held source. Secondary is
ignored unless paired. A paired move binds distinct hands/items; a two-handed
item cannot be counted as two weapons. EmptyHand explicitly requires a free hand;
Kick requires the selected functional leg(s), not a free hand. Natural sources
use the actor's apparatus/body capabilities. `functionalHands` and `freeHands`
are additional requirements, not replacements for source requirements.

SourceCommitment has exactly two CommittedHand slots because anatomy has two
hands, not because moves have a two-strike limit. `primaryHand` captures Either
binding and determines the opposite secondary hand. `primaryKind`,
`secondaryKind`, `paired`, functional limb masks, and naturalApparatus capture
requirements. A required empty hand has item=0. Unrelated hands are not required.
Every strike revalidates identity, grip, capability, target life, reach, and
obstruction. It never swaps in another source. Definitions remain immutable for
the World's lifetime. Pay all move costs once before execution, including paired
costs, and preserve recovery when later strikes become invalid.

## Selection, reaction, and effects

CollectAdjacentTargets returns perceived, attackable, reachable hostile actors
among eight same-Y neighbors, sorted by persistent ID. ChooseAdjacentTarget
draws uniformly from that list. Bump supplies its exact target; movement into an
empty neighbor never targets a farther actor. Explicit target requests can use
authored reach 2. QueryAttack and QueryAbility consume no resources/RNG.

Select eligible unlocked moves by positive authored weight in stable definition
name order. First select move, then choose among its bindings in source-kind and
left/right order so two usable weapons do not double that move's weight. Weight
sums must fit uint32; reject overflowing external definitions, fail on internal
violations. DrawBelow uses the existing full-period uint32 LCG with rejection
sampling, no draw for bound=1. Empty selection consumes no draw. Valid strikes
always consume an outcome draw; there are no independent miss/defense rolls.

One response is selected from the defender's style per incoming strike. Commit
its stamina/cooldown before drawing even an intrinsic miss. No free Dodge XP.
Recovery-compatible reactions preserve recovery. Windup reactions also require
disjoint source limbs, even when allowedDuringWindup is true. Incoming defense
mask and permitsArmedAttack gate parry eligibility. A failed response still pays.

Pure formulas live in combat_rules.cpp:

* Ratings are hundredths of skill level: 40*Melee + 60*handling + bonuses - penalties;
  Dodge is 100*Dodge + bonuses - penalties. No intermediate whole-level rounding.
* Defense chance is clamp(base + 5*(D100-A100), 500, 9000); bases Dodge/Parry 4000,
  Block 6000. None gives zero.
* Draw bands: intrinsicMiss, then floor((10000-miss)*defenseChance/10000), then
  contact. A successful block still resolves residual contact, unlike parry/dodge.
* Skill efficiency is clamp(skill,0,10)*200 ratio units: cost ceil(base*(1-bonus)),
  capacity floor(base*(1+bonus)). Maximum discount/increase is 20%.
* Block is ONE shared budget over all packet amounts, proportional largest
  remainder allocation, ties by DamageType. Packets must have unique types.

Preparation currently exposes an authored opening to the target. Follow-through
has a typed condition and stagger/knockback/opening fields. No arbitrary opcode
program. Counter openings belong to `(owner, opponent)`, refresh instead of stack,
expire in active simulation time, and are consumed before counter commitment.
Counters use the CounterOpening trigger and set action.counter; they cannot grant
further counter openings. Process them iteratively after the triggering contact,
never recursively. Stagger refreshes max remaining duration and cancels applicable
windup without shortening existing recovery. Knockback advances collision-checked
steps and stops at obstruction, with no implicit collision damage.

CombatSystem::DrainEvents clears/fills caller-owned reusable storage and removes
the drained events. Emit events in actual resolution order, not storage order;
simulation never depends on draining. The coordinator must bound/regularly drain
presentation storage rather than retain an unbounded world history. StrikeResult
separates response success, armor stop, actual HP/max-HP loss, and death. The
legacy last-feedback summary remains a projection, not the experiment log.
CombatEventKind also reports paid commitments (including defensive responses),
invalidated commitments, suppressed training, art-study completion, and deaths.
For non-Strike kinds use actor/opponent/move/ability/art/reason/cost/staminaSpent;
leave strike default. ArtStudyCompleted uses learnedArt to distinguish success.
For Strike use its StrikeResult; incomingDamage, blockAbsorbed, armorAbsorbed,
and postProtectionDamage are hundredths, with each amount counted exactly once.

## Contact, armor, and reach

Head, Eyes, Mouth, Ears are separate encumbrance/coverage contacts, all mapping to
Head HP. Arms map to corresponding Hand HP; feet to corresponding Leg HP. Simple
bodies have AttackApparatus/Locomotion/Core. No new injury pools are introduced.
`ReachProfile::regions` is intersected with body contacts and height category.
Lower means legs/feet or simple Locomotion; Upper means the complementary present
contacts. Height is a coarse filter, not cross-Y geometry.

Reach 1..2 requires same Y and deterministic supercover checks against intermediate
actors, solid terrain, and furniture. Both cells touched at diagonal corner
crossings block if either is solid/occupied (excluding endpoints). Landing/support
rules remain movement checks. Contact helpers do not access World or consume RNG.

ArmorRegionDefinition lists unique contacts. Coverage is a unit ratio; intrinsic
encumbrance is local. Sum intrinsic contributions plus 5*max(0,n-1) per region per
layer, where n excludes overlapExempt garments. Exemption does not waive intrinsic
encumbrance or rigid conflicts. Rigid regions must be covered by that garment;
same-layer overlapping rigid regions are incompatible.

After shared defensive block: select contact; traverse worn garments by persistent
ID; independently roll each partial coverage once, sharing it across packets;
sum intrinsic plus covered protection; subtract penetration once per component;
sum residual hundredths before one injury rounding. Zero/full coverage uses no
draw. Existing intrinsic protection stays indexed by injury pool, avoiding a new
per-contact matrix in every actor. Physical DamageContact now has ContactRegion
and explicit RNG; old BodyPart overloads are raw/legacy adapters, not clothing
resolution. A glove cannot protect the Arm contact merely because HP is shared.

## Progression, abilities, and memory

CharacterCombatState is optional unique ownership on Actor. Most monsters pay
only a pointer, not art/manual/grant arrays. Vector owns these variable-size bulk
records; no catalog-sized inline arrays. CloneCharacterCombat performs an explicit
deep copy for snapshots; EqualCharacterCombat compares values, treating null and
empty as distinct representations. Actor remains noncopyable. CombatState remains
value-copyable, but Elapse integration must remove its whole-state copy per actor
and track a changed flag, because cooldown/opening vectors can allocate.

Learned gates use MoveUnlock (all skill requirements AND art XP); runtime bonuses
never unlock moves. ArtProgress XP uses 1/ratioScale units. trainingArt is captured
at commitment and cannot change after switching style. Attack training has one
work budget per action; equal shares are apportioned across its planned strikes
(Sweep includes all planned tiles). Invalidated shares expire and never become
extra rewards for other strikes. Relevant valid misses may train. Packet count
does not multiply work. Defender receives a separate budget only for an actual
committed response. Dodge spends all skill work on Dodge; parry/block/attack split
Melee/handling as authored, merging duplicate skill IDs before award. Art work is
bounded independently by the action's original work budget, not per-packet work.

CombatTrainingRatio uses opponent rank minus learned learner rank: >=0 full,
-1 half, -2 tenth, <=-3 zero. For player offensive rank use floor of learned
40/60 mixture; defensive rank is the highest currently usable learned defense
rank, with Dodge direct. Suppression for each skill compares that learned skill;
art compares learned action composite. Monsters set authoredTrainingRanks and
explicit offensive/defensive ranks; temporary ratings are irrelevant to training.
Coordinator keeps one sparse per-encounter suppression notice per learner/opponent,
refreshing inactivity expiry on interaction. Do not put opponent history in Actor.

Book integration appends SmallVector<ArtManualDefinition,1> artManuals. Manual
attempts are independent of ordinary BookProgress::used. QueryArtStudy reports
eligibility; CollectInfo provides fixed chance and duration. StudyArt funds a
valid attempt; PauseArtStudy preserves its pending remaining time. Duration is
ceil(base*10000/(10000+2000*learnedSurplus)). Snapshot chance/duration at start;
roll once at valid completion through simulation RNG. Failure permits a fresh
full-duration retry and never consumes the manual; no roll on inspection/cancel.
At most one study attempt (ordinary book OR art) is active. LearningSystem owns
funding and scheduler integration, and must accept RNG for art completion.
GrantArt is explicit acquisition for creation/teaching/manual/admin, never an
ordinary client command to bypass study.

AbilityGrantKey distinguishes Art (nonzero art) and Innate (zero art). One ability
may have independent grants; cooldown entries key `(ability, source)`, exist only
when nonzero, and survive grant removal until expiry. No per-catalog ability array.
UseAbility validates source and target; source loss during windup invalidates
execution without refunds. Explicit enum dispatch handles only Focus, Leap, Sweep.
Self Focus replaces the single bonus with the activated definition's bonus and
sets remaining duration to that definition's full duration, without adding either
value; focus fields live in CombatState so simple actors can also receive it.
Leap is nonattack, same-Y distance <=2 with
path/support/landing checks at start and execution. Sweep targets a cardinal
direction and adjacent 3x1 row, visits left/center/right relative to facing, hits
friends, and revalidates each independent strike. Snapshot victims at execution,
not new occupants later displaced into the row. Sweep borrows sweepMove geometry,
damage, source and follow-through; pays AbilityDefinition::cost/cooldown and
training once, not the borrowed move's costs or cooldown. Validate sweepMove as a
Strikes move without counter-opening requirement; never weighted-select it.

Stamina remains 0..100 for this expansion. inexhaustible bypasses stamina spending,
not time/cooldowns. No regeneration during combat commitment including recovery.
Defensive commitments extend combatLockRemaining without resetting an action.
Moving regeneration is reduced; Rest is full; Working has an explicit integration
policy. Use ratioScale fractional remainder, split elapsed accounting at lock and
activity boundaries, discard banking at full stamina. Exhausted-only weak/slow
zero-cost-stamina moves still require a valid body/source. Faction never implies
inexhaustibility. Rates/costs are starter tuning, to be verified by real runner.
`exhaustedOnly` means no otherwise-eligible nonfallback attack can be afforded,
not literally stamina==0; this avoids stranding an actor at one remaining point.
Ordinary candidates are considered first, then only eligible fallback candidates
if the ordinary set is unaffordable. No fallback bypasses source or reach checks.

## Death, scheduling, persistence, and downstream work

Head OR Torso zero kills detailed actors; Core zero kills simple actors. Mark life
Dead immediately, cancel actions/study, suspend deadlines, disallow all reactions,
and enqueue safe-boundary corpse creation. Corpse is an ordinary container item;
transfer inventory roots once, preserve nested ownership, clear references, remove
occupancy. Internal death transfer must accept possessions without ordinary
capacity refusal. Do not reuse DestroyActor's ground-drop behavior implicitly.

Dead actors must be excluded from ready lists, NextActorEventIn, NPC/combat/learning/
crafting processing, perception, regeneration, and pacing. Recheck liveness inside
already-gathered due cohorts. Prevent a zero-time dead-player scheduling loop.
World/server owner adds terminal death queue operations and separates stable
player-session ID from current character ID; replacement creates a fresh actor in
the same world, preserving session token, not skills/memories. Dead views remain
usable; no living sessions means idle pacing. See design handoff for session API.

Old catalog-free state readers/writers remain legacy-only. They must not be used
to persist nondefault expansion state once integrated. New overloads take
ContentCatalog. Persistence owner adds equipment/character state serializers,
explicit state versions and stable names, plus save format 14 (current 13), network
21 (current 20), combat envelope 3 after validating legacy v1/v2 migration.
Keep generation version separate. Migrate legacy styles/moves, grip/occupancy,
cooldowns, accuracy override, pending source/deadline/recovery without paying a
second time. Existing zero-vital characters transition to death exactly once.
Rebind all equipment after entities exist. Missing required definitions fail,
stale entity targets are allowed, and spawn defaults never overwrite saved state.

Required implementation owners:

1. Content: catalog APIs/defaults/JSON, item armor/book manuals/monster fields,
   strict bounds, unique damage/contact entries, supported effect validation.
2. Combat: definition selection/commitment, reactions/effects/stamina, views/events,
   source revalidation; migrate existing methods with no legacy gameplay bypass.
3. World/server: equipment mutation/projections, scheduling/geometry/death/corpses,
   commands and fresh-character session flow. World retains inventory ownership.
4. Learning: skill/art budgets, manual work/roll completion, suppression notices.
5. Persistence/network: versioned snapshots, migration and shape/catalog validation.
6. UI/runner: equipment/ability/manual/death screens and headless real-scheduler
   experiments; event metrics distinguish actual HP loss, block, armor, timeout.
7. Build/coordinator: register helper sources and integrate narrow tests. Acceptance
   tests must cover asymmetric hands, unusable weapon with eligible kick, paid failed
   reactions, mixed packets, save during windup/study, and dead-player scheduling.

The coordinator entry points and serializers described here are implemented.
Numeric defaults remain provisional; the experiment runner uses the real rules
and scheduler to support subsequent tuning.
