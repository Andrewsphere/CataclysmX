# API documentation

- [World API](world_api.md): identity, ownership, coordinates, chunk demand and
  in-memory archives. Start here for scenario setup and administrative tools.
- [Server and simulation API](server_api.md): commands, turn advancement, views,
  player bubbles and client integration. Start here for a game client.
- [Project README](../README.md): build instructions and dependencies.

The public interfaces are [`world.h`](../src/core/world.h),
[`server.h`](../src/core/server.h) and
[`simulation.h`](../src/core/simulation.h), in namespace `cataclysmx`.
These pages explain contracts and usage rather than repeat every declaration.

V1 is a deterministic, local single-player demo. It has no networking or
multiplayer, disk-save API, or general scheduler. Chunk archives retain values
inside one `World`; they do not provide bounded-memory world streaming.
