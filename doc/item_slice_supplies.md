# Supplies slice: authoring and integration record

> Historical authoring/integration record: source paths, counts, and verification
> below describe that pass, not current ownership. See
> [current source organization](content_pack.md#source-organization) for semantic
> ownership.

Twenty new definitions, no upgrades. Coverage target and actual: eight packaged
foods/drinks; four medical/hygiene; three repair/shelter supplies; two empty
containers; three electrical supplies. Locations and progression below record
authoring intent; actual routes are in [the integration record](item_slice_integration.md).
All are civilian salvage, available
early if found; the charger and rechargeable cell support longer-term reuse.
Books, newspapers, and manuals are deferred pending skills.

| ID | Represented unit and role | Suggested location / progression | Mass g / exterior ml and rationale |
| --- | --- | --- | --- |
| canned_beans | one full steel can of beans, food | pantry / early | 450 / 450, contents plus can |
| dill_pickles | one full glass jar of pickles, food | kitchen / early | 900 / 1000, brine and glass |
| potato_chips | one sealed bag of chips, food | shop / early | 170 / 1500, bulky air-filled bag |
| chocolate_bar | one wrapped bar, food | shop / early | 100 / 120, compact wrapper |
| instant_noodles | one sealed noodle packet, food | pantry / early | 90 / 450, dry loose block |
| long_grain_rice | one sealed bag of dry rice, food | pantry / early | 1020 / 1300, one kilogram plus bag |
| peanut_butter | one full plastic jar, food | pantry / early | 540 / 550, spread plus jar |
| bottled_water | one full capped water bottle, drink | shop / early | 520 / 600, 500 ml water plus bottle |
| antiseptic | one capped bottle of topical antiseptic, medical | bathroom / early | 280 / 300, liquid and plastic |
| sterile_gauze | one sealed sterile gauze packet, dressing | clinic / early | 15 / 80, folded pad and wrapper |
| cloth_bandage | one nonsterile cloth strip, dressing | home / improvised | 20 / 50, half a cloth patch |
| bar_soap | one wrapped bar of soap, hygiene | bathroom / early | 120 / 150, soap and wrapper |
| sewing_supplies | one closed small sewing kit, repair | bedroom / early | 180 / 350, case and contents |
| nylon_rope | one coiled length of nylon rope, tying | garage / early | 900 / 1600, packed coil |
| camping_tarp | one folded synthetic tarp, shelter | camping store / early | 1200 / 2500, folded sheet |
| steel_water_bottle | one empty capped stainless bottle, storage | camping store / early | 300 / 1200, 1000 ml interior and exterior shell; glass_bottle is 200 / 600 |
| plastic_storage_box | one empty lidded rigid box, storage | garage / early | 300 / 2500, 2000 ml interior plus walls |
| aa_alkaline_battery | one AA alkaline cell, disposable power | shop / early | 25 / 10, demo disposable battery anchor |
| aa_rechargeable_battery | one AA rechargeable cell, reusable power | shop / sustained | 30 / 10, demo rechargeable battery anchor |
| battery_charger | one portable AA charger with internal source | electronics store / sustained | 500 / 500, demo field charger anchor |

The implemented `cut_cloth_bandage` recipe cuts one 40 g / 100 ml `cloth_patch`
with a retained cutting tool into two nonsterile `cloth_bandage` strips.
Cutting does not sterilize. Sealed packaging, food, hydration, antisepsis, healing,
sterility, cleaning, sewing, rope and shelter use are descriptive only. Empty
containers have ordinary item storage, not liquid-specific behavior. Default melee
is retained (4 HP blunt); no item is designed as a weapon. Electrical capabilities
match the demo; energy values are game units, not real-world units.

All masters below are complete monochrome grids. Review at tall-cell aspect:
wide packet and folded-sheet outlines compensate for tall cells; containers have
caps, shoulders or rims; paired cells and the charger have distinct contacts and
slots. Color is applied after these grids without layout changes. Integrated pack
validation and the coordinator's renderer review are complete; see
[verification](item_slice.md#verification).

Status for every row: definition authored in `data/items/slice_supplies.json5`;
external art authored at `data/art/items/<id>/art.txt`; recipe and loot integrated
and checked. The implemented recipe is the cloth-strip recipe above. A catalog
ID search found none of the 20 IDs in existing data at authoring time; the demo
battery counterparts are deliberately distinct IDs matching the slice plan. A
local script compared all 20 markup-stripped art grids with these masters, checked
referenced color names and ASCII glyphs, and found no mismatches. Subsequent AA
redraws use approved box/block glyphs and passed the same full master check.
Dominant dimensions range from 21 to 31 cells; the narrower bottles preserve
recognizable proportions. Content-loader and actual SDL checks are recorded in
the master verification record. No pilot definitions or artwork were changed.

## Monochrome masters

### canned_beans
```text
    ______________________
  /========================\
 |  ______________________  |
 | |                      | |
 | |    .--.    .--.      | |
 | |   ( BEANS )(    )    | |
 | |    '--'    '--'      | |
 | |______________________| |
 |                          |
  \========================/
    \____________________/
```

### dill_pickles
```text
       ______________
      /==============\
      |==============|
     /                \
    |  ______________  |
    | |  ( )  ( )    | |
    | | (   )(   )   | |
    | |  ( )  ( )    | |
    | |   PICKLES    | |
    | |______________| |
    |                  |
     \________________/
```

### potato_chips
```text
  /\/\/\/\/\/\/\/\/\/\/\/\
  |\                      /|
  | \    ____________    / |
  |  |   POTATO     |   |  |
  |  |    CHIPS     |   |  |
  |  |  (  ) (  )   |   |  |
  |  |   (  )       |   |  |
  | /|______________|\  |  |
  |/                  \ |  |
  \/\/\/\/\/\/\/\/\/\/\/\/
```

### chocolate_bar
```text
  ____________________________
 / / / / / / / / / / / / / /|
| / ______________________ / |
|  | CHOCOLATE            |  |
|  |  ____ ____ ____ ____ |  |
|  | |____|____|____|____||  |
|  | |____|____|____|____||  |
|  |______________________|  |
| / / / / / / / / / / / / / |
 \____________________________/
```

### instant_noodles
```text
  /\/\/\/\/\/\/\/\/\/\/\/\/\
 |                            |
 |     INSTANT NOODLES        |
 |       ____________         |
 |      / ~~~~~~~~~~ \        |
 |     | ~~~~~~~~~~~~ |       |
 |      \____________/        |
 |         \______/           |
 |                            |
  \/\/\/\/\/\/\/\/\/\/\/\/\/
```

### long_grain_rice
```text
    ______________________
   / / / / / / / / / / / /\
  /                        \
 |    LONG GRAIN RICE       |
 |    ________________      |
 |   |  -  -  -  -   |      |
 |   | -  -  -  -  - |      |
 |   |  -  -  -  -   |      |
 |   |________________|      |
  \                         /
   \_______________________/
```

### peanut_butter
```text
      ________________
     /================\
    |==================|
    |                  |
    |  ______________  |
    | |   PEANUT     | |
    | |   BUTTER     | |
    | |    (oo)      | |
    | |______________| |
    |                  |
     \________________/
```

### bottled_water
```text
        __________
       |==========|
       |__________|
       /          \
      /            \
     |  __________  |
     | |  WATER   | |
     | |  ~~~~~~  | |
     | |__________| |
     |  ~~~~~~~~~~  |
     |  ~~~~~~~~~~  |
     |______________|
```

### antiseptic
```text
        __________
       |==========|
       |__________|
      /            \
     /______________\
     |              |
     |  __________  |
     | |    +     | |
     | | ANTISEPTIC| |
     | |__________| |
     |              |
     \______________/
```

### sterile_gauze
```text
  ____________________________
 |::::::::::::::::::::::::::::|
 |:  ______________________  :|
 |: |  +                   | :|
 |: |  STERILE GAUZE       | :|
 |: |  ######## ########   | :|
 |: |  ######## ########   | :|
 |: |______________________| :|
 |::::::::::::::::::::::::::::|
 |____________________________|
```

### cloth_bandage
```text
      ______________________
    /  / / / / / / / / /  /|
   /______________________/ |
  |  # # # # # # # # #   |  |
  | # # # # # # # # # #  |  |
  |  # # # # # # # # #   |  /
  |______________________| /
    \____________________\/
```

### bar_soap
```text
     ______________________
   /                        \
  /   ____________________   \
 |   /                    \   |
 |  |        SOAP          |  |
 |  |     .  o  .          |  |
 |   \____________________/   |
  \                          /
   \________________________/
```

### sewing_supplies
```text
    ______________________
   /_____________________/|
  |  __________________  | |
  | |  OOO     /        | | |
  | |  OOO    /  . . .  | | |
  | |  OOO   /          | | |
  | |___________SEW_____| | /
  |______________________|/
         [______]
```

### nylon_rope
```text
       ________________
    .-'  ____________  '-.
   /  .-'  ________  '-.  \
  |  /  .-'        '-.  \  |
  | |  |   ________   |  | |
  | |  |  (________)  |  | |
  |  \  '-.________.-'  /  |
   \  '-.____________.-'  /
    '-.________________.-'---
                          ===
```

### camping_tarp
```text
      ______________________
    /  o                 o /|
   /______________________/ |
  |  ____________________ | |
  | |                    || |
  | |   FOLDED TARP      || |
  | |____________________|| /
  |__o_________________o__|/
     ==================
```

### steel_water_bottle
```text
       ____________
      |============|
      |____________|
      /            \
     /              \
    |  |             |
    |  |             |
    |  |             |
    |  |             |
    |  |             |
    |                |
    |                |
     \______________/
```

### plastic_storage_box
```text
      ______________________
     /_____________________/|
    /_____________________/ |
   |  [ ]             [ ] | |
   |  __________________  | |
   | |                  | | |
   | |                  | | /
   | |__________________| |/
   |______________________|
```

### aa_alkaline_battery
```text
    ┌──┐
    │+ │
 ╭──┴──┴──╮
╭┴────────┴╮
│▐████████▒│
│▐████████▒│
├──────────┤
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐   AA   ▒│
│▐░░░░░░░░▒│
│▐ ALKALI ▒│
│▐  1.5V  ▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
├──────────┤
│▐████████▒│
╰┬────────┬╯
 ╰────────╯
```
Review correction: 12×24 narrow upright cylinder with protruding positive nub,
rolled metal rims, copper shoulder and gold wrapper with a shaded base. At 10×19
pixels per cell its physical height is approximately four times its diameter.

### aa_rechargeable_battery
```text
    ┌──┐
    │+ │
 ╭──┴──┴──╮
╭┴────────┴╮
│▐████████▒│
│▐████████▒│
├──────────┤
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐   AA   ▒│
│▐░░░░░░░░▒│
│▐  NiMH  ▒│
│▐  1.2V  ▒│
│▐░░░░░░░░▒│
│▐  ──>   ▒│
│▐   <──  ▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
│▐░░░░░░░░▒│
├──────────┤
│▐████████▒│
╰┬────────┬╯
 ╰────────╯
```
Review correction: matching 12×24 AA cylinder, with silver shoulders and a green
wrapper, NiMH label and return arrows distinguishing the rechargeable cell.
Corrected masters precede color; original renderer contact sheet inspected,
coordinator rerender completed and both revised previews inspected.
Ad-hoc correction check: both decoded colored grids match their masters exactly,
including spaces and the escaped literal `<` in the return arrow. Glyph allowlist,
local palettes/defaults, markup, 32×32 bounds, 24–32 dominant dimension and file
byte limits pass. No build or CTest was run.

### battery_charger
```text
      ______________________
     /_____________________/|
    |  AA CHARGER          | |
    |  ________________    | |
    | | +            - |   | |
    | |________________|   | |
    |       ( o )          | /
    |______________________|/
          \________/
```
