# API documentation

- [World API](world_api.md): identity, ownership, coordinates, chunk demand and
  in-memory archives. Start here for scenario setup and administrative tools.
- [Server and simulation API](server_api.md): commands, turn advancement, views,
  player bubbles and client integration. Start here for a game client.
- [Save format](save_format.md): SQLite v1, snapshots, validation and checkpoints.
- [Project README](../README.md): build instructions and dependencies.

The public interfaces are [`world.h`](../src/core/world.h),
[`server.h`](../src/core/server.h) and
[`simulation.h`](../src/core/simulation.h), in namespace `cataclysmx`.
These pages explain contracts and usage rather than repeat every declaration.

V1 is a deterministic, local single-player demo with SQLite disk saves. It has
no networking, multiplayer, or general scheduler. Unloaded archives stay in
memory until a successful checkpoint permits eviction.
