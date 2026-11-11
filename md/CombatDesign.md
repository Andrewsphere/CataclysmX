# Combat Design

## Contents

- [Status and scope](#status-and-scope)
- [Goals and agreed direction](#goals-and-agreed-direction)
- [Combatants and control](#combatants-and-control)
- [Combat styles and moves](#combat-styles-and-moves)
- [Suggested deterministic selection](#suggested-deterministic-selection)
- [Suggested melee process](#suggested-melee-process)
- [Body parts, reach, and protection](#body-parts-reach-and-protection)
- [Injury, disability, and defeat](#injury-disability-and-defeat)
- [Damage and status effects](#damage-and-status-effects)
- [Future ranged combat](#future-ranged-combat)
- [Open questions](#open-questions)

## Status and scope

This document records the combat design discussion. It describes agreed direction,
suggested mechanics, and unresolved questions; it is not a description of an
implemented subsystem or a finalized public API. Sections explicitly marked as
suggestions must not be treated as approved implementation contracts.

The immediate focus is melee combat, with room for ranged combat later. Gameplay
and clarity take priority over physical simulation. The related
[entity AI design](EntityAIDesign.md) discusses how AI produces combat intents;
implemented APIs belong under `doc/`.

## Goals and agreed direction

- Keep routine combat input light enough for fighting hordes. Players choose an
  existing combat style; the game chooses individual moves deterministically.
- Support simple NPCs and character NPCs with different anatomy and behavioral
  complexity. Hostility is independent of that distinction.
- Use combat moves for ordinary attacks, techniques, and out-of-turn responses.
  A plain attack is itself a move with broad availability.
- Give moves availability conditions, preparation effects, execution, and
  follow-through. Strict availability rules restrict out-of-turn use and abuse.
- Use hands and legs as the limb categories for injury tracking.
- Apply local injury penalties at zero HP rather than progressively weakening a
  limb at every injury threshold. Player injuries should not be overly punishing.
- Further damage to a player limb at zero HP reduces its maximum HP at a
  configurable rate, with a configurable loss cap. Recovery uses natural
  regeneration, restoring regular HP before maximum HP.
- Make enemy zone failure visibly change capabilities, while making it difficult
  to remove every meaningful threat without defeating the enemy.
- Support physical damage types and environmental harm, with groundwork for
  status effects on both players and NPCs.
- Leave player death and defeat rules for a later discussion.

## Combatants and control

Anatomy, decision-making, and allegiance are separate concerns:

| Combatant | Body model | Control and combat behavior |
| --- | --- | --- |
| Player | Detailed body parts | Player requests actions; style selects moves automatically |
| Character NPC | Player-like body and combat mechanics | AI requests actions through the same combat rules |
| Simple NPC | A few functional hit zones | Simpler behavior and a smaller move repertoire |

Either NPC category can be hostile, neutral, or friendly. An enemy is a combatant
with an opposing relationship, not a separate anatomy category. A human enemy
need not automatically be a character NPC; the distinction follows its intended
gameplay role. Changing allegiance should not replace its body model.

Character NPCs should be like players except for AI control, rather than using a
separate attack system. Simple NPCs can use the same move mechanism with fewer
conditions, capabilities, and choices. This preserves shared combat rules without
requiring every creature to carry the complexity of a persistent character.

## Combat styles and moves

### Agreed interaction

The player selects an existing combat style, making that style's moves available.
Enemies can have their own styles, implemented through the same mechanism but
hidden from the player.

- Pressing `Tab` requests an automatic attack against the nearest target.
- Attempting to move into an enemy requests an attack against that enemy.
- Both inputs let the game choose the applicable combat move deterministically.
- A plain attack participates in selection as a normal move and can almost always
  be chosen. Its exact physical and resource requirements remain to be defined.

The earlier custom deck and manual technique selection suggestions are superseded
by selecting an existing style. Routine attacks should not require technique
menus, reaction prompts, or extra per-attack input.

### Move structure

| Part | Purpose | Illustrative example |
| --- | --- | --- |
| Availability | Establish whether the move can be used | Suitable weapon, functioning hand, opening, or triggering event |
| Preparation | Apply effects before the main execution | Brace, step forward, or expose a vulnerability |
| Execution | Perform the move | Strike, parry, shove, or counterattack |
| Follow-through | Apply consequences after execution | Knockback on hit or an opening after a miss |

These examples are suggestions, not a fixed move list. A useful proposed
refinement is to distinguish outcomes such as miss, dodge, parry, contact stopped
by armor, and damaging contact. Follow-through could then require the appropriate
outcome rather than treating every attack attempt as a successful hit.

### Out-of-turn moves

Out-of-turn execution is allowed when a move explicitly supports it. Availability
is the primary mechanism for limiting reactions and preventing repeated abuse;
no universal reaction allowance was agreed.

Suggested conditions include awareness, a suitable weapon, usable body parts,
stamina, cooldowns, a particular triggering event, and consumption of an opening.
Their exact use and costs remain undecided.

For example, a counter could require an unconsumed opening from a successful
parry. Its eligibility rules could exclude counters as triggering attacks, so the
same mechanic cannot produce an endless counter chain. This is an illustration
of an explicit restriction, not a final rule for all counters.

## Suggested deterministic selection

Deterministic move choice is agreed; the following algorithm is only a proposal:

1. Consider the current style's moves for the relevant trigger.
2. Filter them through their availability conditions.
3. Choose the eligible move with the highest authored priority.
4. Break equal priorities using a stable move identifier.

An ordinary attack trigger considers offensive moves; an incoming attack can
consider defensive reactions. A conditional technique can outrank the broadly
available plain attack when its opportunity exists. Explicit priorities would
make overlap understandable without requiring a general combat optimizer.

The proposed selector uses no random draw. This does not settle whether contact
or damage uses seeded randomness. Simulation reproducibility must still follow
the project's logical-state, explicit-RNG, and stable-identity policy rather than
depending on runtime handle allocation.

Target selection is distinct from move selection. For `Tab`, limiting candidates
to perceived hostiles attackable from the current position was suggested, but
distance measurement, ties, reachability, and any automatic approach remain open.
The chosen enemy is explicit when attacking by movement input.

## Suggested melee process

The proposed sequence is:

```text
Attack request
    -> target selection
    -> move availability and deterministic selection
    -> commitment of costs and timing
    -> preparation effects
    -> eligible reactions and contact resolution
    -> protection, injury, and status effects
    -> follow-through and recovery
```

1. Resolve the target and check whether an attack is possible. Explain unavailable
   actions rather than silently substituting an unrelated action.
2. Select the primary move before committing its time and resource costs. A
   stronger technique should carry its own commitment rather than being a free
   addition to a cheap basic attack.
3. Apply preparation. Any movement, lunge, or displacement comes from the move;
   bump input does not inherently add movement to the attack.
4. Resolve permitted reactions and determine contact and the struck region.
5. Resolve protection, injury, status applications, and lost capabilities.
6. Apply outcome-dependent follow-through and establish the next opportunity to
   act according to the committed timing.

Selecting one primary move rather than executing every eligible technique was
suggested. A move could explicitly contain several strikes or effects. Exact
reaction ordering and effect timing are still open.

Ordinary attacks resolving immediately followed by recovery, with interruptible
windups reserved for selected telegraphed moves, was also suggested. For example,
a brute could announce an overhead smash; disabling a capability that this move
requires could interrupt it. This does not imply that every injury interrupts
every move. Attack costs after interruption or loss of the target need a decision.

The existing [entity scheduling](../doc/entity_scheduling.md) provides simulation
ticks and individual deadlines. Combat timing should integrate with that model;
this discussion does not authorize changing its timing contracts or introducing
a separate universal round system.

## Body parts, reach, and protection

### Detailed bodies

The limb categories are hands and legs. A suggested starting layout is head,
torso, left and right hands, and left and right legs. The torso is a single region.

Consider incoming attack reach: a short enemy such as a dwarf zombie or an angry
fish may only reach the player's legs. Tall creatures may extend beyond a single
Y layer. The body model needs to accommodate these interactions without requiring
unnecessarily detailed anatomy.

### Suggested separation of responsibilities

| Concept | Responsibility | Example |
| --- | --- | --- |
| Injury part | HP and loss of function | Hand or leg |
| Contact region | Which location an attack can reach and strike | Head, torso, hand, or leg |
| Clothing coverage | Which protection applies at that location | Glove, leggings, or torso garment |

Under this proposal, a glove protects a hand, and injury there reduces hand HP.
The torso has one contact region and one HP pool. Clothing coverage can be
resolved without adding an injury pool for every garment.

This separation is a suggestion. Clothing layers, partial coverage, protection
resolution, region geometry, posture, and cross-layer reach have not been chosen.
Coarse size and reach categories were suggested before detailed body geometry.

### Simple NPC zones

A representative simplified body has one zone for both hands, one for both legs,
and one for head and torso together. The capability-based interpretation is
agreed: zones should describe attack apparatus, locomotion, or a vital core rather
than forcing every creature into human anatomy.

For example, loss of a hand zone could remove grabs and heavy swipes; loss of a
leg zone could remove lunges and change pursuit to crawling. These are
illustrative consequences, not universal effects for every enemy.

## Injury, disability, and defeat

### Local disability

A part receives its negative functional effect at zero HP. Damage above zero
does not progressively penalize its operation. Zero HP does not automatically
mean severance or irreversible destruction. Precise disability effects remain
open; player limb recovery follows the rules below.

Enemy consequences should be more drastic and noticeable than routine player
injuries, but it should be hard to turn an enemy into a harmless collection of
disabled zones. Suggested responses include fallback attacks, altered movement,
or a changed threat pattern. An enemy that becomes genuinely helpless should
not require a long cleanup of irrelevant HP pools.

### Destroyed enemy zones

Two approaches remain under consideration:

- Remove destroyed zones from valid targeting.
- Allow contact with a destroyed zone and transfer damage to another zone,
  possibly only a critical one.

A proposed refinement distinguishes an absent or inaccessible zone from a
disabled but attached one. Attached zones could transfer damage along explicit
anatomical connections to the core instead of to an unrelated limb. Transfer
destinations and amounts have not been decided.

Enemy vital-zone and defeat rules still need definition. Player death, defeat,
critical injury, and rescue rules are explicitly deferred. A separate overall
health pool was suggested earlier but was not accepted as a requirement.

### Temporary maximum HP reduction

This is an agreed player injury rule:

- When an attack damages a player limb already at zero HP, it instead reduces
  that limb's maximum HP by 10% of the incoming attack damage. The conversion
  percentage is configurable.
- The accumulated reduction is capped at 50% of the limb's unreduced maximum HP.
  The cap percentage is configurable; repeated hits cannot exceed it.
- Regular limb HP regenerates first, up to the currently reduced maximum.
  Maximum HP begins restoring only after regular HP has healed to that maximum.
- No explicit healing actions or direct HP restoration are planned. Recovery is
  natural regeneration; available aids can speed regeneration or mitigate
  penalties while the limb recovers.

For example, with the default settings, a limb with an unreduced maximum of 100
HP and current HP of zero takes a 20-damage hit. Its maximum falls to 98 HP, while
regular HP remains zero. Further hits can reduce the maximum as far as 50 HP.
Regular HP must recover to the reduced maximum before maximum HP recovery begins.

This gives continued attacks against a disabled limb a bounded, lasting
consequence. The reduced maximum persists during recovery without adding
progressive functional penalties at nonzero HP. Regeneration rates and recovery
modifiers remain to be defined.

## Damage and status effects

The requested damage categories are blunt, slashing, melee piercing, ballistic,
and environmental harm including heat, chemicals, and choking where breathing is
required. Both players and NPCs need support for status effects.

Suggested refinements, not finalized rules:

- Separate damage amount from penetration and from applied statuses.
- Let attacks specify effects such as bleeding, staggering, or knockback, with
  eligibility determined by the target and the resolved contact or injury.
- Treat heat and chemical protection separately rather than using one universal
  environmental resistance. Consider choking as a breathing-related process.
- Specify each status's application conditions, duration or removal conditions,
  repeat-application behavior, and effect on actions or periodic injury.
- Derive disability from body state where possible rather than duplicating it
  as a separately maintained status.

A slash need not always cause bleeding, and a blunt hit need not always stagger.
Whether armor-stopped contact can apply a particular effect must be explicit.
Status stacking, immunities, timing, and the initial status list remain open.

## Future ranged combat

Detailed firearms are a desired future direction, but they are not the current
implementation focus. No firearm feature set or simulation depth was settled.

A suggested boundary keeps weapon operation separate from shared contact,
protection, injury, and status handling. Ranged attacks could supply aiming,
projectile paths, and cover interactions while using common body and damage
rules after contact. Melee reactions should not automatically apply to bullets.

Magazine and chamber state, ammunition, weapon actions, attachments, condition,
and malfunctions were examples of possible future detail, not approved features.
Mechanical detail should remain compatible with straightforward routine inputs.

## Open questions

- Which deterministic selection algorithm, priorities, and tie rules should styles
  use? What happens when no move is available?
- How does `Tab` define nearest and valid targets? Does it ever approach a target?
- What costs and restrictions apply to reactions, counters, and consumed openings?
- Which moves have windups, and what happens to costs on interruption or failure?
- How are contact regions, clothing coverage,
  armor layers, posture, and cross-Y-layer reach represented?
- What are the exact disability effects, fallback capabilities, and defeat rules
  for each enemy body? How does damage to destroyed zones behave?
- What are the regular HP and maximum HP regeneration rates, and which effects
  speed regeneration or mitigate recovery penalties?
- Which statuses ship first, how do they stack, and how do protection and immunity
  affect their application?
- How much firearm detail is appropriate later, and which ranged rules share the
  melee resolution path?

Player death and defeat remain deliberately deferred. These questions should be
resolved before implementing their respective mechanics, rather than treating
the examples in this document as implicit decisions.
