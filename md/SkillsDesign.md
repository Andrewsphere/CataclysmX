# Skills Design

## Contents

- [Status and scope](#status-and-scope)
- [Skill levels and requirements](#skill-levels-and-requirements)
- [Progression models](#progression-models)
- [Activity eligibility and teaching limits](#activity-eligibility-and-teaching-limits)
- [Books and study](#books-and-study)
- [Martial arts and active abilities](#martial-arts-and-active-abilities)
- [State, presentation, and integration](#state-presentation-and-integration)
- [Design acceptance scenarios](#design-acceptance-scenarios)
- [Open questions](#open-questions)

## Status and scope

This is the agreed skill-system design, not an implemented API or content schema.
The current engine has no character skill progression. See
[combat design](CombatDesign.md) for martial arts, attacks, defenses, equipment,
and balance experiments, and [crafting design](CraftingDesign.md) for the existing
job model and planned skill integration. This pass changes Markdown only.

Skills are shared character state, usable by combat, crafting, deconstruction,
and future relevant activities. A skill defines its progression category and can
provide level requirements or authored passive benefits. Learning a recipe,
learning an art, and gaining skill XP are distinct concepts.

The core direction is:

- Learned skill levels range from 0 through 10.
- Effective levels can exceed 10 through modifiers/status effects.
- Recipe and permanent technique requirements use learned levels.
- Noncombat advancement toward levels 1-5 uses ordinary XP; advancement toward
  levels 6-10 requires paired theory and practice.
- Combat advancement toward every level uses ordinary XP.
- Ordinary skill books grant their learning benefit once per character, remain
  physical items, and have an authored teaching range.
- Book multipliers use the strongest applicable value and last until the relevant
  skill levels up. They have no elapsed-time expiry or consumable bonus-XP budget.
- Crafting/deconstruction teach only at appropriate levels; a level-3 activity
  stops teaching that skill when the character reaches level 4.

## Skill levels and requirements

### Learned and effective level

The **learned level** is earned progression, an integer in [0, 10]. Advancement XP
and, where applicable, theory/practice reserves belong to this learned skill.
Temporary modifiers do not change the progression model, teaching eligibility, or
which level a book benefit is attached to.

The **effective level** combines learned level with applicable modifiers. Values
above 10 are valid for performance calculations; they do not create learned level
11 or permit authors to require level 11 in a recipe. Negative modifiers can
reduce performance, with the lower bound and effect-specific formulas to be
specified. Each formula must define its behavior above 10 rather than indexing
past a ten-level table or assuming unlimited linear benefits.

| Use | Relevant level/state |
| --- | --- |
| Recipe skill requirement | Learned level |
| Permanent skill-gated art/move unlock | Learned level, plus any separate art prerequisites |
| Contact, defense, efficiency, or handling performance | Effective level and the relevant equipment/state |
| Teaching limit and progression category | Learned level |
| Skill-book multiplier expiry | Actual learned-level advancement in that skill |
| Art acquisition and art XP | Separate art rules; not automatically granted by a skill modifier |

Recipes may require levels 1-10, inclusive. An ungated activity has no positive
skill requirement; a novice teaching level of 0 is not a recipe requirement beyond
that range. A temporary effective-level bonus cannot satisfy missing learned
knowledge. Permanent unlocks and current action availability remain separate: a
known move can still be unavailable because its required hand, weapon, stamina,
or triggering condition is missing.

### Skill definitions and passive benefits

Use stable content identities and explicit references from recipes, moves, books,
and activities. A skill's progression category determines the XP model; avoid
special-casing it by display name or by which menu happens to grant XP.

Combat needs general Melee, Unarmed, Dodge, and a small set of weapon-handling
families. The exact initial list, especially weapon families and noncombat skills,
is still to be authored. Weapon handling is not simply damage type: a knife and
spear can both pierce while requiring different handling. Actions may train
several explicitly relevant skills; allocation and rates need definition.

Certain levels may provide passive benefits. Their thresholds and effects must be
authored explicitly. Skill modifiers primarily affect competence and efficiency;
avoid compounding large damage, speed, contact, and defense multipliers at every
level. Whether each passive is a permanent learned unlock or a current-performance
effect must be clear in its definition.

## Progression models

### Advancement bands

Bands refer to the **next level being earned**, not the current level's label.

| Skill category | Current learned level | Advancing toward | Model |
| --- | --- | --- | --- |
| Noncombat | 0-4 | 1-5 | Ordinary advancement XP |
| Noncombat | 5-9 | 6-10 | Theory + practice -> advancement XP |
| Combat | 0-9 | 1-10 | Ordinary advancement XP |
| Either | 10 | No further learned level | No learned-level advancement |

For example, a noncombat character who has just reached level 5 immediately uses
the paired model for progress toward level 6. An effective-level modifier does
not move a character between these bands.

### Ordinary XP

Eligible activities grant advancement XP directly. For noncombat levels 1-5,
crafting, deconstruction, and other authored activities can contribute. Combat
skills use this model throughout levels 1-10; they never require crafting practice
or theoretical XP to pair with battle experience.

Relevant study can grant a bounded direct contribution and improve subsequently
earned activity XP, as described below. The XP requirement for each next level,
activity rates, and fractional accounting are tuning/schema work.

### Paired theory and practice

For noncombat advancement toward levels 6-10:

- Books and eligible deconstruction grant **theoretical XP**.
- Eligible crafting grants **practice XP**. Other practical activities, such as
  repair, can grant practice when explicitly supported.
- One theoretical XP plus one practice XP produces one advancement XP.
- Both inputs are consumed by conversion. Unmatched credit remains in bounded
  reserves until it can be paired, subject to the eventual rollover policy.

```text
Before conversion: theory = 80, practice = 30
Convert:           30 matched pairs -> 30 advancement XP
After conversion:  theory = 50, practice = 0
```

Conversion is automatic as matching credit becomes available. A theory-granting
book can therefore advance a character who already has practice; it still has
not granted unpaired direct advancement XP. Do not apply a study multiplier to
the paired result again or reuse either consumed input.

Advanced progression requires **a theory source and a practice source**, not
necessarily a book. Appropriate deconstruction can supply the theory instead.
Repeatedly crafting and dismantling something can train within its teaching
range, but cannot bypass the level limit of that activity.

Reserves are bounded to prevent introductory material from supplying unlimited
future mastery. A cap around one level's requirement is the starting tuning
direction, not a finalized number. Exact reserve caps, cross-level rollover, and
source-range handling of unmatched credit remain implementation design decisions.
Those rules must respect teaching limits and make any discarded/capped credit
visible; do not silently assume unlimited banking.

## Activity eligibility and teaching limits

### Crafting and deconstruction

For a skill taught by an activity of required/training level `r`, teaching ends
when the learned skill reaches `min(r + 1, 10)`. This is a ceiling on the learned
level that the source can help the character attain, not permission to keep
training throughout that resulting level.

For a craft requiring level 3:

- Below learned level 3, the requirement is not satisfied.
- At learned level 3, appropriate work grants XP toward level 4.
- At learned level 4 or above, the craft is still usable but grants no further XP
  in that skill.

Deconstruction follows the same rule using its own requirements/training level.
It need not use the original crafting recipe's difficulty. An ungated beginner
activity can have training level 0 and teach toward level 1. A level-10 recipe
does not create learned advancement beyond 10.

Apply eligibility separately to each skill an activity explicitly trains. Award
processing must stop or split at teaching ceilings: a large level-3 award cannot
spill into training toward level 5. At a progression-band boundary, re-evaluate
which XP channel is appropriate rather than treating old ordinary XP as unrestricted
advanced credit. Precise surplus routing and reserve rollover are open details.

At learned level 5, an appropriate level-5 craft grants practice toward level 6,
and appropriate level-5 deconstruction grants theory toward level 6. A level-3
craft cannot do either. Effective-level buffs do not change those teaching limits.

### Credit from actual activity

Training follows meaningful work or combat participation, not menu inspection,
refused actions, repeated selection, or a count of internal damage packets.
Combat training may reward relevant attempts and defensive effort, not only HP
damage. Exact rates, challenge limits, and attribution among Melee, weapon/Unarmed,
Dodge, and art XP remain to be specified.

Crafting integration must define credit for funded/completed work, completion,
pause, cancellation, and worker changes. Resuming or loading a job must not credit
the same work twice. Whether XP is credited incrementally or at completion is
still open; the existing job timing and reservation contracts are not silently
changed by this design. Teaching caps make increasing activity difficulty
necessary without requiring per-object crafting-history tracking.

## Books and study

### Once per character, not destroyed on reading

An ordinary skill book's learning benefit can be obtained once by a character.
The physical item is not consumed. Track learning-content identity rather than
the physical item handle: another equivalent copy does not reset that character's
benefit. Another character can study the same physical book for their own benefit.
Exact identity rules for editions or items containing several distinct learning
benefits need definition when those content cases are introduced.

Books have authored teaching ranges. Define those ranges in terms of the levels
they help earn, so eligibility is unambiguous at the level-5/6 boundary. A range
allows a book to be useful at an appropriate point in that range; it does not
grant a new once-per-book benefit at every level in the range. Rereading after a
multiplier expires does not refresh it.

Completed eligible study grants the benefit and records its use. Inspection and
canceled study do not consume that benefit. Reading duration and interruption
details still need specification. Flavor rereading need not grant training.

### Noncombat books for advancement toward levels 1-5

A book can provide both a direct XP grant and a multiplier on later eligible
activity XP. These are separate benefits:

- Total **direct** book XP for one advancement level is capped at 30% of that
  level's full XP requirement, across books. The cap is not 30% per copy and not
  30% of the remaining XP.
- A particular book can grant less than that cap. Direct awards are not multiplied
  by the active study boost and do not bank unused allowance for another level.
- Book multipliers can be substantial, such as x2, x3, or x4; those example values
  are tuning options, not a required progression table.
- **The largest applicable multiplier wins.** Values neither add nor multiply
  together. A later weaker book does not reduce an existing stronger boost.
- The boost stays with the character until that skill's next learned-level gain.
  It has no time limit and no finite bonus-XP budget.
- At level-up, the boost ends. Books already used remain used; effects are not
  queued to reactivate at later levels. Raising an effective level or a different
  skill does not expire this skill's boost.

For example, if a level needs 1000 XP, books can directly contribute at most 300.
After one supplies 200, another can directly supply at most 100 more for that
level. An active x3 boost beats x2 and remains x3 until the skill levels up. It
can accelerate the remaining activity XP; the 30% cap limits direct book grants,
not the total bonus XP earned while doing activities.

Benefits are attached to the advancement level for which study is completed.
Level-up resets that level's direct-book accounting and ends its multiplier; it
does not give the same reading a second benefit in the new level. Exact rounding
and award splitting must preserve those boundaries.

### Noncombat books for advancement toward levels 6-10

These books grant theoretical XP within their teaching range and reserve rules.
They do not replace required practice with a direct advancement grant. The
ordinary x2/x3/x4 book-boost model does not automatically extend into this band.
The 30% direct-XP limit belongs to the ordinary model; it is not an additional
30% limit on theoretical XP that legitimately pairs with earned practice.

An ordinary boost earned while advancing toward level 5 expires on reaching
level 5, before the paired model begins. A book spanning both bands needs explicit
eligible benefits; its range must not accidentally turn low-level study into
unrestricted advanced theory.

### Combat books for advancement toward levels 1-10

Combat skills retain ordinary XP at every level. Combat books provide a small
activity-XP boost, **at most +20% (x1.2)**. The strongest applicable book wins and
the boost lasts until that combat skill levels up, just as in the ordinary model.

Immediate combat-book XP is little to none. Zero is a suitable starting default;
the exact small allowance, if any, is still tuning work. Any direct grant also
counts toward the shared 30% per-level book ceiling, but that ceiling is not
permission to routinely grant 30% combat XP from a book.

Book benefits apply to their named skills. A Melee XP boost does not automatically
multiply every weapon skill, art XP, or all training from the same attack.

## Martial arts and active abilities

Skill progression is distinct from knowing an art and earning XP within it. See
[combat styles](CombatDesign.md#combat-styles-and-moves) for the full rules:

- Art manuals permit repeated time-consuming chance-based acquisition attempts.
  Failure does not consume the manual or mark the art as permanently unlearnable.
- The acquisition chance depends on Melee skill. Teachers, starting choices, and
  other explicit sources can also grant an art.
- Once known, relevant use earns that art XP and unlocks moves at authored
  art-XP and/or learned-skill thresholds.
- A technique requiring both skill and art XP requires both; effective skill
  modifiers cannot permanently unlock it early.
- Selecting an art alone does not train it. Multi-hit actions do not automatically
  multiply art credit through their internal damage events.

The art-manual acquisition rule is an intentional exception to exhausting an
ordinary skill-book benefit after successful study: a failed learning attempt
has not learned the art. If a manual also carries an ordinary skill benefit,
track the two uses separately. Repeated acquisition failures do not repeat that
ordinary benefit. Reacquiring an already known art does not manufacture art XP.

Arts can also grant manually activated abilities. Bionics and mutations are other
possible grant sources; they do not automatically add a new learned skill or
progression model. Ability availability and skill performance remain separate.

## State, presentation, and integration

The design requires authoritative character state for learned levels, advancement
XP, bounded unmatched theory/practice, direct-book XP credited for the current
level, the active per-skill study multiplier, and consumed learning benefits.
Art knowledge, art XP, relevant selection history, and in-progress study also
need authoritative persistence when introduced. Derived unlocks need not be
duplicated if they can be computed consistently from stable definitions/state.

Use the project's integer-ratio convention: `ratioScale == 10000`, 3000 is 30%,
12000 is x1.2, and 30000 is x3. Specify rounding and carry fractional XP where
incremental accounting requires it. Work subdivision, pauses, saving/loading, and
UI inspection must not create extra credit or reroll completed manual attempts.
The precise saved-state schema and migrations are implementation work.

Presentation should show learned and effective levels separately, progress toward
the next level, unmatched theory/practice and their caps, the active study boost
with "until next level" expiry, a book's range and used status, and why an activity
no longer teaches a skill. Recipe inspection must distinguish missing learned
knowledge from a temporary performance penalty. Manual study should expose its
learning chance and time commitment.

Balance runs compare fixed learned/effective skills and art unlocks by default.
Separate progression scenarios should measure time/work/books needed for levels,
theory/practice bottlenecks, changing activity tiers, and diminishing usefulness
of beginner recipes. These experiments use the real skill rules rather than a
separate progression formula.

## Design acceptance scenarios

These examples define intended gameplay, not tests executed in this Markdown pass.

| Scenario | Expected outcome |
| --- | --- |
| Noncombat character reaches learned level 5 | Further advancement toward 6 needs theory/practice pairs. |
| Combat character reaches learned level 5 | Further advancement toward 6 still uses ordinary XP. |
| Theory 80 and practice 30 become available | Gain 30 advancement XP, retaining 50 unmatched theory. |
| Advanced crafting without theory | Bank bounded practice; no advancement until matching theory arrives. |
| Appropriate deconstruction plus crafting, without a book | Theory and practice can produce advanced progression. |
| Level-3 craft used at learned level 3 | Can teach toward level 4. |
| The same craft used at learned level 4 | Craft remains usable but grants no XP in that skill. |
| Ungated teaching-level-0 beginner activity | Can teach a novice toward learned level 1. |
| Learned level 2, effective level 4, recipe requires 3 | Requirement is not met. |
| Learned level 10 plus a modifier | Effective level may exceed 10; no recipe requires 11 and no learned level 11 is earned. |
| Several ordinary books contribute direct XP in one level | Combined direct credit cannot exceed 30% of its full requirement. |
| x2 and x3 book benefits apply to the same skill/level | x3 wins; the result is neither x5 nor x6. |
| Time passes or boosted XP is earned without a level-up | The applicable boost remains; no timer/budget is exhausted. |
| The boosted skill levels up | Boost ends; already used books do not become usable again. |
| Another equivalent copy is found | It does not refresh the same character's ordinary learning benefit. |
| Another character reads the same physical book | That character can obtain their own eligible benefit. |
| Combat book improves training | At most x1.2 activity XP; direct XP is little to none. |
| Art-manual acquisition fails | The manual remains available for another time-consuming attempt. |

## Open questions

The progression models and limits above are agreed. Remaining details include:

- Initial skill IDs/families, passive benefits, effective-level formulas, and
  behavior above 10.
- XP requirements per level; activity rates and multi-skill allocation; combat
  challenge limits; exact art-XP thresholds.
- Theory/practice reserve capacities, rollover at level-up, and surplus routing
  across teaching limits and the ordinary/paired boundary.
- Credit timing for crafting/deconstruction work, cancellations, and worker changes.
- Book teaching ranges, reading times, study interruption/completion rules,
  edition/multi-benefit identities, and any small immediate combat XP grants.
- Manual learning chance/time curves and teacher acquisition conditions.
- Exact content schemas, saved-state/network migrations, and frontend controls.

Do not infer unsupported JSON fields or runtime APIs from these conceptual rules.
