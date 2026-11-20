# Workshop stations and fueled metalworking

**Status: Accepted gameplay contract; W1/W2, W3A/W3B and W4 functionally verified.** D2 of the
[urban workshop progression plan](urban_workshop_progression.md). The user accepted
the proposed defaults and both progression branches. D1's
[electrical accounting](power_design.md) is implemented.

## 1. Recommended direction

Build two reachable branches that reconnect:

```text
scavenged hand tools + stock
  -> crude anvil + hand-blown charcoal forge
  -> forged tools and substantial metalworking

electrical salvage + hand tools
  -> assembled or found soldering iron + D1 supply
  -> electrical assemblies
found welder + accessible protection + electrodes + D1 supply
  -> welded workshop equipment
```

Finding equipment skips making that equipment. Operating costs, skills, materials,
and recipe knowledge still apply. The first forge needs neither welding nor
electricity; the first soldering iron needs no soldering operation.

Recommended implementation order is **station capabilities -> fueled manual
crafting -> playable routes -> selectable autonomous jobs**. The machine contract
is included here so D3 can design printers against it.

### Accepted decisions

| Decision | Accepted rule | Consequence |
| --- | --- | --- |
| Forge consumption | Ignition cost plus fuel per funded base work; no idle burn | Predictable costs, no unattended forge simulation or heat catch-up |
| Occupancy | One actively working manual job per placed station; handheld tools retain existing sharing | Anvils/forges have meaningful capacity; pause or supply failure frees them |
| Cancellation | Keep actual reserved ingredients refundable; spent fuel/electricity stays spent | Extends current crafting semantics, including reserved solder/electrodes |
| Older sword routes | Add the same workshop gates to both recipes producing `arming_sword` | Forge progression has value; existing cold-work machete/spear routes remain early options |
| Recovery | Portable station equipment can be packed when idle, cold, and empty | Recovery preserves the original equipment item; fixtures can remain fixed |
| Crafting protection | Ordinary accessible tool traits, including existing handheld-tool sharing; proper use is implicit | Acquiring job-appropriate protection matters; existing selection and paid-work rules apply |

The runtime rules and matching workshop gates for both sword routes are accepted.
The [W3A/W3B implementation record](workshop_rosters.md) owns equipment identities,
quantities, work/fuel tuning, acquisition and verification. The earlier
[W3 proposal](workshop_content.md) is historical source material for revision
against the accepted [shared crafting progression](crafting_progression.md). Its low
station/sword skill gates are not accepted contract extensions.

The [W3A/W3B roster](workshop_rosters.md) supplies useful practice
products, suitable blade/tool stock, recovered-component electrical assembly and
both welding/protection tiers. Its scope and learning revisions are authorized.

The subsequent [progression discussion](crafting_progression.md#accepted-outline-metal-preparation-and-forging)
accepts anvil tiers 1 (rail/block), 2 (fabricated) and 3 (professional), with first
forge/tongs construction around Fabrication 2, smithing hammer/fabricated anvil
around 3, and proper swords at 4. Both sword routes require anvil 2,
hammering 2, metal holding 2, forge heat 1 and suitable blade stock. Ordinary
charcoal forging remains sufficient. These are accepted authoring anchors;
the implementation record lists their authored budgets and checks.

The later progression discussion also accepts soldering/welding capability and
equipment-construction bands, including an Electronics-3/Mechanics-2 makeshift
welder. Welding additionally requires accessible protective glasses (tier 1) or a mask
(tier 2), with requirements determined by the job. See
[welding protection](crafting_progression.md#welding-protection) for the accepted
ingredient/tier outline and ordinary tool-selection/paid-work semantics.
Protection is an ordinary `tool_traits` requirement; nearby accessible equipment
and normal tool sharing qualify. The user retired M-Worn's separate worn-only
mechanics proposal in favor of this abstraction.

## 2. Inspected starting point

This is the historical pre-W1/W2 inspection. Current content ownership is in
[the semantic source map](content_pack.md#source-organization); the
[W3 roster](workshop_rosters.md) records the implemented sword and equipment gates.

- `CraftingSystem::Gather`, `Select`, and `Fund` in
  `src/crafting/crafting_system.cpp` select items, including nearby object-storage
  contents. Objects themselves supply no traits.
- `ObjectDefinition` has no station traits or forge behavior. Its crafting machine
  has one input type, one output type, and one fixed recipe.
- Manual jobs own actual reserved ingredients in an unfinished item. Handheld
  tools are shared; all distinct powered tools pay per funded slice. Actor speed
  changes elapsed time, while costs follow base work. Pause finishes a paid slice.
- D1 grid machines own autonomous work and paid ticks. Stop freezes their paid
  remainder; power loss waits and retries automatically. Their inputs currently
  disappear at start, unlike manual-job ingredients.
- `PlacePowerObject` accepts batteries, generators and grid machines. Batteries
  retain their original item; the other placements consume it. General station
  placement and player recovery are additions.
- `soldering_iron` and `solder_electrical_connectors` already provide powered
  soldering. There is no forge, anvil, welder, or charcoal item in the inspected
  catalog.
- `make_salvaged_arming_sword` in `data/crafting/low_tech.json5` and
  `make_current_arming_sword` in `data/crafting/slice.json5` both produce
  `arming_sword` with hammering/cutting and no heat. The former has no skill gate.
  Their current descriptions deliberately describe cold work.

Relevant existing tests were read: `crafting_world_tests.cpp`,
`house_crafting_tests.cpp`, `power_crafting_tests.cpp`, and the grid-machine cases
in `object_behavior_tests.cpp`. They establish paid-slice survival, shared-tool
accounting, blocked storage reach, and manual versus machine pause behavior.

## 3. Equipment, reach, and capabilities

### Representation and placement

Portable tools remain items. An anvil, forge, or other installed workstation is a
typed world object. Station capabilities belong to the placed object; carrying
its packing item grants none of those capabilities.

Add optional object tool traits using the existing trait IDs and tier comparison.
Passive stations need no ticking behavior. A forge has a small explicit behavior
for fuel and readiness. Disabled or broken objects supply no new work.

Portable station placement retains the actual equipment item as a protected
backing child. It is excluded from ordinary storage, tool selection, and recycling.
It is part of the equipment, not volume occupying its public storage bin.
Packing returns that original item, subject to carry capacity with ground fallback.
Placement/recovery validate access, support, occupancy and state before mutation.
Proposed base cost: 400 ticks each, following D1 equipment placement.

A portable definition needs one unambiguous backing-item mapping. Generated
portable equipment creates that item once; restoration never creates it again.
Fixed fixtures have no packing action. Packing requires no active claim, no
machine job, empty public storage, and, for a forge, cold state and zero fuel.
An explicit cold-forge fuel-discard action allows emptying a partial charge.
Destroying a station releases claims and drops its backing/public items once;
remaining forge fuel is lost.

### Reach and selection

Use the job's existing same-layer crafting radius: default 1, configurable 0..8,
snapshotted on start/resume. Reuse the passable-path and blocked-diagonal rules.
A blocking station may be a reachable endpoint, like a cabinet, without requiring
a storage capacity. Tools/materials elsewhere keep their current access rules.

Select qualifying available providers in persistent-ID order, with an explicit
provider-kind tie-break if necessary. Exact retained-item inputs still require
items. A provider can satisfy multiple traits and is claimed/billed once.
Skip already claimed stations; inspect the chosen providers in the UI. As today,
selection need not search every alternative combination after a supply refusal.

### Proposed trait vocabulary

| Trait | Tier 1 | Higher tier when a recipe demonstrates a need |
| --- | --- | --- |
| `hammering` (existing) | Ordinary suitable hammer | Tier 2 smithing hammer |
| `anvil` | Mounted rail/solid steel block | Tier 2 fabricated workshop anvil; tier 3 professional anvil |
| `forge_heat` | Ready hand-blown forge | Higher-capability forge |
| `metal_holding` | Suitable pliers for small/long stock | Tier 2 forging tongs |
| `soldering` (existing) | Found or assembled soldering iron | Precision equipment |
| `welding` | Basic arc welder | More capable welding equipment |
| Metal cutting/drilling | Hacksaw/hand drill | Only add higher tiers with useful recipes |

Tiers are capability gates, not automatic speed or efficiency multipliers.
Broad `house_cutting` does not make a knife a metal saw. Add narrowly useful
metalworking traits rather than changing that existing trait's meaning.

Keep eight input types, including retained tools, and four required traits.
A forging recipe can use hammering, anvil, forge heat and metal holding, with an
exact retained file if needed. A four-trait object limit also matches current
items. No demonstrated D2 example requires increasing these bounds.

## 4. Manual jobs and station occupancy

Keep the unfinished item as the sole manual-job owner. Ingredients, work, worker
contributions, and fractional balances remain there. There is no second job in the
anvil or forge.

Acquire all selected station claims and fund a slice atomically. An active job
claims each selected station once, regardless of how many traits it supplies.
Another player receives `StationBusy` when otherwise suitable stations are already
claimed; an absent capability is a distinct missing-station/tool outcome.
Handheld tool sharing retains its current behavior.

Claims last while the job actively continues. Pause finishes the current paid
slice and releases claims. Completion, cancellation, supply refusal, worker
unavailability, and station loss also release them. A paused unfinished item
does not reserve workshop furniture indefinitely. Claims are derived from job
bindings, not a second independently saved ownership ledger.

Ordinary movement/packing of a claimed station is refused. Destruction or an
administrative move can invalidate access: already paid work survives; the next
slice needs available tools/stations again. Worker unavailability releases claims
while preserving paid work, as it currently preserves prepaid tool use. Resuming
that paid remainder does not rebill or reclaim already purchased station service;
new work must acquire claims normally.

Server command order resolves initial competition. Continuing due jobs use
persistent job-ID order. Manual work has no automatic waiting queue: a refused or
paused job needs explicit resume. A continuously working job may keep its station
until it pauses or finishes; this is distinct from D1's rotating machine priority.

Materials, solder, and welding electrodes are actual reserved ingredients.
Completion consumes them; cancellation returns them using the existing
inventory/ground fallback. Heat and electricity already paid are never refunded.
This deliberately uses the current coarse crafting abstraction rather than
introducing partially used electrode/spool state.

Manual output retains current behavior: ground at the unfinished item's actual
spatial root, then optional collection if accessible and carry capacity permits.
A station does not become the output owner merely by supplying a trait.

## 5. Forge fuel and usable heat

### Recommended gameplay model

A forge stores **remaining fuel budget**, expressed in integer heat units, and a
`Cold`/`Ready` state. Heat units cannot charge batteries or feed electrical grids.
There is no separate accumulated-temperature or hot-metal inventory.

- Load a whole accepted fuel item only if its entire yield fits. Consume that item
  and add its heat budget atomically. Loading does not ignite.
- Ignite requires fuel exceeding the ignition cost. Debit that cost, set Ready,
  and charge the ordinary actor action cost. This is an immediate action effect
  with setup time, not an autonomous warm-up job.
- A Ready, enabled, unbroken forge supplies its authored `forge_heat` tier.
  Recipes requiring heat also declare a positive `heat_per_work` rate.
  Only forge objects supply this trait; one selected forge funds the recipe's
  heat requirement.
- Pay fuel for each new manual work slice. Even a fractionally zero-cost slice
  requires a Ready forge with positive remaining fuel.
- Extinguish changes Ready to Cold and retains unspent fuel. Relighting pays the
  ignition cost again. Exhaustion makes the forge Cold; refueling requires a new
  explicit ignition. Already funded work survives either transition.
- Ready but unused equipment burns no fuel and holds no offscreen columns.
  Save/reload and chunk inactivity preserve its state without elapsed catch-up.
  Disable/breakage makes it unusable; re-enabling alone does not mint fuel.

Lighting and tending are included in the ignition action for this first slice.
A separate match-count/ignition-source system is not a prerequisite. Lighting and
the absence of idle burn are accepted gameplay abstractions.

Prototype values for review, not final balance:

| Quantity | Proposed value |
| --- | --- |
| One charcoal charge | 1 kg item; 6,000 heat units |
| Crude forge capacity | 24,000 heat units |
| Ignition | 1,000 heat units; 400 base action ticks |
| Load / extinguish / discard cold fuel | 100 base action ticks |
| Small forging recipe | 2,000 base work; `heat_per_work = 10000` |

Start with charcoal. A typed fuel-entry list can admit coke/other fuels later,
with explicit item-to-heat yields. Arbitrary combustible items do not qualify.
Charcoal must be obtainable in ordinary hardware/outdoor/workshop loot before
forge-dependent work; charcoal production is not needed for the first route.

### Accounting and interruption example

Use `ratioScale`: a rate of 10000 means one heat unit per base work. Store a
separate job-owned heat remainder with denominator 100000000, following the
existing fixed-point work calculation. Never mix it with electrical fractional
debt. It survives changing forges, pause, and save.

Preflight station claims, heat, local tool charge, and the aggregate D1 grid
demand before committing any of them. Failed funding acquires no claims and spends
no fuel, power, work, or ingredients. An already active job then enters its
ordinary paused/refused state and releases its previous claims.
Bound paid manual slices to at most 100 elapsed ticks when
using a forge, and additionally to D1's boundary when using grid tools.

Example: load one 6,000-unit charge, ignite (-1,000), and perform a 2,000-work
recipe (-2,000). It leaves 3,000 units. Pausing after 1,000 credited work leaves
4,000 units when no next slice has been funded. Resuming without extinguishing
still ends at 3,000. Extinguishing and relighting before the remaining work ends
at 2,000 instead. A faster worker changes elapsed time, not these work costs.
Paid but uncompleted work and fuel debits remain conserved through save/reload.

Continuous burning and warm-up/cooling were not selected: they would require
elapsed-time fuel scheduling, residency rules, and different speed/interruption
economics.

## 6. Reachable content slice

These are proposed roles/routes, not newly authored definitions or an approved
complete roster. The earlier [bounded W3 roster](workshop_content.md) supplies
candidate IDs and recovery/operating examples, but its progression must be revised
after the [broader framework](crafting_progression.md) is discussed.

| Step | Concrete route and useful result |
| --- | --- |
| Crude anvil | Mount one salvaged solid steel block on timber using fasteners and hand tools. The block is ordinary hardware/industrial stock; assembling scrap into a solid block is not a bootstrap assumption. |
| Crude forge | Refractory bricks, sheet/pipe, timber, leather and fasteners form a hearth with manual bellows. Use found saw/drill/wrench/hammer capabilities; no forge, soldering or welding requirement. Refractory material needs explicit early loot. |
| First forging | Two steel rods plus a fastener become forging tongs. Hammer, crude anvil, forge heat and suitable ordinary pliers handle the bootstrap operation. Tongs provide better metal holding for subsequent work. |
| First soldering tool | Mechanically assemble a salvaged heating element, cable, copper, insulating handle and fasteners using pliers/screwdriver. No soldering trait prerequisite. The result draws from D1 supply. |
| Found-tool alternative | Find a proper anvil/tongs/forge or soldering iron in workshop loot and use the same operating requirements. Finding tools bypasses construction only. |
| Found welder | Scavenge a corded arc welder and electrode packets from repair/workshop contexts. Use building mains, then a D1 generator/battery, to make a welded workshop-rack kit with useful storage after placement. |
| Branch reconnection | Welding supports improved workshop construction; forging supplies forming tools. Better equipment and skill/knowledge gates support later substantial weapons and fabrication. |

The forge assembly can fit six consumed input types plus two exact retained tools;
cutting/drilling can use traits. The tongs example uses four traits and fewer than
eight input types. Author exact counts, mass loss, recycler yields, and skill
training together with the roster; solder/electrodes are nonrecoverable in
completed-product recycling.

Preserve stable IDs for the two existing sword recipes and add matching
workshop gates to both. Review their skill discrepancy and revise the cold-work
descriptions with that change. Keep useful lower-tier cold-work routes such as
`make_salvaged_machete` and `make_steel_spear`. Audit every producer/recycler of a
chosen gated output, including legacy manifests, before accepting reachability.
The later accepted D3 direction deliberately allows curated printer routes to
use Computers and machine/file/feedstock requirements instead of the manual
Fabrication gates. Treat those as explicit alternative production routes in the
audit, rather than imposing hand-forging requirements on printer operation.

Use existing `loot_tools`, `loot_metal_stock`, `loot_hardware`,
`loot_electrical_parts`, `loot_auto_repair` and workshop placement contexts.
Bulky stations/stock need ground-only pools like `loot_workshop_bulk_ground`;
do not enlarge furniture loot beyond its container budgets accidentally.
New equipment starts cold, empty and idle. Generated acquisition changes need an
explicit generation-version decision before shipping; archived chunks retain
their saved depleted state.

## 7. Autonomous machine contract for D3

Manual labor and machine time stay explicit:

| Property | Manual job | Autonomous job |
| --- | --- | --- |
| Owner | Unfinished item | Machine object |
| Duration | Base work scaled by worker speed | Recipe machine ticks; no worker-speed scaling |
| Supply loss | Finish paid slice, then pause; explicit resume | Finish paid slice, then wait and retry |
| Manual pause/Stop | Finish paid slice | Freeze paid remainder immediately |
| Offscreen work | Requires an eligible active worker | D1 machine/source residency |
| Skill practice | Current completed-work contribution rules | Commissioned batches award the initiator on completion under D3; continuous production earns none |

Add explicitly selected machine recipes with typed IDs, bounded consumed-input
lists, one output type/count, machine duration and positive grid energy-per-tick.
The machine definition lists its supported recipes. Use the existing eight-input
and 255-count bounds for this new recipe form; retain the legacy fixed-recipe
machine's existing bounds/behavior.

There is one job per machine, explicit Start/Stop/Resume/Cancel/Collect, and no
implicit repeat queue. Start selects/reserves actual leaf items from accessible
sources, including the machine's public storage, in persistent-ID order. It may
record run intent while unpowered, matching D1. Resume never consumes more inputs.
The object owns the job and its reserved original items; they are excluded from
public transfers and crafting selection.

Use one finite shared staging/output bin. At start, reserve space for:

```text
unreserved contents volume + max(reserved input volume, complete output volume)
```

Subsequent insertions honor that reservation. Collect can remove unreserved
contents but not job inputs. Completion atomically replaces inputs with output
once, so capacity cannot unexpectedly turn completion into a fatal failure.
Initialize product energy stores to zero, then transfer only compatible remaining
ingredient energy under the existing crafting conservation rules; paid grid work
is not product charge.
Cancel returns original ingredients to the machine's public bin, loses progress
and paid work, and refunds no electricity. Destroying the machine drops its
original ingredients/public outputs and discards the job; it creates no product.

Jobs retain their capacity reservation while stopped. Player movement/packing of
a machine with a job is refused. Completion and Cancel release run intent/holds;
a power-waiting intended job retains D1 residency. Finishing a job does not start
the next one.

Use D1's paid slices, rotation and grid authority directly. Generalize the existing
machine behavior's explicit dispatch rather than introducing another scheduler.
Keep legacy local-energy and fixed-recipe machines readable; never reconstruct
already consumed legacy inputs to invent a cancellation refund.

D3 adds file eligibility, printer tiers, feedstock, and human setup/post-processing
requirements to this boundary. An ordinary known human recipe does not yet imply
printer permission. D2 does not assume a generic multi-phase job framework:
separate manual preparation recipes can feed a selectable autonomous recipe.

#### Later progression requirements and queued extensions

The user requested selected unattended process machines, specifically coal
gasification and oxygen-free wood-gas production for adapted gasoline engines.
See [the process-machine brief](crafting_progression.md#queued-design-selected-unattended-process-machines).
Heat/fuel funding, gas handling, possible byproducts and optional repeat behavior
need a separate design review against this single-output, grid-funded, single-job
contract. W4 is a starting point, not evidence those extensions already work.
Most crafting remains manual and tool-based.

For D3, operating printers uses Computers rather than Fabrication. Simple plastic
printing needs very little skill; more demanding/durable and advanced-material
work uses appropriate Computers skill, machine, design and feedstock. Basic
printers should be reasonably accessible as futuristic salvage; constructing one
can still be difficult. Resolve setup/processing within those requirements.
The [accepted D3 learning rule](digital_fabrication_design.md#5-production-modes-and-learning)
awards commissioned-batch learning to the initiating character on completion,
independently of collection, including offscreen completion and save/load.
Continuous production earns no ongoing XP. This supersedes the earlier blanket
no-passive-operator-XP rule. Batch cancellation/destruction grants no completion
award; pause/resume preserves it. Nominal learning scales with recipe executions,
not Start count or elapsed machine time; [W4/D3 runtime](digital_runtime.md) implements
captured awards and atomic completion, including archived recipients.

Selected intricate printer recipes also require usable schematics distinct from
the control software. The first D3 example instead uses a contemporary home
polymer printer with supplied simple designs, requiring no acquired schematic.
Ordinary copying/import remains in scope; schematic extraction and the
recovered-design example wait for a separate hacking effort. Do not introduce
temporary schematic-crafting routes. See
[printer schematics](crafting_progression.md#schematics-for-intricate-prints).

Selected sophisticated equipment across trades can also require compatible
production software, including packages acquired by hacking factory systems.
Software can be copy-protected, and broken machines can retain extractable
software. Equipment operability and information recoverability need distinct
design decisions; the later D3-C/hacking effort owns recovery/protection actions
and hacking laptop modifications, outside the current D3-A/B design.
See [production software](crafting_progression.md#accepted-direction-production-software).
The [accepted D3 information boundary](digital_fabrication_design.md) uses explicit
imports into persistent machine-local design libraries. Startup validates and
captures the job's required instructions/software eligibility. Carrier removal or
library deletion does not invalidate the existing job, including Stop/Resume and
save/load; new jobs use the current library. Nearby media alone do not qualify.
This preserves the reserved-input and paid-work rules above. Detailed capture,
ordinary deployment and compatibility still need implementation design; extraction
and protected acquisition have their own later contract.

## 8. Ownership, interfaces, persistence, and presentation

### W1 runtime interfaces

- Objects accept `tool_traits` (up to four existing trait/tier requirements) and
  optional `packing_item`. W1 stations use `behavior: "none"`; packing items point
  back through `placed_object` and cannot supply carried tool traits.
- `World::PlaceStation` / `PackStation` return `StationActionResult` with a typed
  outcome and unscheduled base cost (400 ticks). The server owns action scheduling.
  `QueryPackStation` is read-only; `CollectStations` fills reusable caller storage.
  These explicit transactions keep backing-item mutation inside world ownership.
- `InteractiveObject::equipment` is a protected child handle, separate from public
  storage accounting. Generated stations create one original; archives resolve its
  saved ID. Recovering preserves identity and item state, with ground fallback.
- Manual jobs own bounded persistent `stations` bindings. Claim queries derive
  from assigned jobs rather than maintaining a second ownership ledger.
- The shared Power workshop menu places station equipment with `p` / Enter and
  packs the station at the cursor with `r`. Station tiers, availability and worker
  IDs are visible without a multimeter; crafting jobs list their selected stations.
  Headless commands are `stations`, `place-station <item> <x> <z> <y>` and
  `pack-station <object>`.
- Save format **16**, manual-job payload **3**, and LAN protocol **24** carry these
  fields. Older supported save envelopes and job payloads retain paid work with
  empty station bindings. No generation-version change is part of W1.

### W2 runtime interfaces

- `ForgeDefinition` contains capacity, ignition cost and an immutable typed fuel
  list. Crafting and recycling definitions accept `heat_per_work`; it is positive
  exactly when `forge_heat` is required. Only forges supply that trait.
- `World::UseForge` and its read-only `QueryForgeAction` use the closed
  `ForgeAction` set: LoadFuel, Ignite, Extinguish, DiscardFuel. They reuse
  `StationActionResult` and return unscheduled base costs. World's transactions
  own whole-item consumption; forge behavior owns only `{fuel, ready}`.
- No per-object job or scheduler is added. An immutable `Vector` of fuel entries
  avoids a speculative content bound and inline storage paid by non-forge
  definitions. Only forge instances allocate forge behavior state.
- Manual jobs fund at most 100 elapsed ticks of forge service, preflighting heat,
  local stores, aggregated grid supply and claims before mutation. The independent
  heat remainder travels with the unfinished item.
- Job payload **4**, strict forge payload **1**, and LAN protocol **25** preserve
  heat debt, readiness, fuel and paid work. Save envelope remains **16**: both
  payloads already reside in versioned opaque state fields. Older jobs load with
  zero heat debt; no generation-version or shipped roster change is part of W2.
- [Forge schema/API](objects_api.md#fueled-manual-forges) and
  [player controls](power_presentation.md#fueled-workshop-controls) document usage.
  Both local and LAN commands are available through shared terminal/SDL controls
  and headless input.

Cross-system contracts retained for W3–W4:

- Content: optional object traits, typed forge fuel/settings, recipe heat cost,
  portable-station backing mapping, and explicit machine-recipe definitions.
  Validate unknown fields, tier/rate bounds, impossible trait/heat combinations,
  packing mappings, and unresolved references at load time.
- Crafting: a closed item-or-object provider value containing typed handles.
  Reusable selection/payment scratch; active jobs retain persistent station
  bindings and their own heat remainder. Reacquire entities after mutations.
- World: explicit station place/pack and forge load/ignite/extinguish/discard
  transactions; selected-machine recipe start/cancel. Return typed outcomes such
  as missing station, busy station, cold forge, insufficient fuel and capacity.
  Invalid handles/content IDs remain typed errors.
- Views: owned station/provider snapshots, fuel and readiness, required/available
  tiers, selected machine recipe, paid/remaining work and reason for waiting.
  Expose commands through the server, headless interface and shared terminal/SDL
  menus; use persistent IDs over the network and revalidate every request.
- Saves: version manual-job payloads for heat/bindings and object payloads for
  forge/selected-machine state; update the save envelope where its structure
  changes, protocol and content fingerprint for new fields/commands. Allocate
  actual version numbers at implementation, not in this draft.
- Restoration: resolve stable content names, validate actual backing/reserved
  children, capacity reservations, fuel and remainder bounds, job/claim
  consistency and D1 device-index relationships before publishing state.
  Missing new fields in older manual jobs mean zero heat debt/no station bindings;
  existing funded work survives, and new slices recheck current requirements.

Rebuild active claims from eligible jobs in stable order; duplicate persisted
active claims are invalid rather than permission to duplicate station service.
Cold/Ready manual stations add no residency reason. Selectable grid machines
participate in D1's existing indexed machine/source holds. A restored stopped
machine remains stopped, and restoration grants no initial fuel, charge or output.

## 9. Implementation slices and acceptance

| Slice | Scope | Gate / focused evidence |
| --- | --- | --- |
| W1 Station foundation — verified | Object traits, provider selection, claims, placement/packing, saved/LAN inspection | Occupancy/recovery accepted. Thirteen focused tests cover tiers/reach, contention, atomic refusal, recovery, inactivity/death, save migration and LAN/UI. |
| W2 Fueled manual work — verified | Forge state/actions, heat rates and atomic combined payment | Verified conservation, speed independence, failed mixed power/heat payment, partial fuel, pause/reignite, source loss, elapsed partitions, paid-work reassignment, disk/LAN and shared controls. |
| W3 Playable metalworking | [W3A/W3B implementation](workshop_rosters.md) follows [accepted progression](crafting_progression.md) and the [delivery plan](crafting_delivery_plan.md) | 11 + 11 new items, 8 existing-definition upgrades, 17 new crafting recipes and 5 objects. W3A uses W1/W2; W3B uses D1 and ordinary protection traits. See the roster for budgets, affected-save policy and current verification. |
| W4 Selectable machines | Implemented recipe selection, reserved originals/output space, cancel, D1 integration and durable completion learning | [Pass-3 content/evidence](pass3_content.md); seeded integrated balance playtesting remains. |

Each implemented slice includes its player commands/status and relevant
serialization; it is not complete as an isolated backend feature.

Acceptance walkthroughs:

1. Construct crude anvil/forge from pre-forge loot, fuel/ignite, make useful tongs,
   pause halfway, save, reload and finish with the predicted fuel balance.
2. Repeat with found equipment. A second worker competes for the same anvil;
   refusal spends nothing, and pausing the first job makes it available.
3. Remove/break a station or exhaust fuel during a paid slice. Preserve the
   purchased work, stop subsequent funding, and resume at valid equipment.
4. Assemble a first soldering iron without soldering; operate a found welder
   on mains and finite D1 supply. A combined shortage changes no other resource.
5. Start a selected machine recipe, leave the area, lose supply, restore supply,
   and collect one output. Stop/save/reload must preserve paid ticks and capacity.
6. Try filling a busy machine's bin, cancelling, destroying and repeatedly loading
   its save. Preserve originals or outputs exactly once and grant no free fuel.

### Current evidence and handoff

W1 is complete, including content validation, runtime ownership/claims, server and
headless commands, shared terminal/SDL controls, persistence and LAN codecs.
`tests/workshop_tests.cpp` contains the focused acceptance cases.

W1 verification (prior pass):

- `cmake --build --preset win-msvc-debug`: passed.
- Debug CTest selection covering crafting, power, saves, objects, inventory,
  networking, content and death: **344/345 passed** on the broad run. Its remaining
  new death case needed a proper damage/finalization fixture; the corrected case
  passed its focused rerun. Earlier version/packet-offset fixture failures were
  corrected and passed before that broad run.
- Final workshop CTest selection
  `-R "Station |Portable stations|Workshop |Failed station"`: **13/13 passed**.
- `cmake --build --preset win-msvc-sdl --target cataclysmx_sdl_game`: passed.
- `ctest --preset win-msvc-sdl -R "^sdl_frontend_smoke$"`: **1/1 passed**.
- Tracked diff and new-file whitespace checks: clean.

W2 is complete: forge schema/validation, actions, atomic manual funding,
independent heat debt, cold/empty recovery rules, persistence, shared terminal/SDL
and headless controls, and LAN state/command validation. Astra's read-only review
found no correctness blockers in ownership, conservation, interruption or transport.

W2 verification commands:

```powershell
./scripts/build.ps1 -Preset win-msvc-debug -SkipTests
./scripts/build.ps1 -Preset win-msvc-sdl -SkipTests
./scripts/test.ps1 -Preset win-msvc-debug -R '([Ff]orge|Crafting payload four|Station |Portable stations|Workshop |Failed station|Crafting state|Current protocol preserves)'
./scripts/test.ps1 -Preset win-msvc-debug -CTestArgs @('-R', '([Cc]raft|[Rr]ecycl|[Pp]ower|[Ss]ave|[Nn]etwork|LAN|[Oo]bject|[Ii]nventory|[Cc]ontent|[Ss]torage|[Cc]ommand)', '-E', '([Ff]orge|Crafting payload four|Station |Portable stations|Workshop |Failed station|Crafting state|_roundtrip|smoke)')
./scripts/test.ps1 -Preset win-msvc-sdl -R '^(sdl_frontend_smoke|sdl_lan_roundtrip|headless_lan_roundtrip)$'
git -c core.safecrlf=false diff --check
```

- Both Debug and SDL builds passed, including terminal/headless and SDL frontend.
- Final focused selection: **45/45 passed**. The initial run's server fixture
  incorrectly assumed the demo had tool traits; it now loads a real `forge_heat`
  definition. Exhausted-forge save and paid-work reassignment coverage was added.
- Regression selection: **321/322 passed** on its broad run. The sole failure was
  an old protocol-23 assertion; updated to protocol 25, it passed in the final
  focused run. No production-code changes were needed after these runs.
- SDL frontend smoke, SDL LAN and headless LAN: **3/3 passed**.
- Tracked diff and new-test-file whitespace checks: clean.
- Focused coverage lives in `tests/forge_tests.cpp`, `tests/forge_content_tests.cpp`
  and the object, crafting, skill and workshop persistence/network suites.

The [shared progression](crafting_progression.md) now records accepted skill,
equipment and product bands. The [delivery direction](crafting_delivery_plan.md)
is agreed. The [W3A/W3B implementation](workshop_rosters.md) records concrete
budgets and compatibility policy. Repeated practice and incomplete later
progression remain acceptable.

W3's original low gates and blanket runtime assumptions are historical. Forging can
use W1/W2; welding protection uses ordinary traits and handheld grid tools use D1.
Powered placed manual equipment uses [M-Bench](powered_manual_stations_design.md)'s
station-local grid funding through the existing manual job. W3 verification is recorded with its roster.
W2 changes no shipped item roster or generation version. Review section 7 with
D3 before W4.
