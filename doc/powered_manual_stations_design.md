# M-Bench: powered manual stations

**Status: Proposed gameplay and implementation design; review pending.**
This standalone follow-up builds on [manual crafting](crafting.md),
[D1 power](power_design.md), [W1/W2 workshops](workshop_design.md) and the
[delivery plan](crafting_delivery_plan.md). W3A/W3B implementation is a separate,
concurrent pass. This document defines the next mechanics slice.

## 1. Recommended contract

A powered bench supplies ordinary crafting traits while a character works.
Its electricity is another cost of the character's existing manual job.

| Decision | Recommendation |
| --- | --- |
| Work owner | The unfinished item owns progress, paid work, worker contributions and fractional balances |
| Electrical connection | A placed provider draws from the grid covering its own tile |
| Handheld connection | Existing item tools continue drawing from the worker's tile |
| Rate ownership | One positive energy-per-base-work rate on the station definition |
| Multiple capabilities | Charge and claim each distinct selected station once per slice |
| Payment | Preflight all station grids, handheld grids, local stores and forge heat together |
| Interruption | Existing paid-work, claim-release and explicit-resume rules apply |
| Activity | Work requires the manual worker; an idle bench has no ticking behavior or autonomous hold |
| Fractional accounting | Extend the existing job-owned electrical remainder to selected stations |
| Source residency | Include grids intersecting the player's bounded crafting area in source-loading demand |

Capability tiers still describe operations. Electricity does not automatically
raise a tier, improve output, shorten work or substitute for skill. A found bench
uses the product recipe's requirements, independently of its construction recipe.

The first useful demonstration can be a powered soldering bench performing the
existing `solder_electrical_connectors` recipe. A soldering-1 fixture is sufficient
to verify M-Bench. Controlled soldering-2 equipment and its fine-work consumers
belong to a later content review under the accepted Electronics-3 assembly anchor.

## 2. Content and data shape

Propose the existing item spelling for an optional **object** property:

```json5
{
  format_version: 1,
  objects: [{
    id: 'powered_soldering_bench',
    name: 'Powered soldering bench',
    behavior: 'none',
    tool_traits: [{id: 'soldering', tier: 1}],
    crafting_power: {store: 'grid', energy_per_work: 10000},
  }],
}
```

This is proposed schema, not content currently supported by the object loader.
The example ID and rate are illustrative, not a new W3 roster commitment.

Validation:

- `crafting_power`, when present, requires exactly `store` and `energy_per_work`.
  `store` must be `grid`; the rate is an integer in `[1, UINT32_MAX]`.
- Accept it on `behavior:'none'` objects with at least one `tool_traits` entry.
  Preserve the current four-trait limit.
- Reject it on forges, autonomous crafting machines, generators and battery
  housings. A manual job can combine a powered bench with a separate W2 forge.
- Absence means unpowered service. Reject explicit zero, negative/fractional
  values, unknown/duplicate fields and local-store names.
- Fixed fixtures and W1 packing-backed stations both qualify. Existing packing
  mapping, protected backing item and storage-capacity validation still apply.

Recommended typed addition:

```cpp
// In the immutable ObjectDefinition:
std::uint32_t craftingEnergyPerWork = 0; // Zero: passive; positive: grid at object.
```

A scalar is sufficient because this slice has one supported supply kind and no
per-instance station energy store. It adds no runtime field to every object and
needs neither a new behavior enum member nor a heap-allocated bench behavior.
Content loading and `ValidObjectDefinition` both enforce structural constraints;
content fingerprinting includes the resolved rate.

All traits on one powered provider use that rate. For example, a combined
testing/soldering bench selected for either operation bills once at its authored
rate; selected for both, it still bills once. Content should represent genuinely
independent passive tools separately if they need to work during an outage.
Rates cover the whole funded base-work slice, as they do for current item tools.

## 3. Selection and electrical endpoints

Use existing same-layer reach, blocked-path, enabled/broken, tier and claim rules.
The first qualifying providers in the current persistent-ID ordering remain the
selection. An unpowered selected bench can cause `InsufficientEnergy`; selection
does not search all alternative combinations for a cheaper or energized provider.
Disabling or moving that provider out of reach permits ordinary reselection.

Exact retained-item requirements still select items. A packing item supplies no
carried station traits, and its protected backing item is not selected or billed
again. Mere proximity to a bench adds no charge: the job must select its service.

| Arrangement | Required electrical coverage |
| --- | --- |
| Bench only; worker outside the wired area | Bench tile only |
| Corded item only; tool lies in nearby storage | Worker tile, as in D1 |
| Bench on grid B and corded tool used by worker on grid A | B pays for the bench; A pays for the tool |
| Two selected benches on separate grids | Each bench's grid pays its own charge |
| Several providers on the same grid | Their whole-unit demands share one atomic supply plan |

Reach does not bridge grids or transfer electricity. Wiring can deliberately
join regions using D1's existing geometry. Re-resolve endpoints on each new
funding attempt rather than persisting a grid ID in the unfinished job.

**Coverage must be checked even when rounding produces zero whole-unit cost.**
Check `GridAt` for every selected grid-powered endpoint before payment.
`PowerSystem::Plan` currently skips zero-energy entries, so passing a zero demand
alone is insufficient. A covered but empty grid may fund such a slice, following
the existing D1 policy; the carried fractional balance remains due.

## 4. Work, rounding and transaction

Preserve the existing fixed-point work and `ratioScale` convention:

```text
workFixed: base work * 10000
rate:     energy per base work * 10000
D:        100000000

numerator = workFixed * rate + remainder
charge    = numerator / D
remainder = numerator % D
```

Use one electrical remainder on the unfinished job, independently of its heat
remainder. Enumerate distinct selected providers in this explicit payment order:

1. Retained and trait-selected item tools, deduplicated and sorted by persistent
   item ID, matching current funding.
2. Selected stations, deduplicated and sorted by persistent object ID.

Skip zero-rate providers. This preserves all existing item-only accounting and
extends it without per-provider/per-grid saved debt. Fractional cost can cross
from one provider to the next, including across grids, exactly as current local
and corded item tools share the balance. This is integer rounding of one job's
cost, not movement of stored energy. For example, two half-unit contributions on
different grids can debit zero on the first and one on the second.

Provider replacement, trait-slot changes, pause and reassignment never reset the
balance. Identical rates and funded work produce the same total electrical cost
regardless of slicing or worker speed. Choosing different-rate equipment
deliberately changes subsequent costs. Completion/cancellation discard only the
final sub-unit remainder under the existing rule.

For each new slice:

1. Select providers and validate claims, learning/reference gates and inputs as
   the existing start/resume/continuation path requires.
2. If **any item or station** needs grid power, cap duration using
   `PowerSystem::SliceLimit(now)`. Preserve the forge's additional 100-tick cap,
   configured slice limit and final-work shortening.
3. Compute prospective work, electrical charges, heat and new fractional balances
   in scratch storage. Validate every grid endpoint, including zero charges.
4. Check local item energy, forge readiness/fuel and all station claims. Submit
   all grid demands together to `CanSupply`.
5. On success, commit `TrySupply`, local charges, forge heat, claims and the paid
   work grant within the existing synchronous crafting mutation boundary.
   Ingredient reservation on start is part of the same successful operation.

Failure commits no new claims, payment, ingredients, work or remainders. An active
job that cannot fund its next slice releases its previous claims and pauses with
the existing typed outcome. No provider callbacks may invalidate handles or change
the payment plan between successful preflight and commit.

Use the existing checked/bounded arithmetic for every individual provider; do not
multiply work by an unchecked sum of all provider rates. Funding scratch remains
reusable: item payments can contain up to `8*255 + 4` instances, while selected
stations have the existing justified hard bound of four.

### Mixed-supply example

For 100 base work with an initially zero electrical/heat remainder:

| Provider | Rate | Payment |
| --- | ---: | ---: |
| Corded item, worker grid A | 10000 | 100 electrical units from A |
| Selected bench, grid B | 20000 | 200 electrical units from B |
| Local rechargeable tool | 5000 | 50 units from that tool |
| Separate forge | Recipe heat rate 10000 | 100 heat units |

If B has only 199 available units, none of those resources is debited and no new
station claim is acquired. If A and B are the same grid, it must supply the
combined 300 electrical units. Selecting two traits from the same bench still
costs 200, not 400.

## 5. Occupancy, interruption and recovery

Station occupancy remains W1 occupancy: a selected bench is exclusively claimed
by one active manual job, even if it offers multiple traits. Handheld tools retain
their existing sharing policy. Initial competition follows command order;
continuing manual jobs follow persistent job-ID order.

- A pause request completes the already paid slice, then releases claims.
- Outage after payment does not revoke paid work. Failure to fund the next slice
  yields `InsufficientEnergy` and requires explicit resume after supply returns.
- Disable, breakage, destruction or administrative movement releases affected
  claims without refunding or erasing paid work.
- Worker loss/unavailability preserves paid remainder under the current rules.
  Resuming that grant does not repay electricity or reacquire purchased station
  service; the next unpaid slice selects and claims equipment normally.
- Cancel returns the actual reserved ingredients under ordinary rules. Spent
  electricity and heat are never refunded.
- Ordinary packing/movement of a claimed station is refused. Once eligible,
  packing returns the original backing item and requires empty public storage.
  The bench has no electrical reservoir to drain, refund or carry away.

There is no automatic retry queue or offscreen completion for a manual bench.
An enabled idle bench consumes no electricity. Manual crafting retains the
existing scheduling order relative to autonomous power-machine funding.
Ordinary accessible welding-protection traits also retain their existing checks
and paid-work semantics.

## 6. Source residency across a wiring boundary

Current player power demand follows the player's own grid. That is insufficient
when a reachable bench lies on a different grid whose battery/generator is in an
unloaded column. Source residency must be ready **before the first funding
attempt**, not only after a successful station claim.

Recommend a conservative, bounded extension of the existing player-source rule:

1. For each living, unparked player, form an inclusive same-layer square centered
   on the player. Its radius is the larger of the current crafting radius and
   that player's assigned continuing job's snapshotted radius, if any.
2. Collect every grid intersecting that square's tiles. Use the grid's actual
   region union, preserving holes; a combined bounding box is insufficient.
3. Request the registered battery/generator columns of those grids through the
   existing source index. Sort and deduplicate the resulting columns.
4. Recompute through normal reconciliation when position, settings, assignment,
   topology or indexed sources change. Drop dependencies once no player area or
   existing autonomous hold needs them.

Radius stays within the existing 0..8 bound. This deliberately loads conservatively:
an adjacent grid's sources may remain loaded even when a wall blocks its bench or
no powered bench is currently selected. Reach, availability and payment still use
the exact normal checks. The small bounded geometry avoids a new persistent bench
index or archive-time station/path discovery solely to find source dependencies.
Empty wiring regions add no column requests; only registered sources do.

Recommended power query:

```cpp
void PowerSystem::CollectConsumerDemand(
    eastl::span<const PowerBounds> areas,
    Vector<ColumnCoord>& output) const;
```

It clears/fills reusable sorted unique output, performs geometry/source-index
queries only, and neither pays nor activates equipment. This generalizes the
current point-based `CollectPlayerDemand`; World supplies one area per player,
and existing point consumers use degenerate bounds. World owns interpretation
of player/job settings; PowerSystem does not inspect actors or crafting state.
Reuse bulk scratch vectors rather than adding per-player owning containers.

Extend both live demand refresh and **staged save/activation preflight** to use
these areas. Include continuing-job radii from incoming state and planned
generated coverage before the activation mutation boundary. A corrupt remote
source archive must not be discovered only after partially activating the player.
Complete the existing reconciliation closure before accepting input.

These dependencies are player-active source demand, not autonomous bench holds.
Parking/leaving releases them under existing player rules. A bench alone cannot
keep its own column, a worker or an entire workshop running remotely. Active
nonplayer crafting callers retain their responsibility to establish residency;
this slice does not add autonomous NPC workshop scheduling.

## 7. Presentation and compatibility

Use the existing crafting commands and power/workshop controls:

- Station rows show the authored rate and **"grid at station"** connection rule.
  Powered idle equipment should be labelled idle, rather than implying that
  unclaimed equipment is necessarily energized.
- Existing multimeter-gated coverage display identifies the station tile's grid.
  The meter's current-grid totals still describe the worker's grid; label that
  distinction clearly when inspecting nearby equipment.
- `InsufficientEnergy` remains the crafting outcome for missing coverage or
  inadequate supply. `MissingTools`, `StationBusy`, `ColdForge` and
  `InsufficientFuel` retain their existing meanings.
- Job views retain paid ticks, station identities and ordinary resume/pause/cancel.
  Show authored rates from the shared catalog and dynamic facts from snapshots.
  Terminal, SDL, headless and LAN consumers use the same authoritative contract.

No new command, job status or runtime save field is required by this proposal.
The existing `energyRemainder`, `heatRemainder`, pending work, timers and station
bindings already carry the durable state. Grids remain resolved from current
positions and are not copied into jobs. Derived consumer areas are reconstructed
after load.

Fingerprint the object rate, including the passive default, so mismatched catalogs
cannot agree about whether station use is free. Existing old definitions load as
zero-rate providers. Old paid work remains paid; any subsequent slice uses the
loaded catalog's provider rates. Content-upgrade policy still applies to recipe
input/work changes independently of this mechanics extension.

Assign version changes against the integrated baseline after W3A/W3B lands.
An additive content property needs loader/fingerprint updates; this proposal does
not by itself demand a crafting-job, world-generation or network payload bump.
If implementation adds transported inspection fields, it must deliberately
version and validate that actual wire change.

## 8. Implementation handoff and focused evidence

Likely touchpoints for a later implementation pass:

- Object parsing, `ObjectDefinition`, validation and content fingerprint.
- `CraftingSystem::Fund`: unique station charges, endpoint coverage, slice limits
  and the existing mixed-resource transaction.
- Power source-demand query and World's live/staged reconciliation callers.
- Shared workshop labels and headless inspection using existing view/catalog data.
- Durable crafting/object/power documentation after integration.

Focused behavioral acceptance cases:

| Area | Required evidence |
| --- | --- |
| Content | Positive-rate passive station accepted; malformed rates, local stores, empty traits and incompatible behaviors rejected; rate changes alter fingerprint |
| Connection | Bench works with an unwired worker; wired worker cannot power an unwired bench; separate grids pay their own providers |
| Transaction | Shared-grid and cross-grid totals; local-tool, heat or any grid shortage spends nothing and acquires no claims |
| Deduplication | One multi-trait station bills once; two stations each bill; protected packing item is not a second provider |
| Rounding | Zero-charge coverage; fractional carry across items/stations, replacement, speed changes and save/load; failed preflight preserves balances |
| Timing | Station-only jobs cap at power boundaries; paid work survives source loss; no automatic resume or offline progress |
| Occupancy | Competing workers, pause release, disabled/destroyed station, paid reassignment and packing of the original item |
| Residency | First start across a grid boundary loads remote sources; radius snapshots and grid holes respected; parking releases dependencies; bad source archives fail staged activation |
| Shared presentation | Same rate/connection explanation and refusal behavior in shared UI/headless paths; LAN views remain snapshot-only |
| Useful operation | A placed powered soldering provider completes existing connector production with observed power debit and outage/resume |

Read-only design references included `crafting_system.cpp`, `crafting_state.h`,
`object_definition.h`, `power_system.cpp`, World's live/staged power-demand paths,
and existing power-crafting/workshop tests. No implementation verification is
claimed by this design.

### Decisions to review

1. **Station-local grid connection**, including atomic multi-grid jobs.
2. **One provider-owned rate** and the existing shared electrical remainder.
3. **Conservative crafting-area source residency** to support first use across
   wiring boundaries without a new station index.

After these choices are accepted, implement M-Bench as its own mechanics pass and
select its first authored powered station/consumer separately.
