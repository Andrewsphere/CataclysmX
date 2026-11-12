# Entity AI Design

## Contents

- [Status and implementation order](#status-and-implementation-order)
- [Goals and principal decisions](#goals-and-principal-decisions)
- [Current foundations](#current-foundations)
- [Knowledge boundary and ownership](#knowledge-boundary-and-ownership)
- [Entity vision](#entity-vision-bounded-candidate-driven-raycasts)
- [3D optical geometry](#3d-optical-geometry)
- [Other senses](#other-senses)
- [Decision-making and behavioral variety](#decision-making-and-behavioral-variety)
- [Navigation and crowd movement](#navigation-and-crowd-movement)
- [Factions, communication, and hive minds](#factions-communication-and-hive-minds)
- [Scheduling, determinism, and scale](#scheduling-determinism-and-scale)
- [Content and runtime state](#content-and-runtime-state)
- [Implementation prerequisites and open questions](#implementation-prerequisites-and-open-questions)
- [Staged implementation and verification](#staged-implementation-and-verification)

## Status and implementation order

The sight-driven baseline is implemented on the world, actor and combat
foundations. See [sighted entity AI](../doc/entity_ai.md) for current behavior,
APIs and limits. The rest of this document records broader goals and proposed
extensions, not promises that they already exist. Open questions apply to the
corresponding future work.

## Goals and principal decisions

- Support varied enemies, including dumb sighted zombies, poor-sighted scent
  trackers, good listeners, thermal detectors, and hive-linked creatures.
- Make decisions from sensory evidence, memory, and the entity's own state.
  Losing contact must stop access to a target's current hidden state.
- Support independent factions and enemy-on-enemy interactions. The player is
  one possible target, not a special global goal for every enemy.
- Configure senses, behavior, navigation capabilities, and communication through
  typed JSON5 content definitions.
- Aim to support thousands of loaded entities, potentially over a thousand
  active enemies, through bounded work and shared calculations. This is a design
  target, not an established performance result.
- Prefer understandable gameplay and small explicit systems over physical
  simulation, generic AI frameworks, or speculative optimization.
- Keep simulation scheduling deterministic and independent of rendering and
  wall-clock execution speed.

**The chosen starting point for entity vision is candidate-driven 3D raycasting,
not a visibility map per enemy.** Search for useful detectable entities, stop
after enough useful observations, and continue unfinished searches on later
simulation updates. This supersedes the earlier suggestion to build a planar
FOV once per enemy scan. Shared sound and scent fields remain appropriate for
those modalities; the vision decision does not prohibit environmental fields.

## Current foundations

The baseline includes Wander, Melee and Sighted controllers, simple combat,
single-contact memory, local pursuit and bounded discovery. Actions use simulation
ticks and stable actor ordering; planned routing and other senses remain future work.

- [`src/core/world.h`](../src/core/world.h) exposes world-owned actors through
  typed handles. Chunks have actor lists useful for spatial candidate discovery.
  Actor borrows do not survive uncontrolled mutation or unloading.
- Active and resident-only chunks have different simulation semantics.
  Resident-only entities are loaded but frozen; loaded count is not active count.
- [`src/core/perception.cpp`](../src/core/perception.cpp) implements planar
  shadowcasting plus downward projection. `LineVisible` builds a local planar
  visibility field; despite its name, it is not a cheap ray traversal.
- Player sight does not project upward. Entity 3D LOS is an intentional different
  geometric contract, not merely an optimization of that projection.
- Chunk reconciliation is currently a synchronous boundary operation. Its
  explicit-boundary model is useful, but a budgeted AI work scheduler would be
  new functionality.
- The [content catalog](../doc/content_format.md) includes named AI profiles;
  richer actor and faction definitions remain future work.

## Knowledge boundary and ownership

The conceptual flow is:

```text
Authoritative world
    -> sensors
    -> observations and memory
    -> decisions using knowledge, own state, and capabilities
    -> action intents
    -> authoritative movement/combat validation
    -> world changes and new sensory events
```

Sensors must inspect real positions and terrain to evaluate detection. The
decision layer should not receive unrestricted `World` access. It receives only
information the entity is entitled to use, including its own condition,
capabilities, current observations, remembered contacts, and permitted reports.

An observation should be able to express:

- Modality: sight, hearing, smell, thermal, or communication.
- Observation tick and, where necessary, delivery tick.
- A position, approximate area, bearing, or local gradient, depending on modality.
- Strength/confidence and a perceived category.
- Recognized entity identity only when the observation supports it.
- Original source/provenance where necessary for shared reports.

These are distinct observations with different behavioral consequences:

- A recognized human is visible at a particular tile.
- An unidentified loud noise came from approximately east of the observer.
- Fresh human scent is stronger in a neighboring cell.
- A hive scout reported prey near an entrance two turns ago.

An internal entity handle or sound emitter ID must not silently grant knowledge
of current position, faction, health, or identity. Unidentified evidence can
remain an anonymous contact. Use generational handles for live references and
persistent identities where archival state needs them; do not retain entity
pointers across mutation boundaries.

### Memory

Keep a small bounded collection of contacts rather than an exact copy of the
world. A contact can retain its last observed position, time, modality, confidence,
and recognized identity. Confidence decays and memories expire according to the
profile. Forwarded reports retain the time of the original evidence.

On losing sight, an enemy may pursue the last observed position, search nearby,
follow another sense, or abandon the goal. It must not follow the target's current
hidden coordinates. The same rule applies to navigation destinations and hive
orders.

A skipped or budget-limited scan is not evidence of absence. Clearing a contact
because its location was searched requires an actual sufficiently complete
observation of that location. A scan spread across turns is a sequence of
observations, not an instantaneous complete visibility snapshot.

## Entity vision: bounded candidate-driven raycasts

### Query pipeline

1. Discover candidates through spatial buckets/chunk actor lists.
2. Apply cheap sensory rejection: range, relevant detectable category, facing if
   applicable, and light/signature thresholds where cheaply available.
3. Test geometric LOS with a 3D grid raycast.
4. Produce an observation if the observer's detection rules pass.
5. Stop after enough useful observations or exhaustion of the work allowance.
6. Resume unfinished discovery during a later scheduled sensing update.

Candidate filtering belongs to the sensor boundary. It must not teach the
decision-maker the identities or positions of rejected/occluded candidates, or
assume hidden faction membership is already recognized.

Use spatially local traversal rather than scanning all loaded actors or sorting
every possible target for each enemy. Bound candidate examination as well as
raycasting: a cap on retained contacts alone does not cap discovery cost.

### Algorithm and cost

The recommended starting algorithm is 3D DDA voxel traversal, commonly called
the Amanatides-Woo algorithm. Visit the cells/faces crossed by the segment and
stop at the first obstruction. Use deterministic arithmetic and explicit edge/corner
tie rules; the exact numerical representation remains an implementation choice.

Approximate geometric work is:

```text
tested observer-target pairs * average traversed cells per ray
```

Early obstruction and early successful discovery reduce typical work. The
difficult case is a dense region with many candidates and no detectable target.
That case still needs hard budgets and fair continuation. Spatial indexing does
not eliminate dense local worst cases.

Do not call the existing `LineVisible` once per pair and assume it provides this
cost model. The new query needs an actual bounded grid traversal, avoiding
per-query visibility-map construction and unnecessary allocation.

### Tracking versus discovery

Separate two kinds of attention:

- **Tracking:** check whether the current target remains detectable, usually with
  high priority. An observation refresh requires a successful sensory check.
- **Discovery:** examine other candidates occasionally, even while tracking.
  This allows a nearby threat or better stimulus to be noticed.

A simple zombie commonly needs one main target; more alert creatures may retain
several contacts. These are profile choices, not a requirement to find the globally
best target on every update. Decisions score available observations rather than
unobserved world entities.

Stopping at the first useful result is acceptable, but incidental container order
must not permanently determine attention. Use deterministic traversal with
per-entity phases/cursors and guaranteed discovery progress. Nearby interactions
can receive priority without consuming all background discovery service.

### Resumable searches

Retain small continuation state identifying the next spatial bucket/candidate.
Revalidate handles, residency, and range when examining a candidate. Do not keep
raw pointers or blindly reuse vector indexes across structural changes.

Finish ordinary sight rays synchronously and distribute complete candidate
checks across simulation updates. With bounded range, each ray has bounded work;
this avoids retaining half-finished rays across changing terrain or positions.

For example, checking ten candidates and continuing next turn is a valid policy,
but ten is illustrative, not a selected default. A hundred candidates would take
ten such updates for a full pass if nothing interrupts it. Define and measure
acceptable discovery delay for each attention profile. Persistent success on one
contact must not starve discovery of all others.

## 3D optical geometry

3D traversal allows enemies below a balcony to detect an exposed player above
them. It does not make every elevated target visible: the balcony slab should
still hide someone when it intersects the ray.

Checking only the current `blocksSight` flag is insufficient. Ordinary floors
do not obstruct horizontal sight through their occupied space, but should
normally obstruct sight across the floor surface between storeys.

The proposed minimal model is:

| Feature | Optical interpretation |
| --- | --- |
| Wall | Opaque volume |
| Floor/ceiling | Opaque surface at a layer boundary |
| Open space | No supporting floor surface |
| Stairs/opening | Explicit vertical transmission rule |
| Observer | Eye sample relative to its supported position |
| Target | One or a few body samples relative to its position |

Grid traversal can test both volumes and crossed faces; meshes and a physics
engine are unnecessary. Start with one useful eye-to-body segment if gameplay
permits. Add further target samples, such as a head sample, only if partial-cover
behavior needs them.

The baseline uses center samples, Euclidean range, conservative supercover ties,
and the floor/stair rules documented in [sighted entity AI](../doc/entity_ai.md).
Geometric reversal does not imply symmetric detection: observer sensitivity and
target illumination can differ.

Unavailable geometry is treated as unknown/occluding; a sensor query should not
implicitly load distant chunks. Decide how this interacts with boundary sensing
and active/resident entities as part of the world API contract.

The player retains 2.5D perception while AI uses 3D rays. This asymmetry is an
explicitly accepted decision, not a shipping blocker. Upward player projection
is later work; reconcile the geometries only if gameplay reveals a problem.
Future firing checks may have distinct physical rules that should be explicit.

## Other senses

### Ordinary vision and night vision

Ordinary vision uses geometric LOS plus range, lighting, and target detectability.
Night vision primarily changes low-light sensitivity/range; it does not require
a separate behavior system. Existing dark/dim/glare concepts are useful starting
points. Facing restrictions are optional profile behavior, not an assumed property
of every zombie.

### Thermal vision

Thermal vision detects a simple heat signature instead of depending on visible
illumination. It still has range, sensitivity, and occlusion. Seeing through walls
is not implied; any exceptional penetration must be an explicit ability.

Use simple signatures/material rules initially. Physical temperature evolution
and radiometry are unnecessary. Decide whether thermal uses ordinary opacity or
needs distinct rules for materials such as glass before exposing those settings.

### Hearing

Generate sound events from actions and propagate each relevant event toward many
listeners, rather than searching independently from every listener to the source.
A bounded flood/Dijkstra propagation can account for attenuation through terrain,
doors, and vertical connections, including sound arriving around corners.

Listeners receive perceived loudness, category, time, and localization quality.
Do not automatically expose the emitter's identity or precise coordinates.

Bound sound production and propagation as well as listener observations. Repeated
footsteps or similar low-value events may be aggregated under explicit rules.
A strongest-sound-only field loses quieter sources; top-K arrivals and masking
are gameplay approximations, not lossless optimizations. Preserve important
distinct events when their distinction matters.

A propagation job must use a coherent acoustic state. Do not let one flood
partially inspect a door before opening and partially after opening without a
defined temporal model.

### Smell

Use shared historical scent fields with deposition and decay. Creatures sample
nearby cells for strength or a gradient; decisions do not inspect the entire field
to find the true source. A moving target leaves historical evidence rather than
moving its old scent with it.

Start with trails and decay; add limited spreading/diffusion if gameplay needs it.
Respect walls, floors, openings, and the chosen vertical connectivity rules.
Update on a defined simulation cadence over relevant regions.

Use a small number of meaningful channels, such as human scent, blood, or hive
pheromone, rather than a full map per entity or faction. Mixed scent cannot
reliably identify individuals at crossing trails. Accept confusion initially;
specialized identity-preserving tracking is a later feature if justified.

## Decision-making and behavioral variety

Use small explicit states with lightweight utility scoring as the starting
approach. Suggested states are idle/wander, investigate, pursue, search, attack,
and flee. Scores choose among known goals/stimuli using factors such as:

- Perceived hostility or threat.
- Stimulus strength, confidence, and freshness.
- Estimated effort using permitted navigation knowledge.
- Own injury, fear, aggression, or curiosity where modeled.
- Commitment to the current goal.

Use hysteresis/commitment so a slightly better score does not cause a goal switch
every update. Weights, thresholds, retention, and persistence belong in content.
Exact formulas and state transitions are not finalized.

Do not start with a generic behavior-tree editor, GOAP planner, scripting language,
or learning system. Reconsider such machinery only when implemented behavior
demonstrates a need.

Sense quality, intelligence, and social behavior are independent dimensions:

| Example | Sensory emphasis | Possible behavior |
| --- | --- | --- |
| Sighted shambler | Ordinary vision | Direct pursuit, short memory, obstacle frustration |
| Scent tracker | Poor sight, strong smell | Follow trails, become confused at intersections |
| Listener | Sensitive hearing | Investigate approximate disturbances |
| Thermal stalker | Heat signatures | Find warm targets without visible light |
| Hive scout/soldier | Configurable individual senses | Report observations and pursue shared goals |

A scent tracker can be clever or dumb. A thermal creature can be solitary or
hive-linked. These examples are combinations of traits, not separate engines.

## Navigation and crowd movement

### Intelligence is not the pathfinding algorithm

A* does not inherently make a creature intelligent. Apparent intelligence depends
on destination choice, known terrain, planning horizon, considered actions, and
persistence. A simple mover with perfect hidden target knowledge is less honest
than an A* user pursuing an old observation.

Provide a small set of navigation policies:

| Policy | Intended use |
| --- | --- |
| Direct pursuit | Move toward evidence; tolerate getting stuck or attack a blocking obstacle |
| Limited local detour | Try nearby alternatives without finding a distant entrance |
| Stimulus following | Follow a locally available sound/scent cue |
| Planned route | Bounded A* toward a legitimately known destination |
| Shared pursuit | Follow a shared cost-to-go field toward a compatible group goal |

Ordinary zombies should start with direct pursuit, small local detours, and
optional obstacle frustration. Avoid pointless oscillation, but allow failure
to discover a distant entrance. Obstacle attacks depend on combat/terrain
capabilities established by the underlying systems.

### Knowledge applies to terrain too

Routing to a remembered target over the entire true map can still leak unknown
doors and passages. Simple enemies use local terrain information. Capable enemies
can use remembered terrain; hive members can use appropriately shared knowledge.
Any innate/global map knowledge must be a deliberate policy.

The movement validator can reject an attempted step using real terrain without
giving the decision layer unrestricted map access. Unknown-terrain exploration
policy and the extent of terrain memory need a decision before planned routing.

Entity-target raycasts do not automatically provide terrain knowledge. Define how
local navigation probes, observations along rays, or other bounded terrain sensing
may teach traversability before implementing remembered-map routing. Reusing
authoritative terrain queries inside a planner must not bypass this decision.

### Shared navigation

For many compatible entities moving toward the same known destination, compute
a reverse search once and let entities follow decreasing costs:

- BFS for uniform movement cost.
- Dijkstra for varying movement/action cost.

Share only across compatible destinations, movement capabilities, costs, and
terrain-knowledge policies. Bound the number and extent of active fields; one
field for every entity defeats the benefit. Multiple goal seeds are appropriate
only when their goal-selection semantics match the intended behavior.

Never attach a destination to a hidden target's current position. Update goals
from observations/reports. Reuse fields or routes until relevant goal/topology
changes require invalidation, and validate the next action against reality.

Handle dynamic occupancy locally rather than invalidating a terrain field after
every actor step. Initially choose an alternative step, wait, or make a short
detour under defined conflict ordering. Organized groups may later reserve
approach positions or assign attackers/flankers. Full cooperative multi-agent
pathfinding is not a prerequisite.

## Factions, communication, and hive minds

### Faction relationships

Represent relationships explicitly, including allied, neutral, hostile, and
avoidance/fear where useful. Relationships may be directional: a predator's
interest in prey is not the prey's desire to fight it.

Target selection uses perceived identity and remembered aggression. Unknown
sounds do not reveal faction membership. Being allied does not automatically
share knowledge, and hive membership is distinct from faction membership.

### Shared knowledge

Separate communication from coordinated tactics. Start by sharing observations;
add assignments and roles after individual behavior works.

A communication profile can express range/connectivity, delivery delay, permitted
modalities, and report retention. Shouting, pheromones, radio, and telepathy offer
different gameplay. Immediate telepathy shares member knowledge, not global world
truth.

Reports retain the original evidence time and provenance. Repeated forwarding
must not turn old evidence into a fresh sighting or count one report as multiple
independent confirmations. When evidence stops, confidence ages normally.

### Target-centric witness searches

For a knowledge-sharing group, an important query is: **can any eligible member
currently detect this target?**

1. Find plausible nearby members through spatial indexing.
2. Test the last successful witness first.
3. Try other members if that witness no longer detects the target.
4. Stop when one eligible member detects it.
5. Publish a report according to the communication policy.

The savings come from early witness success and shared observations, not from
reversing loop order alone. Checking every member against every target remains
an all-pairs calculation. Retained witnesses are especially useful for stationary
targets visible to a large horde.

Apply each member's actual eye geometry, facing, range, sensitivity, activity,
and network eligibility. A reverse segment traversal does not use the target's
senses. An inactive member is not automatically a continuously sensing witness.

Target-centric queries are internal sensor work, not permission for hive decisions
to enumerate hidden enemies. New-target discovery remains spatially bounded;
known-target tracking is not a substitute for discovering new contacts. The
concrete discovery schedule still needs design.

When witnesses fail to refresh a report, the shared location remains the last
observed location. Other members may pursue it. Shared location knowledge does
not establish personal attack range or line of fire; combat validates those
separately.

Later coordination can assign approach points, distribute search areas, or limit
the number of pursuers per target. Begin with small deterministic assignments,
not a global tactical optimizer.

## Scheduling, determinism, and scale

Separate action timing, sensing cadence, and replanning cadence. An entity need
not rescan or replan whenever it takes a step.

| Activity | Starting scheduling policy |
| --- | --- |
| Immediate interaction | Cheap local validation when executing the action |
| Combat/tracking | Frequent relevant sensory checks |
| Pursuing remembered evidence | Reuse movement goal/plan; refresh periodically |
| Investigation | Moderate discovery cadence |
| Idle | Sparse scans and appropriate sensory event activation |
| Resident-only/inactive | Respect the established frozen-simulation policy |

Stagger work using stable entity identity and deterministic phases. Define
ordering for candidate traversal, actions, reports, conflicting movement, and RNG
consumption. Avoid accidental behavior dependence on container iteration order.

Budget both per-entity attention and total work. Useful counters include candidate
examinations, traversed LOS cells, pathfinding expansions, sound propagation
cells, and scent updates. Include bounded pending work, baseline service for
background discovery, and capped urgent work. Supersede obsolete requests rather
than accumulating an unbounded backlog. Changing inputs must not cause endless
restart starvation.

Bounded computation necessarily allows limited completeness or delayed discovery.
Treat those limits as explicit attention/gameplay policies. Measure discovery
latency and queue age, not only average CPU cost. Do not skip an entity's scheduled
action merely because its next sensory scan has not run; it acts from available
knowledge under defined timing rules.

### Computation progress versus observation time

Distinguish two mechanisms:

- Yield CPU work across host/render updates while holding a simulation boundary.
- Deliberately publish sensory results at later simulation ticks.

Do not let render rate or worker completion order choose when an enemy notices
something. Deterministic logical work budgets or fixed publication ticks are
possible approaches; choose the concrete scheduler contract before integration.
If a required result is not ready at its publication boundary, the policy cannot
silently depend on machine speed.

Vision initially slices between complete ray checks. Other incremental jobs,
such as pathfinding and sound propagation, need coherent inputs and explicit
invalidation/publication rules. An observation computed from tick T remains a
tick T observation even if delivered later. No job may refresh hidden target
coordinates merely because computation continued.

## Content and runtime state

Use strict typed JSON5 definitions consistent with the existing content loader.
C++ implements the algorithms; content selects capabilities and tunes policies.
Do not introduce a general scripting language as part of initial AI.

Proposed reusable configuration dimensions are:

| Dimension | Examples |
| --- | --- |
| Senses | Enabled modalities, ranges, thresholds, facing, sensing intervals |
| Attention | Tracking/discovery allowance, useful-contact limit |
| Memory | Contact capacity, retention, confidence decay |
| Behavior | Aggression, curiosity, fear, persistence, goal-switch threshold |
| Navigation | Policy, movement/door/obstacle capabilities, planning limits |
| Social | Faction, recognition/retaliation policy |
| Communication | Hive/network membership, range, delay, shared evidence |

The initial sighted profile schema and defaults are implemented; richer schema
dimensions, inheritance and reuse rules remain open.
Validate references and incompatible settings at load time, resolve stable IDs
to typed runtime definitions, and keep parsing outside simulation logic.

Separate immutable definitions from runtime state: contacts, current goal,
attention cursors, pending reports, and any behavior-relevant RNG state. Determine
what must survive saving and chunk archival. Derived paths/fields can be rebuilt
only under rules that preserve intended behavior and observation latency; a
different cache warm-up must not accidentally change the replay contract.

## Implementation prerequisites and open questions

The following are contract decisions to make before the corresponding work.
They do not all block the first sight-driven enemy.

| Area | Question or prerequisite | Resolve before |
| --- | --- | --- |
| Entity ownership/API | Stable handles/identities, controlled mutation, position and capability queries, spatial candidate iteration, deletion/unload boundaries | Core AI integration |
| Basic combat | Intent submission, range/LOS validation, action costs, damage/death outcomes, stale/invalid target handling, sensory consequences of actions | Combat AI |
| Optical geometry | Additional samples, body occlusion and richer geometry | Future sight extensions |
| Player consistency | Upward player projection and possible reconciliation if gameplay reveals a problem; asymmetry is accepted | Later player perception work |
| Attention semantics | Measure discovery delay under load; extend recognition if needed | Scale and richer recognition |
| Timing | Sensing/planning events relative to actions and input barriers; logical budgets, publication boundaries, RNG ordering | Incremental scheduling |
| Activity boundaries | Which resident/active entities can observe or be observed; witness eligibility, boundary queries, whether any operation acquires residency | World/AI boundary integration |
| Memory/persistence | State that survives save/unload, time basis for aging while inactive, identity rebinding, treatment of pending reports/work | AI archival/save support |
| Factions | Initial relationship data, recognition, retaliation, neutral behavior, separation from hive membership | Multi-faction behavior |
| Navigation knowledge | Local versus remembered terrain, how terrain is learned without FOV maps, unknown terrain policy, legal movement/action costs, shared-field compatibility | Planned/shared routing |
| Hearing | Volume/attenuation units, localization accuracy, vertical transmission, aggregation/masking, propagation timing | Hearing implementation |
| Smell | Channels, deposition/decay cadence, geometry, optional diffusion, scent identity and unloaded-region policy | Smell implementation |
| Thermal | Signature model, material opacity, range/contrast rules | Thermal implementation |
| Hive communication | Network membership/connectivity, report delay/provenance, new-target discovery, witness scheduling and activity | Hive implementation |
| Content | Definition ownership, schema IDs/units, defaults/limits, reference validation, compatibility/versioning | Data-driven profiles |

### What the preparatory refactoring should provide

- Authoritative action submission and validation shared by players and AI,
  instead of AI directly rewriting positions or health.
- Safe, efficient nearby-entity queries and explicit borrowing lifetimes.
- Read access for sensor services to terrain optics and entity signatures without
  granting unrestricted world inspection to decision code.
- Explicit simulation time and action/event boundaries.
- Clear result types distinguishing normal gameplay outcomes from recoverable
  errors and broken invariants.
- A way for movement/combat and other actions to produce sensory evidence, with
  delivery/localization handled by the sensory subsystem.

These are requirements on subsystem boundaries, not prescribed class names or
an invitation to build a generic framework during the refactor.

### Deliberately deferred complexity

Do not require full FOV maps per enemy, full individual scent maps, physical heat
or fluid simulation, global cooperative pathfinding, a generic behavior editor,
or comprehensive hive tactics for the initial version. Reconsider algorithms
based on measured bottlenecks and actual gameplay requirements. The baseline has
per-decision allowances and numeric defaults, but no global hard budget or general
asynchronous scheduler; large populations still need profiling.

## Staged implementation and verification

Implementation proceeds incrementally:

1. **Sight-driven baseline (implemented):** 3D optical geometry, bounded raycast discovery,
   tracking, memory, basic states, local pursuit, and faction-aware combat intents.
   Establish deterministic scheduling and inspection counters from the start.
2. **Hearing and smell:** shared environmental evidence, investigation, competing
   stimuli, and clear localization/forgetting rules.
3. **Navigation scaling:** bounded A*, compatible shared BFS/Dijkstra fields,
   and local crowd handling where gameplay needs them.
4. **Hive behavior:** shared reports and witness searches, then modest role/goal
   coordination.
5. **Specialization and measured optimization:** thermal traits, richer searching,
   extra target samples if needed, and optimizations justified by profiling.

During implementation, add focused headless engine tests for behavior and
contracts, including:

- An exposed balcony target is detectable from below; intervening floors block
  the appropriate segments; edge/corner traversal follows the chosen rules.
- An unseen moving target does not update remembered coordinates, route goals,
  or hive reports without new evidence.
- Tracking does not permanently starve discovery; unsuccessful scans resume
  fairly across updates, including after target deletion/unloading.
- Anonymous sound/scent does not reveal precise emitter identity or hidden state.
- Faction enemies can choose each other rather than always choosing the player.
- Hive witness loss stops contact refresh, and repeated forwarding does not renew
  evidence time or create independent confirmations.
- Equivalent simulation input produces equivalent results despite different host
  update chunking, under the selected scheduling/replay contract.
- Save/load and inactive-region behavior obey the chosen memory/timing policy.

Profile representative scenes: open hordes, obstructed interiors with no visible
targets, multiple fighting factions, many sound events, and vertical environments.
Track candidate visits, ray-cell visits, contact age, worst discovery latency,
queue age, propagation/search work, and surrounding simulation costs. Current
per-action player observation updates and actor enumeration may also contribute
to large-population cost; do not attribute all overhead to new AI.

Extend lightweight AI inspection as needed: current intent/goal, remembered contacts and
their ages, why a target was selected, sensing budget usage, and hive report
provenance. Being able to inspect what an entity believes is central to debugging
this design. The baseline already exposes contact, behavior and recent work
counters; the broader checks and measurements above remain future work.
