# Sighted entity AI

The implemented `Sighted` controller is a small sight-driven baseline. For the
larger planned design, see [Entity AI design](../md/EntityAIDesign.md); controller
ownership and persistence interfaces are in [actors API](actors_api.md).

## Runtime pipeline

On a ready NPC's decision, sensing reads the stable World and produces value
observations and local movement affordances. The decision function receives these,
the actor's own state and its one remembered contact, rather than unrestricted
World access. It emits a wait, move or attack intent; World/combat executes and
validates it. A successfully detected actor's actual faction is recognized in this
baseline; rejected or hidden candidates do not expose their faction or position.
Only Survivors and Undead are currently hostile to one another.

Tracking checks the remembered actor on **every decision**, independently of the
discovery allowance. Four horizontal neighbors also receive immediate checks.
Background discovery runs only on a decision when its countdown has reached zero;
it then resets the countdown. There is no independent sensing event when a timer
expires. Successful sight refreshes the contact's persistent identity, last seen
position, observation tick and memory duration. Lost sight leaves the last known
position unchanged. A visible empty remembered location is evidence of absence;
a skipped or incomplete scan is not.

The controller selects the nearest observed hostile by Manhattan distance, with
persistent ID as tie-breaker. It keeps its current contact unless another is
strictly closer by more than `switchAdvantage`. A visible, cardinally adjacent
same-layer target can be attacked if the actor has attack capability. Otherwise
the actor pursues the remembered location using local steps, searches within
Manhattan radius 2 after reaching a visibly empty location, or wanders when idle.
Search ends on its timer or memory expiry. Movement probes check local active,
passable, unoccupied tiles; vertical steps require stairs at both ends. A brief
anti-backtracking preference limits oscillation, with a wait releasing it when
boxed in. This is local movement, not global pathfinding.

## Sight and discovery

`AiLineVisible` in `actors/ai_vision.h` is an allocation-free, center-to-center
3D geometric ray, separate from range and lighting. Its conservative closed-cell
supercover checks all incident cells at edge/corner ties. Opaque or unavailable
geometry blocks sight, including endpoints; actors do not occlude. Floors are
zero-thickness lower faces of upper tiles: a vertical crossing transmits when the
upper tile is open space or both adjoining tiles are stairs. Rays do not load
chunks or update player FOV. Optional `AiVisionStats` accumulates ray calls and
tile reads, including rejected calls and missing tiles.

Detection first applies 3D Euclidean range and the target tile's light band:
dark uses `darkRange`, dim uses `dimRange`, and ordinary/bright use `sightRange`.
Only then is a ray attempted. The geometric API itself bounds each coordinate
delta to 32; profile sight range is also bounded to 32.

Player perception retains its existing 2.5D planar/downward projection, while AI
uses these 3D rays. This asymmetry is an accepted gameplay decision, not a blocker
to shipping AI. Upward player projection is later work; reconcile the geometries
if gameplay reveals a problem.

Discovery walks nearby chunk buckets in stable order and resumes using a bucket
and persistent-ID cursor via `World::NextActorInChunk`, not a mutable vector
index. A pass is anchored until it completes, even if the observer moves; IDs
inserted below a visited cursor wait for the next pass. Each decision is bounded
by candidate and bucket allowances, with at most one completed pass. Bucket
counts are **query attempts**, including repeated queries in a bucket and empty
results; candidate counts include returned self and parked actors even though
they cannot be observed. Tracking and neighbor checks do not consume these
discovery allowances. Resident-only actors may be observed if not parked, but
do not take sensing turns while inactive.

World maintains a resident identity-to-handle index for tracking and a tile-indexed
occupancy table for local movement/interaction probes. These avoid population-wide
identity scans and repeated chunk actor scans outside the discovery allowance.
Chunk actor lists remain sorted by persistent ID specifically for resumable
discovery; the world entity store retains its unordered layout.

## Configuration, inspection and persistence

`World::SetActorController(npc, AiControllerKind::Sighted)` installs intrinsic
defaults; `World::SetActorAiProfile(npc, name)` installs a fresh sighted controller
using a validated named content profile. The latter rejects players, stale handles
and unknown names. The catalog provides `FindAiProfile`, `TryGetAiProfile`,
`GetAiProfile` and `AiProfileCount`; see [content format](content_format.md) for
the JSON5 schema and validation. Intrinsic defaults are sight/dim/dark ranges
12/6/1, scan/memory/search durations 100/1200/300 ticks, candidate/bucket budgets
8/16 and switch advantage 2. Legacy Wander and Melee remain available.

`World::InspectActorAi` returns a value-only `AiInspection`: behavior, target
persistent ID, last known position and observation tick, memory and scan
remaining, target visibility, and candidates/buckets/rays/cells from the latest
decision. Visibility is cleared on forgetting; work counts are per decision, not
population-wide performance measurements.

The versioned controller state saves the profile name, persistent contact ID,
last observation, remaining timers, behavior, discovery anchor/cursor and local
movement history. Runtime handles and runtime catalog IDs are not serialized in
memory. Archive validation checks the state and resolves nonempty profile names
against current content before restoration. Remaining timers advance only while
the NPC is active and unparked; inactive, parked and archived time freezes them
without catch-up. See [entity scheduling](entity_scheduling.md).

The per-decision allowances do not provide a global hard work budget or a general
asynchronous scheduler. Large-population discovery latency and total simulation
cost still need representative profiling; no performance result is implied.
