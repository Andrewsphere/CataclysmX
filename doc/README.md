# API documentation

- [Sledgehammer Editor](sledgehammer_editor.md) for visual content authoring, seeded previews,
  and saving edits into a checkout.

- [Sequence containers](containers.md): bounded lists, overflow-enabled small
  vectors, bulk vectors, and inline-storage/move-cost tradeoffs.

- [Graphics tool](graphics_tool.md): standard-library Python PNG operations,
  sprite editing, contact sheets, and bundled-artwork regeneration commands.

- [Individual sprite tilesets](source_tilesets.md): package names, local metadata,
  startup atlas assembly, anchored sprites, variants, and connected roads.

- [Windows SDL in VS Code](vscode_windows.md): build, run, debug, and prerequisites.
- [Vulkan builds](vulkan_build.md): shared Windows/Linux rendering, runtime GLSL compilation and dependencies.
- [Default tileset artwork](tileset_artwork.md): individual Gen1 sprites, references,
  preparation, transparent overlays and lightbulb frames.
- [Imported item sprites](item_sprites.md): ComfyUI artwork, content mappings,
  scenario compatibility, retained bulb animations and item fallbacks.
- [SDL frontend architecture](sdl_client.md): session ownership, snapshots,
  local pause and live multiplayer menus.
- [SDL multiplayer](sdl_multiplayer.md): hosting, joining, discovery, reconnect,
  persistent host saves and cross-frontend launch commands.
- [Sprite animation](sprite_animation.md): loop/once playback, visual activation,
  item overlays, and the three lightbulb demos.
- [Gameplay UI](gameplay_ui.md): responsive map/status layout, on-demand inventory,
  players and controls, selected-item drops and terminal presentation API.
- [Shared item UI](inventory_ui.md): inventory tables, examination, two-pane advanced
  inventory, exact selections and interruptible quantity transfers.
- [SDL inventory](sdl_inventory.md): scaled item icons, colored art rendering,
  pane layout and pause/paid-action handoff.
- [Debug item browser](item_browser.md): F11 catalog search, examination,
  single/batch giving, and the authoritative local/host grant API.
- [UI navigation controls](ui_navigation.md): baseline keyboard controls, focus,
  selection, cancellation, and modal input behavior.
- [Settings, input, and localization](settings_api.md): sparse global/world
  overrides, the F2 editor, case-sensitive Ctrl bindings, and text catalogs.
- [World API](world_api.md): identity, ownership, coordinates, chunk demand and
  in-memory archives. Start here for scenario setup and administrative tools.
- [Actors and AI controllers](actors_api.md): deterministic intents and controller state.
- [Sighted entity AI](entity_ai.md): sensing, memory, discovery, profiles and inspection.
- [AI debug inspection](ai_debug.md): F11 live actor inspection, historical movement
  overlays, shared movement costs, and owned frontend captures.
- [Combat API](combat_api.md): body, injury, moves, disruption and recovery.
- [Combat presentation](combat_presentation.md): frontend status and feedback text.
- [Combat runtime](combat_runtime.md): action resolution, training and local costs.
- [Combat content](combat_content.md): authored definitions, bounds and starter roster.
- [Combat learning](combat_learning.md): arts and repeatable manual study.
- [Combat lifecycle](combat_lifecycle.md): equipment, death and replacement.
- [Combat UI](combat_ui.md): frontend-neutral menus, commands and inspection.
- [Combat experiments](combat_experiments.md): headless runner and interpretation.
- [Runtime skills](skills.md) and [skill content](skills_content.md): progression,
  books, crafting gates and shipped definitions.
- [Objects API](objects_api.md): furniture, storage, movement, lighting, crafting machines and saved state.
- [Electrical power](power_design.md): coverage grids, mains cutoff, generators,
  installed batteries, funded jobs, remote workshop residency and persistence.
- [Power commands and views](power_presentation.md): wiring/placement, multimeter
  coverage and host-only grid/chunk diagnostics in terminal, SDL and LAN.
- [Recovered solar and wood-gas power](renewable_power_design.md): batched daylight
  supply, fixed installations, prepared wood, assembly routes and acquisition.
- [Object gameplay](object_gameplay.md): interaction, collection and push/pull scheduling.
- [Grabbing](grabbing.md): persistent eight-direction furniture movement, timing and feedback.
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
- [Inventory and equipment](inventory_engine.md): wielding, weapon-based melee,
  nearby storage access, authoritative transfers, persistence and transport.
- [Content asset loading](content_assets.md): data-root paths, synchronous readers,
  file/memory loading, ownership, diagnostics, and distribution.
- [Item presentation content](item_presentation.md): descriptions, colored UTF-8
  external and inline art, single-cell character policy, melee profiles and defaults.
- [Item authoring](item_authoring.md): item, flags, art and progression workflows.
- [Second item expansion](item_expansion.md): historical 80-item authoring record,
  acquisition routes, crafting/salvage, artwork and starting balance evidence.
- [Item balance anchors](item_balance.md): reference items, progression tradeoffs,
  acquisition checks and combat comparison criteria for new designs.
- [Definition flags](definition_flags.md): shared vocabulary, domains and set API.
- [Item content pilot](item_pilot.md): five-item roster, crafting and balance rationale.
- [Runtime crafting and recycling](crafting.md): jobs, reservations, work,
  energy accounting and persistence.
- [Integer ratios and bounded lists](ratios.md): the 10000-based project ratio
  convention, checked floor scaling, and compact inline content storage.
- [Crafting content](crafting_content.md): recipes, tool traits, recycling and validation.
- [Crafting commands and transport](crafting_commands.md): authoritative commands,
  results and snapshot layout.
- [Crafting controls and frontend ownership](crafting_ui.md): menus, controls and
  headless script commands.
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
- [Content packs](content_pack.md): manifest loading, semantic source ownership,
  current catalog membership, asset roots and deployment.
- [Urban content](urban_content.md): Town scenario, building roster, specialists,
  resources and current generation limits.
- [Urban resources](urban_resources.md): detailed furniture, loot, monsters and recipes.
- [Urban content design](urban_content_design.md): implemented design and integration rationale.
- [House content](house_content.md): furniture, loot, monsters and building schemas.
- [First furnished house](first_house_design.md): implemented scenario, generation and design rationale.
- [World generation](worldgen_api.md): deterministic region plans, boundary
  crossings, parcels/buildings, terrain materialization and debug inspection.
- [Diagnostics](diagnostics.md): structured error causes and contextual frames.
- [Terrain rendering](terrain_rendering.md): UTF-8 symbols, colors, and frontend behavior.
- [Save format](save_format.md): SQLite persistence, snapshots, validation and checkpoints; current save format is v17 with solar source state.
- [Furniture sprites](furniture_sprites.md): imported artwork, normalization, object mappings and remembered identity.
- [LAN networking](networking.md): ENet hosting, joining, authoritative messages and discovery.
- [Network presentation](network_presentation.md): owned map visuals, interaction
  choices, filtered layer inspection and receipt-updated input context.
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

## Gameplay design

These documents describe implemented direction alongside remaining design work;
the API pages document current contracts:

- [Urban workshop progression plan](urban_workshop_progression.md): implemented grid
  power, fueled workshops, computer information, 3D printing, old/futuristic cities,
  population and reward progression, and coordinated design/implementation passes.
- [Workshop integration measurements](workshop_integration.md): screwdriver
  bootstrap, generation probe/report usage, 33-seed acquisition results and stock
  tuning; [progression audit](workshop_progression_audit.md) for prerequisite,
  learning, transport and operating budgets.
- [Digital fabrication design](digital_fabrication_design.md): accepted transferable
  information, persistent machine libraries, job capture and commissioned-batch
  completion learning; first home-printer scope and deferred hacking/extraction.
- [Home-printing contract](home_printing_design.md): accepted useful products,
  prepared filament, setup/batch rules, resource budgets and acquisition.
- [City identity contract](city_identity_design.md): accepted settlement size/era,
  population, rewards, starts, centralized computing, dystopian sites and terrain.
- [Combat design](../md/CombatDesign.md): JSON-defined arts, progression, weighted
  move/target selection, reactions, active abilities, two-hand equipment, armor,
  human death, and headless balance experiments.
- [Skill design](../md/SkillsDesign.md): learned/effective levels, ordinary and
  theory/practice XP, level-scoped book boosts, and activity teaching limits.
- [Crafting design](../md/CraftingDesign.md): existing jobs and planned skill gates
  and training rewards.

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

The terminal, SDL and live headless clients load the saved token when connecting and
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
