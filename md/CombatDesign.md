# Combat Design

## Contents

- [Status and scope](#status-and-scope)
- [Goals and agreed direction](#goals-and-agreed-direction)
- [Combatants and control](#combatants-and-control)
- [Combat styles and moves](#combat-styles-and-moves)
- [Deterministic selection](#deterministic-selection)
- [Suggested melee process](#suggested-melee-process)
- [Body parts, reach, and protection](#body-parts-reach-and-protection)
- [Injury, disability, and defeat](#injury-disability-and-defeat)
- [Damage and status effects](#damage-and-status-effects)
- [Future ranged combat](#future-ranged-combat)
- [Open questions](#open-questions)

## Status and scope

This document records agreed direction, suggestions and unresolved questions.
The implemented first melee slice is described in [combat API](../doc/combat_api.md)
and [combat presentation](../doc/combat_presentation.md). Its numerical values are
initial balancing defaults, not approved final values. Suggestions below that go
beyond that slice remain deferred rather than implementation contracts.

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

- Pressing `Tab` requests an automatic attack against an eligible adjacent target.
- Attempting to move into an enemy requests an attack against that enemy.
- Both inputs let the game choose the applicable combat move deterministically.
- A plain attack participates in selection as a normal move; available limbs
  determine whether it can be chosen.

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
The initial Defensive parry uses stamina, a cooldown and a functional attack
capability; other reaction conditions and costs remain undecided.

For example, a counter could require an unconsumed opening from a successful
parry. Its eligibility rules could exclude counters as triggering attacks, so the
same mechanic cannot produce an endless counter chain. This is an illustration
of an explicit restriction, not a final rule for all counters.

## Deterministic selection

Deterministic move choice is agreed and the first slice implements:

1. Consider the current style's moves for the relevant trigger.
2. Filter them through their availability conditions.
3. Choose the eligible move with the highest authored priority.
4. Break equal priorities using a stable move identifier.

An ordinary attack trigger considers offensive moves; an incoming attack can
consider defensive reactions. A conditional technique can outrank the broadly
available plain attack when its opportunity exists. Explicit priorities would
make overlap understandable without requiring a general combat optimizer.

The selector uses no random draw; contact and region use the explicit seeded LCG.
Simulation reproducibility must still follow
the project's logical-state, explicit-RNG, and stable-identity policy rather than
depending on runtime handle allocation.

Target selection is distinct from move selection. `Tab` considers perceived,
reachable cardinal hostiles on the same Y layer and picks the lowest persistent ID;
it does not approach. Bump input selects the perceived hostile in that direction.

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

Ordinary attacks resolve immediately followed by recovery; the initial heavy move
has a windup. Damaging hits and forced movement apply the strongest accuracy
penalty to that windup, without changing its deadline; default 15, capped at 30.
Losing a required capability or being staggered cancels a pending windup; aiming
also cancels on damage or forced movement. Cancellation starts 20 ticks of recovery
by default, which further disruptions cannot reset. None or already committed
recovery is not canceled. Heavy costs are committed at the start; losing the
target does not refund them or eliminate the post-windup recovery.

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
does not progressively penalize its operation. Detailed hands and legs become
broken at zero; this does not imply severance or irreversible destruction.
After seven logical days of mending a broken limb returns at 40 HP by default,
clamped to its effective maximum; 60 is a configurable policy. Further injury
does not reset mending. Recovery requires regeneration to be enabled.

Enemy consequences should be more drastic and noticeable than routine player
injuries, but it should be hard to turn an enemy into a harmless collection of
disabled zones. Suggested responses include fallback attacks, altered movement,
or a changed threat pattern. An enemy that becomes genuinely helpless should
not require a long cleanup of irrelevant HP pools.

### Destroyed enemy zones

The first slice transfers subsequent hits to a disabled simple attached zone to
its Core; reaching zero Core defeats a simple non-player. Other anatomy and
targeting approaches remain under consideration:

- Remove destroyed zones from valid targeting.
- Allow contact with a destroyed zone and transfer damage to another zone,
  possibly only a critical one.

A proposed refinement distinguishes an absent or inaccessible zone from a
disabled but attached one. Attached zones could transfer damage along explicit
anatomical connections to the core instead of to an unrelated limb. Transfer
destinations and amounts beyond the simple Core transfer have not been decided.

Enemy vital-zone and defeat rules still need definition. Player death, defeat,
critical injury, and rescue rules are explicitly deferred. A separate overall
health pool was suggested earlier but was not accepted as a requirement.

### Temporary maximum HP reduction

This is an agreed player injury rule:

- When a hit injures a detailed limb already broken, it instead reduces
  that limb's maximum HP by 10% of the whole post-protection injury. The conversion
  percentage is configurable.
- The accumulated reduction is capped at 50% of the limb's unreduced maximum HP.
  The cap percentage is configurable; repeated hits cannot exceed it.
- Regular limb HP regenerates first, up to the currently reduced maximum.
  Maximum HP begins restoring only after regular HP has healed to that maximum.
- Injury below 1 HP after protection is ignored. Maximum loss accumulates in
  hundredths, including fractional losses from small hits; the breaking hit's
  overkill does not contribute. Later hits do not reset mending progress.
- Initial recovery defaults are 1 regular HP per logical hour and 1 maximum HP
  per logical day, switching back to regular healing after maximum increases.
- No explicit healing actions or direct HP restoration are planned. Recovery is
  natural regeneration; available aids can speed regeneration or mitigate
  penalties while the limb recovers.

For example, with the default settings, a limb with an unreduced maximum of 100
HP and current HP of zero takes a 20-damage hit. Its maximum falls to 98 HP, while
regular HP remains zero. Further hits can reduce the maximum as far as 50 HP.
Regular HP must recover to the reduced maximum before maximum HP recovery begins.

This gives continued attacks against a disabled limb a bounded, lasting
consequence. The reduced maximum persists during recovery without adding
progressive functional penalties at nonzero HP. Recovery modifiers remain open;
the initial rates are configurable balancing defaults.

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

- How should future styles and moves extend the initial selection rules?
- What costs and restrictions apply to reactions, counters, and consumed openings?
- Which additional moves have windups and interruption consequences?
- How are contact regions, clothing coverage,
  armor layers, posture, and cross-Y-layer reach represented?
- What are the exact disability effects, fallback capabilities, and defeat rules
  for each enemy body? How does damage to destroyed zones behave?
- Which effects speed regeneration or mitigate recovery penalties?
- Which statuses ship first, how do they stack, and how do protection and immunity
  affect their application?
- How much firearm detail is appropriate later, and which ranged rules share the
  melee resolution path?

Player death and defeat remain deliberately deferred. These questions should be
resolved before implementing their respective mechanics, rather than treating
the examples in this document as implicit decisions.
