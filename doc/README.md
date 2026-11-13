# API documentation

- [Graphics tool](graphics_tool.md): standard-library Python PNG operations,
  sprite editing, contact sheets, and bundled-artwork regeneration commands.

- [Individual sprite tilesets](source_tilesets.md): package names, local metadata,
  startup atlas assembly, anchored sprites, variants, and connected roads.

- [Windows SDL in VS Code](vscode_windows.md): build, run, debug, and prerequisites.
- [Vulkan builds](vulkan_build.md): shared Windows/Linux rendering, runtime GLSL compilation and dependencies.
- [Default tileset artwork](tileset_artwork.md): individual Gen1 sprites, references,
  preparation, transparent overlays and lightbulb frames.
- [SDL sprite client preparation](sdl_client.md): frontend integration points,
  snapshot limits, and a first playable milestone.
- [Sprite animation](sprite_animation.md): loop/once playback, visual activation,
  item overlays, and the three lightbulb demos.
- [Gameplay UI](gameplay_ui.md): responsive map/status layout, on-demand inventory,
  players and controls, selected-item drops and terminal presentation API.
- [UI navigation controls](ui_navigation.md): baseline keyboard controls, focus,
  selection, cancellation, and modal input behavior.
- [Settings, input, and localization](settings_api.md): sparse global/world
  overrides, the F2 editor, case-sensitive Ctrl bindings, and text catalogs.
- [World API](world_api.md): identity, ownership, coordinates, chunk demand and
  in-memory archives. Start here for scenario setup and administrative tools.
- [Actors and AI controllers](actors_api.md): deterministic intents and controller state.
- [Sighted entity AI](entity_ai.md): sensing, memory, discovery, profiles and inspection.
- [Combat API](combat_api.md): body, injury, moves, disruption and recovery.
- [Combat presentation](combat_presentation.md): frontend status and feedback text.
- [Objects API](objects_api.md): typed crafting machines, actions, and saved-state defaults.
- [Object callbacks](object_callbacks.md): explicit identity, atomic transactions,
  and deferred destruction.
- [Entity scheduling](entity_scheduling.md): absolute deadlines, suspended time,
  and the authoritative World clock.
- [Y layers](y_layers.md): bounded elevation, whole-height chunk columns, stairs,
  gravity, 3D sight, and layered generation/persistence contracts (introduced
  historically with six layers; now 21 bounded Y values).
- [Lighting, perception, and player memory](perception.md): resident light,
  visibility channels, observation timing, and owned exploration memory.
- [Bulk perception](bulk_perception.md): exact 3D visibility, World revisions,
  cache invalidation, and work counters.
- [Surface lighting](surface_lighting.md): observer-facing illumination and
  incremental light-field updates.
- [Sight defaults](sight_defaults.md): sight, residency, viewport, and save behavior.
- [Terminal performance](terminal_performance.md): map DOM reuse and timer wakeups.
- [Perception benchmark matrix](perception_benchmarks.md): scenarios and measurement
  method, integrated Debug/Release results, and measured remaining limits.
- [In-game profiling](profiling.md): instrument the terminal game with Visual Studio
  2022, full symbols, optimized or Debug builds, and reproducible captures.
- [Items API](items_api.md): physical properties, volume-limited containment,
  installed batteries, explicit use actions, charging and timed door controllers.
- [Adding item behaviors](item_behaviors.md): virtual interfaces, explicit factories,
  shared definitions, local actions and safe callback/scheduling contracts.
- [Structured state](data_object.md): owned typed objects and versioned capability
  serialization behind the JSON persistence boundary.
- [Server and simulation API](server_api.md): commands, turn advancement, views,
  player bubbles and client integration. Start here for a game client.
- [Tick scheduling](tick_scheduling.md): simulation units, runtime pacing,
  speed-scaled input deadlines and idle policy.
- [Content format and catalog](content_format.md): JSON5 terrain and item definitions,
  capabilities, IDs, building patterns, appearance inheritance, and loading APIs.
- [World generation](worldgen_api.md): deterministic region plans, boundary
  crossings, parcels/buildings, terrain materialization and debug inspection.
- [Diagnostics](diagnostics.md): structured error causes and contextual frames.
- [Terrain rendering](terrain_rendering.md): UTF-8 symbols, colors, and frontend behavior.
- [Save format](save_format.md): SQLite v10, snapshots, player memory, validation and checkpoints.
- [LAN networking](networking.md): ENet hosting, joining, authoritative messages and discovery.
- [LAN client profile](lan_profile.md): endpoint identities, typed errors and durability.
- [Project README](../README.md): build instructions and dependencies.
- [Running tests](testing.md): compact CTest runner and result-log behavior.
- [Sanitizer follow-ups](sanitizer_followups.md): outstanding SDL leak and TSan reports from the Vulkan validation.

The public interfaces are [`world.h`](../src/core/world.h),
[`server.h`](../src/core/server.h) and
[`simulation.h`](../src/core/simulation.h), in namespace `cataclysmx`.
These pages explain contracts and usage rather than repeat every declaration.

The deterministic server supports local-only sessions and authoritative LAN
guests, with SQLite host saves. Unloaded archives stay in memory until a
successful checkpoint permits eviction.

## LAN profile API

`client/lan_profile.h` owns a bounded set of endpoint identities. `Load(path)`
reads a versioned text file, treating a missing file as empty and rejecting a
malformed or truncated file without changing already loaded entries.
`Find(address, port)` returns the saved token/name for precisely that endpoint;
`Set(identity)` replaces that endpoint only, and `Forget(address, port)` removes
it. `Save(path)` writes an exclusively created temporary file next to the target
and replaces the target on success. A failed write leaves the prior file intact.
Profiles are local client credentials, separate from host SQLite game saves.
Endpoints are bounded to 64; address/name to 255/64 bytes, preserving UTF-8 and
forbidding control characters. Tokens are nonzero decimal unsigned 64-bit values.

The terminal and live headless client load the saved token when connecting and
persist a newly issued token as soon as the server accepts. A rejected token is
kept, so an unavailable save cannot silently replace a character. The terminal
LAN menu offers **X** for explicit new-character admission at its direct address.
The terminal retains its local world until a guest join succeeds; guest views
and commands use `LanClient` exclusively. The host retains ownership of pacing,
saves and the local command pipeline; `LanHost::Close()` parks remote players
before a final save. The profile API uses checked C stdio because project code
builds without exceptions. Including the port in endpoint keys prevents token
collisions between independent sessions on the same address.

- [SDL controls and inspection/interaction APIs](sdl_controls.md)

- [Planned SDL options redesign](sdl_options_redesign.md): layout, focus, editing, and failure behavior for the later UI rebuild.
