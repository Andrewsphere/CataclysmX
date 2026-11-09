# Parallel SDL3 frontend architecture

This is the proposed frontend architecture, researched on 2026-11-09. The SDL
client, thread channels, worker jobs, and checkpoint interfaces below are not
implemented. Existing clients are FTXUI (`cataclysmx_game`) and
scriptable/headless (`cataclysmx_headless`).

The first playable SDL client has independent rendering, authoritative session
execution, and background asset preparation. The process main thread owns SDL
events, UI, and rendering; a session thread owns gameplay. Later milestones
separate network transport and database execution once their current coupling
to the world has been removed. Frame rate never drives simulation time.

## Evidence and decisions

These precedents inform the design; the recommendations are our synthesis.
None establishes a measured speedup for this project.

| Case | Relevant evidence | Decision here |
| --- | --- | --- |
| OpenTTD | Its [video driver](https://github.com/OpenTTD/OpenTTD/blob/master/src/video/video_driver.cpp) separates game and drawing loops, with locks around shared game state. | Separate the loops, using owned snapshots so drawing needs no world lock. |
| Factorio | [FFF #70](https://www.factorio.com/blog/post/fff-70) describes overlapping update and rendering. [FFF #215](https://www.factorio.com/blog/post/fff-215) describes parallel sprite preparation and cache contention between apparently independent mutable systems. | Parallelize immutable preparation and explicitly separate ownership of mutable data. |
| Godot | Its [threading guidance](https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html) distinguishes resource preparation from access to active shared state and GPU resources. | Prepare privately, then hand results to the owning thread for activation. |
| Bright Nights | Its [tile definitions and texture wrappers](https://github.com/cataclysmbn/Cataclysm-BN/blob/main/src/cata_tiles.h) connect visual identities to atlas regions. | Resolve stable content names into compact sprite tables during loading. |
| Dungeon Crawl Stone Soup | Its [tiles frontend](https://github.com/crawl/crawl/blob/master/crawl-ref/source/tilesdl.h) separates regions, layout, DPI, and redraw state; [tile buffers](https://github.com/crawl/crawl/blob/master/crawl-ref/source/tilebuf.h) collect drawing work. | Separate map presentation from screen layout and graphics submission. |
| Brogue CE | Its [platform interface](https://github.com/tmewett/BrogueCE/blob/master/src/platform/platform.h) provides an explicit input/display boundary. | Keep platform events and graphics resources outside simulation APIs. |

SDL3 requires [presentation](https://wiki.libsdl.org/SDL3/SDL_RenderPresent) and
[event polling](https://wiki.libsdl.org/SDL3/SDL_PollEvent) on its main thread.
Therefore the main thread is the dedicated platform/render owner, with gameplay
on another thread. SDL's 2D renderer is sufficient for the initial sprite map;
no graphics-backend change is needed to separate rendering from gameplay.

## Current integration points

| Existing boundary | SDL use |
| --- | --- |
| [Server](../src/core/server.h) | Submit typed commands, supply elapsed microseconds, consume `WorldView` and receipts on the session owner. |
| [World](../src/core/world.h) | Retain exclusive ownership of runtime entities on the session thread. |
| [Network adapter](../src/network/network.h) | Preserve hosting, admission, snapshots, discovery, reconnect, and sequence/opportunity checks. |
| [Content catalog](../src/content/content.h) | Resolve stable terrain/item names; keep the catalog immutable and alive until every reader stops. |
| [Input](../src/client/input.h) | Translate SDL events to `KeyChord`, then use `ResolveAction` in the active context. |
| [Settings](../src/client/settings.h) and [localization](../src/client/localization.h) | Reuse configuration, effective bindings, labels, outcomes, and item action text. |
| [Terminal application](../src/terminal/main.cpp) | Reference for lifecycle, command routing, receipts, and local/LAN modal behavior. |
| [Cell appearance](../src/client/text.cpp) | Preserve visibility filtering and overlay priority without coupling sprite rendering to FTXUI. |
| [World generation](../src/worldgen/worldgen.h) | Run pure region/chunk generation on immutable inputs; install results on the session owner. |

The existing APIs are synchronous and provide no concurrent access guarantee.
In particular, a `const` world inspection call is not permission to race with a
world mutation. Snapshot creation stays on the session owner.

## Execution and ownership

| Lane | Exclusive ownership and work | Introduction |
| --- | --- | --- |
| Main/render thread | SDL initialization/window/events, renderer, textures, renderer text engine, UI state, layout, drawing, presentation | First playable |
| Session thread | `Server`, `World`, simulation RNG, command admission/resolution, pacing, snapshot capture | First playable |
| Bounded worker pool | Asset reading/parsing/decoding, snapshot-based map preparation, deterministic region/chunk generation | Asset work first; presentation/generation next |
| Network thread | ENet hosts/peers, sockets, packet transmission, transport lifecycle events | LAN separation |
| Storage thread | SQLite connection, ordered checkpoint transactions and chunk reads | Checkpoint separation |

```mermaid
flowchart LR
    Main["Main: events / UI / rendering"] -->|"ordered requests"| Session["Session: authoritative state"]
    Session -->|"latest owned snapshot + reliable replies"| Main
    Main -->|"asset / map jobs"| Workers["Bounded worker pool"]
    Workers -->|"owned presentation results"| Main
    Session -->|"generation jobs"| Workers
    Workers -->|"owned generation results"| Session
    Session -.->|"outbound packets"| Network["Network owner: LAN stage"]
    Network -.->|"ordered transport events"| Session
    Session -.->|"checkpoint batches / reads"| Storage["Storage owner: checkpoint stage"]
    Storage -.->|"completion / owned archives"| Session
```

Rendering reuses the last completed snapshot or map packet while gameplay or
loading takes longer. The session never waits for a frame, present, or UI
callback. UI processing never waits synchronously for a session operation.

Use a small capped worker pool for independent coarse jobs. Give each job
immutable inputs and owned output storage; do not create a permanent thread per
screen, chunk, or item behavior. Start with explicit job kinds and dispatch,
RAII-managed threads, mutexes, and condition variables. Standard threading
facilities are appropriate where EASTL has no equivalent. A generic dependency
scheduler, lock-free queue library, and new allocator are unnecessary for this
design.

## Requests, replies, and snapshots

The following are proposed application-boundary contracts, not new engine or
wire APIs. Keep them SDL-free so channel tests can run headlessly.

| Proposed value | Purpose |
| --- | --- |
| `SessionRequest` | An explicit request kind and owned payload, session generation, and request ID. Gameplay requests retain player/opportunity identity; settings, pause, save, and connection requests use the same ordered control boundary. |
| `SessionReply` | A correlated admission, command result, control acknowledgement, or typed failure. Transport sequence numbers remain distinct from UI request IDs. |
| `SessionSnapshot` | An owned `WorldView`, session generation, and presentation-facing session state. No borrowed world, transport, or UI storage. |
| `MapPacket` | Owned sprite/glyph draw data tagged with session, view revision, asset generation, and viewport generation. |
| `CheckpointBatch` | A future owned session/chunk checkpoint with captured dirty versions, consumed by storage and acknowledged to the session. |

### Ordered control and reliable results

Use bounded queues with explicit enqueue failure. Enqueue success means only
that the owner received responsibility for the request. Distinguish it from
authoritative admission, gameplay completion, and durable save completion.
An accepted save request must not display "saved" until its transaction succeeds;
settings Apply becomes active only after owner acknowledgement.

Reject requests for an obsolete session or input opportunity. Never silently
retarget an old action to a newly loaded world or manufacture a fresh opportunity
when delivering delayed input. Keep the server's existing pending-intent policy;
the cross-thread FIFO must not become a chain of key-repeat-generated actions.

Replies, receipts, errors, and lifecycle acknowledgements are reliable and
ordered. Reserve result capacity for accepted requests and apply backpressure
before accepting more work. Report a typed busy/queue-full result to the UI;
never silently drop accepted actions or block the render thread waiting for room.
Stop/cancellation must still wake an owner when its ordinary inbox is full.

### Latest snapshot mailbox

Use three reusable owned values: the session's building value, a pending mailbox
value, and the renderer's current value. The producer completes capture privately,
then swaps with pending under a short mutex and marks it fresh. The consumer
swaps pending with current under that mutex only when fresh, then renders its
current value without the lock. A replaced pending snapshot returns to the
producer for reuse. Neither side mutates the other's working value.

Only display snapshots may be replaced this way. Receipts remain in their
reliable queue, and network snapshot acknowledgement rules remain unchanged.
Keep channel critical sections limited to ownership transfer: no rendering,
simulation, parsing, resource destruction, or I/O while holding a channel lock.
Retire replaced resource-bearing values after releasing the lock.

Workers wake the frontend with a registered SDL user event. The event is only a
notification, with no owning payload pointers; data remains in the channels.
[SDL_PushEvent](https://wiki.libsdl.org/SDL3/SDL_PushEvent) is callable from other
threads, but can fail. Coalesce notifications and retain a bounded event-wait
timeout so queued data is discovered even after a failed wakeup.

### Snapshot and result lifetime

Screen selections retain persistent IDs scoped to a session generation. A
session switch invalidates selection, pending requests, and outstanding jobs.
Accepted requests still receive a correlated stale/cancelled or completed reply;
invalidation must not silently erase their results.
When an item disappears, do not silently activate its neighboring row. Recheck
the selected ID against current authoritative data when submitting an action.

Map jobs receive copied cell data and the required immutable visual mapping,
never a reference to the live mailbox or mutable UI state. Adopt only complete
packets for the active session, asset generation, and viewport. Older job
completions cannot overwrite a newer accepted packet. Retain the last matching
packet while a newer revision is prepared; a layout/asset mismatch invalidates
it. Hit testing uses the transform of the displayed packet, followed by normal
authoritative validation.

## Session timing and deterministic jobs

The session adapter reads a monotonic clock outside the engine and supplies
elapsed microseconds explicitly. It wakes on requests and its own service
deadline, initially scheduled no more than 20 ms ahead while active, independently
of render cadence. This is a target service interval, not a real-time guarantee
or a new simulation timestep: preserve the existing tick units, action costs,
pacing rules, and fractional elapsed budgets. Record overruns and account for
actual elapsed time. VSync and window resizing must not determine session progress.

Before admitting newly observed input, settle the elapsed interval preceding
its admission boundary. Input cannot retroactively beat an expired deadline.
Record the actual admitted command/control order and elapsed values for replay;
render timing and optional job completion timing are not simulation inputs.

Local modal opening requests a pause. The session settles the preceding
interval, applies the pause, and acknowledges it with a snapshot. Only then is
the modal considered open for browsing or gameplay commits. Closing/resuming
uses an ordered request and resets the elapsed baseline, discarding paused wall
time. Already advanced state is not rewound to a UI event timestamp. A hosted
LAN session keeps advancing and servicing guests while its local UI is modal;
a guest modal never pauses the host. Follow [UI navigation](ui_navigation.md).

Separate independent preparation from authoritative commits:

- `GenerateRegionPlan` and `GenerateChunk` already take an explicit seed,
  coordinates, and immutable catalog/plan. Workers return plans or tile arrays;
  the session installs them in defined coordinate order. Include session,
  catalog/generation version, seed, and coordinates in result identity.
- A required generation result is committed at a defined simulation boundary.
  If it is late, the session waits there while rendering and transport continue.
  Optional prefetch changes cache availability only, not gameplay decisions.
- Map preparation and asset decoding operate on owned inputs and private
  outputs. Results may be cancelled or discarded without changing the world.
- Actor movement, occupancy, item events, command resolution, and the shared RNG
  currently overlap. Preserve their existing commit and RNG-consumption order.
  Any future parallel calculation produces proposals from an immutable input
  snapshot, merged in a stable order on the session owner. Worker count and
  completion order must not decide action winners or consume shared RNG.

Read-only access to an immutable catalog can be shared for its documented
lifetime. Mutable caches, diagnostics, parser documents, and decoder state belong
to a job or owner. Geographically separate entities alone do not establish
independence: movement and effects can cross their boundaries.

## Network and storage separation

### Transport ownership

Today `LanHost::Pump` calls `Server::Update`, admits inputs with `Submit`, and
captures player views. `QueueLocal` also reads the server. Moving these existing
methods to a network thread while the session runs would race.

Extract transport servicing from authoritative protocol/session processing.
The network owner creates, services, sends through, and destroys each ENet host.
It transfers bounded owned packet data and lifecycle events in service order.
Use typed peer IDs with connection generations across the boundary, never
`ENetPeer*`; a reused peer slot must not receive an old connection's input.
ENet's [FAQ](https://github.com/lsalzman/enet/blob/master/docs/FAQ.dox) explicitly
requires guarded access to host state; exclusive ownership supplies that guard.

The session owns player admission, command validation, authoritative ordering,
and view capture. Preserve protocol/channel ordering, input sequences and
opportunities, content compatibility, reconnect identities, and snapshot/receipt
acknowledgements. Transport servicing continues when a session operation stalls;
it does not promise authoritative progress before that operation completes.
Bound per-peer buffering and retain explicit slow-peer handling rather than
blocking the world on one client. Connection completion and profile persistence
must still obey the [LAN profile contract](lan_profile.md).

Keep synchronous `LanHost`/`LanClient` use available for terminal, headless, and
existing tests through the separated components. The SDL adapter coordinates
their threaded ownership. Thread separation alone does not require a wire change.

### Checkpoint and chunk I/O ownership

Currently `World` owns `SaveDatabase`. Its checkpoint captures live dirty chunks,
writes them and unloaded archives in one transaction, then clears dirty state
and evicts archives only after successful commit. Calling `Server::Save` or
`World::Save` on a background thread would access the live world concurrently.

Before adding the storage thread, split capture, transaction execution, and
completion. Capture an owned `CheckpointBatch` on the session owner, including
session metadata, next persistent ID, all relevant chunk archives, and dirty
versions. Transfer it to the storage owner, which exclusively owns the connection
and executes the transaction. Start with one checkpoint in flight per world.

On success, the session clears only dirty versions covered by that checkpoint;
newer edits remain dirty. Do not evict an archive still needed for newer changes,
pending reads, or failure recovery. Failure preserves unsaved state and reports
the typed error. Preserve atomic source/destination ownership changes when items
move across chunks. Save success means transaction completion, not enqueue.

Order chunk reads and writes through the storage owner. Keep newer in-memory
state authoritative over an older disk result. A required read blocks only the
session's deterministic commit boundary, never rendering or socket servicing.
[SQLite's threading modes](https://www.sqlite.org/threadsafe.html) describe
connection-level synchronization; they do not make our world checkpoint protocol
safe for concurrent mutation.

Preserve synchronous save wrappers and existing persistence formats. Until this
split is implemented, keep database calls on the session owner; rendering is
already independent of those calls. Asynchronous storage is a separate milestone,
not an assumed property of the current save API.

## Frontend modules and build boundary

The following files are proposed. Use small `.h` interfaces, `.cpp`
implementations, explicit dispatch, and composition.

```text
src/sdl/
  main.cpp                 startup and exit diagnostics
  app.h/.cpp               ownership, startup/shutdown, main/render loop
  session.h/.cpp           session-owner loop and local/host/guest routing
  channels.h/.cpp          typed requests/replies and snapshot exchange
  workers.h/.cpp           bounded jobs, completion, cancellation
  input.h/.cpp             SDL events to shared input vocabulary
  renderer.h/.cpp          SDL drawing, clipping, submission
  assets.h/.cpp            GPU resources and resolved sprite tables
  map_view.h/.cpp          camera, transforms, pure map preparation
  text.h/.cpp              fonts and reusable renderer text objects
  ui/
    controls.h/.cpp        composed focused lists, buttons, editors
    gameplay.h/.cpp        map/status layout and modal dispatch
    inventory.h/.cpp       inventory and item flows
    settings.h/.cpp        drafts and visual preferences
    lan.h/.cpp             discovery, connection, reconnect screens
    debug.h/.cpp           debug presentation
src/content/
  visual_pack.h/.cpp       JSON5 to typed SDL-free visual definitions
```

The directory groups responsibilities, not permanent threads. Extract portable
item workflow state into `src/client` when adding SDL inventory, keeping terminal
behavior and tests intact. Screens consume snapshots and emit explicit UI
requests or existing `Command` values; they do not access `World`.

Add an optional `CATACLYSMX_BUILD_SDL` target group with a small frontend library
and `cataclysmx_sdl_game` executable. Reuse `cataclysmx_no_exceptions`. SDL headers
and linkage stay in the frontend; yyjson parsing stays in `src/content`, with
the visual loader built only for the SDL target group. Keep engine, terminal,
and headless targets usable independently. A CMake target option alone does not
make manifest dependencies optional: select added frontend packages through a
matching vcpkg feature/build configuration.

Use EASTL containers, strings, vocabulary types, and explicit value ownership.
Resource wrappers have one owner and release on the required thread. Expected
startup/load failures return typed results with optional `Diagnostics*`; format
human-readable reports at the application boundary. Diagnostics transferred
between threads are owned values. Broken invariants and OOM remain fatal.

## Visual assets, text, and UI

Use one selected visual pack with PNG atlases, stable-name sprite mappings,
font selection, semantic colors, and spacing values. Screen behavior and layout
remain C++. Parse and validate pack metadata before touching GPU resources;
check duplicate mappings, atlas bounds, and referenced files. Resolve terrain
names into dense catalog-indexed sprite tables once, and item names similarly
where snapshots provide item types. Catalog-local numeric IDs are not asset
identities to persist in packs. Visual packs do not alter gameplay content,
save formats, or the network content fingerprint.

For pack PNG decoding, propose `sdl3-image` with only its PNG feature. Decode on
workers into owned CPU surfaces, then upload on the render owner with
[SDL_CreateTextureFromSurface](https://wiki.libsdl.org/SDL3/SDL_CreateTextureFromSurface).
SDL3 3.4 also has a [PNG convenience loader](https://wiki.libsdl.org/SDL3/SDL_LoadPNG)
suited to trusted bundled test assets; it is not the arbitrary-pack decoder.
Use `sdl3-ttf` and its
[renderer text engine](https://wiki.libsdl.org/SDL3_ttf/TTF_CreateRendererTextEngine)
for production UI text. Fonts, renderer text objects, and their destruction stay
on the render owner. These packages are proposed additions, not dependencies
already declared in the project manifest.

The current pinned baseline provides SDL3 3.4.20. Retain that baseline and the
existing Linux D-Bus feature constraint. Python must remain unnecessary for
building. Keep SDL_ttf's optional HarfBuzz/SVG features off initially: the pinned
HarfBuzz port brings Meson/Python. State the resulting complex-script shaping
limitation rather than promising full Unicode shaping. Recheck the dependency
graph and clean build when implementing these additions.

Manual asset reload has three stages: worker preparation, render-owner resource
creation, then activation between frames after all required resources succeed.
A failure keeps the active pack and reports diagnostics. Old jobs are tagged
with the old generation and cannot activate against the new resources. Retire
textures/fonts on the render owner after their last frame use. Bound upload work
per frame. Defer file watching, layered pack composition, and scripting.

Implement screens with explicit modal/submode dispatch and small composed
controls. Keyboard and mouse activate the same operations. Reuse the shared
keymap, localization, settings draft/Apply/Save semantics, and item action schema
including target selection, action availability, and typed refusal feedback.
The active modal consumes input; closing events cannot fall through to gameplay.

Translate SDL key events to `KeyChord`, preserving `j`/`J`, Ctrl case distinctions,
and Shift+Tab as `BackTab`. Use Unicode text/composition events for editors,
isolated from gameplay shortcuts. Ignore gameplay keydown repeats initially;
allow explicit menu-navigation repeat. Binding hints show effective bindings.
Store selection/focus/scroll state separately from draw calls and world entities.

## Rendering contracts and performance

- `WorldView::cells` is row-major: columns follow X, rows follow Z, starting at
  `origin`. This is a top-down XZ map, not world elevation.
- `Server::View` clamps radius to 0–12, at most 25×25 cells; the default and
  current LAN view are 17×17. Resize and zoom operate within the supplied view.
  Larger views need an explicit server/network contract change.
- Check `visible` before resolving terrain or overlays. Hidden cells expose no
  world information, and there is no remembered-terrain snapshot.
- Visible cells provide terrain/damage, actor ID/player flag, ground-item count,
  and object ID/enabled state. Use generic actor/item/object map sprites where
  type information is absent. Compare actor ID with `WorldView::playerId` for
  the viewing player; other player actors can still be distinguished from NPCs.
- Draw terrain below one chosen overlay: actor, then ground items, then object.
  Preserve terrain backgrounds. Missing terrain mappings use the catalog's
  resolved glyph/colors, with a visible missing-glyph fallback; missing generic
  sprites have an explicit fallback. Terrain does not retain its `looks_like`
  chain for sprite resolution.

Use atlas textures with explicit nearest sampling and `SDL_RenderTexture` first.
Reuse worker scratch vectors and screen-owned text objects. Update text only
when content, font, or layout changes. Keep string lookup, file access, texture
creation, and font loading out of routine drawing loops. Refresh the authoritative
snapshot after relevant session revisions, view-radius changes, and session
changes; drawing does not reconstruct it.

Separate window coordinates, render pixels, logical UI units, and world cells.
Follow [SDL's high-DPI guidance](https://wiki.libsdl.org/SDL3/README-highdpi).
Use shared camera/layout transforms for drawing and hit testing, clip the map,
snap pixel-art placement consistently, and keep UI scale independent of map zoom.
Resizing preserves focus and selection; compact layouts retain essential hints.

Redraw for new presentation data, UI changes, resize/expose, or animations, with
a separate frame deadline and bounded SDL event waits. A new world revision
need not rebuild unchanged labels or textures. Presentation animations use a
frontend clock and do not consume simulation RNG. Once a frame is drawn, clear
and redraw its backbuffer before presenting; previous backbuffer contents are
not a persistent cache.

The 625-cell limit bounds initial map work, not simulation or I/O latency.
Independent rendering is required from the start. Extra geometry batching,
render-target caches, and finer job partitioning require measurements.

## Startup, cancellation, and shutdown

Startup creates the immutable content/catalog lifetime before launching session
and worker readers. Initialize SDL window/renderer on main; bring up each owner
with an explicit success/failure handshake. Display loading state while workers
prepare assets. A partial startup failure stops and joins started owners before
releasing their dependencies; do not enter play with a failed required service.

Cancellation invalidates optional queued/running jobs by generation. A running
job keeps its inputs alive until completion even when its result will be ignored.
Do not abandon an accepted command or checkpoint without a terminal result.
Required simulation jobs complete at their prescribed boundary or the session
ends with an explicit error.

On shutdown, stop accepting UI requests, cancel optional work, and request the
session to finish its current boundary and outstanding accepted operations.
Keep channels/services alive and drain replies/completions while they are needed;
do not wait for an owner while blocking a completion path it needs. Drain/close
transport with a bounded deadline, settle in-flight storage transactions, and
stop and join all background owners. Release server/archives and the immutable
catalog only after their readers finish. Destroy text objects, text engine,
fonts/textures, renderer, window, and finally SDL on the render owner in dependency
order. No worker may still post an SDL wakeup after SDL shutdown. Never hold a
channel lock while joining or waiting for another owner.

## Delivery and validation

| Milestone | Deliverable and acceptance |
| --- | --- |
| 1. Threaded foundation | Optional SDL target, main/render owner, independent session loop, bounded channels, worker asset preparation, one atlas, resizable local play with movement/wait/pickup/drop/interaction and readiness/result feedback. A stalled session must not stop UI/rendering. |
| 2. Client functionality and jobs | Inventory/item actions, settings, localized responsive screens, parallel map preparation and region/chunk generation with deterministic installation. |
| 3. Independent services | Extract LAN transport ownership, then checkpoint batches and storage ownership. Keep synchronous adapters, current wire/save formats, and receipt/save completion semantics. |
| 4. Full parity and profiling | Complete LAN/reconnect, debug screens, manual reload, then measured optimization. |

Use controlled latches/barriers or fake owners for concurrency tests, rather
than relying on sleeps to produce a particular interleaving. Add focused checks:

- Render/UI progress during a stalled session; session and network progress
  during stalled rendering; continued transport servicing during storage work.
- Complete snapshot ownership and absence of tearing, bounded queue saturation,
  retained correlated receipts, failed/coalesced wakeups, cancellation, partial
  startup failures, and shutdown with full queues or running jobs.
- Identical checksums/state for the same admitted command/time trace across
  worker counts and deliberately reordered completions. Preserve existing RNG,
  actor/item ordering, and generation output.
- Stale opportunities, replaced sessions and reused IDs/peer slots, pause/resume
  acknowledgement, elapsed-time discard, and input arriving after deadlines.
- Reload during rendering, obsolete map packets, decode/upload failure, font and
  viewport changes, and resource release on the correct owner.
- Checkpoint failure, newer edits during a checkpoint, cross-chunk ownership,
  archive retention, and read/write ordering without losing dirty state.
- Atlas bounds/fallbacks, hidden cells, overlay order, coordinate round trips,
  DPI/resize hit testing, rebound/case-sensitive controls, editor isolation,
  focus/cancellation, and item action/target/refusal behavior.

Assert SDL thread affinity with `SDL_IsMainThread` and owner assertions for
session/transport/storage calls. Add an offscreen/software-renderer smoke test
where supported and a dedicated Linux ThreadSanitizer configuration separate
from ASan/UBSan. Run existing deterministic headless, pacing, generation,
network, item, and save regressions as their boundaries change. Retain terminal
tests when extracting shared interaction state.

Measure frame latency, snapshot age, queue depth/high-water marks, session-update
time, job duration/worker utilization, texture/resource counts, and draw calls.
Separate cold load/reload, idle UI, active play, and LAN/storage stalls. Publish
hardware/build configuration with results; make no speedup or latency claim
before benchmarking. This document changes no code and supplies no build,
sanitizer, or benchmark result.

See [server integration](server_api.md), [tick scheduling](tick_scheduling.md),
[items](items_api.md), [settings](settings_api.md), [terrain rendering](terrain_rendering.md),
[networking](networking.md), and [save format](save_format.md) for existing contracts.
