# Stock and hardware expansion

Twenty expansion stock units, quality-reviewed in place: eight metal, seven
wood/textile/polymer and five hardware. All 20 are placed as early salvage in
hardware/workshop, auto, warehouse and textile-repair contexts.
Descriptions: all 20 plain; no atmospheric or humorous exceptions.
All 20 complete monochrome masters and RGB illustrations
have had a source-grid review with 1:2 terminal cells in mind. Corrected mesh and
weave borders, packet silhouettes, webbing width, sheet depth and screw spacing
precede the matching color layouts. The coordinator subsequently reviewed the SDL
contact sheets; the focused nine-asset follow-up is recorded below.

All physical values are whole represented units, with handling envelopes rather
than solid-material displacement. The current 100 g steel/aluminum offcuts,
200 g copper coil and 200 g wood scrap anchor returns. No custom melee is authored:
these are feedstock and use the ordinary improvised fallback. Material properties
are descriptive flags; structural strength, fastening, insulation, elasticity,
springs and repair behavior are not simulated by those flags.

| ID | Unit; g/ml | Role and loot context | Processing; status |
| --- | --- | --- | --- |
| steel_pipe | short pipe; 1200/1400 | structural stock; hardware/auto | spear input; cut to scrap |
| steel_rod | rod; 800/300 | bar stock; workshop | spear input; cut to scrap |
| steel_wire | coil; 200/180 | binding stock; hardware | spear and machete input; scrap |
| steel_chain | length; 1500/1000 | chain stock; auto | scrap |
| wire_mesh | panel; 600/2000 | mesh stock; warehouse | scrap |
| aluminum_sheet | panel; 600/1200 | light sheet; workshop | scrap |
| aluminum_tube | short tube; 300/800 | hollow stock; auto | scrap |
| copper_sheet | offcut; 900/400 | copper fabrication feedstock; workshop | future use; no current recycler |
| plywood_panel | panel; 1800/3500 | sheet wood; warehouse | wood scraps; ground-only bulk pool |
| wooden_dowel | long dowel; 350/650 | shaped wood; hardware | crafted from timber |
| rubber_sheet | sheet; 300/500 | rubber stock; auto | future feedstock |
| foam_padding | pad; 150/2000 | padding stock; clothing/outdoors | padded jacket input |
| canvas_patch | patch; 150/500 | heavy fabric; clothing/outdoors | padded jacket and duffel input |
| leather_patch | patch; 120/250 | leather stock; clothing/outdoors | tool belt input |
| nylon_webbing | length; 100/200 | strap stock; outdoors | tool belt and duffel input |
| framing_nails | packet of 20; 100/80 | fasteners; hardware | future feedstock |
| wood_screws | packet of 20; 100/80 | fasteners; hardware | future feedstock |
| nuts_and_bolts | packet of 10 paired sets; 200/100 | fasteners; auto | tool belt input |
| door_hinge | one hinge; 150/70 | fitting; hardware | future feedstock |
| coil_spring | one spring; 120/100 | spring stock; auto | future feedstock |

## Processing and review decisions

Final recipe count: **one craft and eight explicit recyclers**. Processing uses
retained hand tools; losses account for kerfs, unusable pieces and the coarse
whole-unit return sizes. The removed `draw_stock_copper_sheet` recipe could not
turn sheet strips into the existing bare drawn-wire coil using snips and pliers.
`copper_sheet` remains fabrication feedstock for future use, with no current
conversion or implied smelting/drawing action.

| Recipe / target | Consumed g | Result g |
| --- | ---: | ---: |
| shape_timber_dowels / timber_plank | 2000 | 1400 (four dowels) |
| cut_stock_steel_pipe | 1200 | 1000 (ten steel scraps) |
| cut_stock_steel_rod | 800 | 700 (seven steel scraps) |
| cut_stock_steel_wire | 200 | 100 (one steel scrap) |
| cut_stock_steel_chain | 1500 | 1200 (twelve steel scraps) |
| cut_stock_wire_mesh | 600 | 400 (four steel scraps) |
| cut_stock_aluminum_sheet | 600 | 500 (five aluminum scraps) |
| cut_stock_aluminum_tube | 300 | 200 (two aluminum scraps) |
| cut_stock_plywood_panel | 1800 | 1400 (seven wood scraps) |

The dowel recipe consumes solid timber, not plywood; its hacksaw and
equipment-batch wood chisel are retained. Plywood is a wood laminate, and its
small cut offcuts use the existing general wood-scrap unit, not solid timber or
finished dowels. Canvas stays canvas, leather stays leather, and nylon stays
nylon: none is converted into the explicitly cotton `cloth_patch`. Rubber and
open-cell foam likewise retain their distinct identities. Hardware packets count
as whole packets (20 nails, 20 screws, ten paired bolt sets), not loose pieces.

Flags were checked against current shared meanings: bare metal hardware is rigid
and thermally conductive; a chain remains flexible, and a spring elastic. Mesh
openings do not make its steel transparent. Sheared sheet edges are explicitly
cutting-sharp in the prose. Palettes distinguish cool steel/aluminum, copper and
localized tarnish, wood grain/laminate, matte rubber, foam pores, textile weave,
leather creases and paper packets, with restrained highlights and lower shadows.

Static verification completed for all 20 whole-unit mass/volume contracts against
the pre-review table, art references, shared flag references/domains, complete
master/RGB equality, supported glyphs, RGB declarations/references, dimensions and
file limits. All dominant grid dimensions are 24–32 cells. All nine recipe records
were checked for current referenced inputs, distinct input/return types, integer
counts, retained tools, bounded return mass and unique recycling targets across
the current crafting sources. These were author-side static checks; final native
integration and renderer results are recorded in [verification and art review](item_expansion.md#verification-and-art-review).

## Final art-only SDL feedback refinement

Read the actual `out/item-expansion_stock_contact_1.png` and
`out/item-expansion_stock_contact_2.png` sheets alongside the equipment sheet.
Redrew the six stock masters below before RGB colorization: pipe with a thick
elliptical hollow rim, narrower solid-ended rod, brighter thin-wall aluminum
tube, wood-grained dowel with end rings, five overlapping helical spring turns
without a center spool, and softly irregular canvas with loose edge threads and
sparse weave marks. Source-grid self-inspection accounted for 1:2 terminal cells.

The focused read-only asset audit passed exact master/decoded glyph-and-whitespace
equality, RGB declaration/default/local-reference checks, native glyph allowlist,
UTF-8/header/newline/file limits and decoded bounds for all nine revised assets.
Stock sizes (width × height): pipe **31×7**, rod **28×5**, aluminum tube **31×7**,
dowel **30×6**, spring **29×6**, canvas **26×9**. The coordinator subsequently
rendered and re-inspected the refinements; see [verification and art review](item_expansion.md#verification-and-art-review).

## Monochrome masters

### steel_pipe
```text
                       ╭────╮
     ╭────────────────╱ ▄▄▄▄ ╲
   ╱  ───────────────│ █    █ │
  │                 │ █    █ │
   ╲                │ ▀▄▄▄▄▀ │
    ╰────────────────╲      ╱
                       ╰────╯
```
### steel_rod
```text
                         ╭─╮
    ╭───────────────────╱▓▓│
   ( ──────────────────(▓▓▓│
    ╰───────────────────╲▓▓│
                         ╰─╯
```
### steel_wire
```text
       ╭──────────────╮
   ╭───╯ ╭──────────╮ ╰───╮
  │  ╭───╯ ╭──────╮ ╰──╮  │
  │  ╰─────╰──────╯─────╯  │
   ╰────╮ ╭────────╮ ╭────╯
        ╰─╯        ╰─╯──────
```
### steel_chain
```text
   ╭────╮  ╭────╮  ╭────╮
  ╱ ╭──╮ ╲╱ ╭──╮ ╲╱ ╭──╮ ╲
  ╲ ╰──╯ ╱╲ ╰──╯ ╱╲ ╰──╯ ╱
   ╰────╯  ╰────╯  ╰────╯
```
### wire_mesh
```text
  ╭────────────────────────╮
  │╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─│
  │╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─│
  │╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─╳─│
  ╰────────────────────────╯
```
### aluminum_sheet
```text
    ╱─────────────────────╱│
   ╱  ╱───────────────╱  ╱ │
  ╱       ──────────    ╱  │
 ╱─────────────────────╱   │
 ╰─────────────────────╯───╯
```
### aluminum_tube
```text
                       ╭────╮
     ╭────────────────╱      ╲
   ╱  ───────────────│ ╭────╮ │
  │                 │ │    │ │
   ╲                │ ╰────╯ │
    ╰────────────────╲      ╱
                       ╰────╯
```
### copper_sheet
```text
      ╱──────────────╲
  ╱──╱  ────────      ╲──╲
 ╱       ╱──────╲         ╲
 ╲──────╱        ╲────────╱
   ╲────────────────────╱
```
### plywood_panel
```text
   ╱──────────────────────╱│
  ╱ ───╮  ───(─)───────  ╱ │
 ╱  ───╯ ─────────────  ╱  │
╱──────────────────────╱   │
│══════════════════════│───╯
```
### wooden_dowel
```text
                       ╭───╮
    ╭─────────────────╱     ╲
   ( ──────  ────────│ (())  │
   (  ───────  ──────│  ()   │
    ╰─────────────────╲     ╱
                       ╰───╯
```
### rubber_sheet
```text
   ╭───────────────────────╮
  ╱ ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ ╱│
 ╱ ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ ╱ │
╰───────────────────────╯  │
 ╰─────────────────────────╯
```
### foam_padding
```text
   ╭──────────────────────╮
  ╱ · · · · · · · · · ·  ╱│
 ╱ · · · · · · · · · ·  ╱ │
╰──────────────────────╯  │
│░░░░░░░░░░░░░░░░░░░░░░│ ╱
╰──────────────────────╯╱
```
### canvas_patch
```text
      ,__   __,___
   _,/   ~~~      ~──.__,
  /  ·  ·  ·  ·  ·     ~\,
 ,│ ·  ·  ·  ·  ·  ·    /
  /  ·  ·  ·  ·  ·  ·  │,
  \ ·  ·  ·  ·  ·  ·   /
  ,\    ·  ·  ·  ·   _/~
    ~─.__    __,───~~
         ~~~~  '
```
### leather_patch
```text
      ╭────────────────╮
  ╭───╯  ┄  ┄    ┄     ╰──╮
  │ ┄   ╭──────╮   ┄      │
  │   ┄ ╰──────╯  ┄     ╱─╯
  ╰───────╮  ╭─────────╯
          ╰──╯
```
### nylon_webbing
```text
   ╭───────────────────────╮
 ╭─┤ ═╪═╪═╪═╪═╪═╪═╪═╪═╪═ ├─╮
 │ │ ─┼─┼─┼─┼─┼─┼─┼─┼─┼─ │ │
 ╰─┤ ═╪═╪═╪═╪═╪═╪═╪═╪═╪═ ├─╯
   ╰───────────────────────╯
```
### framing_nails
```text
  ╭──────────────────────╮
  │ ╞═══════════════╲    │
  │   ╞═══════════════╲  │
  │ ╞═══════════════╲    │
  │   ╞═══════════════╲  │
  ╰──────────────────────╯
```
### wood_screws
```text
  ╭──────────────────────╮
  │ ╞╪╪╪╪╪╪╪╪╪╪╪╪╪╪╪╪╲   │
  │   ╞╪╪╪╪╪╪╪╪╪╪╪╪╪╪╲   │
  │ ╞╪╪╪╪╪╪╪╪╪╪╪╪╪╪╪╪╲   │
  ╰──────────────────────╯
```
### nuts_and_bolts
```text
  ╭──────────────────────╮
  │ ┌──╮ ╭─╮  ╭──╮       │
  │ ╞══╪═╪═╪══╡  │ ╭──╮  │
  │ ╞══╪═╪═╪══╡  │ │o │  │
  │ ╘──╯ ╰─╯  ╰──╯ ╰──╯  │
  ╰──────────────────────╯
```
### door_hinge
```text
  ╭──────────╮ ╭──────────╮
  │  o       ├─┤       o  │
  │          ╞═╡          │
  │  o       ├─┤       o  │
  ╰──────────╯ ╰──────────╯
```
### coil_spring
```text
    ╭──╮ ╭──╮ ╭──╮ ╭──╮ ╭──╮
   ╱    ╳    ╳    ╳    ╳    ╲
  │    ╱│   ╱│   ╱│   ╱│    │
 ─╯   ╱ │  ╱ │  ╱ │  ╱ │   ╱
  ╲  ╱  ╲ ╱  ╲ ╱  ╲ ╱  ╲  ╱──
   ╰╯    ╰╯   ╰╯   ╰╯   ╰╯
```
