# Combat expansion verification

## Build and automated checks

The `win-msvc-sdl` preset built the engine, headless client, terminal client, SDL
client, test executable, and opt-in combat experiment executable. Engine code
remains compiled with exceptions disabled.

```powershell
cmake --build --preset win-msvc-sdl --target cataclysmx_tests cataclysmx_headless cataclysmx_game cataclysmx_sdl_game cataclysmx_combat_experiment
./scripts/test.ps1 -Preset win-msvc-sdl -j 8 --timeout 60
```

The final suite completed in 50.87 seconds: **1,379 passed, four skipped, zero
failed** (1,383 CTest entries). The four skipped checks concern platform-dependent
symlink/native Windows path handling. LAN roundtrips and SDL frontend smoke checks
passed. The report is under
`out/test-results/42b4d24c-07da-47d1-ae8e-4930b2f42e20/`.

Coverage includes weighted selection and restored identities; committed source
and grip loss; one paid defense per strike; mixed damage and shared block budgets;
local garment coverage/encumbrance; counter recovery overlap; stamina locks;
opponent-limited skill/art XP; repeated manual attempts and frozen funded work;
terminal death/corpses/new characters; legacy save migrations; malformed command
and state rejection; and terminal/SDL targeting, equipment, and event history.

Earlier failing runs identified outdated fixed-move/catalog-index fixtures and
integration defects. Those were corrected before the final full run. No automated
tests were added for the experiment tool itself.

## Initial gameplay experiment baseline

```powershell
./out/build/win-msvc-sdl/cataclysmx_combat_experiment.exe --seeds 16 --seed 1 --scenario duel --skill 3 --csv out/combat-duel.csv
./out/build/win-msvc-sdl/cataclysmx_combat_experiment.exe --seeds 16 --seed 1 --scenario crowd --loadout swordshield --skill 3 --csv out/combat-crowd.csv
```

These 80 runs use production rules, the real scheduler, fixed skill levels, and
the runner's declared attack-whenever-ready policy. They do not use learned arts,
manual abilities, movement tactics, or planned rest. All use `basic_balanced`.

| Scenario/loadout | Wins | Deaths | Timeouts | Median ticks |
| --- | ---: | ---: | ---: | ---: |
| Duel, unarmed | 3 | 13 | 0 | 2990 |
| Duel, sword/shield | 5 | 11 | 0 | 2450 |
| Duel, two-handed sword | 12 | 4 | 0 | 1238 |
| Duel, dual swords | 12 | 4 | 0 | 1977 |
| Three enemies, sword/shield | 0 | 16 | 0 | 960 |

Per-run and summary CSVs retain actual injury, defense/block/armor results,
stamina expenditure, and move frequencies. These small seed sets are an initial
tuning baseline, not a balance target or a claim of optimal player performance.
In particular, constant attacks/reactions exhaust the survivor quickly; style
weights, action costs, and ordinary unarmed performance need playtesting.

See [combat experiments](combat_experiments.md) for reproducibility, metrics,
loadouts, action ordering, and command options.
