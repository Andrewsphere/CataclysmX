# Materials and electronics batch

> Historical authoring record: source paths, counts, and verification below
> describe that pass, not current ownership. See
> [current source organization](content_pack.md#source-organization) for semantic
> ownership.

Twenty new definitions: 11 everyday material units and nine electronic or
electromechanical components. The items are included in Town and FirstHouse;
actual acquisition routes and implemented crafting and recycling are recorded in
[the integration handoff](item_slice_integration.md). The table's locations and
unimplemented returns are authoring suggestions, not a complete route inventory.
External art, renderer review and content validation are complete; see the
[coordinator's verification record](item_slice.md#verification).
Existing pilot items are outside this batch. Flags describe physical properties, not behaviors. No motor,
control, fluid transfer, material-processing, or electrical simulation is added.
All items use the existing default improvised melee profile rather than pretending
these parts are weapons.

| ID | Unit; mass / external volume | Role; suggested acquisition / stage | Physical reasoning and suggested recycling returns |
| --- | --- | --- | --- |
| steel_scrap | selected offcut; 100 g / 100 ml | general metal feedstock; garage offcuts / early | Matches existing input unit; too small to yield another defined unit; recover from larger steel parts. |
| copper_wire | bare coil; 200 g / 200 ml | conductor stock; electrical workshop / early | Matches existing input unit, loose coil includes air; recover from large motors. |
| copper_tube | short tube; 500 g / 700 ml | plumbing feedstock; house renovation / early | Hollow, rigid tube; no smaller copper unit defined. |
| steel_sheet | cut panel; 2000 g / 1200 ml | sheet stock; garage / early | Thin panel with handling envelope; implemented recycler returns 15 steel scraps. |
| aluminum_scrap | bent offcut; 100 g / 250 ml | light metal feedstock; workshop / early | Low density and irregular shape; no smaller aluminum unit defined. |
| rigid_plastic_scrap | molded fragment; 100 g / 300 ml | plastic stock; household appliance salvage / early | Light hollow fragment; no smaller plastic unit defined. |
| corrugated_cardboard | flattened panel; 150 g / 1000 ml | packing stock; shop back room / early | Bulky fluted fibers; no smaller cardboard unit defined. |
| glass_shard | single shard; 100 g / 100 ml | glass stock; damaged windows / early | Dense, irregular shard; no smaller glass unit defined. |
| rubber_hose | short hose; 300 g / 700 ml | flexible plumbing stock; garage / early | Hollow rubber with an air-filled envelope; no smaller rubber unit defined. |
| cloth_patch | clean cotton patch; 40 g / 100 ml | repair stock; bedroom sewing kit / early | Matches existing input unit; cutting produces two 20 g cloth bandages. |
| timber_plank | one plank; 2000 g / 3000 ml | construction stock; lumber yard / early | Matches existing input unit; implemented cutting recipe returns 10 wood scraps. |
| transistor | one through-hole transistor; 2 g / 5 ml | electronic component; repair bench / advanced salvage | Small package and leads; no smaller defined return. |
| diode | one axial diode; 1 g / 3 ml | electronic component; repair bench / advanced salvage | Individual glass device and leads; no smaller defined return. |
| microcontroller_board | one small board; 30 g / 80 ml | embedded controls; electronics shop / advanced salvage | Board plus projecting header; suggest individual components only if supported by future disassembly. |
| desktop_motherboard | one board; 800 g / 2500 ml | computer electronics; office computer / advanced salvage | Large board with sockets and heatsinks; suggest a microcontroller board only if an actual compatible module is represented, otherwise components. |
| tiny_electric_motor | one small motor; 20 g / 30 ml | small mechanism; toy repair / advanced salvage | Thumb-sized can and shaft; no full 200 g copper coil return. |
| appliance_electric_motor | one appliance motor; 4000 g / 3500 ml | machine drive; appliance workshop / advanced salvage | Compact mains motor, much larger than toy motor; implemented recycler returns 4 copper coils and 20 steel scraps. |
| traction_electric_motor | one vehicle motor; 60000 g / 45000 ml | vehicle drive; garage floor / advanced salvage | Heavy traction assembly, intentionally too large for small containers; suggest multiple copper coils and steel scraps bounded by mass. |
| servo | one geared actuator; 60 g / 100 ml | controlled positioning; robotics bench / advanced salvage | Small plastic case and output horn; component returns require future disassembly rules. |
| water_pump | one electric pump assembly; 2500 g / 3000 ml | fluid transfer component; utility room / advanced salvage | Housing, ports and motor; implemented recycler returns 12 steel scraps and 2 copper coils. |

## Monochrome masters and silhouette review

Full glyph grids below are the colorization masters. The irregular offcuts, wire
coil, open tube, fluting, shard, hose and fabric/plank grain distinguish the stock;
leads, board traces, motor shafts and pump ports distinguish components. Terminal
cells are tall: wide shallow outlines represent panels and coils, while motors
use compact housings with projecting shafts. Source-level review was followed by
the coordinator's SDL contact-sheet review. Colors retain these glyphs and spaces.

### steel_scrap
```text
       ╱────────╲
   ╱──╱  ╱────╲  ╲────╲
  ╱  ╱  ╱      ╲      ╲
 ╱  ╱──╱  ──────╲      ╲
 ╲      ╲        ╲   ╱─╱
  ╲──────╲────────╲─╱
```

### copper_wire
```text
        ╭──────────╮
    ╭───╯ ╭──────╮ ╰───╮
  ╭─╯ ╭───╯      ╰───╮ ╰─╮
  │  │  ╭────────╮  │  │
  │  │  ╰────────╯  │  │
  ╰─╮ ╰───╮      ╭───╯ ╭─╯
    ╰─────╰──────╯─────╯───
```

### copper_tube
```text
     ╭─────────────────────╮
   ╭─╯ ─────────────────── │
  │  ╭───────────────────╮ │
  │  ╰───────────────────╯ │
   ╰─╮ ─────────────────── │
     ╰─────────────────────╯
```

### steel_sheet
```text
     ╱────────────────────╱│
    ╱  ────────────────  ╱ │
   ╱   ──────────────   ╱  │
  ╱    ────────────    ╱   │
 ╱────────────────────╱    │
 ╲────────────────────╲────╯
```

### aluminum_scrap
```text
      ╱──────────╲
  ╱──╱  ╱────╲    ╲────╲
 ╱   ╲ ╱      ╲    ╲   ╱
 ╲    ╳────────╲────╳─╱
  ╲──╱ ╲        ╲  ╱
    ╲───╲────────╲╱
```

### rigid_plastic_scrap
```text
      ╭──────────────╮
  ╭───╯ ╭──────────╮ ╰──╮
  │  ╭──╯ ──────── ╰─╮  │
  │  │  ╭────────╮   │  ╱
  ╰──╯  ╰────────╯  ╱──╯
      ╲────────────╱
```

### corrugated_cardboard
```text
   ╱──────────────────────╱│
  ╱  ────  ────────────  ╱ │
 ╱  ──────────────────  ╱  │
╱──────────────────────╱   │
│~~~~~~~~~~~~~~~~~~~~~~│  ╱
╰──────────────────────╯─╯
```

### glass_shard
```text
             ╱╲
        ╱───╱  ╲
   ╱───╱   ╱    ╲──────╲
  ╱       ╱  ╱──╲      ╱
 ╱   ╱───╱  ╱    ╲    ╱
 ╲──╱      ╱──────╲──╱
```

### rubber_hose
```text
     ╭──────────────╮
  ╭──╯ ╭──────────╮ ╰──╮
 ╱  ╭──╯          ╰──╮  ╲
│  │                  │  │
 ╲  ╰──╮          ╭──╯  ╱
  ╰────╯          ╰──╮ ╱
                      ╰O╯
```

### cloth_patch
```text
  ╭──────────────────────╮
  │ ┆ ┆ ┆ ┆ ┆ ┆ ┆ ┆ ┆    │
  │┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄│
  │ ┆ ┆ ┆ ┆ ┆ ┆ ┆ ┆ ┆    │
  │┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄│
  ╰──────────────────────╯
```

### timber_plank
```text
 ╱────────────────────────╱│
│  ───╮  ───────  ╭───── │ │
│ ────╯ ──(──)─── ╰───── │ │
│ ────────────  ──────── │ ╱
╰────────────────────────╯─╯
```

### transistor
```text
       ╭──────────╮
    ╭──╯ ░░░░░░░░ ╰──╮
   │  ░░ ────── ░░  │
   │  ░░ ────── ░░  │
    ╰──┬────┬────┬──╯
       │    │    │
       │    │    │
       │    │    ╰───────
       │    ╰────────────
       ╰─────────────────
```

### diode
```text
           ╭────────╮
           │ ░░░ ▓▓ │
───────────┤ ░░░ ▓▓ ├───────────
           │ ░░░ ▓▓ │
           ╰────────╯
```

### microcontroller_board
```text
  ╭──────────────────────╮
  │ o ──┬─────┬─── o o   │
  │ ╭───┴─────┴──╮ ───   │
  │ │ ▓▓▓▓▓▓▓▓▓▓ │ ╭──╮  │
  │ │ ▓▓▓▓▓▓▓▓▓▓ │ │██│  │
  │ ╰───┬─────┬──╯ ╰──╯  │
  │ o o ┴ ─── ┴ o o o    │
  ╰─┬─┬─┬─┬─┬─┬─┬─┬─┬────╯
    │ │ │ │ │ │ │ │ │
```

### desktop_motherboard
```text
 ╭──────────────────────────╮
 │ o ┌──────┐ ║║║║  ┌───┐ o │
 │   │░░░░░░│ ║║║║  │███│   │
 │ ──│░░░░░░│ ║║║║  └───┘ ─ │
 │   └──────┘ ║║║║  ┌─────┐ │
 │ ═══════════════  │▓▓▓▓▓│ │
 │ ═══════════════  │▓▓▓▓▓│ │
 │ o ── o ── o ───  └─────┘ │
 ╰─┬─┬─┬────────────────────╯
   ╵ ╵ ╵
```

### tiny_electric_motor
```text
       ╭──────────────╮
   ╭───┤ ░░░░░░░░░░░░ ├──╮
   │ o │ ░░░░░░░░░░░░ │ o├─────
   ╰───┤ ░░░░░░░░░░░░ ├──╯
       ╰──────────────╯
      ╱                ╲
```

### appliance_electric_motor
```text
     ╭─────────────────╮
 ╭───┤ ║║║║║║║║║║║║║║║ ├───╮
 │ o │ ║║║║║║║║║║║║║║║ │ o ├───
 │   │ ║║║║║║║║║║║║║║║ │   ├───
 ╰───┤ ║║║║║║║║║║║║║║║ ├───╯
     ╰──┬───────────┬──╯
    ╭───┴──╮     ╭──┴───╮
    ╰──────╯     ╰──────╯
```

### traction_electric_motor
```text
      ╭───────┬────────╮
 ╭────┤ ║║║║║ │ ║║║║║║ ├───╮
╭┤ o  │ ║║║║║ │ ║║║║║║ │ o ├╮
││    │ ║║║║║ │ ║║║║║║ │   │├───
││ o  │ ║║║║║ │ ║║║║║║ │ o │├───
╰┤    │ ║║║║║ │ ║║║║║║ │   ├╯
 ╰────┤ ║║║║║ │ ║║║║║║ ├───╯
      ╰──┬────┴─────┬───╯
     ╭───┴───╮  ╭───┴───╮
     ╰───────╯  ╰───────╯
```

### servo
```text
          ─────┬─────
        o──────┼──────o
          ─────┴─────
       ╭───────┴──────╮
   ╭───┤ ░░░░░░░░░░░░ ├───╮
   │ o │ ░░░░░░░░░░░░ │ o │
   ╰───┤ ░░░░░░░░░░░░ ├───╯
       ╰───────┬──────╯
               ╰──────────
```

### water_pump
```text
        ╭──────╮
        │  o   │
    ╭───┴──────┴──╮   ╭──────╮
  ╭─┤  ╭──────╮   ├───┤ ║║║║ ├─╮
  │ │  │  o   │   │   │ ║║║║ │ │
  ╰─┤  ╰──────╯   ├───┤ ║║║║ ├─╯
    ╰──────┬──────╯   ╰──┬───╯
       ╭───┴─────────────┴──╮
       ╰────────────────────╯
```
