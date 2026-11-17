# Headless combat experiments

`tools/combat_experiment.cpp` compares actual production combat under the real
event scheduler. It loads `CATACLYSMX_CONTENT_PACK`, creates a fresh `World` for
each seed/loadout/skill combination, and calls `AdvanceUntilInput`. Zombie turns
are executed by `World::ProcessNpcActions` through the production melee controller.
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

From a configured Visual Studio developer shell:

```powershell
cmake --build --preset win-msvc-sdl --target cataclysmx_combat_experiment cataclysmx_headless
./out/build/win-msvc-sdl/cataclysmx_combat_experiment.exe --seeds 100 --seed 1 --scenario duel --csv out/duel.csv
./out/build/win-msvc-sdl/cataclysmx_combat_experiment.exe --seeds 200 --seed 1 --scenario crowd --loadout swordshield --skill 3 --csv out/crowd.csv
./out/build/win-msvc-sdl/cataclysmx_combat_experiment.exe --seeds 100 --scenario passive --style basic_defensive
./out/build/win-msvc-sdl/cataclysmx_combat_experiment.exe --seeds 100 --scenario attrition --max-ticks 60000
```

CSV parent directories must already exist. The runner writes the per-run file at
the supplied path and the aggregate file at `<path>.summary.csv`. Standard output
is also aggregate CSV; standard error lists move commitment/invalidation counts
by stable move ID for each loadout/skill group. Failures return 1; malformed
arguments return 2. `--help` lists the complete option set.

## Controlled setup and policy

The arena is one active, full-height chunk column. Generated actors and furniture
are removed at setup, the surface is leveled to floor with a wall perimeter, and
daylight is fixed. Generated loose items are irrelevant to the combat policy and
are never picked up. The survivor starts at `(8,8,0)` and the first zombie at
`(9,8,0)`. Zombies use the production `cellar_zombie` recipe, including its
authored body, ratings, training ranks, and stamina policy. The controller is
explicitly set to `Melee` to isolate close-range combat from sighted search AI.

Default comparisons use **all combat skills at levels 0, 3, 6, and 10**.
`--skill N` selects any single level from 0 through 10. Skills are initialized
once and both skill/art training are disabled throughout combat, including
reactions and timed strikes. No repeated XP resets or modified catalog formulas
are used. No extra combat arts are granted; the selected baseline style uses its
production repertoire.

Loadouts (`--loadout`, default `all`):

| Name | Held equipment |
| --- | --- |
| `unarmed` | Empty hands |
| `swordshield` | Left one-handed `arming_sword`, right `wooden_shield` |
| `twohand` | The same `arming_sword`, two-handed grip |
| `dual` | Two one-handed `arming_sword` instances |

Every loadout wears `leather_work_gloves` and `work_boots`, so production armor
coverage and encumbrance are exercised without changing clothing between groups.
The default stance is `basic_balanced`; `--style basic_aggressive` and
`--style basic_defensive` select the other baseline stances. Setup equipment and
style changes do not contribute combat costs.

Scenarios:

* `duel`: one zombie; survivor attacks whenever ready.
* `crowd`: three simultaneous zombies, initially east, south, and west.
* `passive`: one zombie; survivor only waits, retaining automatic reactions and
  any engine-triggered counterattacks.
* `attrition`: five zombies sequentially, spawning each successor on the first
  free adjacent horizontal tile in production step order. Survivor HP, stamina,
  injuries, recovery, and cooldowns carry over without healing/reset or a rest gap.

At a ready input barrier, the survivor uses production `FindTarget` and `Attack`.
If no attack commits, it waits one normal turn. It never deliberately moves,
changes stance, uses abilities, heals, or optimizes a move selection. NPCs move and
attack according to their controller. This is a fixed-policy experiment rather
than an optimal-play claim.

Scheduler-selected event boundaries are also observation boundaries, so terminal
deaths can be noticed after the survivor stops supplying input. Runs end when the
survivor dies, all scenario opponents die, or the tick limit is reached (default
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

* Setup identities/options, outcome (`win`, `loss`, `timeout`), elapsed ticks,
  opponents killed, final survivor stamina, and policy waits.
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
win/loss/timeout counts and rates; nearest-rank median/p90 ticks, actual survivor
HP lost, HP dealt, stamina spent, and incoming block/armor absorption; pooled
outgoing strike outcome rates and incoming defense rates; and invalidation,
wasted-windup, and exhausted-action frequencies. Pooled rates divide total event
counts, rather than averaging noisy per-run ratios. Empty denominators report
zero. Medians/p90 include **all outcomes**, including timeouts; elapsed ticks are
not a conditional time-to-kill statistic. A passive survivor still alive at the
limit is a timeout, not an inferred combat victory. Preserve the per-run CSV when
computing survival curves or outcome-conditional statistics.

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
