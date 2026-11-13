# Combat API

The first melee slice is implemented by [`CombatSystem`](../src/combat/combat_system.h),
accessed through `world.Combat()`. The world owns the coordinator and actor storage;
each actor owns its `CombatState`, including body, injury profile, faction, style,
stamina and pending action. World inspection is const and combat uses narrow scoped
mutation, so callers do not retain actor pointers across mutations or deletion.
Anatomy, controller and allegiance are independent. New players are detailed,
Survivors and regenerating; new NPCs are simple, Neutral and non-regenerating.
Only Survivors and Undead are mutually hostile.

`Configure` installs validated state on a ready actor with no pending action;
`SetStyle`, `Describe`, `FindTarget`, `CanReach`, `Attack`, `Damage`, `Disrupt`,
`StartAiming`, `Elapse`, `ProcessEvents` and `NextEventIn` expose the coordinator's
operations. `Attack` returns an `AttackResult` distinguishing uncommitted failures,
windup, miss, parry, blocked contact, hit and defeat; RNG is explicitly passed by
reference. `Damage` applies local, type-specific protection and penetration in
hundredths, then passes whole post-protection injury to the body; less than 1 HP
is blocked. `Describe` produces a value-only view. These boundaries keep rules in
combat while World owns identity, scheduling and persistence.

`CanAttack(actor)` queries the actor's own attack capability for AI decisions;
it does not establish readiness, target hostility, reach or range. `Attack` and
the scheduling/execution path remain responsible for validating an actual attack.

## Body and recovery

Initial balancing defaults, subject to tuning, are 100 HP per detailed head, torso,
hand and leg; simple attack apparatus and locomotion have 60 each, and Core 100.
HP above zero is fully functional; detailed extremities at zero are broken. The
hit that breaks a limb does not transfer overkill to maximum loss. Subsequent whole
injury to an already broken limb reduces maximum HP by 10%, accumulated in
hundredths, capped at 50% of unreduced maximum. Further hits do not reset mending.
Simple disabled attached zones redirect subsequent hits to Core; a defeated simple
non-player is destroyed. Detailed vital defeat is deferred.

With regeneration enabled, broken limbs mend after seven logical game days to
40 HP, clamped to effective maximum; `InjuryProfile::mendedHp` can instead be 60.
This slice defines one logical second as `ticks_per_turn` (100 ticks), and a day
as 86,400 such seconds; wall-clock pacing never changes recovery durations.
Regular HP heals at 1 per game hour up to effective maximum; maximum HP restores
at 1 per game day only at full regular HP, then regular healing resumes for the
new capacity. Profile intervals and loss policy are configurable. Disabling
regeneration freezes mending and healing. Detailed zero-HP head/torso do not
implicitly revive. Recovery progresses only during active simulation, not while
parked, inactive or archived.

`LocomotionBaseCost` returns 100 healthy, 150 with one disabled detailed leg,
and 200 with both disabled or a disabled simple locomotion zone. NPC movement
scales its authored base cost by this factor before speed scaling.

## Moves and disruption

The initial selector filters eligible moves, chooses highest priority and breaks
ties by stable `CombatMove` enum value, without RNG: aggressive heavy priority
100, plain and fallback bite priority 10. Hit and region use the explicit seeded
LCG (`state = state * 1664525 + 1013904223`, modulo 2^32). Reach includes eight immediate neighbors on the
same Y, with passable endpoint tiles and no adjoining-wall restriction. Automatic player targeting requires perception
and picks the lowest persistent ID among adjacent reachable hostiles.

Default plain attacks resolve immediately, base cost 100 and damage 12; fallback
bite for a simple actor with disabled attack apparatus deals half base damage
(minimum 1 for nonzero damage). Plain/bite base cost is 150 without functioning
legs/locomotion. Heavy costs 20 stamina, has 80 base windup plus 120 base recovery,
400 active-tick cooldown, and doubles base damage. Both phases capture the speed
at commitment; losing the target does not undo recovery. Defensive style can parry
an incoming hit for 10 stamina and a 100 active-tick cooldown. Stamina regenerates
1 per 10 active ticks up to 100. These are initial balancing defaults, not approved
final numbers.

`ActorActionState` stores a persistent target ID, not a handle; it may become
stale. Shared `ApplyDisruption` handles damaging hits and forced movement: a heavy
windup retains its deadline and the strongest accuracy penalty (default 15, cap
30), rather than accumulating penalties. Aiming cancels into default 20-tick
disruption recovery. Required capability loss or stagger cancels pending windup or
aiming; None, ordinary recovery and disruption recovery ignore disruption, so
repeated events do not reset cancellation recovery or shorten committed recovery.
The aiming API is a headless pending-action foundation, not a firearm feature.
See [entity scheduling](entity_scheduling.md) and [combat presentation](combat_presentation.md).

No body geometry, clothing system, general status engine or firearms are implemented
by this slice. See [combat design](../md/CombatDesign.md) for agreed and deferred ideas.

Melee reach includes all eight immediate horizontal neighbors on the same Y level.
It checks source/destination passability and normal perception; adjoining diagonal
walls add no restriction. Target ties remain ordered by persistent identity.
