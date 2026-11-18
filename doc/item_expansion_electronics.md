# Electronics expansion batch

Scope: 20 expansion items reviewed in place, 18 component/assembly units and two household donors.
All are placed as early-to-mid pre-collapse salvage in electronics, workshop,
office and household contexts. `toaster`
is the sole deliberate humorous description. A unit is one complete part or
device, except the cable, which is one short complete cable assembly.

## Tracker and integration contracts

All rows: expansion definition, reviewed complete RGB art and monochrome master,
early/mid salvage, with loot and manifest integration complete. E = electronics shop
or repair bench, O = office, H = home, K = kitchen. Both retained tools are
currently in household loot; `tiny_electric_motor`, `diode`, `transistor`,
`copper_wire`, `steel_scrap`, and `rigid_plastic_scrap` are existing slice
items. The latter three units weigh 200, 100 and 100 g respectively. No other
expansion batch inputs are required. All definitions use incidental default
melee; there is no combat tuning or equipment advantage.

| ID | Unit / role | g / ml | Loot context | Recycler / acquisition |
| --- | --- | ---: | --- | --- |
| resistor | one axial component | 1 / 2 | E, O | display, sensor, PSU, radio |
| capacitor | one electrolytic component | 5 / 8 | E, O | display, sensor, PSU, radio |
| light_emitting_diode | one indicator LED, distinct from axial rectifier | 1 / 2 | E, O | display |
| toggle_switch | one panel switch | 20 / 30 | E, H | toaster, radio |
| power_relay | one boxed relay | 35 / 45 | E | electrical parts loot |
| cartridge_fuse | one glass fuse | 5 / 8 | E, H | PSU |
| electrical_connector | one multipin connector | 15 / 25 | E, O | display, PSU, radio |
| insulated_power_cable | one short complete cable | 350 / 500 | E, O, H | toaster; strip for copper |
| transformer | one iron-core transformer | 600 / 500 | E, O | PSU; strip for copper and steel |
| induction_coil | one substantial bench-equipment coil | 250 / 300 | E | electrical parts loot; strip for copper |
| heating_element | one framed appliance element | 250 / 450 | E, K | toaster |
| processor_chip | one packaged processor | 20 / 20 | E, O | computer parts loot |
| memory_module | one desktop memory board | 30 / 45 | E, O | computer parts loot |
| solid_state_drive | one cased drive | 80 / 120 | E, O | computer parts loot |
| computer_power_supply | one older industrial-desktop PSU | 1500 / 2500 | E, O | dismantle PSU |
| cooling_fan | one framed fan | 120 / 350 | E, O | PSU; dismantle fan |
| display_module | one display assembly | 150 / 250 | E, O | dismantle display |
| distance_sensor | one ultrasonic ranging board | 30 / 60 | E | dismantle sensor |
| toaster | one two-slot appliance | 1600 / 5500 | K | dismantle toaster |
| desk_radio | one radio | 900 / 2500 | H, O | dismantle radio |

Masses and external volumes describe whole units; the existing 1 g / 3 ml
rectifier, 2 g / 5 ml transistor, 20 g / 30 ml tiny motor and 800 g / 2500 ml
motherboard anchor component scale. Donor volumes need capacity-safe placement.
The 200 g copper return is reserved for substantial windings or cable, not tiny
boards. Nine explicit unique recyclers use retained pliers and/or screwdriver;
none require an unsupported powered soldering iron. Returns are bounded by
target mass and each uses at most eight distinct types:

| Recycler | Target g | Return g |
| --- | ---: | ---: |
| strip_insulated_power_cable | 350 | 200 |
| strip_transformer | 600 | 400 |
| strip_induction_coil | 250 | 200 |
| dismantle_cooling_fan | 120 | 20 |
| dismantle_display_module | 150 | 23 |
| dismantle_distance_sensor | 30 | 8 |
| dismantle_computer_power_supply | 1500 | 1055 |
| dismantle_toaster | 1600 | 1270 |
| dismantle_desk_radio | 900 | 260 |

Final recipe count: **zero crafts and nine explicit recyclers**. The radio no
longer returns the 250 g induction coil: a small radio winding is not the
substantial bench-equipment winding that yields a whole 200 g bare-copper unit.
That coil remains electronics-shop salvage with its own recycler. Radio casing
returns remain rigid plastic; no fabric is turned into cotton. The heavy PSU is
explicitly an older industrial-desktop unit with an iron-core transformer, rather
than suggesting that every lightweight switching PSU contains this 600 g part.
The toaster returns two framed elements, one complete cable, four steel-scrap
units and one switch: 1270 g, leaving 330 g of unrepresented debris. The radio
returns four resistors, three capacitors, two diodes, two transistors, one switch,
two plastic-scrap units and one connector: 260 g, leaving 640 g of speaker,
board, fittings and unrecoverable material. Its tuning and speaker windings do
not round up to a copper coil. Nested dismantling remains mass-bounded: the PSU
ends at 755 g after its fan and transformer are stripped; the toaster at 1120 g
after cable stripping. Each dismantled target is consumed once.

Flags follow exposed constituents: component leads, module contacts, fuse caps,
switch hardware, plug contacts, metal cases and the radio antenna qualify for
conduction. Insulated internal wiring alone does not. The plastic-framed fan has
no claimed exposed conductor. The LED lens and glass fuse are transparent; the
backed display assembly is opaque. Continuous exposed metal cases/frames carry
thermal flags; small leads alone do not justify applying every metal property
to a mixed assembly. Substantial exposed plastics/board laminates are marked
combustible where applicable. No new flag vocabulary is required.

Descriptions now give the parts a former home (parts tape, lab inventory sticker,
maintenance dates, repair-shop labels and kitchen crumbs). Only `toaster` is
deliberately humorous. Electrical conduction, sensing, computing, radio, display,
lighting, heating and appliance operation remain metadata-only, with no authored
runtime capabilities. No recipes consume other approved-batch items.

Components use `loot_electrical_parts` and computer assemblies use
`loot_computer_parts`, nested in electronics and office contexts. The toaster
uses ground-only `loot_kitchen_appliance_ground`; the radio appears in household
and urban pools. This keeps the 5500 ml toaster out of shared furniture pools.

## Art and validation review

All 20 complete grids and colored assets were source-reviewed with the 1:2 cell
aspect in mind. The capacitor, LED and switch no longer use long repeated rows
to reach the size target; the fan has a near-square physical frame. Board, packet,
sensor and case edges were realigned in the masters before matching colorization.
All illustrations have 24–32-cell dominant grid dimensions. Local palettes use
restrained upper-left highlights and lower/right shadows: resin and visible
internal metal, sleeve/vent/stripe, tinned leads, brass contacts, solder mask,
black packages, copper lacquer, steel laminations, unlit glass, plastic blades,
enamel and speaker grille each have distinct roles. The simple winding uses four
colors; more complex assemblies use six to nine. No colored glow implies power.

Static verification completed for all 20 unchanged mass/volume contracts against
the pre-review table, canonical art references and shared flag references/domains.
The audit compares every decoded colored glyph and space against its complete
master, checks the single-cell allowlist, RGB declarations/references, dimensions
and file limits. All nine recycler records were checked for mass totals above,
current referenced inputs, count bounds, distinct types, retained tools and unique
targets across current crafting sources. The PSU is the eight-return-type boundary;
the revised radio has seven. These were one-off author-side static data checks,
not an engine loader run. The subsequent native loading and frontend review are
recorded in [verification and art review](item_expansion.md#verification-and-art-review).

## Monochrome masters

These grids are the approved layout for the external RGB illustrations. Unequal
line lengths and leading spaces are intentional; terminal cells are tall.

### resistor
```text
       ╭──────────╮
───────┤ ││ │ ││  ├───────
       ╰──────────╯
```
### capacitor
```text
   ╭──────────────────────╮
   │ ╭──────────────────╮ │
   │ │        +         │ │
   │ ╰──────────────────╯ │
   │  ─ ─ ─ ─ ─ ─    ┃┃   │
   │  ─ ─ ─ ─ ─ ─    ┃┃   │
   ╰─────┬────────┬───────╯
         │        │
         │        │
         │        │
         │        │
```
### light_emitting_diode
```text
        ╭──────────╮
     ╭──╯          ╰──╮
    ╱   │              ╲
   │    │   ╭────╮      │
   │        │    │      │
   │        ╰─┬──╯      │
  ╭┴──────────┼─────────┴╮
  ╰──────┬────┴───┬──────╯
         │        │
         │        │
         │        │
                  │
```
### toggle_switch
```text
                 ╭──╮
                ╱  ╱
               ╱  ╱
       ╭──────╱──╱───────╮
    ╔══╧════════════════╧══╗
    ║       SWITCH         ║
    ╚════╤══════╤══════╤═══╝
         │      │      │
         o      o      o
```
### power_relay
```text
     ╭──────────────────╮
    ╱  RELAY  ──┐      ╱ │
   ╱            └─    ╱  │
  ╔═══════════════════╗  │
  ║                   ║  │
  ║    12V  DC        ║  │
  ║                   ║  ╱
  ╚═══════════════════╝ ╱
    ││    ││    ││   ││
    ││    ││    ││   ││
```
### cartridge_fuse
```text
   ╔══╦════════════════╦══╗
───╢  ║ ────────────── ║  ╟───
   ╚══╩════════════════╩══╝
```
### electrical_connector
```text
      ╔════════════════╗
  ════╣  o  o  o  o   ╠════
  ════╣  o  o  o  o   ╠════
      ╚══════╤╤════════╝
             └┘
```
### insulated_power_cable
```text
  ╔══╗                   ╔══╗
  ║::╠══╮           ╭════╣::║
  ╚══╝  ╰──╮     ╭──╯    ╚══╝
           │ ╭───╯
           ╰─╯
```
### transformer
```text
      ╔════════════════╗
   ╔══╣ (((((  )))))   ╠══╗
   ║  ║ (((((  )))))   ║  ║
   ║  ║ (((((  )))))   ║  ║
   ╚══╣ (((((  )))))   ╠══╝
      ╚══╤════════╤════╝
         ││      ││
```
### induction_coil
```text
   ───╭─╮╭─╮╭─╮╭─╮╭─╮╭─╮───
      │ ││ ││ ││ ││ ││ │
      ╰─╯╰─╯╰─╯╰─╯╰─╯╰─╯
```
### heating_element
```text
 ╔════════════════════════╗
 ║  ╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱╲  ║
 ║  ╲╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱╲╱  ║
 ╚════════════════════════╝
    ││                ││
```
### processor_chip
```text
    │ │ │ │ │ │ │ │ │ │
  ╔═╧═╧═╧═╧═╧═╧═╧═╧═╧═╧══╗
──╢  ╭────────────────╮  ╟──
──╢  │   PROCESSOR    │  ╟──
──╢  │     CPU        │  ╟──
──╢  ╰────────────────╯  ╟──
  ╚═╤═╤═╤═╤═╤═╤═╤═╤═╤═╤══╝
    │ │ │ │ │ │ │ │ │ │
```
### memory_module
```text
  ╔══════════════════════════╗
  ║ ███  ███  ███  ███       ║
  ║ ███  ███  ███  ███       ║
  ╚╤╤╤╤╤╤╤╤╤╤══╤╤╤╤╤╤╤╤╤╤════╝
```
### solid_state_drive
```text
   ╔══════════════════════╗
   ║  SOLID STATE DRIVE   ║
   ║  ────────────────    ║
   ║  ▓▓▓▓▓▓▓▓▓▓▓▓▓       ║
   ║                      ║
   ╚══════════════╤╤╤╤════╝
```
### computer_power_supply
```text
    ╔══════════════════════╗
   ╱║  ╭──────╮  ─ ─ ─    ╱║
  ╔═╬═│ ╲│╱  │══════════╗ ║
  ║ ║ │ ─o─  │  POWER   ║ ║
  ║ ║ │ ╱│╲  │  SUPPLY  ║ ║
  ║ ║  ╰──────╯         ║ ║
  ║ ╚═══════════════════╬═╝
  ╚═════════════════════╝
```
### cooling_fan
```text
  ╔════════════════════════╗
  ║ o  ╭────────────╮   o  ║
  ║   ╱    ╲  ╱      ╲     ║
  ║  │      ╲╱        │    ║
  ║  │  ───╭──╮─────  │    ║
  ║  │     │oo│       │    ║
  ║  │  ───╰──╯─────  │    ║
  ║  │      ╱╲        │    ║
  ║   ╲    ╱  ╲      ╱     ║
  ║ o  ╰────────────╯   o  ║
  ╚════════════════════════╝
```
### display_module
```text
  ╔════════════════════════╗
  ║ ┌────────────────────┐ ║
  ║ │                    │ ║
  ║ │       ░░░░░        │ ║
  ║ │                    │ ║
  ║ └────────────────────┘ ║
  ╚════╤╤╤╤╤╤╤╤════════════╝
```
### distance_sensor
```text
   ╔══════════════════════╗
   ║  ╭────╮    ╭────╮    ║
   ║  │ o  │    │ o  │    ║
   ║  ╰────╯    ╰────╯    ║
   ║  . . .   ─────       ║
   ╚════╤╤╤╤══════════════╝
```
### toaster
```text
        ╭──────────────╮
     ╭──┴──────────────┴──╮
    ╱  ─────    ─────     ╱│
   ╔═════════════════════╗ │
   ║                     ║ ├─┐
   ║      TOAST          ║ │ │
   ║                     ║ │ ┘
   ║    o                ║ ╱
   ╚═════════════════════╝╱
     ▀▀               ▀▀
```
### desk_radio
```text
                   ╱
                  ╱
   ╔═════════════╱════════╗
   ║  ┌──────────────┐    ║
   ║  │ 88  100  108 │  o ║
   ║  └──────────────┘    ║
   ║  ▒▒▒▒▒▒▒▒▒▒▒▒▒▒   o  ║
   ║  ▒▒▒▒▒▒▒▒▒▒▒▒▒▒      ║
   ╚══════════════════════╝
```
