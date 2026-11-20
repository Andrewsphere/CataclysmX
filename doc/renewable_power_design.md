# Recovered solar and integrated wood-gas power

**Status: Implemented and functionally verified (pass 2 / C-Energy).**
This is the bounded D5 energy slice from [urban workshop progression](urban_workshop_progression.md)
and the [crafting delivery plan](crafting_delivery_plan.md). It extends
[D1 electricity](power_design.md), ordinary crafting and W3 welding.

## Accepted player contract

- A recovered-panel solar installation supplies at most **1 EU/tick**.
  Tick zero corresponds to 08:00. Solar daylight rises linearly from zero at
  06:00 to full at noon, then falls to zero at 18:00. Visual world lighting
  retains its separate future day/night-cycle work.
- Direct sky exposure is required. Roofs and overhead blocking geometry prevent
  supply; artificial light, indirect skylight and rendering settings do not supply
  electricity. The installation's supporting floor does not shade its top.
- One integrated wood-gas generator supplies **4 EU/tick**, consuming prepared
  wood at **4,000 EU per 1 kg portion**, with **24,000 EU** fuel capacity.
  Gasification, cooling, filtering and engine adaptation are internal.
- Wood gas uses D1's whole-item refueling and load-following consumption. Fuel is
  spent only on delivered energy. Start/Stop/Refuel are the operating actions.
- Both sources start stopped; wood gas starts empty. Construction and placement
  create neither stored electricity nor fuel. Using found equipment has no
  construction-skill gate.
- The user explicitly accepted **fixed installations with grabbing disabled**.
  A kit is consumed on placement. Installed objects are not packable; recycling
  below operates on uninstalled kits. Administrative relocation preserves state.
- Source priority is mains, solar, fueled generators, then batteries; each finite
  class is ordered by persistent identity. Due machines precede surplus battery
  charging. Batteries retain their original authoritative item stores.
- Started, enabled, unbroken sources hold their columns and connected source
  columns. Solar holds persist overnight and offscreen. Stopping releases its
  hold. Empty running generators retain intent and resume supply after refueling.

### Example

Find a complete solar kit or assemble one from a working panel and controller.
Place it outdoors, extend wiring to the workshop and install an existing battery.
Start the solar source: its peak output fills a 4,000-EU pack in 40 game seconds.
Use stored power or mains to weld a gasifier, assemble it with recovered engine
and generator hardware, prepare wood and operate the wood-gas generator at night.
One portion supplies 4,000 EU; a regulated-welder rack consumes three portions.

## Batching, ownership and API shape

EU/tick is a **rate**, not an update frequency. Reuse the absolute 100-tick power
interval. `SolarIntervalAllowance(now, peakOutputPerTick)` integrates the remaining
interval using differences of floored cumulative daylight energy. Clock phase
carries fractional production without a per-panel fractional accumulator.
The calculation is integer-only and reduces time modulo one day before arithmetic.

`SolarDaylightRatio(now)` exposes the current factor using `ratioScale`;
`TicksUntilSolarDawn(now)` provides a positive future wake delay. Solar-only
nighttime holds wake at dawn. Existing other machine/generator quantum scheduling
continues independently. Solar behavior has no elapsed-tick loop.

`SolarDefinition::peakOutputPerTick` is immutable content. The dedicated solar
behavior alone owns run intent, an initialized interval marker, absolute interval
start and remaining allowance. No source state is added to every item or object.
`ObjectStateView` reuses the existing source interval fields and adds derived
daylight/exposure inspection values. `PowerInspection` separates solar totals.

World's `HasDirectSkyExposure` evaluates bounded resident vertical geometry.
Private `PrepareSolarInterval` and `SpendSolarEnergy` wrappers connect power
transactions to the behavior. Preview and payment use identical allowance rules.
The existing persistent device index owns the once-per-interval surplus marker.

- Preparation, including zero under cover, claims an interval only once.
- A late first start gets the remaining interval, not a whole new grant.
- Stop discards remaining allowance but preserves its marker.
- Re-enable, repair, movement, merges and reload cannot reissue a claimed grant.
- Current exposure gates unpaid supply. Restoring exposure can use a residual
  grant but cannot replace an already claimed one.
- Old intervals expire; unloaded elapsed time creates no production backlog.
- A failed batch payment changes no fuel, battery, allowance or work.
- Already funded work survives loss of exposure or supply under D1's rules.

The solar behavior is a new closed dispatch case. Its strict version-1 state has
`run_requested`, `interval_initialized`, `interval_start`, `output_remaining`,
and `version`. Save validation cross-checks object state, role, position, intent
and charging markers before device synchronization. Power-state version 2 admits
solar; legacy version 1 remains readable without solar and upgrades on restore.
Persistence uses format 17 and network protocol 27 with the integration.
Generation stays v7: revised content affects future generation, while saved
chunks keep their actual equipment and loot.

## Accepted roster and construction

Nine new items, six crafting recipes and four explicit recyclers. Existing
material/tool identities and public primer carriers are reused. Descriptions are
plain factual prose; atmospheric and humorous exceptions: **none**.

| ID | Unit / role | g / external ml |
| --- | --- | ---: |
| `recovered_pv_panel` | One working framed panel | 8,000 / 18,000 |
| `solar_charge_controller` | One compatible screw-terminal controller | 500 / 700 |
| `solar_generator_kit` | One complete panel/control/mounting kit | 10,000 / 22,000 |
| `recovered_gasoline_engine` | One functional small engine for conversion | 12,000 / 18,000 |
| `generator_alternator` | One suitable regulated generator-capable alternator | 5,000 / 6,000 |
| `generator_drive_assembly` | One fitted coupling and mounting assembly | 2,000 / 2,500 |
| `woodgas_processing_assembly` | One gasifier/cooler/filter assembly | 13,000 / 22,000 |
| `woodgas_generator_kit` | One integrated generator kit | 33,000 / 50,000 |
| `prepared_wood` | One 1 kg portion of sized untreated wood | 1,000 / 1,500 |

All techniques are default-known. Skill requirements use normal matching training.

| Product | Consumed inputs | Retained tools / traits | Skills | Work |
| --- | --- | --- | --- | ---: |
| Solar kit | panel, controller, insulated cable, fastener packet, steel pipe | pliers; screwdriving 1, testing 1, wrenching 1 | Electronics 2, Mechanics 1 | 4,000 |
| Drive | 2 steel rods, 4 steel scrap, fastener packet | file; metal saw 1, drilling 1, wrenching 1 | Mechanics 3 | 5,000 |
| Gasifier assembly | 4 steel sheets, 4 steel pipes, mesh, hose, electrode packet | wrench; welding 1, welding protection 1, metal saw 1, drilling 1 | Fabrication 3, Mechanics 2 | 6,000 |
| Wood-gas kit | engine, alternator, drive, gasifier, lead set, steel sheet, fastener packet | wrench; wrenching 1, screwdriving 1, testing 1 | Mechanics 4, Electronics 3 | 9,000 |
| 2 prepared wood | timber plank | house cutting 1 | Ungated | 400 |
| 1 prepared wood | 5 wood scrap | house cutting 1 | Ungated | 250 |

Solar's screw-terminal assembly requires no powered soldering. Welding costs
12,000 EU with the regulated welder or 18,000 EU with the makeshift welder.
The separate gasifier supports useful Mechanics-2 work; the drive closes the
Mechanics-3 practice gap leading to generator assembly at 4. Existing electrical
practice reaches Electronics 3. Found assemblies skip their construction.

| Recycler target | Returns | Requirement / work | Returned mass |
| --- | --- | --- | ---: |
| Solar kit | panel, controller, cable | screwdriving 1 / 800 | 8,850 g |
| Drive | 2 rods, 2 scrap | wrenching 1 / 600 | 1,800 g |
| Gasifier | 3 sheets, 3 pipes, mesh | metal saw 1 / 2,000 | 10,200 g |
| Wood-gas kit | engine, alternator, drive, gasifier, lead set | wrenching 1, screwdriving 1 / 2,400 | 32,400 g |

Assembly input masses are respectively 10,250, 2,200, 14,700 and 34,600 g.
Offcuts and used electrodes are lost. Fuel preparation conserves represented wood
mass. Recipe cancellation returns actual reserved inputs under ordinary rules;
paid labor and electricity remain spent.

## Acquisition, presentation and verification

Use existing workshop/hardware/auto-repair/warehouse ground stock for bulky
parts and uncommon complete kits, plus electrical retail stock for controllers.
Prepared wood also occurs in suitable stock. Public workshop primer benefits
support Electronics and Mechanics; its existing printed/electronic carriers
share learning identity. Techniques are usable without a unique reference find.
Weighted alternatives provide multiple acquisition contexts, not guaranteed kits.

Item text art uses canonical external RGB assets. Placed IDs are
`solar_generator_station` and `woodgas_generator_station`, both blocking,
sight-transparent, immovable and initially stopped. Solar inspection distinguishes
stopped, covered, nighttime and generating states, with current/peak output.
Shared terminal/SDL/headless/LAN paths use authoritative owned snapshots.

External SDL artwork status: **Ready for artwork**. Solar needs a blue-black
framed panel on a low mounting rack; wood gas needs a vertical hopper/filter beside
a compact engine/alternator frame. Both occupy one tile. Existing generic object
fallbacks provide functional map presentation pending the external artwork pass.

Focused checks cover daylight integration and partial intervals, overflow edges,
roof/artificial-light independence, pure failed payment, source ordering, no
regrants, surplus charging, dawn wakeup, fixed installation refusals, offscreen
holds, save/load/index corruption and LAN presentation. Content checks cover both
affected packs, exact recipe/trait bounds, mass/recovery and skill reachability.
### Verification, 2026-11-20

- `./scripts/build.ps1 -Preset win-msvc-debug -SkipTests` and the corresponding
  `win-msvc-sdl` build pass.
- Debug CTest selection covering power/solar/generators/batteries/grids,
  renewable/catalog/workshop content, protocol, headless LAN, terminal rendering
  and save-client round trips: 162/163 initially passed. The new inspection
  fixture incorrectly removed the player actor while retaining its player flag;
  correcting the fixture resolved that failure.
- A focused Debug rerun, including three added acceptance cases for mains
  priority, due-machine priority/paid work under cover, and whole-portion
  nighttime wood-gas operation, passes **5/5**.
- SDL selection covering the item-browser renderer, solar inspection/shared
  workshop controls, both frontend smoke scenarios and SDL LAN round trip passes
  **7/7**. All nine new text-art assets rendered at 480x360 and 1120x900; both
  preview sets were visually reviewed. Compact views use the existing scrolling.
  Previews/contact sheets remain local under `out/renewable*`.
- Final wire validation also bounds remaining solar output by its saved
  interval's integrated daylight allowance, rather than only the peak quantum.
  The targeted inspection round trip and corruption checks pass **1/1** on each
  Debug and SDL preset after that change.
- `git diff --check` passes. No tooling tests were added or run.

These checks verify functionality and conservation. Seeded acquisition/balance
playtesting belongs to the integrated package; external map sprites retain the
artwork handoff above.
