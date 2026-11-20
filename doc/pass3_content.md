# Pass 3: digital fabrication content

This records the implemented roster and functional verification for the accepted
[home-printing slice](home_printing_design.md). Seeded balance playtesting remains.
Targets: seven printer-related identities and three found information devices;
six print designs, three manual consumers, four recyclers, and four printer
acquisition contexts. All ten identities are new; no existing item is upgraded.
Atmospheric exceptions: none. Humorous exceptions: none.
All ten identities are loaded by both principal packs and covered by the shared
SDL item-browser rendering checks.

| Item | Unit and role | Acquisition / progression | Art |
| --- | --- | --- | --- |
| `home_polymer_printer` | One 8 kg desktop machine and controller | Placed in electronics store, school and selected duplex hobby basement; optional packed workshop-cellar find | `data/art/items/home_polymer_printer/art.txt` |
| `polymer_filament` | One 10 g portion of prepared feedstock | Dedicated large and small ground draws in the four contexts | `data/art/items/polymer_filament/art.txt` |
| `plastic_cup` | One 40 g, 300 ml container | Computers 0 print | `data/art/items/plastic_cup/art.txt` |
| `small_parts_case` | One 120 g, 600 ml container | Computers 1 print | `data/art/items/small_parts_case/art.txt` |
| `plastic_buckle_set` | Four buckles, one consumed set | Computers 1 print; duffel alternative | `data/art/items/plastic_buckle_set/art.txt` |
| `printed_machete_grip` | One pair of fitted scales | Computers 1 print; machete alternative | `data/art/items/printed_machete_grip/art.txt` |
| `printed_electronics_case` | One empty fitted shell | Computers 2 print; battery alternative | `data/art/items/printed_electronics_case/art.txt` |
| `usb_data_stick` | One empty portable data carrier, 10 g / 15 ml | Office, school, electronics | `data/art/items/usb_data_stick/art.txt` |
| `phone` | One reader with a 1,000-energy rechargeable cell, 180 g / 150 ml | Residential, electronics; supplied workshop primer | `data/art/items/phone/art.txt` |
| `legacy_laptop` | One reader with a 4,000-energy rechargeable cell, 1,800 g / 2,500 ml | Less-common office, electronics and hobby find; supplied salvage notes | `data/art/items/legacy_laptop/art.txt` |

The supplied designs also produce the existing `plastic_storage_box`. The
alternative consumers retain the existing `canvas_duffel`, `salvaged_machete`
and `workshop_battery_pack` identities and existing recyclers. The four new
recyclers return only ordinary salvage, never filament. The software and
designs are separate supplied packages on the packed controller. The two
reader packages reference existing BookIds, not new learning benefits.
The phone and laptop have a single explicitly full rechargeable cell each;
the USB has no initial package. Devices and the printer are found, not crafted.

The two principal manifests include all five new source files. Printer/filament
draws remain separate, and bulky equipment is ground-only. Placed sites use
electronics store `(6,2,0)`, school `(5,1,0)`, and duplex hobby basement
`(4,2,-1)`; the furnished house workshop basement uses `(3,2,-1)` for the
independent 30% packed-printer opportunity and filament. Coordinates are
unrotated authored pattern coordinates. Generation remains version 7.

For integration, recommend a named **Pass3-0-13-42 panel**: seeds 0, 13 and 42,
Town region `(0,0)` plus FirstHouse seed 0. Record actual selected building
origins, rotations, basement variants, approach paths, counts, and save/reload
results; a site is not guaranteed in each seed. Existing urban documentation
reports seed 42 region `(0,0)` as a Crossroads town, not a guarantee of these
particular buildings. No panel was run for this authoring pass.

## Functional evidence — 2026-11-20

- Debug and SDL presets build. The full Debug CTest run reported 1,514 passed,
  10 failed and two environment-dependent skips out of 1,526. All ten failures
  passed targeted reruns after correcting stale catalog counts, protocol offsets,
  prerequisite reachability, book expectations and legacy-save fixture shapes.
  Four subsequently added unattended-completion/destruction and real-LAN cases
  also pass. These are results across runs, not a second full-suite run.
- The final affected Debug selection (digital/information, inventory and wire/
  protocol checks, including the two real-LAN cases) passed **81/81**.
- The 74 new digital/content/information/machine cases cover actual reservations,
  quantities/capacity, paid setup and Stop/Resume, helper ownership, controller/USB
  copying, non-draining charge gates, loss of access, book identity, explicit empty
  libraries, malformed saves, cross-chunk worker conflicts and protocol 28.
- The shipped ten-cup test consumes **50 original filament portions and exactly
  12,000 EU**, after 400 setup work, and produces ten cups. Split batches preserve
  additive completion learning. Current boosts and advanced 80% practice are tested.
- Live, parked, resident, archived and destroyed initiators are covered. An
  exclusive database lock preserves pending completion and originals; save/reload
  and retry publish products and XP once without further electricity or work.
- An unattended selectable machine holds its workshop, completes, releases its
  hold and survives save/reload. Destruction drops the original inputs/equipment
  without output or reward.
- Real ENet host/client sessions with shipped content verify commissioning,
  paid setup Stop/Resume, processing, output collection and initiator XP, plus
  controller-to-USB copying, duplicate no-ops and deletion. Decoded snapshots
  retain their own state. Devices with mutable libraries remain separate inventory
  entries; wire validation rejects forged stackability.
- `tests/printer_pack_tests.cpp` checks both packs, associations, budgets,
  conservation, dedicated loot and unrotated approaches.
- The 115-case SDL/LAN selection initially reported 113 passed, one failed and
  one environment-dependent skip. The corrected item-browser renderer passed its
  rerun; the final renderer/content-budget selection passed **2/2**. All ten arts
  render at 480×360 and 1120×900. Actual renderer
  previews and a reviewed contact sheet remain local under `out/pass3-art_*` and
  `out/pass3-sdl-art-sheet.png`; no monochrome masters were retained.

Local verification logs: `out/pass3-ctest-debug.log`,
`out/pass3-ctest-debug-recheck.log`, `out/pass3-ctest-offscreen.log`,
`out/pass3-ctest-digital-lan.log`, `out/pass3-ctest-final-regression.log`,
`out/pass3-ctest-sdl-lan.log` and `out/pass3-ctest-art.log`.

Runtime/API and exact headless commands are in [digital runtime](digital_runtime.md).
Save format remains 17 using versioned structured payloads; LAN protocol is 28.
The named seeded panel above and integrated progression/balance playtesting have
not been run.
