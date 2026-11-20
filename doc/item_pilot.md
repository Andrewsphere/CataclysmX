# Item content pilot

> Historical authoring record: source paths, counts, and verification describe
> the original pilot, not current ownership. See
> [current source organization](content_pack.md#source-organization) for semantic
> ownership.

The later [broad item slice](item_slice.md) preserves all five definitions and
original recipes, adds current-material crafting alternatives, replaces the
original saw/knife loot with current tools, and marks retained older material
dependencies `obsolete`.

**Sword progression superseded:** [W3A/W3B](workshop_rosters.md) preserves the
sword and both recipe IDs but replaces the historical cold-scrap routes below.
Both now use continuous suitable steel and require Fabrication 4, anvil 2,
hammering 2, metal holding 2 and a fueled forge. The original stats/art remain.

## Scope and coverage

Five-item target: **two new IDs**, **three existing-ID upgrades**, five large
illustrations, two crafting recipes, and a shared vocabulary of **36 flags**
(12 categories, 24 physical properties). Also migrate three existing small art
grids without redrawing. No new loot placement is required: the sword is crafted.

| ID | Unit / role | Acquisition and progression | Status |
| --- | --- | --- | --- |
| `wood_scrap` | One offcut; salvage and grip stock | New; cut an existing house-storage plank | Integrated and verified |
| `claw_hammer` | One wooden-handled steel hammer; practical tool | Existing; house hammer pool / urban tools; early crafting tool | Upgraded in place |
| `glass_bottle` | One uncapped bottle; small container | Existing; household supplies; early storage | Upgraded in place |
| `work_shirt` | One cotton shirt; everyday clothing | Existing; urban clothing pools; early salvage | Upgraded in place |
| `arming_sword` | One crude salvaged sword; dedicated melee weapon | New; scrap, offcuts, wire, hammer and cutter; later low-tech craft | Integrated and verified |

Actual coverage meets the requested roster: salvage, tool, container, clothing,
and crafted melee weapon. Existing electronics, batteries and wire remain the
advanced pre-collapse salvage context; there is no museum dependency. The roster
is not a loot pool. Actual acquisition is subject to existing placement rolls.

## Physical and combat choices

| ID | Mass g | External ml | Melee HP / ticks / hands |
| --- | ---: | ---: | --- |
| `wood_scrap` | 200 | 300 | Default blunt 4 / 100 / 1 |
| `claw_hammer` | 600 | 500 | Authored blunt 12 / 110 / 1 |
| `glass_bottle` | 200 | 600 | Default blunt 4 / 100 / 1 |
| `work_shirt` | 300 | 1200 | Default blunt 4 / 100 / 1 |
| `arming_sword` | 1800 | 2000 | Authored slashing 18 / 130 / 1 |

Existing physical values and bottle capacity (500 ml) are preserved. One offcut
is one tenth of the 2000 g / 3000 ml plank. Sword volume represents a long,
awkward object, not steel displacement; 1800 g deliberately describes a heavier,
less balanced implement than a well-made historical sword.

The hammer's focused steel head earns twice the rock's 6 HP at the same 110
ticks, below the pipe's 20 HP / 150 ticks / two hands. The sword trades the pipe's
raw damage for slashing, one-handed use and a 130-tick swing, at substantial
crafting cost. Its zero penetration avoids a forged-quality or armor-piercing
claim. Damage uses hundredths of HP (`1200`, `1800`), not whole HP. These are
authored starting points, not playtested balance findings. No separate thrust,
reach, breakage, tempering, or defensive capability is implied.

## Crafting route and accounting

Existing roster reviewed: stone marker/recycler, improvised bandage, wooden
handle, wire salvage, scrap knife, and shirt/trouser recyclers. The existing
scrap knife uses handle + wire + scrap with 200 work and no authored melee
override (therefore 4 HP blunt). The sword is deliberately a larger investment.

1. Obtain a `house_cutting` tier-1 tool: the house kitchen knife is guaranteed
   by its existing placement; saws and urban utility knives are alternatives.
2. `cut_wood_scrap`: one plank -> ten offcuts, cutting 1, **200 work**.
   This conserves 2000 g and 3000 ml at the represented-unit level. Work abstracts
   cutting/paring; tier 1 is the existing broad cutting vocabulary, not a new
   claim that all cutters perform equally well.
3. Obtain wire directly from urban hardware or use existing
   `salvage_wire_bundle`: electrical cord + cutting 1 -> wire bundle, 100 work.
4. Obtain the existing claw hammer (`hammering` tier 1) and scrap stock.
   `make_salvaged_arming_sword`: scrap 14 + offcut 2 + wire bundle 1,
   hammering 1 and cutting 1, **6000 work** -> sword 1.
   Select a long steel strip from the scrap stock, cold-straighten it, bend a
   separate scrap guard, then slowly abrade the edge/point against rough masonry;
   cut wood forms grip scales bound in wire. This does not join short fragments
   into a blade. Generic scrap selection follows the existing scrap-knife recipe's
   abstraction; no new material suitability field is asserted. The fixed abrasive
   surface is abstracted by work, not checked by an invented environment requirement.
   The hammer and cutter are not consumed. No forge, anvil capability,
   powered grinder, heat treatment, welding, or historical smithing is asserted.
   Consumed mass is 1400 + 400 + 200 = 2000 g; 1800 g remains, 200 g is implicit
   shaping waste. No waste item or recycler is introduced.

Both recipes have 1..8 distinct inputs, counts 1..255, at most four traits and
explicit positive work. Sword inputs occupy three slots, and
two trait slots. No new item beyond `wood_scrap` and `arming_sword` is required.
The new `hammering` trait describes ordinary impact work, not forging proficiency.

Planks occur in house storage, scrap in household workshop pools, and wire/cord
and hammer in urban pools. The full route targets the expanded Town pack.
Availability is not guaranteed in each starting house. Tests verify authored loot
placements and the complete crafting chain independently of seed luck. A
pipe-and-rock draft was rejected after
source review: rocks are explicitly placed only in the demo start, and the
`scrap_press` pipe producer has no authored Town placement. The final recipe does
not depend on either. The basic demo lacks household cutting and urban wire.
The `hammering` trait definition lives in `items/household.json5`, alongside its
provider, so the existing FirstHouse sources resolve the upgraded hammer without
including the new recipes or urban dependencies.

## Flags and descriptive boundaries

`data/flags/core.json5` owns the shared vocabulary. Flag IDs have a separate
namespace from item and other ordinary definition IDs. Each definition explicitly
states `kind`, meaning and allowed domains, selected from item/monster/terrain/
object. All flags are descriptive until an explicit consumer interprets them.
There is no implication or propagation, and no flag duplicates tool tiers,
container capacity or mutable wet/open/charged/broken state.

The five pilot items receive all clearly relevant flags from this vocabulary.
Mixed metal/wood implements receive exposed-metal conductivity plus combustible,
absorbent, porous wood properties. They are not electrically insulating.
The shirt is flexible, not designed collapsible; it has no armor category.
The uncapped bottle has no supplied seal, so no resealable, airtight or watertight
flag. Its intact glass does not have a sharp edge or point. No uncertain chemical
hazards are assigned. The three art-only migrations retain their existing flags
(none) rather than expanding this pilot's classification scope.

## Monochrome master grids — authored before color

These complete grids are the comparison masters for the canonical colored files.
Silhouette self-review at source-text scale: offcut reads as a split timber block,
hammer separates striking face/claw from wooden shaft, bottle has an open lip and
shoulders, sword separates taper, bent guard and bound grip. The supplied
`out/item-pilot_work_shirt_1120.png` preview exposed an excessively long, narrow
shirt at approximately 10 x 19 pixel cells. Its revised 32-column x 16-row master
uses a wider torso and sleeves, a short hem, a clear collar, two chest pockets,
four buttons and one small company insignia on the left pocket. Box-drawn cuffs,
pocket seams and hem strengthen the silhouette; sparse cotton texture and brighter
muted blue-gray outlines/highlights improve fabric readability. At that cell aspect
the full grid is approximately 320 x 304 pixels, instead of a tall gown silhouette.
The revised shirt was rendered and inspected at both preview sizes; its shorter,
wider silhouette reads as a shirt at the renderer's actual cell aspect ratio.
Manual source comparison, ignoring color tags and preserving glyphs and spaces,
found no mismatches against the masters for all five original assets. A second
manual comparison of the revised colored shirt against its new master also found
no glyph or spacing mismatches; no automated stripping check ran.
Colorization must retain every glyph and leading space below;
rows need not have equal lengths and have no required trailing padding.

### `wood_scrap` — roughly 24 x 16

```text
       __________________
      / _______________ /|
     / /___/____/____/ / |
    /_______________ /  |
   |  ────   ─────  | /|
   | ──   ────────  |/ |
   |    ─────   ──  |  |
   | ─────  ────    | /|
   |   ───────  ──  |/ |
   | ───   ───────  |  |
   |    ─────  ───  | /|
   | ─────   ─────  |/ |
   |   ────   ────  |  /
   | ───  ────────  | /
   |________________|/
    / /     / /    /
```

### `claw_hammer` — roughly 24 x 24

```text
   _________
  /________/|____
  |        |     \____
  |  ░░░░  |  ___     \
  |  ░░░░  | /   \__  \
  |________|/       \  |
  |________|         \ |
       |  |           \|
       |  |
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
       |░░|
      /░░░░\
      |░░░░|
      \____/
```

### `glass_bottle` — roughly 24 x 28

```text
         ______
        / ____ \
        \______/
         |    |
         | │  |
         | │  |
         | │  |
         | │  |
        /  │   \
       /   │    \
      /    │     \
     /     │      \
    /      │       \
   /       │        \
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |  │               |
  |                  |
  \__________________/
   \________________/
```

### `work_shirt` — 32 columns x 16 rows (maximum row width; no trailing padding)

```text
          ┌────┐┌────┐
     ┌────┘   /└┘\   └────┐
    / ░░░░░░\ /  \ /░░░░░ \
   / ░░░░░░░ V │  V ░░░░░░ \
  / ░░░░░░░░░░ o ░░░░░░░░░░░ \
 / ░░░░░┌─────┐│ ┌─────┐░░░░░ \
┌──────┐│ ─── ││ │     │┌──────┐
└─────┐│└──v──┘o └──v──┘│┌─────┘
      ││       │        ││
      │ ░      │      ░  │
      │ ░      o      ░  │
      │ ░      │      ░  │
      │ ░      │      ░  │
      │        o         │
      │ ░░░░░░ │ ░░░░░░  │
      └────────┴─────────┘
```

### `arming_sword` — roughly 16 x 32

```text
       /\
       /\
      /│ \
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
      |│ |
  ____|│ |____
 /____   _____\
      |==|
      |//|
      |==|
      |//|
      |==|
      |//|
      /__\
      \__/
```

## Art and pack integration

Canonical assets: `data/art/items/<id>/art.txt` for all five pilots plus `rock`,
`backpack`, `metal_pipe`. Item references are `/art/items/<id>/art.txt`, rooted at
the manifest's data directory. Files use `@text-art 1`, local RGB palette,
optional default, `---`, then tagged lines. Color resets each line. Only approved
single-cell ASCII and U+2500–259F glyphs are used; no tabs, combining or wide glyphs
are intended. Small migrations preserve exact source glyphs, unequal line lengths
and SDL colors: gray #AAAAAA, darkgray #5A5A5A, yellow #E1CC59, green #58C284.
Uncolored leading spaces use a default; their invisible foreground is immaterial.
The large grids use fewer than six colors where their simple materials warrant it.

Integrated manifest sources (relative to each manifest's `data/` directory):

- Every affected pack (`demo.json5`, `first_house.json5`, `content.json5`,
  `zombie_showcase.json5`) includes `flags/core.json5` once.
- Expanded Town `content.json5` includes `items/low_tech.json5` and
  `crafting/low_tech.json5` once. Its existing demo/household/urban item and crafting
  sources supply all dependencies. No art files belong in the definition `files`
  array: the external art resolver reads the referenced assets.
- Do not add the low-tech recipe file alone to demo/FirstHouse: it depends on
  urban `wire_bundle` and household `house_cutting`. If extending those packs is
  desired, their missing source dependencies require a separate integration choice.

See [content asset loading](content_assets.md) and
[definition flags](definition_flags.md) for the implemented loader contracts.

Map presentation uses the **generic fallback**: inventory text art is not a map
tileset asset, and no dedicated map sprites or tileset mappings are supplied.

## Validation and preview workflow

The `win-msvc-sdl` build passed. Focused CTest selections covered content loading,
flags, art, inventory, crafting, Town/house integration and both headless/SDL LAN
round trips. One stale expected diagnostic was updated for the new `flag` kind;
the corrected case and all four pilot cases passed on the focused rerun.
Two file-reader cases skipped internally on this host: symlink creation requires
a Windows privilege, and the active single-byte code page has no malformed byte
sequence. UTF-8 rejection and non-ASCII pack/asset loading passed.

`tests/item_pilot_tests.cpp` verifies the five definitions, relevant flag boundaries,
available loot placement paths, missing-tool refusals, complete server-driven
plank-to-scraps-to-sword crafting, exact consumption and retained tools. These
checks establish functionality, not final combat balance.

`tests/sdl_inventory_tests.cpp` renders all five at 480x360 and 1120x900, checks
scrolling and render-state preservation, and can write ten previews:

```powershell
$env:CATACLYSMX_ITEM_PILOT_PREVIEW = "$PWD/out/item-pilot"
./scripts/test.ps1 -Preset win-msvc-sdl -R 'SDL item pilot'
Remove-Item Env:CATACLYSMX_ITEM_PILOT_PREVIEW
```

Ensure `out/` exists first. Filenames are `out/item-pilot_<id>_<width>.png`.
Generated previews were inspected, and all five colored source grids were manually
compared against their monochrome masters. The original shirt became a long coat
at real font aspect; the final 32x16 grid restores recognizable shirt proportions.
Compact views use the existing vertical scrolling rather than wrapping art.

Remaining gameplay work is balance playtesting and future mechanics for worn
clothing/protection, liquid sealing, hazards and flag-driven material interactions.
The bottle has functional ordinary storage through its existing capability.
