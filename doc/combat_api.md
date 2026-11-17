# Combat API

`World::Combat()` exposes the world-owned `CombatSystem`. Actors own combat
state; callers use typed identities and value views rather than retaining entity
pointers across world mutations. Anatomy, control and allegiance are independent.
Queries such as `CanAttack`, `CanReach` and `Describe` do not commit an attack;
actual execution validates readiness, sources, target and reach. RNG is passed
explicitly. See [runtime integration](combat_runtime.md) for execution details,
[content](combat_content.md) for schema and bounds, and
[lifecycle](combat_lifecycle.md) for ownership, death and persistence.

## Actions and timing

Styles, moves, arts and abilities are catalog definitions identified persistently
by stable names. Basic styles are available without an art; learned styles require
their art. Switching style is a paid action, with authored `switch_cost` defaulting
to 20 base ticks. The selector filters eligible moves and chooses by authored
weight in stable definition order. A definition's multiple source bindings do not
multiply its weight. Queries draw no RNG. Attacks commit costs and capture source
identities, speed, work and recovery; execution revalidates each planned strike.
Lost sources or targets do not yield a free replacement or refund.

Each strike has at most one chosen defense; separate strikes have separate contacts
and pay applicable costs and cooldowns. Immediate counters execute after all
triggering contacts and follow-through, through an action-local queue. Windup
counters use the scheduler; counter recovery overlaps existing recovery as
specified in [runtime integration](combat_runtime.md#execution-and-presentation).
Ability grants retain their source identity and independent source-specific
cooldowns. See [manual study](combat_learning.md) and [combat UI](combat_ui.md).

Stamina ranges from 0 to 100. Combat locks regeneration through commitment and
recovery; rest/working restore one per 10 active ticks, movement half that rate.
Exhausted-only attacks provide an explicit fallback. Effective handling skill
reduces positive windup/recovery base costs by 1% per level to at most 10%; the
separate stamina efficiency discount is bounded at 20%. Combat ratings combine
Melee and handling at 40/60; ratings are in hundredths of a level. Defense chances use 40/40/60 base percentages, 5 percentage points
per skill difference, clamped to 5–90%. These are initial tunable rules, not a
claim of balance. [Runtime](combat_runtime.md#local-encumbrance-and-costs) documents
rounding and local encumbrance.

## Body, protection and recovery

Detailed injury pools include head, torso, hands and legs; Eyes, Mouth and Ears
are distinct local contacts sharing the head pool, and feet share the corresponding
leg pool. Arms and hands likewise map to the corresponding hand pool. Garment
coverage and encumbrance remain local to the contacted region despite shared HP.
Simple bodies use Core, AttackApparatus and Locomotion. Coverage is rolled per
garment per physical contact and shared across damage packets; intrinsic and
covering garment protection combine before penetration. Raw body-part damage
bypasses garments. See [combat content](combat_content.md#items-armor-manuals-monsters).

Initial HP and injury policies are tunable: detailed pools start at 100 HP;
simple apparatus and locomotion start at 60 and Core at 100. Broken detailed limbs
at zero HP incur bounded maximum-HP loss on later injuries, default 10% up to a
50% cap. Regeneration, when enabled, mends after seven logical days by default,
heals 1 HP per game hour and restores maximum HP 1 per day after regular HP is
full. Recovery advances only during active simulation. Head or torso zero, or
simple Core zero, is terminal; dead bodies do not recover.

`ActorMovementBaseCost` starts from the authored movement base (normally 100),
multiplies by the locomotion factor (100%, 150% for one disabled detailed leg,
200% for both or disabled simple locomotion), then applies the ceiling-average
leg/foot encumbrance at 1% per unit. Direction and speed scaling follow. Eye
encumbrance is exposed locally for inspection; shooting effects are future work.

Death is marked immediately and finalized at safe boundaries. Inventory trees
move to an ordinary corpse container; a dead player can request a fresh character
in the same world, leaving the old character and corpse behind. See
[lifecycle](combat_lifecycle.md#death-boundary). Save format 14 and network
protocol 21 carry the expanded state; stable names are used for persisted content.
The aiming foundation is not a firearm implementation; cross-Y melee and ranged
precision remain future work. See [combat design](../md/CombatDesign.md).
