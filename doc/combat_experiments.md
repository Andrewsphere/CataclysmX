# Headless combat experiments

`tools/combat_experiment.cpp` compares actual production combat under the real
event scheduler. It loads `CATACLYSMX_CONTENT_PACK`, creates a fresh `World` for
each seed/loadout/skill combination, and calls `AdvanceUntilInput`. Zombie turns
are executed by `World::ProcessNpcActions` through the selected production controller.
Move selection, perception, reactions, injury, equipment, stamina, action deadlines,
death, and corpse creation all remain engine behavior.

## Build and run

The coordinator-owned CMake target is `cataclysmx_combat_experiment`,
`EXCLUDE_FROM_ALL`, with source `tools/combat_experiment.cpp`, linked to
`cataclysmx_engine`, and with `CATACLYSMX_CONTENT_PACK` defined as the absolute
path to `data/content.json5`. It requires the runtime
`CombatSystem::SetTrainingEnabled(bool)` setting; each experiment disables
training before combat. The setting defaults to enabled for normal worlds.
The headless executable additionally uses `client/combat_menu.cpp` in the client
library.

From a configured Visual Studio developer shell, build and run a focused comparison:

```powershell
cmake --build --preset win-msvc-sdl --target cataclysmx_combat_experiment cataclysmx_headless
./out/build/win-msvc-sdl/cataclysmx_combat_experiment.exe --enemy zombie_shopper --loadout hammer --skill 0 --csv out/shopper.csv
```

`--enemy ID` selects a catalog monster; `--loadout` accepts `all`, `unarmed`,
`swordshield`, `twohand`, `dual`, `hammer`, `knife`, `sword`, or `axe`.
`--outfit` accepts `none`, `standard` (the default), or `work`. `--style ID`
selects a catalog style and `--art-xp N` sets raw catalog XP for its owning art.
Use `--policy attack` (default) to attack whenever possible, or `--policy cautious`
to recover at 25 stamina or below until 45 and flee when head or torso HP reaches
40% or less. Cautious runs use the arena's four open edge exits; escape is counted
after reaching an opening and surviving until ready again. Both policies use the
same perimeter. `--csv-format plain` writes comma-delimited CSV; `excel` (the
default for files) writes UTF-8 BOM, `sep=;`, and semicolon-delimited CSV. Standard
output remains comma-delimited.

CSV parent directories must already exist. The runner writes per-run data at the
supplied path and aggregate data at `<path>.summary.csv`. Standard output is also
aggregate CSV; standard error lists move commitment/invalidation counts by stable
move ID. Failures return 1; malformed arguments return 2. `--help` lists options.

## Repeatable power rankings

Use [item balance anchors](item_balance.md) to choose design peers, progression
tradeoffs and relevant comparisons for new items.

Build the runner from current sources before measuring. A Release build is useful
for larger sweeps; the runner itself does not require SDL:

```powershell
cmake --build --preset win-msvc-release --target cataclysmx_combat_experiment
python tools/balance_rankings.py out/build/win-msvc-release/cataclysmx_combat_experiment.exe --output out/balance --seeds 96 --jobs 4
```

Open `out/balance/index.html` for ranked tables. The Python driver needs only the
standard library (Python 3.9+). Choose an empty output directory under `/out` for
each run. Generated HTML, CSV, JSON and optional spreadsheets stay local; keep
methodology in this document rather than checking in snapshots or run diaries.
Legacy files under `doc/balance` are ignored local snapshots.

The driver discovers enemies, authored melee weapon/grip variants, garments and
styles through `--list-catalog`, using the loaded production catalog rather than
parsing JSON5 independently. It produces these separate reference panels:

- **Enemy threat:** player death percentage, equally weighted against novice
  hammer, skill-3 hammer and skill-3 two-handed sword.
- **Weapons:** skill 3, Balanced, gloves and boots; each supported authored grip
  plus unarmed, sword/shield and dual-sword comparisons.
- **Armor:** individual garments and the named outfits, both in sword duels and
  passive 1000-tick exposure. Passive rankings use mean actual HP lost (lower is
  better); duel rankings include the net effects of encumbrance and protection.
- **Styles:** matched unarmed and sword/shield panels, with fresh and 50000-XP
  versions of the compatible learned arts. New arts need an explicit compatible
  panel in `cases`; the current two are boxing and weapon forms.
- **Progression/policy:** skills 0/3/6/10 and separate attack/cautious comparisons
  for duels, crowds and attrition.

Equipment, style and progression panels weight shopper, cellar zombie and police
equally. This is a reference mix, not their world spawn distribution. Every cell
uses the same seed interval. `--seed` changes its start; `--groups weapons
styles_swordshield` selects a focused sweep. `--jobs` changes subprocess
parallelism, not simulation ordering. Identical cells shared by panels run once.
The ranking driver defaults to `--enemy-ai recipe`, retaining normal pursuit AI.
Use `--enemy-ai melee` only for the older in-reach/wander comparison. Obsolete
catalog items remain visible but are labeled; their rank does not imply loot access.

Rankings report outcomes, injury, armor absorption and **win-only** elapsed ticks.
Escapes/timeouts are not wins. Descriptive 95% bootstrap intervals resample seeds
as clusters across reference matchups, avoiding the assumption that repeated uses
of a seed are independent. Close scores need follow-up, and zero observed successes
or failures do not prove a zero probability. Passive exposure is useful only while
subjects survive the observation window; deaths are explicitly reported. Compare
paired per-seed differences before tuning small armor effects.

`matrix.csv` retains every panel matchup and its raw-file name. Raw fights,
move-frequency text, `rankings.json` and `manifest.json` retain options, exact
commands, seeds and SHA-256 fingerprints of the executable, driver and current
source/data tree. Rebuild before running: source hashes alone cannot prove the
executable was built from those sources. The driver rejects source/data changes
during a sweep. These are diagnostic rankings, not an automatic balancing rule:
adjacent starts, fixed policies, no abilities and uniform skills omit reach tactics,
loot/crafting access, carry costs, utility, healing and exploration.

For an individual catalog item, use `--weapon ITEM --grip one_handed|two_handed`
instead of `--loadout`; add `--offhand ITEM` only with a one-handed primary.
Repeat `--wear ITEM` to add garments after the selected outfit. Use `--outfit none`
for isolated armor. Custom weapon/grip/offhand/garment IDs are included in CSV.
`--list-catalog` lists only authored melee profiles; unlisted items may still be
held using the production improvised fallback. Firearms here are melee weapons.

The smaller design-reference sweep remains available as
`python tools/balance_baseline.py <runner>` (default `out/balance-baseline.csv`).
Its optional `--workbook out/balance-baseline.xlsx` requires XlsxWriter and exports
only the newly measured baseline, with no dependency on historical snapshots.
It also defaults to recipe AI; use `--enemy-ai melee` to compare with old snapshots.

## Controlled setup and policy

The arena is one active, full-height chunk column. Generated actors and furniture
are removed at setup, the surface is leveled to floor with a perimeter containing
four open edge exits, and daylight is fixed. Generated loose items are irrelevant
to the combat policy and are never picked up. The survivor starts at `(8,8,0)` and the first enemy at
`(9,8,0)`. Enemies use the selected production monster recipe (default `cellar_zombie`), including its
authored body, ratings, training ranks, and stamina policy. The standalone runner
defaults to `--enemy-ai melee` for compatibility with earlier experiments. This
controller attacks in reach and otherwise **wanders**; it does not pursue the
survivor. `--enemy-ai recipe` retains the monster's authored controller, including
normal sighted pursuit, and is the ranking driver's default. Keep AI choice fixed
within a comparison. Knockback and retreat results are especially sensitive to it.

Default comparisons use **all combat skills at levels 0, 3, 6, and 10**.
`--skill N` selects any single level from 0 through 10. Skills are initialized
once and both skill/art training are disabled throughout combat, including
reactions and timed strikes. No repeated XP resets or modified catalog formulas
are used. Selecting a learned style grants its owning art at the requested XP before the fight.

`--loadout all` runs the original four loadouts; named loadouts can be run
individually. The default outfit equips gloves and boots; `work` adds a T-shirt,
jeans, and rain jacket, while `none` omits clothing. The default style is
`basic_balanced`; styles and owned-art XP come from the loaded content catalog.

Scenarios remain selectable with `--scenario duel|crowd|passive|attrition`;
`--max-ticks` sets the run limit. `--policy` selects attack or cautious behavior
(passive always waits).

Under the default attack policy, the survivor uses production `FindTarget` and `Attack`.
If no attack commits, it waits one normal turn. It never deliberately moves,
changes stance, uses abilities, heals, or optimizes a move selection. NPCs move and
attack according to their controller. This is a fixed-policy experiment rather
than an optimal-play claim.

Scheduler-selected event boundaries are also observation boundaries, so terminal
deaths can be noticed after the survivor stops supplying input. Runs end when the
survivor dies, escapes, all scenario opponents die, or the tick limit is reached (default
30000). Death is checked through `World::IsActorAlive`, including destroyed NPC
handles; it is not inferred from total body HP. Production death finalization and
world invariants are checked before reporting.

`--seed N --seeds M` uses consecutive uint32 seeds without wrapping. World seed
and explicit combat RNG start from the same run seed. Each comparison starts a
fresh world; RNG is never reused across runs. Reproducibility requires the same
engine/content version, options, and seed. Matching seeds give paired input
conditions, but different repertoires legitimately consume different RNG draws.

`--enemy-first` creates the initial opponents before the survivor to vary stable
identity ordering. `--player-delay N` and `--enemy-delay N` fund initial actions
using production scheduling costs (base ticks, converted by actor speed); zero
means immediately ready. The production player-input barrier still precedes NPC
decisions at a shared ready tick. Use a nonzero player delay when investigating an
enemy opening turn rather than assuming creation order bypasses that barrier.

## Metrics and interpretation

All action/strike accounting comes from `CombatSystem::DrainEvents`, with setup
events excluded. `out_*` means strikes/actions by the survivor; `in_*` means
strikes/actions by opponents. A defense belongs to the corresponding incoming
strike: `in_blocks` measures successful survivor blocks, while `out_blocks`
measures blocks against survivor strikes.

Per-run CSV includes:

* Setup identities/options, outcome (`win`, `loss`, `escape`, `timeout`), elapsed
  ticks, opponents killed, final survivor stamina, and policy movement/rest counts.
* Strike counts split into invalidated, intrinsic misses, dodges, parries, and
  contacts, plus successful blocks, armor stops, and counter strikes.
* Actual HP loss, maximum-HP loss, block absorption, and armor absorption in
  hundredths of HP. HP loss excludes overkill and is distinct from raw damage.
* Committed actions, invalidated actions, stamina spent, committed windups,
  invalidated windup actions, and exhausted-only fallback commitments.

Windup classification reads the committed move's production definition.
`wasted_windups` counts `ActionInvalidated` events for moves with a nonzero windup;
it does not infer misses or every possible interruption as a wasted windup.
Stamina spent sums event charges, including defenses and counters, rather than
subtracting final stamina from initial stamina (which would hide regeneration).
Final stamina is available on the retained player actor even after death.

Summary rows group by loadout and skill within the chosen scenario. They include
separate win/loss/escape/timeout counts and rates; nearest-rank median/p90 ticks, actual survivor
HP lost, HP dealt, stamina spent, and incoming block/armor absorption; pooled
outgoing strike outcome rates and incoming defense rates; and invalidation,
wasted-windup, and exhausted-action frequencies. Pooled rates divide total event
counts, rather than averaging noisy per-run ratios. Empty denominators report
zero. Medians/p90 include **all outcomes**, including timeouts; elapsed ticks are
not a conditional time-to-kill statistic. Separate `win_hp_lost` and
`win_max_hp_lost` metrics summarize injury among wins; blank values mean no wins.

## Headless combat commands

Local commands run via:

```powershell
@('combat', 'styles', 'style basic_defensive', 'abilities', 'quit') |
  ./out/build/win-msvc-sdl/cataclysmx_headless.exe --script --scenario demo
```

The same typed combat commands can be supplied with repeated LAN `--command`
arguments. `combat`, `styles`, and `abilities` inspect the current local/received
snapshot; they do not submit an action or advance simulation.

| Command | Meaning |
| --- | --- |
| `combat` | Shared combat detail text and menu entries, including costs/refusals |
| `styles` | List styles (local output also includes stable IDs/known status) |
| `style ID` | Select a content style by stable string ID or catalog-local numeric ID |
| `style balanced\|aggressive\|defensive` | Existing baseline stance aliases |
| `hold ITEM_ID left\|right [one\|two]` | Hold a persistent item, default one-handed |
| `releasehand left\|right` | Release the selected hand |
| `regrip left\|right one\|two` | Change the held item's grip |
| `wear ITEM_ID`, `remove ITEM_ID` | Wear/remove a carried garment |
| `abilities` | List granted abilities with cooldown/availability |
| `use ability ID [self]` | Use a granted self-targeting ability |
| `use ability ID tile X Z` | Target an absolute tile on the character's current layer |
| `use ability ID north\|east\|south\|west` | Target a cardinal direction |
| `study-art BOOK ART` | Study an art manual by book information ID and art ID |
| `pause-art` | Pause art-manual study |
| `new-character` | Request a fresh character for the existing stable player session |

Book/art/ability IDs accept stable content names or catalog-local numeric values.
`use focus self` is also accepted for stable ability names. The explicit
`use ability ID` form avoids ambiguity with existing `use ITEM_ID CAPABILITY_ID ACTION` syntax.
An ability grant is selected from the current character snapshot, ordered by source
kind and stable art ID, preserving the actual grant key and its cooldown identity.
LAN binding occurs when the command is submitted, so earlier commands may change
the current character, grant list, or layer. No CLI command grants an unknown art
or ability. Ordinary gameplay refusals remain authoritative server responses.

`new-character` is a typed session command, not a mutation through a cached actor
handle. CLI syntax errors fail the script/request; valid equipment, style,
ability, and study requests use the same `Command` pipeline as other frontends.

This runner is tooling: no tooling tests are added or run. Build/link verification
and integration prerequisites should be reported separately from experiment data.
