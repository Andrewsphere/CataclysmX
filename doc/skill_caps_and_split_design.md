# M-Skills: individual ceilings and the Cooking/Chemistry split

**Status: Implemented mechanics and migration contract.**
Cooking's maximum of 5, the separate Chemistry identity, and the shared learning
model are already accepted in [crafting progression](crafting_progression.md).
The preceding [M-Bench proposal](powered_manual_stations_design.md) has also been
accepted. This standalone document develops the next independent mechanics slice
from the [delivery plan](crafting_delivery_plan.md).

The implementation uses learning payload version **2** and LAN protocol **26**
(`0x43584e1a`); the enclosing save format remains **16**. See [runtime skills](skills.md)
and [authoring schema](skills_content.md) for the current APIs and content rules.

## 1. Accepted outcome

| Skill | Stable ID | Learned maximum | Learning |
| --- | --- | ---: | --- |
| Cooking | `cooking` | 5 | Ordinary advancement from learned levels 0–4, then capped |
| Chemistry | `chemistry` | 10 | Ordinary advancement from 0–4; paired theory/practice from 5 onward |
| Other shipped skills | Existing IDs | 10 by default | Existing noncombat/combat rules |

Cooking 5 retains the shared meaning of level 5. Its curve is the first five
entries of the existing default curve, without rescaling:
100, 200, 400, 800 and 1,200 XP to earn successive levels.
Chemistry retains the complete ten-entry curve.

**Accepted migration:** an old `cooking_chemistry` record becomes progress in
both new skills. Chemistry retains the full record; Cooking retains it below
level 5 or becomes a clean capped level-5 record. This grants continuity of the
proficiency represented by the former combined skill. After migration, the two
skills earn progress independently.

The integrated pre-split data referenced `cooking_chemistry` only in
`data/skills/core.json5`; no shipped recipe, recycler or book referred to it.
The skill roster therefore grows from 17 to 18 without a shipped recipe/book
rewrite.
Separate food and chemical gameplay/content passes provide useful activities.

## 2. Content schema and definition shape

Skill definitions accept optional `max_level`:

```json5
{type:'skill', id:'cooking', name:'Cooking',
 category:'noncombat', max_level:5}
{type:'skill', id:'chemistry', name:'Chemistry',
 category:'noncombat'}
```

Schema rules:

- `max_level` is an integer in `[1, 10]`, default 10. Zero does not mean disabled
  or unlimited. Unknown/duplicate fields remain errors.
- Absent `xp_to_next` uses the default curve through that maximum.
- Explicit `xp_to_next` contains **exactly `max_level` entries**, each in
  `[1, 1000000000]`. Entry 0 earns level 1; the final entry earns the maximum.
- Read `max_level` before validating the curve, independently of JSON field
  order. Definitions without the new property keep the current ten-entry schema.
- Normalize unused internal curve slots to zero. Fingerprint the maximum and
  the active thresholds so equivalent omitted/explicit defaults agree.

For example, an explicit Cooking curve is:

```json5
{type:'skill', id:'cooking', name:'Cooking', max_level:5,
 xp_to_next:[100,200,400,800,1200]}
```

`SkillDefinition` stores:

```cpp
std::uint8_t maxLevel = maximumSkillLevel;
```

Keep the existing ten-slot `eastl::array` as bounded immutable catalog storage.
The shared definition is small, and variable-length allocation provides no useful
per-character saving. `SkillProgress` gains no field or allocation.

Retain `maximumSkillLevel` as the supported schema/storage bound of 10; distinguish
it from each definition's progression ceiling. Monster difficulty ranks and
separate combat-art ranks do not become Cooking-style caps merely because they
also use a ten-level bound.

## 3. Requirements, training and books

Validate limits after resolving the referenced skill, including forward references:

| Reference | Valid range / relationship |
| --- | --- |
| Crafting/recycling skill requirement | `1..skill.maxLevel` |
| Autolearn threshold | `0..skill.maxLevel`; retains its relationship to recipe gates |
| Training source level | `0..skill.maxLevel`; retains matching-gate/introductory-only rules |
| Book teaching range | Inclusive next levels `1..skill.maxLevel`, ordered |
| Combat/manual prerequisite referring to a skill | Existing lower bound through that skill's maximum |

Reject out-of-range references rather than silently lowering them. A Cooking-6
recipe or book benefit is an authoring error. Diagnose the referring field and
the skill's maximum. Apply the same checks to generated and explicit recyclers,
inherited default training, combat unlock requirements and art-manual skill gates.

Training at source level `r` teaches through:

```text
min(r + 1, skill.maxLevel)
```

A recipe requiring Cooking 5 remains valid and useful, although its default
Cooking-5 training cannot advance an already qualified cook. Other skills on that
recipe can still train, and normal recipe memorization can still progress.
Reaching a skill cap does not disable the product or its reference.

Existing `xp_per_work`, book direct-XP limits, boosts, reference access and
once-per-character book identity rules remain the common learning model:

- Ordinary noncombat book grants share the existing 30% current-level allowance.
- The strongest ordinary boost wins, up to 4x, and expires at level-up.
- Combat keeps ordinary XP and its existing book restrictions.
- Books for advanced Chemistry provide theory; crafting provides practice and
  disassembly provides theory.
- A book with another eligible skill benefit remains studyable when Cooking is
  capped. Apply only the eligible benefits.

Author food preparation/preservation against Cooking, and chemical processing,
reagents and material production against Chemistry. Use both only when a specific
operation genuinely needs both. There is no permanent alias, shared XP pool or
automatic cross-training between them.

## 4. Runtime ceiling behavior

Use the definition's maximum in `TrainSkill`, book eligibility/completion,
`SkillXpRequired`, state validation and `LearningSystem::SetLevel`.
The world-facing `Train` and `SetLevel` APIs reject levels above the referenced
skill's maximum with their existing typed invalid-request result.
Pure internal helpers continue to fail fast on broken invariants.

On reaching the learned maximum, the canonical progress record is:

```text
level             = skill.maxLevel
xp                = 0
theory            = 0
practice          = 0
directBookXp      = 0
multiplier        = ratioScale
workRemainder     = 0
boostRemainder    = 0
```

Further awards produce no advancement. Discard capped surplus and fractional
credit under existing `SkillAward` accounting; do not store XP for a future cap
increase or transfer it to another skill. `SkillXpRequired` returns zero at the
maximum. Requirements still use learned levels.

Check the maximum before applying advanced-learning logic. In particular:

- Cooking 4 -> 5 uses ordinary XP and then clears current-level accounting.
- Cooking 5 has no theory/practice reserves and no next-level study benefit.
- Chemistry 4 -> 5 enters the existing advanced band, with ordinary surplus
  discarded.
- A capped noncombat test skill at level 6 can use paired theory/practice for
  5 -> 6, then clears reserves at its maximum.

`EffectiveSkillLevel` keeps its current modifier semantics: clamp at zero, but
permit effective values above the learned maximum. This is performance state,
not learned advancement, and cannot satisfy a higher learned-skill gate.
For example, Cooking learned 5 with a +2 modifier can display effective 7 while
remaining learned 5/5.

### Study already in progress

Preserve paid study work, timing, progress and the book's `used` flag across load
or a cap-related loss of eligibility. Check eligibility before the next unpaid
slice, as today.

If a final paid slice completes a book while it has no eligible benefits, preserve
full completed work with `used:false`; do not award capped XP or invent a benefit.
The current study API deliberately supports this state after administrative level
changes. A later eligible study request can apply its benefit without repeating
the completed work. At a permanent cap it remains a completed reference without
an available learning benefit.

Shared views must represent that state coherently. The current content-aware LAN
validator rejects a full-work unused book even though the runtime permits it;
include that narrow consistency correction in this pass. This does not make a
reference-only or otherwise ineligible book newly studyable.

## 5. Legacy combined-skill migration

### Conversion table

Let the validated legacy progress be `P`, with learned level `L`.

| Legacy state | New Chemistry | New Cooking |
| --- | --- | --- |
| No record | No record; implied level 0 | No record; implied level 0 |
| `L < 5` | Copy all accounting from `P`, substituting skill ID | Copy all accounting from `P`, substituting skill ID |
| `5 <= L <= 10` | Copy all accounting from `P`, substituting skill ID | Level 5 with the canonical capped accounting from section 4 |

“All accounting” includes current-level XP, theory, practice, direct-book XP,
multiplier and both fractional remainders. Ordinary legacy records below 5 already
have zero theory/practice. Chemistry inherits advanced reserves exactly, including
whether the unmatched reserve is theory or practice.

Examples:

- Legacy level 3 with 250 current-level XP becomes Cooking 3 with 250 XP and
  Chemistry 3 with 250 XP. Any valid current boost/book allowance also carries
  into each record.
- Legacy level 4 with 1,199 XP becomes level 4 with 1,199 XP in each. Their next
  earned XP is independent; Cooking stops at 5 while Chemistry can later continue.
- Legacy level 7 with 600 XP and 100 theory becomes Chemistry 7 with that exact
  progress, plus capped Cooking 5. No Chemistry progress is spent to grant Cooking.
- Legacy level 10 becomes Chemistry 10 and Cooking 5, both with clean cap state.

This deliberately credits the old broad proficiency to both narrower skills.
It does not halve earned levels, reinterpret Cooking 5 as level 10, or convert
advanced reserves into ordinary Cooking XP.

### Versioned, transactional read

Learning payload version 2 records the changed identity/ceiling semantics.
Version 1 remains readable under the migration rules below; the field layout is
unchanged.

`ReadLearningState` flow:

1. Parse the old payload strictly into temporary state. Detect duplicate stable
   names even if a name will expand into two destination entries.
2. Resolve normal names normally. For a version-1 unresolved
   `cooking_chemistry` entry, recognize the one explicit shipped migration.
3. Validate that source against its **frozen legacy definition**: noncombat,
   maximum 10 and the historical default ten-entry curve. Validate reserves,
   boosts, remainders and canonical level-10 state before clipping anything.
4. Require both destination definitions, with the intended noncombat categories,
   Cooking maximum 5, Chemistry maximum 10 and matching original active curves.
   This bounded migration copies progress; it does not rescale it across arbitrary
   content rebalance.
5. Reject a payload containing the legacy entry together with either new
   destination entry. Never sum or select the larger conflicting record.
6. Expand the record using the table, sort by the new catalog IDs, then validate
   the complete destination state before publishing it.

The historical curve used for validation must be stable even if later default
balance changes. A corrupt legacy level-10 XP/reserve cannot be made acceptable
by clearing it into Cooking 5. Failed conversion leaves existing World state
untouched and supplies a targeted optional diagnostic.

Version-2 reads resolve current skill names and validate against current
definitions. They do not apply the legacy expansion. Normal loading of a catalog
that still explicitly defines the old name remains ordinary name resolution;
the shipped split catalog removes that definition.

`CaptureLearningState` writes the current payload version and the two current
names. Repeated read/capture cycles are idempotent. A fresh character's neutral
state stays sparse; migration does not allocate progress for an absent old record.
The existing neutral `{}` compatibility path stays neutral.

Do not silently clamp an unrelated saved skill that exceeds its new authored
maximum. Such a later content change needs its own deliberate migration policy.
M-Skills only supplies the named combined-skill conversion.

### Archived characters and persistence

Put conversion at the existing learning-state decoding boundary so it applies to
active players, parked players, resident/unloaded actors and archived recipe-credit
recipients. Do not migrate only the currently controlled character.

Read-time normalization is value-based. A rewritten archive captures the current
payload; untouched disk archives may retain version 1 and convert when read.
The save transaction remains authoritative, and a failed read cannot leave one
destination skill installed. No full-world activation or second migration ledger
is needed.

Only the learning envelope changes save version here. This design needs no additional
SQLite column, per-character maximum, world-generation change or new crafting-job
field. The enclosing format remains version 16: its archive validator and writer
already decode learning envelopes and capture normalized values transactionally.

## 6. Books, knowledge and unfinished jobs during the split

The migration changes skill progress, not information identity:

- Preserve recipe-learning points and permanently learned recipes by stable ID.
  Re-evaluate autolearn through the normal learning path using the migrated skill
  levels; migration awards no craft/disassembly completion credit.
- Preserve each book's stable ID, completed work and once-used flag. A previously
  used combined reference does not become two unused books.
- Preserve active study timing and paid work; the next eligibility check uses the
  book's current reviewed benefit definitions.
- Preserve unfinished manual-job inputs, work, contributions and paid resources.
  Already awarded XP is migrated once. Future completed slices award the skill
  training declared by the current recipe.

There are no shipped combined-skill recipe/book references to remap in the
inspected data. If integration finds new ones, assign each operation explicitly
to Cooking, Chemistry or a justified combination, retain its information/recipe
identity, and validate its gates and study range against the new ceilings.
Do not duplicate every old benefit indiscriminately.

A book that genuinely covers both domains can have two distinct skill benefits
under its existing single book ID. Its benefit is still once per character.
Recipe references remain usable after study and after reaching either skill cap.

Changing ingredients, work totals, memorization goals or book study-work totals
has separate saved-state compatibility consequences. Such changes are not needed
to perform this split and belong to their explicit content review.

## 7. Presentation and network contract

Present the learned maximum explicitly, for example:

```text
Cooking: learned 4/5 | effective 4 | XP 900/1200
Cooking: learned 5/5 | effective 5 | maximum reached
Chemistry: learned 5/10 | theory 30, practice 0
```

At maximum, omit a misleading `0/0 XP` meter and advanced-theory prompt.
Recipe rows show the source ceiling `min(r+1, maxLevel)` and explain capped
learning separately from whether the recipe is usable.
Books distinguish used benefit, partial study, eligible completed study, and
completed study with no eligible benefit using existing work/used/eligible data.

The shared catalog already contains skill definitions. Frontends can read the
maximum there; no duplicate maximum field is needed in every `SkillView`.
`requiredXp == 0` represents a capped skill in the existing view shape.

Update both levels of network validation:

1. Structural decoding permits a zero-required-XP entry only with canonical cap
   accounting and a learned level within the global supported range. The actual
   per-skill maximum is not available at this stage.
2. Catalog-aware validation requires `level <= definition.maxLevel`, zero required
   XP exactly at that maximum, and the correct positive threshold below it.
   Apply ordinary/advanced accounting constraints only to noncapped entries.

The previous wire validator assumed every learned level below 10 had positive
required XP. Merely changing server progression would therefore reject valid
Cooking-5 views. Protocol 26 carries the new semantics with unchanged byte layout,
and fingerprints skill maxima and active XP curves. Matching clients use the same cap semantics.
Content-local numeric skill IDs may shift; saves continue using stable names.

## 8. Implementation and acceptance

Implemented touchpoints:

- Skill definition/parsing and all resolved skill-reference validation.
- Pure progression helpers, world learning entry points, study previews and state
  read/capture; preserve the current small public interfaces.
- Shared crafting/learning presentation, content fingerprint and LAN validation.
- `data/skills/core.json5`, loaded by both principal packs.
- Durable skill/content documentation and narrowly affected pack expectations.

Focused verification:

| Area | Required evidence |
| --- | --- |
| Definition | Default 10 unchanged; maxima 1/5/6/10; exact curve lengths and integer bounds; field-order independence; fingerprint differences |
| References | Forward-resolved over-cap recipe, recycler, autolearn, book and combat/manual skill gates rejected |
| Ordinary cap | Large and fractional awards reach Cooking 5 exactly, clear every accounting field and cannot advance farther |
| Advanced cap | Chemistry retains its existing 5+ model; a lower custom advanced ceiling clears reserves on its final level |
| Books | Eligible mixed-skill benefits, no boost/reserve at cap, used-ID preservation, and paid completion becoming ineligible |
| API/UI | SetLevel/Train enforce the referenced cap; effective modifiers remain separate; correct source ceilings and maximum presentation |
| Migration | Missing, ordinary boosted/fractional, advanced theory/practice and maximum legacy states; duplicate/conflicting destinations and corrupt source records |
| Persistence | Stable names under reordered catalogs; read/capture idempotence; parked/unloaded characters; partial and paid study; atomic rejection |
| Network | Cooking-5 view accepted; over-cap and noncanonical capped data rejected; full-work unused book represented; fingerprint/handshake mismatch |
| Packs | Both packs resolve Cooking/Chemistry, omit the combined identity, and preserve unrelated skill/recipe behavior |

Relevant read-only references included `skill_definition.h`, `skill_state.cpp`,
`learning_system.cpp`, content skill/book resolution, shared crafting summaries,
learning wire validators and the progression/save test sources. Implementation
is covered by focused schema, progression, world, persistence, network, UI and pack tests.
Verification: the `win-msvc-debug` build and 183 focused CTest cases passed
(`skills`, `skill-content`, `skill-ui`, `skill-pack`, `combat-content`,
`combat-learning`, `combat-ui`, `crafting-ui`, `codec`, and `content-pack` tags).
This includes archived-credit migration, paid study, unfinished-job resources and
independent future training; the full suite and other presets were not run.

### Accepted decisions

1. **Grandfather legacy combined proficiency into both skills**, capping Cooking
   at 5 and retaining full Chemistry accounting.
2. **Authored `max_level` with exactly that many XP thresholds**, while retaining
   the existing small fixed catalog array.
3. **Preserve the current effective-level and paid-study semantics**, with clear
   cap presentation and matching network validation.

M-Skills is independent of M-Bench and W3.
Food, preservation and chemical-process content can use the separate identities
under their own gameplay contracts.
