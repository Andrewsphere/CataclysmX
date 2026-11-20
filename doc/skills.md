# Runtime skills and learning

The character owns sparse, catalog-ID-ordered skill, recipe and book progress.
Missing entries mean level 0 and no earned progress. Ratios and XP use integer
units of 1/10000 (`ratioScale`); work and boost remainders preserve fractional
incremental awards. Content thresholds are whole XP. Default XP to reach each
successive level is 100, 200, 400, 800, 1200, 1800, 2600, 3600, 4800, 6400.
These are defaults to tune, not a guarantee of shipped activities at every tier.

The main and FirstHouse packs ship 18 skills: nine noncombat and nine combat
skills (Melee, Unarmed, Knives, Swords, Clubs & Hammers, Axes, Polearms, Shields
and Dodge). The starter fabrication chain includes ungated
level-0 work toward Fabrication 1, a level-1 coil spear recipe available by
manual or autolearn at level 2, and a level-2 sword recipe. Ungated spear
disassembly also contributes spear recipe-learning credit. Two distinct manuals
reference the spear; a reader carries the same information identity as one print
edition. FirstHouse exposes the manuals through debug spawning. See
[shipped content](skills_content.md#shipped-starter-content) for IDs and details.

## Progression

Learned levels are 0–`SkillDefinition::maxLevel`; requirements use learned levels.
Cooking (`cooking`) stops at 5; Chemistry (`chemistry`) and other shipped skills
stop at 10. Cooking uses the first five thresholds of the shared curve without
rescaling. The fixed ten-slot definition array keeps catalog storage simple;
unused curve slots are zero and characters store no duplicate maximum.
`EffectiveSkillLevel` adds a modifier, clamps at zero and permits values above
the learned maximum. A training source at level `r` teaches only until learned
level `min(r + 1, maxLevel)`. Default training
rate is 100/10000 XP per base work; zero disables a reward. Ordinary work XP
can cross levels within the source ceiling, but surplus cannot become advanced
theory or practice. Fractional work credit at a teaching ceiling, the advanced
band boundary, or a full reserve is discarded along with capped surplus; it cannot
be banked for a later eligible award.

At the learned maximum, XP, theory, practice, direct-book allowance usage and both
fractional remainders are zero; the multiplier is `ratioScale`.
`SkillXpRequired` returns zero there. Further training cannot advance or store
credit. Cooking 4→5 uses ordinary XP and stops; Chemistry 4→5 enters the advanced
band. A capped skill does not prevent using its recipes or memorizing them.

Noncombat levels 0–4 use ordinary XP. At levels 5–9, disassembly and eligible
books supply theory, crafting supplies practice, and one of each converts to one
advancement XP. Each unmatched reserve is capped at the current level's remaining
XP; level-up clears reserves and surplus does not carry to the next level. Combat
skills use ordinary XP through level 10. `SkillAward` reports advancement, capped
amount and level gains.

For ordinary noncombat XP, direct book grants share a 30% cap per level. The
strongest book multiplier wins, capped at 4x, and resets on level-up. Combat
books give no direct or theory XP and at most 1.2x. Advanced books give theory
instead of a multiplier. Eligibility is based on the next level and each book's
inclusive teaching range. A completed book benefit is used once per character
and book ID; it is not consumed as an item and remains a recipe reference.

### Combat training

An attack's authored work is divided over planned strike opportunities, including
Sweep tiles, then split between Melee and the actual handling family; identical
skill IDs merge. Defensive work trains its applicable skill. Damage packet count
does not multiply work. Difficulty is evaluated separately for each trained skill
and art against the opponent's relevant rank: two levels below yields one tenth
training, three or more below yields none. The first fully suppressed award per
encounter is logged; the notice expires after 30 active seconds without interaction. Authored
monster ranks supply difficulty without granting monsters learned-skill XP.
Performance uses effective levels; unlocks use learned levels. See
[runtime training](combat_runtime.md#stamina-and-training-policy).

## Study and references

`World::Learning().Study(actor, book)` starts or resumes timed study and returns
a typed outcome; `PauseStudy` requests a pause after the current paid slice.
Slices last at most 100 ticks, with speed-dependent work and a shortened final
slice. Completed work persists across pauses, loss of access and saves. Access and
eligibility are checked before funding each slice. Equivalent carriers of the same
book ID can resume it; another character has independent progress. A book with no
eligible skill benefit is not studyable merely because it lists recipes.
Mixed-skill books apply only eligible benefits; a capped Cooking benefit does not
block an eligible Chemistry benefit. If the final already-paid slice completes
after all benefits become ineligible, full work persists with `used:false`.
A later eligible request applies the benefit without paying the work again.

References include accessible inventory trees, reachable nearby ground and
storage, using the crafting radius and reach rules. Crafting job reference checks
use the job's snapshotted radius; resume snapshots the current radius. Study and
general reference queries use the current crafting radius. Reserved, installed and
unfinished-craft contents are excluded. An item with `reading_power` must have
positive charge in its own named store for study or reference; reading currently
does not drain it. Study grants skill benefits, never recipe memorization.

## Recipes and credited work

Crafting requires learned skill gates and either permanent knowledge or a
currently accessible book listing the recipe. Disassembly uses its own skill
gates and needs no recipe reference. A recipe can be known by default, explicitly
autolearned when its recipe and autolearn skill thresholds are met, or memorized
through completed crafting/disassembly. Autolearned knowledge is permanent.

The default goal combines two crafts and three disassemblies: each complete craft
contributes one half and each complete disassembly one third, so mixed work also
counts. At job completion, credit is proportional to completed work per worker;
integer rounding residue is allocated in persistent-ID order. Legacy unattributed
work retains its own share. Archived contributors receive their credits through
`World::LearnRecipe`. Skill XP is awarded per completed slice, including work on
later-cancelled jobs; cancelled jobs do not award completion memorization.

## APIs, state and presentation

`skills/skill_state.h` exposes progression, requirement, book and recipe queries,
training, study completion and state capture/read. `World::Learning()` exposes
world-aware study, pause, reference checks, `Train`, administrative `SetLevel`,
and `CollectView`; `World::LearnRecipe` credits persistent actor identities.
`SetLevel` resets current-level accounting. `SetLevel` and `Train` reject levels
above the referenced definition's maximum with `WorldError::invalid_request`;
pure progression helpers fail fast on invalid internal requests.
APIs use typed IDs and world results;
the view supplies skill progress, recipe knowledge/reference and book status.

Sparse character vectors avoid per-entry allocations for untouched actors.
Definition relationships use overflow-capable `SmallVector<..., 1>` for common
small lists; book recipe lists use bulk `Vector`, as do job contributions, avoiding
large inline per-owner costs and allowing growth. Stable content names are saved.
Save format remains 16; the learning envelope alone advances to version 2.
Missing learning state `{}` remains neutral. Version-1 unresolved
`cooking_chemistry` progress is validated against the frozen historical ten-level
noncombat curve, then copied to Chemistry and Cooking. Below level 5 both retain
all accounting; at level 5 or above Cooking becomes a clean capped level-5 record.
Destinations must have the original active curves, noncombat categories and maxima
5/10. Duplicate/conflicting destination names and corrupt source records fail
transactionally. An explicitly defined old name still resolves normally; version 2
never expands it. Unrelated over-cap saved skills are rejected.

Migration preserves book identities, used flags, paid study, recipe knowledge and
unfinished jobs; autolearn is re-evaluated normally. The shared read boundary covers
active, parked, resident/unloaded and archived-credit recipients. Rewritten archives
capture version 2; untouched archives may migrate when read. Missing old progress
stays sparse. See the [migration contract](skill_caps_and_split_design.md).
Crafting job payload version 2 records contributions; version 1 work migrates as
unattributed, without inventing worker credit. Network protocol 26 transports
cap-aware learning state with matching maxima/active-curve fingerprints.

The crafting panel presents learned level/maximum separately from effective level,
omits XP meters and advanced prompts at the maximum, and presents recipe knowledge, teaching limits, available
books and study/pause controls. See [crafting UI](crafting_ui.md) and
[commands](crafting_commands.md). Content schema and tuning are in
[skills content](skills_content.md). Learned arts, their XP, grants and repeatable
manual attempts are described in [combat learning](combat_learning.md).
The local headless script supports `skills`, `study BOOK_ID`, and `pause-study`;
book IDs are shown in the shared crafting menu.
