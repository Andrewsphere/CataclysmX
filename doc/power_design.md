# Electrical power

**Status:** accepted gameplay design; implemented D1/I2 vertical slice. Broader
workshop/printer progression remains in the [coordinating plan](urban_workshop_progression.md).
Player controls and network values are described in [power presentation](power_presentation.md).

The accepted [pass-2 renewable extension](renewable_power_design.md) adds
clock-derived solar and an integrated prepared-wood generator. Source rates are
accounted in absolute 100-tick batches, with solar-only nighttime wakeup at dawn.

## Ownership and APIs

`World::Impl` owns `PowerSystem(world, seed)`. Construction uses the explicit seed;
it does not access the partially constructed World. `World::Power()` exposes const
and mutable references. The module consists of `power_types.h`, `power_system.h`,
`power_system.cpp`, and `power_state.cpp` (geometry and pointer-free DTO validation).
The persistence writer serializes `SavedPowerState` in the same transaction as
object/item chunks. There is no second fuel or battery-charge authority in a grid.

`RegisterBuilding(origin, bounds)` (also a span overload) registers a generated
building idempotently. The origin is its stable generation key. Bindings follow
player merges, so loading another floor cannot recreate an obsolete grid.
Register complete building extents, including basement/floors, before allowing
wiring in its footprint. For generation version 7, do this for every incoming
chunk, including archived chunks without a saved building binding. Coverage
creation must eagerly register every touched building before `ExtendBounds`.
World permits wiring only on resident tiles. This ordering prevents wiring into
an undiscovered building from later making its generated registration overlap a
player grid. Distinct generated grids may touch but may not overlap. An unexpected
overlap returns `WorldError::occupied` without mutation; callers must not turn it
into a fatal assertion or silently auto-merge generated grids. It signals missing
registration/migration preflight, which must be resolved before committing wiring
or publishing a loaded chunk.

`Quote(bounds)` counts newly covered tiles and touched existing grids.
`ExtendBounds(bounds)` commits only geometry; the World gameplay transaction owns
access checks and material consumption. `wiringUnits` expresses newly covered tile
equivalents, not electricity or an energy-store debit. World consumes ordinary
copper-wire material, rounding the required material count up:
`newTiles / tilesPerMaterial + (newTiles % tilesPerMaterial != 0)`, with a positive
content-defined coverage value. The adjustable width/depth settings are integers in 1..64, defaulting to 16;
an extension is a single Y layer. Grids are unions of inclusive AABBs, not their
enclosing rectangle. Overlap or X/Z/Y face contact connects; edges/corners do not.
Only the new player region's contacts merge grids, not a transitive closure of
pre-existing generated-grid adjacency. The smallest touched grid ID survives.

`SyncObject(handle)` registers/updates typed equipment, position and run-intent
holds. Call it after materialization and after movement, start/stop, completion,
refueling, enable/broken changes, and installing/removing battery contents.
`UnregisterObject(id)` is for actual destruction, never unloading. Use `Invalidate`
after a source mutation that does not otherwise call `SyncObject` (e.g. item energy
edits). `Invalidate` calls the private notification `World::PowerChanged()` to mark
world revision and demand dirty. That hook must not reconcile, invoke power, or
retain entity borrows; constructor initialization never calls it. Restore and
changed settings notify through the same path. Payment hooks may notify repeatedly;
the current event pass consumes its own invalidations when it finishes, so a failed
or zero-energy workload does not create an immediate-event loop. Neither mutation
performs residency reconciliation itself.

## Source authority and accounting

Mains is unlimited until the single seeded absolute cutoff, an integer game day
in [7,14]. It never returns. The cutoff is persisted and does not consume gameplay
RNG. A day is `ticks_per_turn * 60 * 60 * 24`, independent of wall-clock pacing.
Only generated mains-connected grids have mains; player extensions inherit it
only through merging such a grid. Player inspection hides the future cutoff.

An installed battery object's sole child is the original item with exactly one
energy store; its `placed_object` must identify that battery housing. Its existing
item charge is authoritative. Carried batteries and
arbitrary nearby items are never sources. Discharge has no rate cap. Surplus
charging additionally requires a rechargeable store. Installation/removal and
electrical-material costs are World transactions; the object does not offer
ordinary player storage. Destruction drops the original item with its actual
remaining charge. A separate player dismantling/recovery action is not part of D1.

Generators use **coarse prepaid output**, with fixed 100-tick intervals. A running
generator's allowance is output-per-tick times remaining interval duration on a
late first start, or a whole interval at a boundary. A payment subtracts both
chemical energy and output allowance atomically. Elapsed time never independently
produces energy. Unspent output allowance expires; chemical energy remains fuel.
This is load-following fuel consumption, without idle burn or hidden electrical
storage. Direct generator supply needs no battery.

Required private World hooks, with `friend class PowerSystem`:

```cpp
WorldResult<void> PrepareGeneratorInterval(ObjectHandle object, Tick now);
WorldResult<void> SpendGeneratorEnergy(ObjectHandle object, std::uint64_t energy);
WorldResult<void> FundGridMachine(ObjectHandle object, Tick ticks);
void PowerChanged();
```

They delegate to typed object behavior and mark its chunk dirty. Preparing at
the same interval is idempotent. A stop discards the allowance but preserves the
initialized interval marker: restarting that interval cannot refill it. Save the
object's initialized marker, absolute interval start and remaining allowance.
Moving, merging, unloading, enabling and reloading must not mint another allowance.
When an old interval is encountered, preparation uses `now`, with no offline
production backlog. Preview computes exactly this effective allowance without
calling preparation. `SpendGeneratorEnergy` must not prepare a second time itself
with a different clock.

`CanSupply(span<PowerDemand>)` and `TrySupply(...)` aggregate a batch against reusable
source scratch. A failed preview or payment changes no authoritative state.
Sources are mains, solar, fueled generators, then batteries; finite sources use persistent
ID order. Successful commits run synchronously without callbacks that destroy,
move, create or query/pay other equipment. Hooks may update an existing equipment
record through `SyncObject`; they must not recursively call `ProcessEvents` or
payment methods. This keeps the small batch transaction atomic without durable
reservation handles. Counters/scratch may change during preview.

Surplus goes to original battery items **after** the due machine batch, using the
same source-class priority. Each solar/fueled source has a persisted
once-per-interval surplus-pass marker. A late first
start with available output can take its pass. Mains has an analogous grid marker.
No surplus pass transfers battery energy into another battery. Charge uses the
same generator fuel and allowance debit as direct supply. Repeated calls, source
movement, merge and reload cannot repeat a generator's charging pass. Full stores
do not create electrical accumulators; unused allowance simply expires.

## Machines and scheduling

Grid machines own `runRequested`, unfinished work and `fundedTicks`. They receive
no free local electricity. A slice is `min(remainingWork, SliceLimit(now))` ticks,
paid in full before advancing. `Elapse` advances only funded work. Outage leaves
run intent and progress intact. Manual stop clears intent and freezes any paid
ticks for explicit resume; completion releases the
hold. A paid slice may finish after loss of its source, but cannot cross an interval
boundary when scheduled normally. Paid grants cannot be refunded as electricity.

Each grid's eligible machine batch is sorted by persistent ID. First opportunity
rotates one position per interval, even if all payment attempts fail. Within an
interval it does not rotate again. This is equal rotating priority, not equal watt
allocation; a machine must afford its entire bounded slice. The grid persists its
cursor and processed interval. Late starts may use residual supply. A mid-interval
restore does not replay elapsed work, reset source allowances or rotate twice.

World calls `ProcessEvents` at due boundaries before the input barrier, after
ordinary object completions and before crafting funds its next slice. Include
`NextEventIn()` in the simulation's next-event selection. `Invalidate()` requests
one immediate pass; processing consumes it, even when all machines lack power.
There must be no recurring zero-delay outage loop. Recurring quantum events are
needed for workshop holds and incomplete mains charging, not every empty grid.

Split `ElapseTicks` at power interval/cutoff boundaries, including direct calls.
Elapse paid work before moving the clock; process interval bookkeeping at the new
tick. Never independently generate energy from elapsed ticks. The full simulation
pump remains responsible for ordinary completion callbacks and NPC actions.
At the maximum representable tick no future interval can be scheduled.

Crafting caps grid-powered slices using `SliceLimit(now)` before calculating work
and costs. Corded items draw at the actor's current position; powered manual
stations draw at their own positions. Aggregate every grid demand, validate local
item energy, station claims and forge heat, then commit without intervening
structural mutations. Check coverage for every endpoint even when its rounded
charge is zero. Existing item energy accounting still serves non-corded tools.
Backpack batteries do not join a grid. A craft rechecks positions and providers
for each new slice; paid work survives disconnection.

## Save/load and remote activity

`Capture()` returns versioned `SavedPowerState`. `Restore(state, now)` validates
before assignment and leaves old state intact on failure. Grids/devices are saved
in ascending identity order. State includes generated bindings, settings, cutoff,
fairness/charging markers and stable device positions/roles/hold flags. Charge,
fuel and work remain in normal item/object archives. A persisted device index
must be updated in the same save transaction as its associated chunk mutation.

Restore global power state **before** residency reconciliation. Then merge
`CollectWorkshopDemand` into active column demand. Load those columns and resolve
object IDs. Do not unregister records for unload or try to discover holds only by
scanning already loaded objects. Validate the materialized objects against the
global index at the World/archive boundary. Because Restore notifies World through
`PowerChanged`, call it only after the owning Impl is published; a speculative,
unpublished candidate should use `ValidPowerState` for its nonmutating preflight.

Each enabled, unbroken, player-started machine/generator holds its own column. It
additionally holds registered source columns on the same grid. Existing column residency includes
all Y layers. A machine waiting for power and a generator waiting for fuel retain
their holds until explicit stop, completion, disable, breakage or destruction.
Disabling or breaking releases the hold without discarding run intent; repair or
enable resynchronizes it. There is **no arbitrary hold cap or timeout**. Generated equipment starts idle with no hold. Empty wired
columns and idle machines need not be retained. A held generator also retains
batteries for autonomous surplus charging.

Corded tools and reachable powered manual stations also need source residency
without an autonomous machine hold.
`CollectConsumerDemand(span<const PowerBounds> areas, Vector<ColumnCoord>& output)`
clears and fills reusable sorted unique source columns from the persistent
equipment index for every grid intersecting an area. Intersection uses the actual
region union, preserving holes. Point consumers can pass degenerate bounds.
The query neither loads nor pays equipment and knows nothing about crafting.

World supplies an inclusive same-layer square per living, unparked player:
radius `max(current crafting radius, assigned continuing job's saved radius)`,
bounded by the existing 0..8 setting. It combines source columns with player
active demand and attributes them to `playerActiveRequests` in DEBUG. Both live
reconciliation and staged activation preflight include these areas, incoming
job snapshots and planned generated coverage before activating chunks. A corrupt
remote source therefore refuses activation before mutation.

This deliberately conservative square does not test paths or require a selected
bench: sources must be ready before the first funding attempt. Empty wiring
columns add no requests. The index needs no runtime handles; unloaded sources
must be loaded to inspect charge and enabled/broken state. Idle/disabled source
columns may be retained but only active, enabled, unbroken equipment can supply.
Moving away or parking releases player dependencies independently of workshop
holds. A manual bench alone retains no column and performs no offscreen work;
nonplayer crafting callers remain responsible for residency.

Persistence must cross-validate the global device index against archived typed
objects before accepting a save: ID/position/role, enabled/broken state, run intent,
job presence for machine holds, and generator interval/charging markers. Do this
before `SyncObject` can overwrite an inconsistent saved record. Global validation
alone rejects non-generator charging markers, impossible mains charging flags and
future/misaligned intervals, but cannot verify chunk-local behavior state.

`CollectWorkshopDebug` reports owner-hold and source-dependency counts per column;
combine these with player/resident reasons for the chunk DEBUG UI. `CollectDebugGrids`
exposes AABBs, IDs, cutoff, device/hold counts, fairness and current source totals.
`Counters` exposes event passes, source scans, payment attempts, funded/failed
slices and charged energy so DEBUG can show the CPU work caused by remote grids.
It is derived instrumentation, not saved simulation state. `Inspect` is the
multimeter DTO and contains no future-cutoff information.

## Build and integration checks

The registered power suites cover face/edge topology, union holes, generated merge bindings, wiring
cost/limits, DTO validation/remote holds, pure failed batch payment, direct finite
generation, original-item charge conservation, restart/reload markers, and rotating
machine competition. Additional regressions cover unloaded player source demand,
disabled/broken hold release, multi-generator/multi-battery allocation with partial
fuel and capacity, machine-before-charging priority, outage event termination,
elapsed partition equivalence and exact mains cutoff. These runtime tests depend
on World integration. Full disk round-trip additionally checks remote residency
restoration, archive/index corruption, legacy saves, and persisted source ledgers.

### Runtime boundary and version notes

`World::WireGrid` and `World::PlacePowerObject` preflight carried tools/materials,
reach, occupancy and clock headroom before committing. Wiring costs 100 base
ticks per new tile (at least 100 for a merge); equipment placement costs 400.
Wiring materials are selected in persistent-ID order and callbacks are deferred
through the transaction. `CanInspectPower` recognizes carried, accessible meters,
including normal nested inventory, but not reserved or installed children.

New world generation is v7. Versions 2–6 retain their existing generation;
v7 uses the v6 layout/terrain/population domain plus persistent building grids.
Save format 15 adds the global power snapshot and migrates supported older
formats to empty electrical state with a seeded cutoff. It does not retrofit
legacy-generation buildings with mains. Protocol 23 carries power commands/views.

Building-grid batches and incoming object/index relationships are validated before
a reconciliation pass mutates chunks. Reconciliation stabilizes player/source
dependencies; `Server::Update` reconciles before and after advancement so equipment
start/stop/completion does not depend on a later player movement. Each pass is
preflighted; this is not a general rollback transaction for arbitrary gravity
changes across multiple passes.

### SDL artwork handoff

Functional terminal presentation, shared menus and item text art are present.
[Gen1 item sprites](item_sprites.md) cover `wiring_kit`, `portable_fuel_generator`
and `generator_fuel_packet`. The placed `generator`, `grid_battery_station` and
`grid_scrap_press` objects still use the generic object sprites. Suggested
silhouettes for these objects: a placed engine, a battery with terminal leads,
and an upright compact press, respectively.
Generator running/stopped and press working/waiting/manual-stop states are available
through inspection.

### Verification at the power-grid handoff

- `cmake --build --preset win-msvc-debug`: passed. The initial stale configure
  cache selected MinGW link/archive tools for MSVC; a fresh configure with MSVC
  `link.exe`/`lib.exe` resolved the local build configuration.
- `./scripts/test.ps1 -Preset win-msvc-debug -j 8`: **1,317 passed, 2 skipped,
  0 failed** (1,319 total). Skips are platform-dependent filesystem checks.
  This includes the 66 core power cases, migration/protocol regressions, full-pack
  loading, world generation, remote workshop saves, and session-driven release.
- `./scripts/build.ps1 -Preset win-msvc-sdl -SkipTests`, followed by incremental
  `cmake --build --preset win-msvc-sdl`: passed.
- SDL executable filter `[power],[sdl]`: 220 cases, 215 passed, 3 failed,
  2 skipped. One failure was the item-browser count after adding three power items;
  its corrected test passed on rerun. Two existing-artwork expectations remain
  failing: `SDL renders bundled furniture on terrain with clear tile borders`
  and `Gen1 item sprites load across scenarios and preserve unmapped fallbacks`
  (first missing mapping: `aluminum_sheet`). The latter mapping/content mismatch
  is also present at HEAD; the border failure was reproduced but not verified
  against a separately built baseline. These tests were not weakened or fixed by
  changing artwork.
- SDL filter `[power][sdl],SDL item slice and expansion render through the item browser`:
  **2 passed**, covering new-item fallback and rendering all item-browser content.
- `./scripts/test.ps1 -Preset win-msvc-sdl -R 'sdl_lan_roundtrip|sdl_frontend_smoke|sdl_town_default_smoke|headless_lan_roundtrip'`:
  **4 passed**, including actual executable startup and cross-frontend LAN.
- `git diff --check`: passed. No tooling tests were added or run.
