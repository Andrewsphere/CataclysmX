# Power commands and presentation

`CommandKind::wire_grid` uses `Command::item` for the carried kit persistent ID
and `powerBounds` for an inclusive single-layer rectangle. The protocol admits
at most `maxPowerWiringExtent` (64) tiles along either axis; authority also applies
the current `PowerSettings` (default 16×16, configurable from 1 to 64 per axis),
actor-layer/reach checks and material accounting.
`CommandKind::place_power_object` uses `item` for carried equipment, `otherItem`
for its wiring kit (zero when not required), and absolute `position` for the
destination. Explicit fields avoid interpreting unrelated movement offsets as
world coordinates. All unused fields must remain canonical zero/default values.

The server delegates transactions to `World::WireGrid` and
`World::PlacePowerObject`, schedules only successful actions using their
`baseCost`, and returns `power_changed` or `power_refused` together with typed
`CommandResult::powerOutcome`. Local and LAN commands share shape validation;
queued commands revalidate authoritative state at execution.

Kits are reusable tools: `ItemDefinition::wiringMaterial` names an ordinary item
type, and `wiringTilesPerMaterial` defaults to 16. JSON uses
`wiring_material:'copper_wire', wiring_tiles_per_material:16`. Wiring consumes
`ceil(newTiles / wiringTilesPerMaterial)` accessible ordinary material leaf
items. Installing a battery costs four tile equivalents, rounded up in the same
way (one material item at the default rate). Kits have no wiring energy store,
so crafting their output cannot leave an empty kit or transfer electrical charge
into fictitious wiring energy. The workshop displays the selected kit's material,
coverage rate and rounded battery installation cost.

`WorldView::currentPower` is optional and present only for an observing player
with carried grid-meter access. It describes the actor's tile, even when the
viewport selects another layer. `ViewCell::powerGridId` describes coverage at
the viewport's layer, independent of terrain projection depth. It is zero
without a meter. Coverage is recalculated when constructing a view even if its
terrain cells are reused, preventing a discarded meter from retaining a map.
`powerSettings` is public configuration, supplied even without a meter so the
workshop can offer valid region sizes.

The player DTO deliberately uses `PowerInspection`, with present availability
and aggregate counts only. `PowerGridDebug`, including its future cutoff tick,
belongs exclusively to the administrative `PowerDebugSnapshot`. Normal network
views never serialize the debug snapshot. Protocol version 23 serializes the
new commands, result outcome, meter inspection, coverage, configuration and
powered-object inspection state. Boolean and enum values are strictly checked;
coverage without optional inspection is rejected.

`client/power_view.h` declares shared value-only workshop state and snapshot
formatting. Both terminal and SDL enter it from the crafting modal. The
workshop creates ordinary authoritative commands rather than mutating local
state, so the same flow works for LAN guests. Its reusable item and output
buffers avoid rebuilding separate frontend implementations. Debug capture must
run on the world-owning thread: SDL's host-only `InspectPower` request returns
an owning, read-only snapshot through its existing session reply channel.
The debug presentation also lists every machine-only loaded column's coordinates,
active/resident state and demand-reason counts below the centered map, making
remote workshop holds inspectable without moving the player.

LAN content fingerprints include grid crafting mode, wiring material identity
and tiles-per-material, placed-object identity and grid-meter eligibility. Object
fingerprints also cover grid-machine mode, generator fuel identity/capacity,
energy per fuel and output per tick, and the generator/grid-battery behavior
implementation versions. Material/object references hash their stable names.

Tile inspection additionally transports the object's grid funding/run request
and generator interval/output state. These fields describe present object
operation and do not include future mains cutoff timing.

## Fueled workshop controls

The shared terminal/SDL Power workshop also controls placed forges without a
multimeter. Move the cursor onto a forge; `f` cycles accepted carried fuel, `l`
loads the selected whole item, `i` ignites, `x` extinguishes, and `c` discards Cold
fuel. `p` / Enter place its packing item and `r` recovers a cold, empty, idle
station. The view shows readiness, fuel/capacity, ignition cost, accepted yields
and packing refusals. Tile inspection also shows current forge fuel/readiness.

Both local and LAN headless interfaces accept:

```text
stations
load-forge <object-id> <item-id>
ignite-forge <object-id>
extinguish-forge <object-id>
discard-forge-fuel <object-id>
```

Forge commands use `Command::target` for the object; only `load_forge` also uses
`item`. All other fields retain their canonical defaults. Server authority calls
`World::UseForge`, schedules successful returned costs, and reports
`station_changed` / `station_refused` with `stationOutcome`.
Protocol **25** carries forge commands, station/inspection state and heat funding
refusals. Definition-aware validation checks fuel capacity and provider kind;
fingerprints include forge fuel identities/yields, ignition/capacity and recipe
heat rates.
