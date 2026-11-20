# Equipment slice: authoring record

> Historical authoring record: source paths, counts, and verification below
> describe that pass, not current ownership. See
> [current source organization](content_pack.md#source-organization) for semantic
> ownership.

20 new items, no upgrades. Targets and actuals: 8 practical tools, 5 wearable
articles, 4 emergency/combat items, 1 improvised weapon, 1 repair supply,
1 container. Workshop, household, emergency and outdoor salvage are represented.
Every item has external art. Pack inclusion, actual loot routes and recipes are
documented in [the integration handoff](item_slice_integration.md); the location
column below records the original authoring intent.

| ID | Unit; mass / volume | Role; proposed location; progression |
| --- | --- | --- |
| adjustable_wrench | one; 500 g / 400 ml | adjustable fastener tool; garage; civilian salvage |
| combination_pliers | one; 300 g / 250 ml | gripping/cutting pliers; workshop; civilian salvage |
| hacksaw | one; 550 g / 1400 ml | cutting tool; garage; civilian salvage |
| flathead_screwdriver | one; 120 g / 120 ml | screw tool; house toolbox; civilian salvage |
| soldering_iron | one corded iron; 250 g / 400 ml | electronics bench tool; workshop; advanced salvage |
| metal_file | one; 250 g / 250 ml | abrasive shaping; workshop; civilian salvage |
| chef_knife | one; 250 g / 250 ml | kitchen cutting; kitchen; civilian salvage |
| pry_bar | one; 1200 g / 800 ml | leverage tool; garage; civilian salvage |
| cotton_tshirt | one; 180 g / 800 ml | garment; bedroom; early salvage |
| denim_jeans | one pair; 650 g / 1800 ml | garment; bedroom; early salvage |
| rain_jacket | one; 500 g / 1600 ml | outerwear; entryway; civilian salvage |
| leather_work_gloves | one pair; 180 g / 400 ml | workwear; garage; civilian salvage |
| safety_goggles | one pair; 100 g / 300 ml | eye wear; workshop; civilian salvage |
| fire_axe | one; 3000 g / 3500 ml | rescue cutting tool; fire station; rare salvage |
| police_baton | one; 700 g / 600 ml | close combat; police station; uncommon salvage |
| pump_shotgun | one; 3500 g / 4500 ml | firearm; police station; uncommon salvage |
| shotgun_shells | one box of 25 shells; 1100 g / 1000 ml | ammunition; gun storage; uncommon salvage |
| coil_spear | one; 2400 g / 3500 ml | improvised piercing weapon; crafted; later improvised |
| cloth_repair_tape | one 300 g roll; 400 ml | repair supply; garage; civilian salvage |
| camping_backpack | one empty pack; 900 g / 2000 ml | 20000 ml storage; outdoor shop; civilian salvage |

The backpack's exterior is the fixed packed-volume abstraction, not an expanding
physical envelope: contents do not change its external volume. The shirt is lighter
and smaller than the 300 g / 1200 ml work-shirt pilot. The iron is an ordinary
corded object with no power or crafting behavior. Clothing, eye protection, weather
resistance, firearms, ammunition and tape use are descriptive only.

The axe does 17 HP slash / 170 ticks / two hands: slower than the 20 HP / 150
two-handed pipe; its advantage is cutting type. The pry bar does 10 HP blunt /
125 / one hand, less than the 12 HP / 110 hammer. Baton: 9 HP blunt / 100 /
one hand, fast but less damaging. Shotgun butt: 6 HP blunt / 130 / two hands,
not a ranged attack. Spear: 13 HP piercing / 150 / two hands, lower than the
18 HP / 130 one-handed sword. Knife is 8 HP slash / 100 / one hand; other
unprofiled objects retain the ordinary 4 HP blunt default. Only the hacksaw and
chef knife provide house_cutting tier 1; none is a credible hammer substitute.

The integrated spear recipe consumes one 2000 g timber_plank, one 100 g
steel_scrap unit, one 200 g copper_wire unit and one 300 g cloth_repair_tape
roll: 2600 g input for a 2400 g spear, allowing 200 g offcuts and waste.
It requires retained hacksaw and combination_pliers, house_cutting tier 1, and
1200 work; it does not imply electrical damage. Sewing supplies plus cloth_patch
could later repair garments when repair exists. See the integration handoff for
the implemented acquisition and other recipes, and their verification status.

## Monochrome masters and silhouette review

The complete grids below are the approved glyph masters; lines are deliberately
shorter vertically because terminal cells are tall. The work-shirt pilot uses a
roughly 32-column by 16-row silhouette; the T-shirt and jacket follow that scale,
with a distinct neck and open front. Tool orientation follows each working end;
the revised wrench, pliers, axe, baton and spear use taller profiles. Glasses have paired
lenses, the shotgun has stock, receiver and barrel, and the shell box exposes
individual round ends. Color must preserve these rows exactly.

### adjustable_wrench
```text
 ╭──╮
 │█▌│     ╭─╮
 │█▌│     │▐│
 │█▌╰─────╯▐│
 │█▌       ▐│
 ╰╮▌ ┌═══┐ ▐│
  │▌ │===│ ▐│
  ╰╮ └═══┘ ╭╯
   ╰╮ ▐▌ ╭╯
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ▐▌ │
    │ ╭╮ │
    │ ││ │
    │ ╰╯ │
    ╰────╯
```
Review correction: 13×24, with the jaw opening reaching the top background;
the lower movable jaw and bright knurled adjustment screw are separate from the
fixed jaw. The slim shank ends in a hanging hole rather than a filled grip block.
### combination_pliers
```text
     ╭──┬──╮
     │▐ │ ▌│
     │▐ │ ▌│
     │▐ │ ▌│
    ╭╯╲ │╱ ╰╮
    │▐ ╲╱ ▌│
    │▐ ╱╲ ▌│
    ╰╮  o ╭╯
     ╰╮  ╭╯
     ╱╱╲╲
    ╱╱  ╲╲
   ╱╱    ╲╲
  ╭─╮    ╭─╮
  │▒│    │▒│
  │▒│    │▒│
  │▒│    │▒│
  │▒│    │▒│
  │▒│    │▒│
  │▒│    │▒│
  │▒│    │▒│
  │▒│    │▒│
  │▒│    │▒│
  ╰─╯    ╰─╯
   ▀      ▀
```
Review correction: upright paired serrated jaws above a visible pivot; two
diverging steel tangs lead to independent red grips with background between them.
### hacksaw
```text
   ┌───────────────────────┐
  /                         \
 │   ┌───────────────────┐  │
 │   │                   │  │
 └───┴─vvvvvvvvvvvvvvvvvv┴──┤
                         ╔═══╗
                         ║▒▒▒║
                         ╚═══╝
```
### flathead_screwdriver
```text
 ┌────────────┐
 │ ▒▒▒▒▒▒▒▒▒▒ ├────────────────┐
 │ ▒▒▒▒▒▒▒▒▒▒ ├────────────────┘
 └────────────┘
```
### soldering_iron
```text
  ╭─────────────────╮
  │  cord  ──────╮  │
  ╰──────────────┼──╯
 ┌────────────┐  │
 │ ▒▒▒▒▒▒▒▒▒▒ ├──┴───────────>
 └────────────┘
```
### metal_file
```text
                  ┌──────────┐
 ┌────────────────┤ ▒▒▒▒▒▒▒▒ │
 │////////////////├──────────┘
 └────────────────┘
```
### chef_knife
```text
    ┌───────────────┐
   /                ├─────────┐
  /                 │ ▒ o ▒ o │
 /__________________├─────────┘
```
### pry_bar
```text
 ┌─┐
 │ │  ┌──────────────────────┐
 │ └──┘  ════════════════════╧─┐
 └─────────────────────────────┘
```
### cotton_tshirt
```text
         ┌───┐┌───┐
    ┌────┘   └┘   └────┐
   / ░░░░░░░░░░░░░░░░░░ \
  / ░░░░░░░░░░░░░░░░░░░░ \
 └─────┐░░░░░░░░░░┌─────┘
       │░░░░░░░░░░│
       │░░░░░░░░░░│
       │░░░░░░░░░░│
       │░░░░░░░░░░│
       │░░░░░░░░░░│
       │░░░░░░░░░░│
       └──────────┘
```
### denim_jeans
```text
    ┌───────────────────┐
    │ ─────┬──┬──────── │
    │ ╲    │ o│     ╱   │
    │  ╲   │  │    ╱    │
    │      └──┘         │
    │ ░░░░░░ │ │░░░░░░░ │
    │ ░░░░░░ │ │░░░░░░░ │
    │ ░░░░░░ │ │░░░░░░░ │
    │ ░░░░░░ │ │░░░░░░░ │
    │ ░░░░░░ │ │░░░░░░░ │
    │ ░░░░░░ │ │░░░░░░░ │
    └────────┘ └────────┘
```
### rain_jacket
```text
          ╭──────╮
       ╭──┤  ╭╮  ├──╮
     ╭─┘  ╰──┴┴──╯  └─╮
    / ░░░░░░░│░░░░░░░░ \
   / ░░░░░░░░│░░░░░░░░░ \
  / ░░░░░░░░░│░░░░░░░░░░ \
 └──────┐░░░░│░░░░┌──────┘
        │ ┌──│──┐ │
        │ │  │  │ │
        │ └──│──┘ │
        │ ░░░│░░░ │
        └────┴────┘
```
### leather_work_gloves
```text
   ╭─╮╭─╮╭─╮      ╭─╮╭─╮╭─╮
  ╭┤ ││ ││ ├╮    ╭┤ ││ ││ ├╮
  ││ ││ ││ ││    ││ ││ ││ ││
 ╭┤         │    │         ├╮
 │ ╲ ▒▒▒▒▒  │    │  ▒▒▒▒▒ ╱ │
 ╰──┤▒▒▒▒▒├─╯    ╰─┤▒▒▒▒▒├──╯
    └─────┘        └─────┘
```
### safety_goggles
```text
  ╭──────────╮  ╭──────────╮
 ─┤ ╭──────╮ ├──┤ ╭──────╮ ├─
  │ │  ╱   │ │  │ │  ╱   │ │
  │ │ ╱    │ │  │ │ ╱    │ │
  ╰─┴──────┴─╯  ╰─┴──────┴─╯
```
### fire_axe
```text
    ╭──────┬───╮
   ╱▐██▒▒▒▒│ o ├──╮
  ╱▐██▒▒▒▒▒│   │▒▒╲
  │▐██▒▒▒▒▒│   ├───>
  │▐██▒▒▒▒▒├─┬─╯
  ╲▐██▒▒▒▒╱│▐│
   ╰─────╯ │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
           │▐│
          ╭╯▐│
          │▒▒│
          │▒▒│
          │▒▒│
          │▒▒│
          │▒▒│
          ╰──╯
```
Review correction: 28 rows give the rescue axe a full-length haft, with a broad
beveled cutting edge, small rear pick, head rivet and short contrasting end grip.
### police_baton
```text
 ╭─╮
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
 │▌│
╭┴─┴╮
╰┬─┬╯
 │=│
 │=│
 │=│
 │=│
 │=│
 │=│
 ╰─╯
```
Review correction: 5×26 straight narrow shaft, small grip stop and six-row ribbed
handle. Slate highlights keep the dark polymer visible against the browser.
### pump_shotgun
```text
          ┌───────═════════════╡
     ╭────┤ ━ o  ├─┬=====┬────┘
 ╭───╯▒▒▒▒╰──┬───╯ ╰─────╯
 │▒▒▒▒▒▒╱    ╰o╯
 ╰─────╯
```
Review correction: 32×5 side view, muzzle right. A long thin barrel and lower
magazine tube lead to a ribbed wood pump; the receiver stays low, with a trigger
guard and dropped wood stock instead of a raised scope-like box.
### shotgun_shells
```text
     ┌──────────────────────┐
    / o  o  o  o  o        /│
   / o  o  o  o  o        / │
  ┌──────────────────────┐  │
  │ ════════════════════ │  │
  │ ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ │  │
  │ ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ │ /
  └──────────────────────┘
```
### coil_spear
```text
    /\
   /▐ \
  / ▐  \
 /  ▐   \
 \  ▐   /
  \ ▐  /
   \▐ /
    ║║
   ╭╫╫╮
   ╰╫╫╯
   ╭╫╫╮
   ╰╫╫╯
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    │▌
    ╰╯
```
Review correction: 9×30 upright spear, a single pointed scrap-steel blade above
two copper binding turns and a deliberately slender uninterrupted wooden shaft.
No powered component is implied. Corrected masters precede color; coordinator
rerender is complete; the coordinator inspected all six revised wide previews
and the compact axe preview after the original contact-sheet inspection.
Ad-hoc correction check: all six decoded colored grids match these masters
exactly, including spaces; glyph allowlist, local palettes/defaults, markup,
32×32 bounds, 24–32 dominant dimension and file byte limits pass. The first
check caught a 33-column shotgun barrel; its master and colored row were
shortened to 32 before the successful final check. No build or CTest was run.
### cloth_repair_tape
```text
        ╭──────────────╮
     ╭──┤ ▒▒▒▒▒▒▒▒▒▒▒▒ ├──╮
    / ▒ │  ╭────────╮  │ ▒ \
   │ ▒▒ │  │        │  │ ▒▒ │
   │ ▒▒ │  ╰────────╯  │ ▒▒ │
    \ ▒ └──────────────┘ ▒ /
     ╰────────────────────╯
```
### camping_backpack
```text
         ╭──────────╮
      ╭──┴──────────┴──╮
     ╱  ╭────────────╮  ╲
    │ ╭─┤ ░░░░░░░░░░ ├─╮ │
    │ │ │ ░░░░░░░░░░ │ │ │
    │ │ ├────────────┤ │ │
    │ │ │ ╭────────╮ │ │ │
    │ │ │ │ ▒▒▒▒▒▒ │ │ │ │
    │ │ │ │ ▒▒▒▒▒▒ │ │ │ │
    │ │ │ ╰────────╯ │ │ │
    │ ╰─┤ ░░░░░░░░░░ ├─╯ │
     ╲  ╰────────────╯  ╱
      ╰────────────────╯
```
