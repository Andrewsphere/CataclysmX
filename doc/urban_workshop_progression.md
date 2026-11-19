# Urban scavenging, power, and workshop progression

## Status and use

**Coordinating plan; electricity implemented; W1/W2 verified; crafting delivery direction accepted.** This document records the agreed
direction and organizes subsequent design, implementation, content, and validation
passes. Implemented electricity contracts are in [power design](power_design.md)
and [power commands and presentation](power_presentation.md).
The user authorized the D1 implementation in this pass, including its small playable
content slice and diagnostics, ahead of the later workshop/city design passes.

The intended loop is:

**Explore settlements -> acquire equipment, materials, and information -> establish
power and a workshop -> manufacture better equipment -> enter larger, more valuable
cities -> recover advanced fabrication capability.**

Every later pass starts here, reads the relevant current module and tests, and
updates its status, decisions, dependencies, and verification evidence here or in
the linked durable subsystem design. Accepted goals below are requirements;
recommendations are proposals for the design passes to resolve with the user.

## 1. Accepted goals

| Area | Agreed direction |
| --- | --- |
| Order of work | Electricity design accepted; implement D1 and its functional content slice now. Use later design passes for broader workshop, information, and city contracts. |
| Electricity | Charge-based supplies/loads, AABB-union grids, generated building mains, installed batteries, generator-only operation, timed machine slices, and machine-held residency with player/debug inspection. |
| Catalog | Unify the earlier item slice and expansion into a coherent catalog and progression; authoring batches should not determine long-term source organization. |
| Knowledge | Integrate books, manuals, and computer information sources into crafting and learning. |
| Production software | Selected high-tier components across trades require software to control sophisticated equipment. Factory systems can be hacked to acquire it; protected software can require hacking to make a usable copy, and broken machines can retain recoverable software. Coordinate information/machine contracts with D3 and recovery/access actions with the hacking design. |
| Workshop | Include functional soldering, welding, anvils, fueled forges, and external workshop power. Tools may be found or crafted; dependencies should create reachable progression. |
| Digital fabrication | Common scavenged basic 3D printers; simple plastic printing needs little Computers skill, stronger/advanced outputs need appropriate skill, equipment and material. Printer use requires Computers rather than Fabrication; constructing one can be difficult. Metal printing is an advanced capability. |
| Process automation | Selected unattended input/output machines, including coal gasification and oxygen-free wood-gas production for adapted gasoline engines. Most crafting remains manual and tool-based. |
| Setting | Futuristic construction and technology should be common, especially in cities. Support old cities and newer, more cyberpunk-like cities. |
| Terrain | Add more than 18 purposeful terrain variants, including distinct colored concrete and old/new brick. Integrate them into generated places. |
| Encounters | Increase interior zombie population roughly 5–7 times the current baseline and add population outdoors within cities. Large cities should usually require preparation; smaller settlements should be quieter. |
| Rewards | Large cities should provide better loot and worthwhile advanced acquisition opportunities. |
| Artwork workflow | SDL artwork is produced separately on another workstation. This workstation supplies content, terminal presentation, functional integration, and an explicit artwork handoff. |

Exact city dimensions, terrain roster, recipe graph,
spawn budgets, loot weights, and learning costs remain design decisions. No exact
new item count or terrain count beyond the agreed lower bound has been approved.

## 2. Verified starting point before D1

This records the inspected pre-power baseline. D1 changes are recorded below;
use current source/data and the linked power contracts as the authority.

- `data/content.json5` and `data/first_house.json5` load both the `slice_*` and
  `expansion_*` item sources. The first batch added 60 items and the second 80;
  these are historical additions, not the complete current catalog count.
- Both batches have recipes and recyclers. `data/books/starter.json5` and
  `data/items/learning.json5` provide workshop manuals and an ebook reader;
  `data/worldgen/loot/urban.json5` places information carriers in ordinary loot.
  Several older urban documents still describe reading content as deferred.
- `soldering_iron` already exists in `data/items/slice_equipment.json5` as a
  corded tool, without a soldering trait or crafting power configuration.
- Crafting selects nearby items and object-storage contents. Tool traits are
  currently item properties. `crafting_power` names a store on the selected item;
  it is not an external electricity connection. See [crafting](crafting.md) and
  [crafting content](crafting_content.md).
- Existing object machines have a single input/output recipe, their own energy,
  and autonomous elapsed work. New machines currently initialize fully charged.
  They are a starting integration point, not a grid or general printer model.
  See [objects](objects_api.md) and `src/content/object_definition.h`.
- Books share information identity across carriers. Electronic reading requires
  positive local charge but does not currently consume reading energy. Human
  recipe memory, reference access, and skill benefits have different semantics.
  See [skills](skills.md), [skill content](skills_content.md), and
  `src/content/item_definition.h`.
- Generation version 6 plans towns inside a region-local 176x176 envelope within
  256x256 regions. MainStreet, Crossroads, and OffsetNeighborhood are geometry
  profiles; there is no independent old/new-city identity or explicit metropolis
  scale. See [worldgen](worldgen_api.md) and `src/worldgen/worldgen.h`.
- `GenerateChunkPlacements` emits population from building/site patterns and
  selected basements. Parks/squares can have authored inhabitants; ordinary roads
  and alleys have no general city-population pass.
- For scale: `bungalow` has one 25% monster marker (0.25 expected inhabitants);
  `workshop` has 1.4 expected surface inhabitants and 0.3 in its basement. Raising
  probabilities alone cannot deliver the requested increase.
- `data/terrain/base.json5` has basic wood/brick/concrete/metal/plaster walls and
  a few floor/window variants. SDL terrain mappings live separately in
  `assets/tiles/gen1_32x32/tileset.json5`.

Relevant source interfaces include `src/content/crafting_definition.h`,
`src/content/item_definition.h`, `src/content/object_definition.h`,
`src/objects/interactive_object.h`, and `src/worldgen/worldgen.h`.

## 3. Design sequence

Complete D1 first. D2 and D3 then establish compatible workshop and information
contracts. D4 can proceed after D1 using the agreed acquisition requirements;
D5 reconciles all designs before broad content work begins.

**Current planning adjustment:** the reviewed skill/domain outlines in the
[shared crafting framework](crafting_progression.md) are agreed. The
[dependency and delivery plan](crafting_delivery_plan.md) separates the next
mechanics/content slices, including W3A forging and W3B electrical/welding.
W3B uses ordinary accessible/shared tool traits for crafting protection; M-Worn
is retired. Powered placed manual equipment needs its own funding extension.
W3A can use W1/W2. The user accepted this delivery direction and authorized the
[W3A/W3B implementation](workshop_rosters.md), which owns concrete balance choices,
acquisition and verification. Later content
fills selected branches; immediate end-to-end completion or a winnable game is
not required.
This cross-domain plan informs D3–D5 without preempting their runtime contracts.

Each design pass produces a decision-ready subsystem document under `/doc`, with:

1. Player actions and one concrete end-to-end example.
2. Chosen rules, brief rationale, and explicitly unresolved questions.
3. Minimal content/runtime ownership and API shapes; serialization, commands,
   presentation, and generation implications where applicable.
4. Failure/interruption rules and focused acceptance scenarios.
5. A bounded implementation breakdown and dependencies on other passes.

Design drafts become accepted contracts after user review. Later implementation
passes follow those contracts and surface a necessary contract revision before
changing dependent behavior. Statuses are `Pending`, `Draft`, `Accepted`,
`In progress`, and `Verified`; a document alone does not verify implementation.

### D1 — Electricity and grid power

**Accepted contract:** [power design](power_design.md). The implementation includes:

- World-owned grids containing a union of inclusive, axis-aligned boxes. Walls do
  not block abstract wiring. Player additions merge overlapping/face-touching
  grids on X, Z, or Y; diagonal-only contact does not connect. Adjacent generated
  buildings remain separate until a player extension joins them.
- Each newly generated v7 building has one mains grid covering its authored
  floors/basement. Unlimited mains ends permanently on one seeded, hidden day
  from 7 through 14. Extending a serviced grid carries its mains connection;
  wilderness wiring alone does not create mains.
- Generators can power loads without batteries. Placed battery objects retain
  the original rechargeable item and its sole authoritative charge store, with
  no discharge-rate limit. Backpack batteries do not join automatically.
- A reusable wiring kit installs a single-layer rectangle, default maximum
  16×16, configurable from 1 to 64 per axis. Ordinary copper wire pays for newly
  covered area: one material item per 16 tiles, rounded up. Battery installation
  also needs the kit and one copper-wire item under the default content values.
- Connected corded tools bill the worker's grid. Autonomous grid machines buy
  bounded work slices, keep progress during outages, and automatically retry.
  Manual Stop preserves the paid remainder but requires explicit resume. Shared
  machine first opportunity rotates each power interval.
- Generator output is a rate-limited, expiring interval allowance; fuel is spent
  only on delivered charges. Surplus charges connected batteries after due jobs.
  Fuel, battery energy, and funded work are never duplicated in the grid state.
- Player-started equipment holds its own column and connected source columns.
  Generated equipment is idle. Stopped/completed/disabled/broken equipment releases
  its hold; waiting jobs retain theirs. Source columns also activate while needed
  by a player on the grid. Offscreen workshops therefore progress in game time.
- Player multimeters show coverage and supply. Host DEBUG views show grid boxes,
  loaded/active chunks, player/machine/shared reasons, machine-only coordinates,
  and power workload counters in terminal and SDL.
- Save format 15 and protocol 23 preserve geometry, source/job state and remote
  holds. Generation v7 retains v6 terrain/layout semantics and adds power grids.
  Legacy local-energy demo machines retain their existing behavior.

The functional content slice adds wiring kits, portable fuel generators and fuel
packets; upgrades rechargeable batteries, multimeters and soldering irons; and
uses a grid scrap press to demonstrate unattended work. D2/D3 still own general
station/printer recipes, computers and the broader fabrication progression.

**Playable example:** scavenge a kit, copper wire, multimeter and corded soldering
iron; use building mains to make connectors; install a rechargeable battery;
place/refuel/start a generator; continue crafting and run the grid press after
shutdown. Leave an active job behind, inspect its machine-only columns in DEBUG,
and return to its conserved output/progress. See the power documents for commands,
failure outcomes, accounting rules and verification evidence.

### D2 — Workshop stations, fuel, and metalworking

**Deliverable:** `doc/workshop_design.md`; consumes D1.

**Accepted contract:** [workshop design](workshop_design.md). It specifies placed
station capabilities, one active manual job per station, ignition plus work-based
forge fuel, refundable reserved ingredients with nonrefundable operating costs,
and selectable autonomous jobs using D1 funding/residency. Both reconnecting
progression branches and the matching gates for existing sword recipes are
approved. W1 station foundations and W2 fueled manual work are implemented and
verified. The earlier [W3 roster/balance proposal](workshop_content.md) is historical
source material for revision against the [agreed progression](crafting_progression.md)
and [delivery plan](crafting_delivery_plan.md). W3A forging and W3B
electrical/welding have different runtime prerequisites; the W4/D3 machine
boundary still needs its bounded review.

Decide:

- Portable versus placed equipment representation, item-to-station placement and
  recovery where needed, access radius, and furniture-supplied tool capabilities.
- Common tool traits and tiers, station occupancy, tool sharing, and explicit
  distinction between character labor and autonomous machine work, including
  whether actor speed affects a phase's duration or resource consumption.
- Forge fuel types and represented units, loading/ignition/extinguishing,
  consumption, usable heat, and interruptions. Select a gameplay-scale heat model
  sufficient for forging, with concrete fuel-to-work accounting.
- How anvils, forge heat, hammering/forming tools, solder, welding consumables,
  and electrical supply constrain their respective recipes.
- Job reservation, output placement/capacity, pause/resume/cancel, loss of a tool
  or station, multiplayer competition, and persistence.
- The interfaces printers will consume: machine recipe selection, unattended
  progress, input/output storage, and D1 power accounting.

Recommended progression has branches that reconnect: basic scavenged hand tools
support crude workshop equipment; electrical salvage supports soldering and
welding; fueled forging supports substantial metalworking independently of every
electronic tier. Found equipment can skip its manufacturing step while leaving
relevant skill, knowledge, material, and operating requirements meaningful.

A crude anvil must have a bootstrap route without an anvil. A first soldering
tool must not require soldering itself. A first forge's materials and fuel must
be obtainable before forge-dependent recipes. Audit older routes producing the
same outputs so compatibility recipes do not accidentally bypass progression.

**Exit examples:** obtain or construct a crude anvil and fueled forge, complete a
useful metalworking recipe, and account for interruption; operate a found welder
from external supply; demonstrate both a found-tool and crafted-tool route on the
progression graph. Decide how recipe input/trait limits affect these examples.

**Queued process-machine design:** draft selected unattended processing routes,
starting with coal + water + heat to fuel gas and oxygen-free wood heating to
wood gas, both usable by modestly adapted gasoline engines. Resolve heat funding,
gas handling, byproducts and any repeat behavior against W4's machine contract.
See [the process-machine brief](crafting_progression.md#queued-design-selected-unattended-process-machines).

### D3 — Digital information and additive manufacturing

**Deliverable:** `doc/digital_fabrication_design.md`; consumes D1 and the D2 machine
contract. D2/D3 may exchange drafts before either interface is accepted.

**Accepted progression direction:** operating printers uses Computers rather than
Fabrication. Simple plastic parts need very little skill; demanding/durable parts
and advanced-material printing need appropriate skill, designs, feedstock and
machines. Basic working printers should be reasonably easy to scavenge in this
futuristic setting, while constructing one can require difficult mechanical and
electrical work. These are deliberate alternative production routes to manual
Fabrication. Setup and any processing of ready-to-use printed products must
respect that skill boundary; assembly into a larger mechanism remains a separate
task. See [printer progression](crafting_progression.md#accepted-printer-progression-direction-for-d3).

**Accepted cross-domain addition:** selected sophisticated production equipment
can require compatible production software, including software acquired by
hacking factory systems. This applies across trades, including chemical
processing, machining and electronics as well as printing. Copy protection can
require hacking to make a usable copy; skilled recovery can extract surviving
software from broken equipment. See
[production software](crafting_progression.md#accepted-direction-production-software)
for the accepted acquisition/package/use and recovery direction.

**Accepted progression bands:** the shared framework records
[Computers activity, printer-operation and equipment-construction bands](crafting_progression.md#accepted-outline-computers-and-digital-production).
Prepared industrial printing can use a moderate operation skill while its
equipment, software, design and feedstock remain advanced acquisition goals.
Exact D3 runtime contracts still require their separate design.

**Accepted schematic requirement:** selected intricate printer recipes require
usable schematics in addition to compatible machine/control software. Simple
supplied designs and recoverable intricate designs are the accepted acquisition
outline; see [printer schematics](crafting_progression.md#schematics-for-intricate-prints).

Decide:

- Computer information sources: workshop terminals, household computers,
  technical archives, portable readers, and removable media. Establish the
  initial useful subset and concrete acquisition contexts.
- Information identity versus carrier identity. Reuse existing book/reference
  semantics where appropriate, preserving once-per-character learning across
  print and digital copies.
- Reading, searching, copying/exporting, and using information: required device,
  power, time, accessibility, storage, and the role of the Computers skill. Select
  bounded gameplay actions and costs, including outcomes when power/access is lost.
- Human-readable manuals/recipes versus machine-readable fabrication files.
  Define schematic eligibility for intricate recipes, supplied/preloaded designs
  and how an acquired file reaches a compatible printer/controller. Specify where
  required designs must be accessible during setup/start/resume. Human
  memorization must have explicitly defined relevance here.
- Production software versus product designs/process specifications, including
  packages that contain both. Define machine compatibility, recovery/export,
  local installation/configuration and meaningful Computers work.
- Installed use versus permission to make a usable copy; terminal access,
  copy-protection bypass and damaged-hardware recovery are separate actions.
  Define readable controllers/storage in broken machines, needed interfaces,
  portable package results and any source-changing outcomes. Coordinate actual
  hardware work with Electronics and access/recovery work with Computers.
- Software eligibility at job start/resume and during execution, including
  loss of source access. Coordinate with W4's paid-work and reservation rules.
  Found installed software and basic supplied printer software are accepted
  acquisition routes alongside recovering specialized packages from factories.
- Basic polymer and endgame metal printers: machine tiers, useful outputs,
  compatible feedstock, preparation/post-processing, setup work, autonomous print
  time, power demand, and interruption/recovery rules.
- Learning and automation: what earns skill progress, who receives it, and when;
  how automated production interacts with recipe memorization.
- Endgame acquisition: found operational equipment, commissioning a salvaged
  machine, and any craftable equipment. Specify the reachable route rather than
  assuming a general repair system exists.

Recommended first scope: local/offline archives, portable information transfer,
and curated print designs. Printers manufacture specified products from explicit
feedstock. Found/commissioned industrial metal printers are a strong endgame
candidate, with useful output gated by power, feedstock, files, and competence.
The exact feedstock technology and whether full printer construction is reachable
are decisions for this pass.

**Exit examples:** power a workshop computer, acquire a useful manual and a print
file, carry information to another site, complete a polymer print, and trace the
full path to one worthwhile metal-printed result. Shared information does not
duplicate study benefits; losing access or power has a specified effect.

### D4 — Settlement scale, identity, population, and rewards

**Deliverable:** `doc/city_identity_design.md`; consumes D1 infrastructure rules and
the acquisition needs from D2/D3.

Keep **settlement size**, **architectural era**, and **district use** as distinct
design axes. Older large cities can be crowded and rich in industrial machinery;
new cyberpunk cities can emphasize advanced fabrication and digital information.
Futuristic construction should be a substantial part of the generated world.

Decide:

- Actual small/medium/large settlement footprints, building/site counts, occupied
  floors, spacing, and generation prevalence. Resolve whether large cities fit a
  regional plan or require coordinated multi-region planning. The current street
  profile names alone are insufficient to establish size classes.
- Old, mixed/redeveloped, and newer cyberpunk identities, their frequency, and
  coherent building/district selection. A candidate roster should include
  futuristic housing, commercial, industrial/fabrication, and civic settings.
- How terrain palettes bind to buildings/rooms/districts and remain coherent under
  rotation, upper floors, and materialization. New-era cities need recognizable
  layout/building differences as well as different colors.
- Indoor populations by building use, size, and floor; outdoor density by eligible
  city area, district, and gathering sites. Record ordinary groups and specialties.
- The exact interpretation of the requested 5–7x interior increase: preserve a
  named pre-change baseline and compare equivalent buildings/floors and an agreed
  seed panel. Reconcile it with quieter small settlements and crowded large ones.
  Record size-specific targets; do not silently multiply marker count, chance,
  and a city multiplier into a second unintended increase.
- Outdoor population ownership and streams, city boundaries versus connecting
  wilderness roads, exclusion of blocked/unsupported cells, and overlap with
  existing park/site markers. Ensure one actor per tile and stable generation.
- Better large-city rewards: themed equipment quality, useful quantity, advanced
  machines, technical information, production software and feedstock. Factory
  terminals/industrial archives and broken equipment with recoverable controllers
  should provide software acquisition targets for the future hacking design.
  Explain both where a reward is generated and what
  risks lie on the route to it. Keep early bootstrap resources obtainable before
  advanced equipment is required.
- Town starting position, quieter entry routes and city approaches, the curated
  FirstHouse scenario's population policy, and visibility of danger/reward cues.
- New generation version, legacy behavior, debugging views, and performance
  measurement with the proposed actual actor populations.

**Terrain proposal for review:** target 36–48 new terrain IDs, comfortably above
the requested minimum of more than 18. Reserve concrete quotas in D4 for
weathered/new masonry, several painted concrete colors, industrial panels,
futuristic composite/clad walls, floors/exterior paving, and doors/windows. Reuse
existing identities where they already express the concept; count upgrades
separately. Every addition needs a named place in generated content.

**Exit examples:** representative layouts and target tables for small quiet
settlements, large older cities, and large futuristic cities. Specify footprint,
indoor/outdoor population, meaningful reward availability, and a fixed seed/location
panel for later measurement. Include a route from a quieter start to the first
powered workshop and a preparation-heavy expedition for advanced fabrication.
Separate measured current baselines from proposed targets; realized new-world
distributions and playthrough results belong to implementation validation.

### D5 — Integration contract and content roster

**Deliverable:** extend this plan with the accepted cross-system contract and
rosters; link the accepted D1–D4 documents.

The [crafting delivery plan](crafting_delivery_plan.md) supplies the initial
cross-domain dependency map and bounded slice boundaries. This is planning input
to D5; it does not claim the pending D3/D4 contracts or revised item rosters are
complete. Early workshop slices can use their relevant accepted contracts.

Reconcile:

- One prerequisite graph connecting tools, stations, power, fuel, information,
  skill practice, raw materials/feedstock, locations, and useful outputs.
- Bootstrap and alternate acquisition routes; ordinary versus specialized
  knowledge; craftable versus found/commissioned equipment; legacy recipe bypasses.
- Energy/work/time units and resource ownership across manual and machine work.
- Stable content IDs, semantic source-file layout, manifest inclusion, and each
  integration boundary. Example domains are tools, materials, electronics,
  reading, metalworking, fabrication, and tailored location loot; final file
  names are selected here before moving definitions.
- A roster with existing/new/upgraded identities and realistic per-pass scope.
  Build item rosters around playable routes rather than filling an arbitrary
  total. Apply the item-authoring skills when authoring begins.
- Player-facing commands/status, headless access, saves, LAN transport and content
  fingerprints, generated initial state, and UI integration responsibilities.
- The artwork handoff and exact independently verifiable implementation slices.

Use these roster fields once IDs are chosen:

| Field | Required information |
| --- | --- |
| Identity | Stable ID, represented unit, new/existing/upgrade, owning file |
| Role | Practical capability, progression stage, useful downstream recipes |
| Prerequisites | Tools/stations, power/fuel, materials, skills, information |
| Acquisition | Found/crafted/commissioned; actual loot pool and placement contexts |
| Recovery | Recycling/consumption rules and mass/energy bounds |
| Presentation | Description, terminal appearance or item text art, external SDL artwork status |
| Delivery | Owning pass, dependencies, status, verification, blocker |

**Exit condition:** the graph has reachable starts and useful endpoints, the
shared schemas/semantics are accepted, and an implementation agent can take one
bounded pass without inventing another subsystem's rules.

## 4. Implementation sequence after design

These are work packages, not a requirement that one agent completes an entire
package in one session. Split each into reviewed vertical slices in its design.
Each content slice follows its relevant accepted/implemented contracts. A bounded
manual workshop slice need not wait for unrelated digital or city systems.

| Pass | Depends on | Deliverable and completion criterion |
| --- | --- | --- |
| I1 Catalog organization | D1–D5 accepted | Move definitions into agreed semantic sources, preserve stable IDs and catalog meaning, update manifests/references, and correct stale durable docs. Independently load all affected packs. |
| I2 Power foundation | D1 contract; early implementation authorized | Sources/stores/connections, consumers, commands/status, save/LAN integration, and one powered-tool slice. Implemented before I1; preserve its IDs through later catalog organization. |
| I3 Workshop and fueled forge | I2, D2 contract | Station capabilities, placement/access, fuel/heat, soldering/welding/forging work, and one reachable found/crafted-tool route. Verify interruption and reservation behavior. |
| I4 Digital fabrication | I2/I3, D3 contract | Computer information access/transfer and polymer/metal printing paths with useful outputs. Verify information identity, file access, power/feedstock, output capacity, and unattended-work semantics. |
| I5 City-generation foundation | I1, D4 contract and I2 interfaces | Settlement scale/era/district selection, infrastructure placement, outdoor population, and size-based population/reward selection. Verify generation and persistence before broad population tuning. |
| I6 Integrated content | Relevant capabilities verified and selected roster accepted | Manuals/digital sources, tools/machines, recipes, feedstock, terrain palettes, city layouts, loot and interior spawns. Deliver functional bounded slices and explicitly record incomplete progression branches. |
| I7 Balance and completion | I6 | Seeded end-to-end play, reachability and reward reviews, population measurement, performance checks, current usage docs, and explicit remaining artwork status. |
| A1 External SDL artwork | Accepted IDs/visual brief; final integration follows stable content | Separate-workstation sprites/mappings and visual review. Functional content can be verified with terminal/glyph presentation before this pass. |

I5 can overlap I3/I4 after its contracts are accepted and shared files have clear
ownership. Terrain content can be a bounded I6 slice after I5, while workshop
content follows its own verified mechanics. A coordinating agent owns manifests,
shared loaders/interfaces, persistence/transport, and final integration. Do not
assign concurrent edits to the same building, loot, or catalog file.

## 5. Cross-system rules to carry into the designs

- Keep one authoritative resource state for each fuel/energy store and one owner
  for each job. Reservations, committed costs, output generation, and cancellation
  must agree across power, crafting, and machine APIs.
- Use typed gameplay outcomes for missing power/fuel/tools/knowledge, unavailable
  stations and insufficient capacity. Frontends consume owned status snapshots;
  the server revalidates commands and concurrent use.
- Use project integer units and `ratioScale` for ratios, with explicit rounding
  and fractional remainders. Large and subdivided elapsed work should agree under
  the same logical events and activation inputs.
- Preserve practical determinism: generation uses seed/coordinates/version/content;
  order-sensitive resource allocation uses stable identities rather than handle
  allocation or storage traversal. Define activation boundaries explicitly.
- Select value ownership, typed handles, and simple closed dispatch where suitable.
  Keep parsing and diagnostics at the content boundary, engine exceptions disabled,
  and persistent/network references independent of transient pointers/catalog IDs.
- Plan save, schema, protocol, and generation changes explicitly. The different
  version domains serve different purposes. Full chunk snapshots must preserve
  depleted loot, removed actors, moved machines, information, and infrastructure.
- Initial charge, fuel, files and operational status must be authored/generated
  intentionally. Restore must not recreate initial resources or completed outputs.
- Respect current recipe limits (eight input types including retained tools, four
  traits, one recycler per target) unless an accepted design changes them with a
  demonstrated need. Recheck mass and energy through crafting and salvage cycles.

## 6. Validation and acceptance

### Design review evidence

Each design must walk through a normal use case plus loss of supply/access,
competition for a shared resource, save/reload, and relevant inactive-chunk
behavior. This is a specification exercise; test results come from implementation.

### Focused implementation checks

- **Catalog:** all affected manifests load independently; moves preserve identities
  and effective definitions; information/recipes/loot resolve; capacity bounds hold.
- **Power/fuel:** conservation, throughput limits, shared-load ordering, fractional
  accounting, interruptions, reconnection, topology edits, and activation boundaries.
- **Workshop:** bootstrap reachability, retained versus consumed resources, labor
  versus machine work, completion/cancellation, output capacity, and multiplayer use.
- **Information:** equivalent carriers share learning identity; computer/media access
  and costs work; unavailable files/power stop the appropriate action; learned
  human recipes and printer files follow the accepted distinction.
- **Printers:** allowed designs/materials, real useful products, interrupted work,
  progression gates and skill awards, no free energy/feedstock/output on restoration.
- **Worldgen:** seed repeatability, chunk-order independence, negative/border
  coordinates, spatial city identity, legal unique outdoor placement, infrastructure
  ownership, legacy generation and saved-population behavior.
- **Balance:** named baseline and fixed seed/location panel; size/era/district
  population and reward distributions; bootstrap-to-workshop and endgame expedition
  playthroughs; useful reasons to visit both old and newer cities.
- **Presentation:** terminal readability, clear refusal/status messages, headless
  command access, and accurate saved/LAN views. Record SDL artwork acceptance in A1.
- **Performance:** actual dense-city active populations and workshop load; inspect
  scheduler/perception/network cost before changing systems. Benchmark thresholds
  should reflect the measured environment, not hardware-sensitive unit assertions.

Existing focused suites include crafting, skill, item/object behavior, save,
network, house/urban content, and worldgen tests. Use the narrowest applicable
CMake/CTest preset; `win-msvc-debug` supports this workstation's non-artwork work.
Follow [testing](testing.md), record the exact commands and actual results, and
expand the run when changes or unresolved failures justify it. SDL artwork
availability is separate from whether an SDL code-level check can run here.

Keep generated balance/population exports under `/out`. Do not add or run tooling
tests. Durable design conclusions and compact verification summaries belong in
the relevant `/doc` pages rather than per-session Markdown logs.

## 7. External artwork handoff

For each terrain, object, or item needing SDL artwork, supply the stable content
ID, name, material/era/context, terminal appearance, intended silhouette and
connections/orientations, existing artwork reference or fallback, and required
states such as open/closed or operational where supported. Include representative
building palette examples and mapping requirements.

Keep status explicit: `Brief pending`, `Ready for artwork`, `Artwork supplied`,
`Mapped`, `Visually reviewed`. Item text art follows the existing item-authoring
workflow; it is separate from SDL map sprites. Temporary glyphs or reused visual
fallbacks do not count as completed new SDL artwork. Content and functional
verification can finish while the external visual pass remains pending.

## 8. Progress and next handoff

| Pass | Status | Evidence / next action |
| --- | --- | --- |
| Coordinating plan | Draft | Accepted user goals and current interfaces recorded; subsystem decisions remain open. |
| Shared crafting progression | Reviewed outlines accepted | [Framework](crafting_progression.md): reviewed trade/skill outlines through Medicine and Computers, software acquisition/recovery, and supplied/recovered intricate-print designs agreed. Exact rosters and subsystem runtime contracts follow their own passes. |
| Crafting delivery sequence | Accepted direction | [Dependency/delivery plan](crafting_delivery_plan.md): W3A/W3B implementation authorized; later tracks retain their explicit dependencies and separate contract reviews. |
| Crafting protection | Accepted | [Ordinary-trait rule](crafting_progression.md#welding-protection): accessible equipment and normal handheld sharing qualify; existing selection and paid-work behavior apply. M-Worn and its W3B mechanics dependency are retired. |
| W3A/W3B rosters | Functionally verified | [Implementation](workshop_rosters.md): 22 new items, 8 definition upgrades, 17 new crafting recipes, 19 new recyclers and 5 objects. All 16 focused tests pass; SDL art and four frontend/LAN checks pass. Includes concrete budgets, acquisition evidence and affected unfinished-sword policy; pacing playtesting remains. |
| D1 Power | Accepted | User reviewed geometry, connection, mains, accounting, residency and inspection rules; see [power design](power_design.md). |
| D2 Workshop/fuel | Accepted | [Workshop design](workshop_design.md): W1/W2 verified and progression gates approved. W3 uses ordinary protection traits; [M-Bench](powered_manual_stations_design.md) implements powered manual stations. W4/D3 coordination remains. |
| D3 Digital fabrication | Pending | Printer use/salvage, rough skill bands, cross-domain software acquisition and intricate-print schematics agreed. Detailed machine/file/software contracts remain. Coordinate jobs with D2/W4 and access/protection/recovery actions with the hacking design. |
| D4 Cities | Pending | D1 infrastructure is available; draft remaining acquisition needs and city design. |
| D5 Integration/roster | Pending | Initial crafting dependency/delivery map drafted; reconcile remaining D1–D4 contracts and select bounded rosters. |
| I2 Power foundation | Verified (functional) | Debug suite: 1,317 passed, 2 skipped; SDL build and four executable/LAN checks passed. Dedicated sprites and two existing-artwork test failures are tracked in [power design](power_design.md#verification). |
| I3 Workshop and fueled forge | W3 content integrated | W1/W2 verification remains in [workshop evidence](workshop_design.md#current-evidence-and-handoff); W3's authored progression, budget and current validation are in its [implementation record](workshop_rosters.md). W4 remains separate. |
| I1, I4–I7 | Pending | Broader implementation/content work follows the remaining design dependencies. |
| A1 SDL artwork | Ready for external workstation | W3 object briefs and map fallback policy are in [the roster](workshop_rosters.md#external-sdl-artwork-handoff--ready-for-artwork); canonical item text art is supplied. |

Suggested next-pass brief:

> Read `doc/crafting_progression.md` for accepted progression and
> `doc/crafting_delivery_plan.md` for the accepted delivery direction.
> Read `doc/workshop_rosters.md` for the W3A forging / W3B electrical-welding
> implementation, concrete budgets, acquisition, save-upgrade policy and validation.
> W3A uses W1/W2; W3B uses D1 and ordinary accessible/shared traits for protection.
> Audit the four-trait budget including protection. Powered placed manual
> providers additionally need M-Bench. Mechanics implementation and content
> authoring follow their separate accepted designs/rosters.
> Preserve forge/tongs 2, forming equipment 3 and proper sword 4, with useful
> level-2/3 practice and both found/crafted equipment routes. The old W3 quantities,
> gates and blanket runtime assumptions are historical, not implementation targets.
> Coordinate W4 with D3-A/B before printing; protected recovery is a later D3-C
> acquisition route. Schedule M-Skills before separate Cooking/Chemistry content.
> Other feature tracks follow their consumers and own designs. Preserve D1 resource
> authority, paid work and residency. A bounded pass may leave later gaps.

After each accepted design or verified implementation pass, update this table,
link its durable document, and record any cross-system decision that changes a
later pass's prerequisites. Keep one agreed target rather than separate batch
assumptions.
