# Runtime skills and learning

The character owns sparse, catalog-ID-ordered skill, recipe and book progress.
Missing entries mean level 0 and no earned progress. Ratios and XP use integer
units of 1/10000 (`ratioScale`); work and boost remainders preserve fractional
incremental awards. Content thresholds are whole XP. Default XP to reach each
successive level is 100, 200, 400, 800, 1200, 1800, 2600, 3600, 4800, 6400.
These are defaults to tune, not a guarantee of shipped activities at every tier.

The main and FirstHouse packs ship eight noncombat skills and the basic Melee,
Unarmed and Dodge combat skills. The starter fabrication chain includes ungated
level-0 work toward Fabrication 1, a level-1 coil spear recipe available by
manual or autolearn at level 2, and a level-2 sword recipe. Ungated spear
disassembly also contributes spear recipe-learning credit. Two distinct manuals
reference the spear; a reader carries the same information identity as one print
edition. FirstHouse exposes the manuals through debug spawning. See
[shipped content](skills_content.md#shipped-starter-content) for IDs and details.

## Progression

Learned levels are 0–10; requirements use learned levels. `EffectiveSkillLevel`
adds a modifier, clamps at zero and permits values above 10. A training source at
level `r` teaches only until learned level `min(r + 1, 10)`. Default training
rate is 100/10000 XP per base work; zero disables a reward. Ordinary work XP
can cross levels within the source ceiling, but surplus cannot become advanced
theory or practice. Fractional work credit at a teaching ceiling, the advanced
band boundary, or a full reserve is discarded along with capped surplus; it cannot
be banked for a later eligible award.

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

Each valid, in-reach hostile `Strike` attempt grants the attacker one whole Melee
XP, plus one Unarmed XP when no weapon is wielded. The defender gains one Dodge XP
on a miss or a non-parried strike, or one Melee XP on an actual parry. Normal
training ceilings and multipliers apply. Raw damage, individual damage packets,
and cancelled or unreachable windups do not independently grant credit. Combat
skill performance scaling has not yet been authored.

## Study and references

`World::Learning().Study(actor, book)` starts or resumes timed study and returns
a typed outcome; `PauseStudy` requests a pause after the current paid slice.
Slices last at most 100 ticks, with speed-dependent work and a shortened final
slice. Completed work persists across pauses, loss of access and saves. Access and
eligibility are checked before funding each slice. Equivalent carriers of the same
book ID can resume it; another character has independent progress. A book with no
eligible skill benefit is not studyable merely because it lists recipes.

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
`SetLevel` resets current-level accounting. APIs use typed IDs and world results;
the view supplies skill progress, recipe knowledge/reference and book status.

Sparse character vectors avoid per-entry allocations for untouched actors.
Definition relationships use overflow-capable `SmallVector<..., 1>` for common
small lists; book recipe lists use bulk `Vector`, as do job contributions, avoiding
large inline per-owner costs and allowing growth. Stable content names are saved.
Save format 13 migrates legacy characters to neutral learning state; missing
learning state `{}` is also neutral. Learning payload version 1 validates its
contents. Crafting job payload version 2 records contributions; version 1 work
migrates as unattributed, without inventing worker credit. Network protocol 20
transports learning state and study commands with matching content fingerprints.

The crafting panel presents skills, recipe knowledge, teaching limits, available
books and study/pause controls. See [crafting UI](crafting_ui.md) and
[commands](crafting_commands.md). Content schema and tuning are in
[skills content](skills_content.md); martial-art progression is not implemented.
The local headless script supports `skills`, `study BOOK_ID`, and `pause-study`;
book IDs are shown in the shared crafting menu.
