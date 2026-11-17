# Combat Design

## Contents

- [Status and scope](#status-and-scope)
- [Goals and agreed direction](#goals-and-agreed-direction)
- [Combatants and control](#combatants-and-control)
- [Combat styles and moves](#combat-styles-and-moves)
- [Pseudorandom selection and targeting](#pseudorandom-selection-and-targeting)
- [Defenses, reactions, and counters](#defenses-reactions-and-counters)
- [Active abilities](#active-abilities)
- [Hands and equipment](#hands-and-equipment)
- [Melee process and timing](#melee-process-and-timing)
- [Stamina and skill effects](#stamina-and-skill-effects)
- [Body parts, reach, and protection](#body-parts-reach-and-protection)
- [Injury, disability, and defeat](#injury-disability-and-defeat)
- [Damage and status effects](#damage-and-status-effects)
- [Future ranged combat](#future-ranged-combat)
- [Balance simulation](#balance-simulation)
- [Design acceptance scenarios](#design-acceptance-scenarios)
- [Open questions](#open-questions)

## Status and scope

This document records the agreed next combat design. It is a design contract,
not a claim that these systems are implemented. The implemented first melee slice
is described in [combat API](../doc/combat_api.md),
[combat presentation](../doc/combat_presentation.md), and
[inventory/equipment](../doc/inventory_engine.md). Existing numerical defaults
remain provisional. Examples below illustrate capabilities, not promised moves.

The first expanded version focuses on melee, including dual wielding, learned
arts, skills, wearable armor, and manually activated abilities. Detailed firearms
and exceptional MindOverMatter-style arts are later work. Gameplay and clarity
take priority over physical simulation. See [skills](SkillsDesign.md) for skill
levels, books, practice, and recipe requirements, and
[entity AI](EntityAIDesign.md) for intent production. Implemented API documentation
belongs under `doc/`; this pass changes Markdown only.

### Implementation baseline and superseded decisions

The existing engine already has shared melee resolution, weapon damage/cost/hand
requirements, injury and recovery, windup disruption, explicit RNG, and saved
pending actions. It still has one wielded-item reference, three hard-coded styles,
highest-priority move selection, lowest-persistent-ID automatic targeting,
guaranteed eligible parries, no ordinary-attack stamina cost, and numeric protection
without worn armor. It has no character skill/art progression or human vital-part
death rule.

This design supersedes the earlier agreements to select moves without randomness,
choose the oldest adjacent target, and defer the ordinary human death condition.
Seeded reproducibility remains required. Weighted choices, equipment, skills, and
death must be implemented explicitly before the runtime documentation describes
them as available.

## Goals and agreed direction

- Keep routine combat input light enough for fighting hordes. Players choose one
  active style; the game chooses individual automatic moves pseudorandomly.
- Define styles and moves in JSON content, with explicit C++ extension points for
  new conditions and effects. Provide basic styles and separately learned arts.
- Distinguish skill progression, learning an art, and earning XP within that art.
- Model two occupied/free hands, weapon grips, utility items, and dual wielding.
- Give worn equipment local coverage, protection, layers, and encumbrance.
- Provide manually activated abilities through a separate menu and world hotkeys.
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
- Ordinary human characters die when head or torso HP reaches zero, including
  player characters and character NPCs. Post-death handling is separate work.

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

### Content and extension boundary

Styles, arts, and moves are content definitions with stable identities. JSON owns
their repertoire, selection weights, costs, requirements, progression thresholds,
and parameters for supported effects. C++ supplies explicitly registered, typed
condition evaluators and effect implementations. Adding a style assembled from
existing mechanics should not require a new C++ class or a central style switch.

The extension boundary must accommodate new mechanics without treating arbitrary
JSON as executable code. For example, a future art could check whether its user
recently moved or reward movement with an action-cost effect. These are extension
examples, not requirements to implement those particular abilities now. Prefer
small explicit state/facts over an unrestricted event-history or scripting system.

There is one active style. Basic fighting styles are generally available; learned
arts provide specialized repertoires. NPCs can use the same mechanisms with smaller
or hidden repertoires. Changing style is a small paid action while ready; it does
not rewrite a committed attack or refresh already spent cooldowns/openings. Exact
switching cost and disposition of style-specific temporary effects remain tuning
and transition details to specify.

### Learning an art

Art acquisition is separate from ordinary skill-book benefits:

- A manual permits repeated, time-consuming learning attempts. Failure does not
  consume the manual or permanently exhaust the character's opportunity to learn.
- Learning is chance-based and depends on Melee skill. Display the chance and
  applicable prerequisites; roll once on completion of a valid study attempt,
  using simulation RNG. Inspection or cancellation does not grant another roll.
- NPC teaching, character creation, and other authored sources can grant an art.
  Their acquisition conditions are explicit rather than implicitly reusing a
  manual's chance roll.
- Once learned, the art is known. Re-reading for acquisition does not repeatedly
  grant it or manufacture art XP. Manuals remain physical items.

The reading duration, learning curve, and teacher costs still need content/tuning.
If an item also provides an ordinary skill-book benefit, that benefit follows the
separate once-per-book rules in [skills](SkillsDesign.md#books-and-study).

### Art progression and move unlocks

Each learned art has its own XP. Every newly learned art provides a usable starter
repertoire so its progression does not depend on already unlocking advanced moves.
A move unlock can require a learned skill level, art XP, or both; when both are
specified, both must be met. Permanent unlocks use learned, not temporarily
modified, skill levels. Unlocking a move does not waive its runtime requirements.

Relevant attacks and defenses train the art that supplied the committed action.
Merely selecting an art, holding incompatible equipment, or switching styles after
commitment does not earn that art credit. Art training is based on meaningful
effort/outcomes, not the number of damage packets in an attack; a multi-hit move
does not automatically multiply training rewards. Exact rates and milestones are
content/tuning work. An additional art-level scale is not required by this design.

### Move structure

| Part | Purpose | Illustrative example |
| --- | --- | --- |
| Availability | Establish whether an unlocked move can be used | Suitable weapon/grip, functioning or free hand, skill, stamina, opening, or trigger |
| Preparation | Apply effects before the main execution | Brace, step forward, or expose a vulnerability |
| Execution | Perform the move | Strike, parry, shove, or counterattack |
| Follow-through | Apply consequences after execution | Knockback on hit or an opening after a miss |

Moves distinguish miss, dodge, parry, defensive block, armor-stopped contact,
damaging contact, and defeat/death where relevant. Follow-through requires the
appropriate outcome rather than treating every attempt as a successful hit.

A plain attack is a broadly available move. One primary move is selected per
request; a move may explicitly contain several strikes or effects and pays for
its full commitment. Availability is checked per move and attack source: an
unusable held weapon must not globally prohibit an otherwise eligible kick or
off-hand attack. Alternatives are explicit moves, not silent substitutions of a
different weapon into an already committed strike.

## Pseudorandom selection and targeting

### Automatic move selection

1. Gather the active style's unlocked moves for the trigger.
2. Filter through current availability, including equipment and resources.
3. Calculate positive selection weights for eligible candidates.
4. Choose one candidate using the explicit simulation RNG.
5. Commit that move's own timing and costs.

Weights replace highest-priority-always-wins selection. Randomly breaking ties
only within the highest priority would retain the repeated-move problem. Authored
weights, conditional opportunities, and cooldowns shape the repertoire. Optional
recent-use weight reductions can discourage repetition where appropriate; ordinary
attacks may repeat. Random selection is not a guarantee of alternating moves.

Order candidates by stable definition identity before drawing. Selection history
that affects weights is authoritative state. Equivalent logical state, inputs,
content, and RNG state must reproduce choices independently of runtime handles.
Inspection, menu rendering, and eligibility queries do not consume selection RNG.
Specify draw boundaries and integer sampling during implementation; probabilities
and modifiers follow [ratioScale](../doc/ratios.md).

### Target selection

- `Tab` chooses a perceived, reachable, attackable adjacent hostile randomly. The
  neighborhood includes all eight horizontal neighbors on the same Y layer; it
  does not move toward a target. No eligible target means an uncommitted refusal.
- Moving/pressing an arrow into a perceived hostile requests an attack on that
  particular enemy. The selected move is still automatic and pseudorandom.
- Target choice precedes move choice. Use stable persistent identities to order
  random target candidates, not as a rule to always attack the oldest actor.
- Explicit attacks outside ordinary bump range need targeting/aim input. An arrow
  into an empty neighboring tile remains movement rather than unexpectedly
  attacking a farther enemy. Exact reach-targeting controls remain to be specified.

Routine automatic attacks require no technique menu or reaction prompt. Manually
activated abilities have their own input path below.

## Defenses, reactions, and counters

### One response per incoming strike

Choose at most one eligible defensive response to a strike. Do not independently
stack a full dodge attempt, then parry, then shield block against that same strike.
Armor still resolves after a failed or partially effective defense. Separate
strikes in an explicitly multi-strike move are separate contacts; cooldowns and
costs continue to apply across them.

| Defense | Result on success | Main requirements |
| --- | --- | --- |
| Dodge | Avoid contact | Attack rating versus Dodge ability, awareness, usable movement, stamina and encumbrance |
| Parry | Deflect an eligible attack | Suitable weapon or explicit technique, handling skill, available grip/limbs |
| Block | Absorb/reduce contact; remaining damage may reach armor/body | Suitable shield, equipment, or technique and its blocking capacity |

Defenses are fallible. A normal bare-handed parry does not automatically negate
every weapon attack. Incoming attacks declare which defense kinds can apply;
melee defenses do not implicitly stop bullets, area effects, or self-only powers.
Attack skill versus Dodge skill is part of dodge resolution. Exact contact and
defense curves, response selection weights, and any intrinsic-miss stage remain
to be specified together so the same defense is not counted twice.

Committed attempts pay stamina and start their cooldown even when unsuccessful.
Availability includes awareness, body function, equipment, resources, cooldowns,
and triggering conditions. These per-move restrictions control out-of-turn use;
there is no additional universal per-round reaction allowance.

### Interaction with pending actions

- Compatible reactions are allowed during ordinary recovery.
- A windup permits only reactions compatible with its committed action and limb/
  weapon use. A committed heavy weapon may be unavailable for parrying while a
  separately held shield remains usable.
- Stagger and other specified states restrict reactions. An unaware defender
  cannot make an ordinary active defense.
- A reaction does not reset or erase existing recovery. Any additional commitment
  or displacement is explicit and uses the common scheduling/disruption rules.

### Openings and counters

A successful defense may grant a short-lived opening associated with the relevant
opponent. A counter requires and consumes that opening once, along with its own
resources, timing, and eligibility. Counterattacks do not generate further counter
opportunities. This prevents recursive counter chains while retaining explicit
out-of-turn attacks. Exact durations, costs, and ordering of simultaneous effects
remain tuning/execution details.

## Active abilities

An active ability is a manually requested action, separate from automatic move
selection. It may be combat or noncombat. Arts, bionics, mutations, and other
explicit sources can grant abilities; an electromagnetic pulse, acid spit, and
adrenaline activation are illustrative future examples, not V1 content promises.

An ability definition identifies its grant requirements, targeting mode, timing,
resource costs, cooldown/charges, effects, and interruption rules. Targeting can
be self/no external target, actor, world object, terrain location, direction, or
area as supported by the effect. Walls and trees need not be actor entities.

- A separate abilities screen lists available actions and explains unavailable
  ones. An ability can be bound to a world hotkey using stable ability identity.
- Menu and hotkey activation invoke the same authoritative action and validation.
  AI can request the same action through its intent path.
- Opening the menu, choosing a target, inspecting, or canceling before commitment
  spends no action resources. Valid activation commits the declared time and
  resources; an ability's actual aiming/windup is part of that paid action.
- Delayed effects revalidate targets at execution. Losing a target does not
  silently replace it or refund an already committed action; special behavior
  requires an explicit effect rule.
- Attacking effects specify applicable defenses, damage, protection interactions,
  and follow-through. A self-only activation is not automatically an attack.

Active abilities and automatic moves may reuse typed effects while preserving
their distinct activation rules. Ability grants do not require implementing the
whole bionics or mutation systems in the first melee expansion. Multiple grant
sources, removal of a grant during windup, and source-specific cooldown ownership
still need explicit identity/lifetime rules before those combinations ship.

## Hands and equipment

Detailed characters have explicit left/right hand occupancy. Inventory owns an
item once; equipment references and two-handed occupancy do not duplicate the
item or its mass. A healthy hand is not necessarily a free hand.

Distinguish holding an item, its selected grip, and the attack source of a move.
Supported loadouts include a one-handed weapon plus a free hand, weapon plus
utility, weapon plus shield, one weapon held in two hands, and two weapons. Full
dual-weapon combat belongs in V1. Holding two weapons does not grant two full
attacks for the cost of one: off-hand and paired attacks are explicit moves with
their own total cost, accuracy, stamina, and timing.

Weapons can author supported grips; alternative grips are not inferred for every
item. Moves specify required occupied/free/functional hands and supporting limbs.
Utility behaviors distinguish holding, wearing, and merely carrying an item where
that matters. A style can explicitly permit an eligible kick while hands are busy.

Disability retains equipment but can invalidate its use. Regripping, switching
hands, drawing, and stowing are explicit equipment actions with declared costs;
they do not replace the source of a committed strike. Pending actions preserve
the identities and grip/capabilities they require. An eligible alternative move
can be selected on a later request.

Furniture grabbing is an intentional convenience exception: actors may grab/move
furniture while holding weapons. It does not require unequipping a loadout, and
does not imply free hands for martial-art moves or other hand-gated actions. Exact
equipment-action costs and compatibility rules remain to be authored.

## Melee process and timing

The agreed resolution structure is:

```text
Attack request
    -> target selection
    -> move availability and weighted pseudorandom selection
    -> commitment of costs and timing
    -> preparation effects
    -> at most one defensive response per strike and contact resolution
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
4. Resolve the permitted defense and determine contact and the struck region.
5. Resolve protection, injury, status applications, and lost capabilities.
6. Apply outcome-dependent follow-through and establish the next opportunity to
   act according to the committed timing.

The existing timing baseline has ordinary attacks resolve immediately followed
by recovery; the initial heavy move has a windup. Damaging hits and forced
movement apply the strongest accuracy
penalty to that windup, without changing its deadline; default 15, capped at 30.
Losing a required capability or being staggered cancels a pending windup; aiming
also cancels on damage or forced movement. Cancellation starts 20 ticks of recovery
by default, which further disruptions cannot reset. None or already committed
recovery is not canceled. Heavy costs are committed at the start; losing the
target does not refund them or eliminate the post-windup recovery.

These are implemented baseline rules, not final numbers for every new move. Each
new windup and reaction must specify its commitment and disruption behavior.
The existing [entity scheduling](../doc/entity_scheduling.md) provides simulation
ticks and individual deadlines. Combat continues to use that model. Movement
rewards should initially modify action costs or timing explicitly, rather than
introducing a separate universal round or movement-point system.

## Stamina and skill effects

Ordinary attacks spend a small amount of stamina. Heavy attacks, demanding
techniques, and committed defenses spend more according to their definitions.
Training can improve efficiency; equipment, grip, and encumbrance affect effort.

The balance target is net expenditure: sustained ordinary fighting slowly drains
stamina, heavy activity drains it faster, and rest restores it. A nominal attack
cost that is always refunded during ordinary recovery would not achieve this.
Regeneration during activity/rest and exhaustion penalties need explicit formulas
and simulation measurements. Exhaustion reduces available options without making
the character completely unable to act; the exact exhausted fallback is open.

[Skills](SkillsDesign.md) influence contact, defenses, efficiency, bounded handling
speed improvements, and move unlocks. Avoid unconstrained multiplicative growth
across accuracy, speed, damage, and defense. Ordinary expert unarmed fighting should
be viable through efficiency and control while weapons retain meaningful benefits,
especially against armor. Future exceptional MindOverMatter-style arts may be
effective against armored opponents through explicit special mechanics; they are
outside V1.

## Body parts, reach, and protection

### Detailed bodies

The existing detailed injury layout is head, torso, left and right hands, and left
and right legs. Keep those coarse injury categories. Contact selection gains
authored weights; the torso should be more likely than an individual extremity.

Consider incoming attack reach: a short enemy such as a dwarf zombie or an angry
fish may only reach the player's legs. Tall creatures may extend beyond a single
Y layer. The body model needs to accommodate these interactions without requiring
unnecessarily detailed anatomy.

### Separate contact, injury, and clothing

| Concept | Responsibility | Example |
| --- | --- | --- |
| Injury part | HP and loss of function | Hand or leg |
| Contact region | Which location an attack can reach and strike | Head, torso, hand, or leg |
| Clothing coverage | Which protection applies at that location | Glove, leggings, or torso garment |

A glove protects its covered contact region, and injury maps to the appropriate
injury pool. Coverage does not require an HP pool for every garment or anatomical
detail. The exact initial contact-region list and sleeve/arm-to-hand injury
mapping must be specified so a glove cannot accidentally protect a whole upper
limb. Prefer coarse reach categories to detailed body geometry. Long-reach weapon
identity is desired, but distances, obstruction rules, posture, and cross-Y-layer
combat still need their own explicit rules.

### Worn equipment and layers

Each garment/armor definition declares its covered regions, coverage ratio per
region (with a shared default where useful), protection by damage type, layer,
intrinsic regional encumbrance, and equip/remove costs. Coverage such as 30% or
95% represents how much of that region is protected, not damage reduction by
that percentage. Use `ratioScale`: 3000 is 30%, 9500 is 95%.

Two garments on the same layer covering the same region add an encumbrance penalty
on that region. Further overlapping garments add further penalties. Equipment on
different layers still contributes its intrinsic encumbrance. The conceptual rule
is:

```text
regional encumbrance = intrinsic contributions + same-layer overlap penalties
```

Definitions can explicitly exempt garments from particular overlap rules or use
special supported clothing behavior. Exemptions name what they bypass; bypassing
layer overlap does not automatically remove intrinsic burden or rigid equipment
incompatibility. Cloaks and zentai illustrate why special clothing needs support,
not why all such items must ignore every equipment rule. A few explicit layers
and exceptions are preferable to a large physical clothing simulation. Final
layer names, overlap formula, and rigid-slot incompatibilities remain open.

Encumbrance has local, action-specific effects: hands/upper limbs affect handling,
parries, and manipulation; torso burden affects attack effort and evasion; legs
affect movement, dodging, and kicks. Avoid charging the same penalty again through
both global speed and overlapping action modifiers. Inventory mass and equipment
references count each physical item only once.

### Protection resolution

1. Select the contacted region using the attack's reach/region rules.
2. Determine which garments cover this contact. Each garment's coverage result
   applies to every damage component of that physical strike.
3. Sum applicable type-specific protection from covering layers and applicable
   intrinsic protection.
4. Apply penetration once to that combined protection for each damage component,
   then subtract the remaining protection from that component's damage.
5. Combine remaining damage before whole-HP injury rounding and apply local injury
   and disruption once for the contact.

An attack can be stopped entirely by armor. A mixed blunt/slashing contact must
not independently find a gap in the same garment for each damage component.
Defensive blocking precedes this armor step and can leave damage to resolve here.
Coverage correlation between overlapping garments and the exact block-reduction
formula still need specification; the per-contact/per-component distinction above
is settled.

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

Additional enemy vital-zone and defeat rules remain anatomy-specific. A separate
overall health pool is not required.

### Ordinary human death

For an ordinary human character, head HP **or** torso HP reaching zero means
death, not merely temporary incapacitation. This applies to players and character
NPCs. Losing a hand or leg alone is not the death condition. Natural limb recovery
does not revive a dead character or restore a failed vital pool.

This replaces the earlier deferral of the human death condition. Corpse creation,
inventory disposition, player-session/death presentation, and other post-death
consequences still need implementation design. Special anatomies or future effects
must explicitly define any exception rather than inheriting human rules by accident.

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

Effect design follows these rules:

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
Openings, stagger, and displacement are the initial focus for differentiated
moves. A general bleeding/environmental-status feature set is not implied by their
damage type names. Exact status stacking, immunities, durations, displacement
collisions, and additional effects still need authored rules.

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

## Balance simulation

Build a headless combat experiment runner early, using the real content, combat
rules, and scheduler. It supplies automated inputs to the game simulation rather
than copying combat formulas into a separate approximation. Headless execution
already exists; the experiment runner and reports are planned work.

### Scenarios and controlled variables

Start with named representative loadouts and skill/art progression stages, then
expand around useful comparisons. Include unarmed, one-handed/utility, shield,
two-handed, and dual-weapon builds; light/heavy armor and overlapping layers;
healthy/injured bodies; and different opponents. Hold skills/art XP fixed during
combat comparisons. Progression experiments separately enable XP and study.

| Experiment | Main questions |
| --- | --- |
| Passive target | Contact distribution, throughput, penetration, armor thresholds |
| Duel | Defense, interruption, time to death/defeat, burst versus sustained damage |
| Multiple attackers | Reaction saturation, cooldowns, defense costs, exhaustion |
| Repeated encounters | Attrition, stamina recovery, sustained loadout performance |
| Open arena and corridor | Reach, displacement, positioning, declared input policy |
| Injury/encumbrance variants | Capability loss, regional penalties, equipment usability |

### Measurements and reproducibility

Record success/death/defeat/timeout separately; time to defeat; actual HP loss by
region, limb failures, and maximum-HP loss; damage prevented; miss/dodge/parry/
block/contact rates; stamina expenditure/exhaustion; move frequencies; and
interrupted or wasted windups. Distinguish raw damage and overkill from actual
injury. Include medians and adverse percentiles, not only averages.

Use repeatable seed sets and record code/content versions, scenario definition,
loadout, learned/effective skills, art unlocks, and decision policy. Vary starting
order and action offsets to expose cadence/order advantages. Identical initial
seeds do not promise identical random draws after different moves consume RNG.
Export machine-readable summaries such as CSV and optional per-run event traces;
last-interaction UI feedback alone is insufficient to explain a whole encounter.

Human trials stop at the agreed head/torso death condition. Until implemented,
the runner must identify any equivalent external stopping rule as a provisional
benchmark condition, not claim that the current runtime already kills humans.
Report stalemates/timeouts rather than forcing a winner. Numeric targets and
sample counts are tunable; simulated results do not replace playtesting controls
and readability.

## Design acceptance scenarios

These are design examples for later gameplay verification, not tests run during
this Markdown pass.

| Scenario | Expected outcome |
| --- | --- |
| Several eligible moves | Weighted seeded choice; highest authored weight is not an unconditional winner. |
| Equivalent state restored with different runtime handles | Same input/RNG produces the same target and move choices. |
| Tab with several adjacent hostiles | Chooses an eligible target randomly; arrow/bump chooses the specified enemy. |
| Manual learning fails | Time is spent; the manual remains usable for another completed attempt. |
| A move needs both skill and art XP | Unlocks only when both learned-skill and art-XP thresholds are satisfied. |
| Two weapons equipped | Explicit off-hand/paired moves, with their own full costs; no free second attack. |
| One held weapon becomes unusable | Equipment remains; later selection can still choose a valid alternative move. |
| Furniture grabbed while wielding | Allowed without granting a free hand to combat techniques. |
| Dodge, parry, and block are all available | At most one defensive response is attempted against the strike. |
| A defensive attempt fails | Its committed resource cost and cooldown still apply. |
| A counter is defended | The counter does not create another counter opportunity. |
| Two garments overlap on one layer | Extra encumbrance affects overlapping regions unless explicitly exempt. |
| A mixed-damage strike contacts a garment | One coverage outcome for that garment, shared across components. |
| Human head or torso reaches zero | Character dies, regardless of player/NPC control. |
| Ability activated through menu or hotkey | Same validation, targeting, costs, and execution rules. |

## Open questions

The core policies above are agreed. Remaining choices are narrower but must be
resolved before implementing their corresponding behavior:

- Initial art/move/ability roster, weapon-handling families, thresholds, and costs.
- Contact/defense curves, defensive-response selection, exact RNG draw boundaries,
  move repetition weights, and simultaneous reaction/follow-through ordering.
- Stamina regeneration during activity/rest and the exhausted-action fallback.
- Equipment-action costs, supported grips, limb commitments, and style-effect
  transitions on switching.
- Contact-region mapping, hit weights, layer names, overlap penalties, rigid
  incompatibilities, coverage correlation, and exact block absorption.
- Long-reach targeting controls, distances, obstructions, posture, and cross-Y reach.
- Additional enemy capability/defeat rules, absent versus attached disabled zones,
  status stacking/immunity, and regeneration aids.
- Manual learning durations/chance curve and teacher acquisition conditions.
- Ability identity across multiple grant sources and grant removal during an action.
- Post-death world/session handling and later firearm/special-art mechanics.

Exact JSON schemas, C++ interfaces, saved-state migrations, network contracts, and
UI layouts are implementation design work. This document does not introduce
unsupported content fields or silently change existing public contracts.
