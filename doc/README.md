# API documentation

- [World API](world_api.md): identity, ownership, coordinates, chunk demand and
  in-memory archives. Start here for scenario setup and administrative tools.
- [Server and simulation API](server_api.md): commands, turn advancement, views,
  player bubbles and client integration. Start here for a game client.
- [Tick scheduling](tick_scheduling.md): simulation units, runtime pacing,
  speed-scaled input deadlines and idle policy.
- [Content format and catalog](content_format.md): JSON5 terrain definitions, IDs,
  building patterns, appearance inheritance, and loading APIs.
- [World generation](worldgen_api.md): deterministic region plans, boundary
  crossings, parcels/buildings, terrain materialization and debug inspection.
- [Diagnostics](diagnostics.md): structured error causes and contextual frames.
- [Terrain rendering](terrain_rendering.md): UTF-8 symbols, colors, and frontend behavior.
- [Save format](save_format.md): SQLite v3, snapshots, validation and checkpoints.
- [Project README](../README.md): build instructions and dependencies.

The public interfaces are [`world.h`](../src/core/world.h),
[`server.h`](../src/core/server.h) and
[`simulation.h`](../src/core/simulation.h), in namespace `cataclysmx`.
These pages explain contracts and usage rather than repeat every declaration.

The current game is a deterministic, local single-player demo with tick
scheduling and SQLite disk saves. Player input barriers and pacing support
future multiplayer integration. Unloaded archives stay in memory until a
successful checkpoint permits eviction.
