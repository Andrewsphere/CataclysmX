# Pass 5: workshop acquisition and progression measurements

**Status: Implemented and measured; focused integration checks passed.**
The measurement scope is complete. Whole-loop pacing and expedition acceptance
remain open.

This pass completes the bounded basic-tool integration and measures the combined
D5 workshop, renewable-power, home-printing and generation-8 city package. It uses
[the shared plan](urban_workshop_progression.md), [delivery dependencies](crafting_delivery_plan.md),
and the existing workshop, power, renewable and digital contracts.

## Accepted boundary

The user chose world generation and static progression/resource measurements
instead of agent playthroughs. Evidence covers actual generated stock, complementary
finds within the same sampled world/settlement, legal structural approaches, travel
distance indicators, transport costs and initial population. These measurements
do not establish combat survival or comfortable player pacing.

The coordinator may tune relevant loot, quantities, recipe work/material budgets
and authored learning from demonstrated bottlenecks. Preserve accepted skill
anchors and multiple acquisition routes; progression-loot/site guarantees remain
deferred. Use fresh worlds. Old-save compatibility and migrations are unnecessary;
current-format round trips and resource conservation still require verification.

## Bounded content roster

| Identity | Role and ownership | Budget and acquisition |
| --- | --- | --- |
| New `makeshift_screwdriver` | One short abraded steel strip with a folded grip; ordinary `screwdriving 1`; `items/tools.json5` | 90 g / 100 ml; crafted from loose stock |
| New `make_makeshift_screwdriver` | `crafting/tools.json5`; default-known, ungated, Fabrication-0 practice | One 100 g `steel_scrap`, 1,000 base work, one tool; no other prerequisites |
| Existing screw-operated recyclers | Replace retained `flathead_screwdriver` with `screwdriving 1` | Preserve other tools, returns and work |

Hand shaping/abrasion is implicit. The fallback is lighter than the 120 g factory
tool because it has no molded handle. Both provide the same ordinary capability;
neither has an invented precision/speed advantage. The 10 g loss represents
abrasion. No recycler rounds a 90 g tool up to the 100 g scrap unit. No additional
loot draw is needed: the fallback consumes existing loose steel. Descriptions are
plain; atmospheric and humorous exceptions: none. Canonical text art belongs at
`data/art/items/makeshift_screwdriver/art.txt`; map presentation uses the existing
fallback pending external artwork.

## Measurement contract

Use seeds **0–31 and 42**, retaining every result. Include the existing four-region
city comparison panel and all nine size/era examples. Also include actual Town
starting settlements and the origin planning patch's settlements, explicitly
labeling sample membership. Deduplicate by seed and full settlement/feature
identity; a world total is always the union of these sampled sites, not the entire
infinite world.

- Generate real chunk-owned loot/furniture with the normal immutable catalog and
  generation APIs. Record selected basement, rotation and all authored floors.
- Measure each site's structural access from its entrance, including matching
  stairs, supported floors, openable doors and blocking furniture. Report blocked
  opportunities rather than silently discarding them.
- Distances from the actual Town start are geometric indicators, not traversed
  paths or survival estimates. Record interior and nearby exterior population
  separately and account for Town's initial same-floor safety circle.
- Retain site-level identities and stock before aggregating. Report distributions,
  missing capabilities and complementary parts per settlement/seed. Keep found
  equipment, recoverable portable equipment and station capabilities explicit.
- Calculate named gross progression schedules and study alternatives from actual
  recipe/book/skill budgets. Expose assumptions and teaching ceilings; do not claim
  an optimized minimum-resource solution.
- Compare older and newer specialist destinations using functional workshop
  equipment, materials, references and printing feedstock.

CSV/JSON exports and renderer previews stay under `out/pass5/`. The measurement
executable and report command are tools, with no tooling tests. Changed content
and gameplay receive focused regression checks. Durable results, commands,
limitations and any tuning rationale belong here and in the
[progression audit](workshop_progression_audit.md).

## Completion

Completion requires an operational bootstrap fallback, compatible tool selection,
measured acquisition distributions, a connected prerequisite/resource audit and
updated usage/status documentation. Human pacing and expedition playtesting remain
the follow-up gate for judging the whole loop before expanding into hacking.

## Reproduce the measurements

From the repository root in PowerShell:

```powershell
./scripts/build.ps1 -Preset win-msvc-release -SkipTests
cmake --build --preset win-msvc-release --target cataclysmx_workshop_probe
./out/build/win-msvc-release/cataclysmx_workshop_probe.exe out/pass5/current
python tools/workshop_report.py out/pass5/current
```

Run the two build commands in the same shell: `build.ps1` initializes the MSVC
environment. The opt-in probe is excluded from the default build. Append a seed,
for example `42`, to measure only that world. Omitting it selects 0–31 and 42.
Python uses only its standard library. Run output stays local under `/out`.

`tools/workshop_probe.cpp` generates immutable plans, chunks and placements; it
never creates a `Server` or advances simulation. It writes:

| File | Meaning |
| --- | --- |
| `starts.csv` | Actual Town start, generation version and sample size for each seed |
| `sites.csv` | Full feature-owner key, sample membership, size/era, rotation/basement, entrance distance, population and access failures |
| `stock.csv` | Site-owned item/object quantities, reachable quantities and shortest cardinal entrance approach |
| `items.csv`, `objects.csv`, `providers.csv` | Live mass/volume, packing/installing relationships, readable identities, initial charge, traits and operating rates |
| `recipes.csv`, `requirements.csv` | Live inputs, results, retained tools, skills, teaching rates, work, setup and operating costs |

`tools/workshop_report.py` writes `summary.json` and five derived CSVs:
`derived_metrics.csv`, `derived_stock.csv`, `derived_baskets.csv`,
`derived_distances.csv` and `derived_routes.csv`.
Rows retain seed and observation identity.
Settlement groups overlap; never add them together. The two seed-union groups
deduplicate sites and never combine different worlds.

Raw baskets check one recipe's direct stock/tools. Finite ordered routes carry
earlier outputs forward, reserve reusable exact tools, check traits before each
operation and add whole-item forge fuel with one ignition. Installable objects
contribute potential station traits; packable objects contribute their original
item once. The missing-screwdriver route spends one extra scrap and 1,000 work.
These explicit sequences were chosen instead of a general dependency solver to
keep assumptions and intermediate consumption reviewable.

Access is a conservative cardinal flood from each entrance through supported
floors, openable doors and matched stairs, with a one-tile exterior ring and
blocking furniture. Ground stock requires reaching its tile; furniture permits
an adjacent approach. It does not model breaking walls or moving furniture.
Distance is Euclidean start-to-entrance distance, not a route. Population is
initial generated population after Town's same-floor radius-16 exclusion;
nearby-outdoor counts can overlap between sites.

## Reference run and tuning

Measured on Windows/MSVC from base revision `c4e3edd` plus this uncommitted pass,
generation **8**, using new worlds:

- **33 seeds, 388 distinct settlements, 15,441 sites**. The unfiltered origin
  planning patches contribute 330 settlements; the full sample additionally
  includes the four-region panel and actual starts. All nine size/era cells
  occur. This is descriptive evidence for chosen seeds, not a population estimate.
- Baseline: `out/pass5/baseline/`, after screwdriver integration but before loot
  tuning. Final: `out/pass5/final-balanced/`. Intermediate `tuned/` and `final/`
  directories are exploratory measurements, not the reference results.
- Baseline catalog exports were enriched with the later operating/setup columns
  only after every original exported field and requirement was verified equal.
  Its original generated stock was retained.
- Final scan: **959,748 item instances and 876,266 furniture instances**, with
  **zero structural access failures** under the rules above.
- The final release generation loop took 11.327 seconds total, median 0.326 and
  maximum 0.736 seconds per seed on this host. This measures probe generation,
  not gameplay frame time or expedition duration.

Baseline small/medium-world unions had median seven charcoal bags, six pipes
and eight rods. None supplied the 22-bag recovered-equipment no-study forging
example; only two supplied a 12-pipe rack. The selected changes therefore target
existing ground stock:

| Ground contexts | Final selected draw |
| --- | --- |
| Forging hardware/workshop, camping, city foundry | Charcoal 3–6 bags instead of one |
| Forging hardware/workshop and city foundry | Firebrick 4–8 instead of 1–4 |
| Hardware/workshop; city fabrication | Pipe bundles 4–8; workshop/fabrication rod bundles 4–8 |
| Workshop/auto repair and city foundry | Suitable steel bars 2–3 instead of one |
| Expanded workshop forging pool | Tongs weight 3→4 and smithing hammer 2→3 to offset new bulk-stock alternatives |

The first bulk iteration reduced suitable-bar supply through pool dilution;
the bar bundles address that measured regression. Restoring recovered-tool
weights preserves approximately their former draw probabilities, but does not
promise that each fixed seed improves. Counts remain bounded and bulk pools
are checked against every furniture destination, including nested pools and
basement populations.

### Quantities in same-seed small/medium unions

Each row has 33 observations, each containing 217–355 sampled sites (median
285). These are multi-settlement acquisition opportunities, not a short trip.

| Resource | Baseline min / median / max | Final min / median / max | Example threshold | Baseline → final worlds meeting it |
| --- | --- | --- | --- | --- |
| Charcoal bags | 0 / 7 / 20 | 0 / 19 / 51 | 22, recovered no-study forging | 0 → 16 |
| Firebricks | 0 / 13 / 33 | 0 / 26 / 66 | 8, one forge | 24 → 29 |
| Pipes | 1 / 6 / 15 | 4 / 24 / 54 | 12, one rack | 2 → 28 |
| Rods | 1 / 8 / 13 | 1 / 19 / 58 | 32, sixteen practice drives alone | 0 → 5 |
| Suitable bars | 2 / 8 / 17 | 2 / 16 / 35 | 7, recovered no-study forging | 20 → 30 |
| Filament portions | 4,211 / 7,049 / 11,202 | Same | 484, full C0→C3 example | 33 → 33 |

Thresholds are independent, not joint route completion. Filament was already
ample at this sample scale; no extra feedstock or public Computers book was added.

### Joint one-off construction bills

These include components, exact tools, provider traits and forge fuel; electricity
is budgeted but availability is not tested. The forging and wood-gas examples
assume the required learned skills. Printer practice starts at C0 but its final
battery consumer still needs E2. Exact ordered schedules and gross mass/work/
power bills are in the [audit](workshop_progression_audit.md#finite-assembled-route-acquisition-reports).

| Independent bill | Small/medium union, baseline → final /33 | Full sampled union, baseline → final /33 |
| --- | --- | --- |
| Found forging equipment + timber preparation + sword | 22 → 21 | 28 → 27 |
| Manufacture forging equipment + timber preparation + sword | 16 → 25 | 24 → 30 |
| Either forging bill | 23 → 26 | 28 → 31 |
| Assemble leads + drive + gasifier + wood-gas kit | 17 → 20 | 24 → 27 |
| Found wood-gas kit or that assembled bill | 21 → 23 | 28 → 29 |
| C0 printing through the printed-case battery consumer | 33 → 33 | 33 → 33 |

Recovered-only forging retains a one-world regression in this fixed sample;
the alternative-route union improves. The final small/medium manufactured bill
misses blocks in four worlds, bricks in four, fuel in two, bars in two, bolts in
one and rods in one (overlapping deficits). The wood-gas bill most often misses
an engine (seven worlds), alternator/mesh/welding (three each), then sheets or
protection (two each). Its complete-kit alternative remains meaningful.

### Starting settlements and local choice

| Final opportunity | Actual starting settlements /33 | Origin-patch settlements /330 |
| --- | --- | --- |
| Screwdriving or one loose scrap | 27 | 279 |
| Seven-capability basic tool set | 6 | 133 |
| Printer + 211 filament | 19 | 250 |
| Either one-off forging bill | 0 | 22 |
| Found/assembled wood-gas bill | 2 | 21 |
| Full printed-case battery bill | 3 | 97 |

The tool set means hammering, metal holding, metal sawing, metal drilling,
wrenching, electrical testing and screwdriving/fallback, all tier 1. Exact file
and other recipe-specific requirements are checked in route bills separately.
All 33 small/medium-world unions contain that set. All starting settlements have
a readable primer; 31 have salvage notes. Copies share the same once-per-character
book identity. Solar kit-or-direct-assembly coverage is 0/33 at the start, 29/33
in small/medium unions and 32/33 in full unions.

Final size/era comparisons below include all sampled settlements, so the panel
and start selection affect the mix. Each count is local to one settlement:

| Size / era | Settlements | Basic tool set | Printer + 211 portions | Median interior hostiles |
| --- | ---: | ---: | ---: | ---: |
| Small older | 60 | 13 | 29 | 61 |
| Small mixed | 104 | 17 | 68 | 75 |
| Small newer | 65 | 3 | 39 | 93 |
| Medium older | 26 | 22 | 24 | 269 |
| Medium mixed | 63 | 46 | 63 | 352 |
| Medium newer | 32 | 20 | 32 | 420 |
| Large older | 7 | 7 | 7 | 1,392 |
| Large mixed | 16 | 16 | 16 | 1,809 |
| Large newer | 15 | 13 | 14 | 2,398 |

Older specialist places retain workshop value; newer settlements increase
population cost and have strong printing opportunities without supplying every
ordinary tool locally. Sampled workshops have median 10 interior hostiles,
hardware/electronics stores six, auto repair eight, electronics service centers
nine, retrofitted foundries fifteen and automated fabrication plants thirty-five.
These are descriptive site populations, not estimated fights to clear them.

### Distance and transport indicators

Nearest available opportunity within each small/medium union, in tiles from the
actual start; missing worlds are excluded and the denominator is explicit:

| Find | Worlds | Median | P90 |
| --- | ---: | ---: | ---: |
| Factory screwdriver | 33 | 73 | 480 |
| Hand drill | 33 | 97 | 492 |
| Multimeter | 33 | 53 | 95 |
| Printer | 33 | 74 | 373 |
| Charcoal | 32 | 655 | 1,297 |
| Smithing hammer | 23 | 810 | 1,704 |
| Tongs | 26 | 711 | 1,490 |
| Recovered PV panel | 26 | 877 | 1,825 |
| Complete solar kit | 14 | 626 | 1,656 |
| Complete wood-gas kit | 9 | 1,033 | 1,482 |

Distance to an entrance excludes indoor approach, collection order, obstacles
and return hauling. Kit burdens are substantial: crude anvil 24 kg / 28 L,
fabricated anvil 25 kg / 30 L, forge 27 kg / 45 L, solar 10 kg / 22 L, wood gas
33 kg / 50 L. The manufactured one-sword bill consumes 90.88 kg / 62.5 L gross
external stock; the found-equipment bill consumes 6 kg / 11.6 L but presupposes
its stations/tools. These are neither simultaneous carried loads nor peak space.

## Learning result and remaining acceptance

The [audit](workshop_progression_audit.md) establishes the connected prerequisites,
teaching ceilings, public-study alternatives and operating costs. The report also
measures three explicit no-study gross schedules, including timber preparation:

| Schedule | Base work / grid EU | Final small/medium coverage /33 | Final full-union coverage /33 |
| --- | --- | ---: | ---: |
| Constructed forging F0→F4 and sword | 161,800 / 0; 21 charcoal bags | 1 | 4 |
| Recovered-equipment forging F0→F4 and sword | 160,200 / 0; 22 bags | 0 | 2 |
| Electronics E0→E3 and welder, separately supplied M2 | 81,100 / 45,000 | 0 | 0 |

These are deliberately gross schedules without salvage credits, study, alternate
practice choices or stock beyond the sample. Their low coverage is evidence
against treating a one-off construction bill as a complete novice progression.
The electrical example needs 150 loose steel scraps; every full sampled union
falls short of that direct-stock bill. It also produces 25 wiring kits and nine
packs. Forging produces 24–27 chisels; the separate mechanical example produces
six gasifiers and sixteen drives. Useful practice diversity and study timing
deserve the next pacing review, rather than flooding worlds with enough stock to
fund every repetitive independent example.

No mandatory construction cycle was found, and ordinary manuals/filament are
available. A solar or found-generator start can fund wood-gas construction;
empty battery packs do not create energy. Actual grid rate, daylight, learned
skills, survival and hauling remain necessary gameplay conditions. This pass
does not sign off the whole loop as comfortably playable.

## Verification and artwork

- Release probe, Debug and SDL builds completed. The final Debug selection
  passed **41/41** in 12.09 seconds: bootstrap/provider gates, shipped manifests,
  acquisition, authored forging/electrical/printing/renewables, current saves,
  funded-work conservation, regeneration and shared wire/control views.
- Earlier focused content/gameplay selection passed **23/23**. SDL item-browser
  rendering plus `sdl_frontend_smoke`, `sdl_town_default_smoke`,
  `sdl_lan_roundtrip`, `headless_lan_roundtrip` passed **5/5**.
- The final art-only renderer rerun passed **1/1**; its 480×360 and 1120×900
  previews were inspected at
  `out/pass5/screwdriver-final_makeshift_screwdriver_{480,1120}.png`.
  The final colored glyphs match the local uncolored master. A brighter edge and
  closed flat blade replaced the initial broken-tip silhouette.
- No tooling tests or agent playthroughs were run. One direct probe build attempt
  lacked the MSVC environment and one renderer regex matched no tests; both were
  corrected using the commands below. No gameplay/content check failed.
- Canonical screwdriver text art is supplied. Its SDL map icon still uses the
  existing fallback; dedicated item/object/city sprite work remains the external
  artwork handoff.

Final Debug selection:

```powershell
./scripts/build.ps1 -Preset win-msvc-debug -SkipTests
./scripts/test.ps1 -Preset win-msvc-debug -R 'Makeshift screwdriver|Ordinary screwdriving|Screwdriver salvage|Town slice|All shipped manifests|W3 |Full and first-house|Home printer|Printed|Workshop acquisition|Workshop stations|Generated town workshop|Renewable|Crafting funded slices|Crafting save reload|Digital disk roundtrip|Learning state saves|Study save roundtrip|Retained tool replacement after save|Saved exhausted forge|Ten shipped cups|Regional generation saves|save_client_roundtrip|Workshop shared controls|Forge shared controls|Solar integrates|Solar export|Wood gas|wood gas'
```

SDL/render commands:

```powershell
./scripts/build.ps1 -Preset win-msvc-sdl -SkipTests
./scripts/test.ps1 -Preset win-msvc-sdl -R 'SDL item slice|sdl_frontend_smoke|sdl_town_default_smoke|sdl_lan_roundtrip|headless_lan_roundtrip'
$env:CATACLYSMX_BOOTSTRAP_PREVIEW = "$PWD/out/pass5/screwdriver-final"
./scripts/test.ps1 -Preset win-msvc-sdl -R '^SDL item slice expansion workshop and digital devices render through the item browser$'
```
