# Project instructions

Build a small, deterministic C++23 roguelike inspired by Cataclysm: Bright Nights. Gameplay and clarity take priority over realism or speculative optimization.

## Code

- Follow `style_guide.md` where available. Prefer explicit, grep-friendly C++: small `.h` interfaces, `.cpp` implementations, composition, qualified calls, and simple control flow.
- Compile **engine code without exceptions**. Return typed results for expected errors, distinguish gameplay outcomes, and fail fast on broken invariants. OOM is fatal.
- Prefer value ownership and RAII. Use EASTL for game containers; use generational, typed, non-owning handles for world objects. Do not keep entity pointers across deletion or uncontrolled mutations.
- Keep dependencies and abstractions minimal. Do not add a library, generic framework, allocator, or clever metaprogramming without a demonstrated need.

## Architecture

- `Simulation` must be deterministic: no UI, network, wall-clock, hidden RNG, or implicit hash-iteration order. Pass commands, time, and RNG explicitly.
- Keep subsystem boundaries clear. Content is parsed/validated into typed data; runtime entities are owned by the world. Preserve headless execution and replayability.
- Do not silently change gameplay timing, public contracts, persistence formats, or dependency versions.

## Workflow

- Read the relevant module and existing tests before editing. Make small, focused changes; add focused tests for changed behavior and edge cases.
- Run the narrowest applicable CMake/CTest preset when available; report precisely what ran, passed, failed, or could not run. Never claim unrun tests passed.
- A coordinating agent owns integration. Subagents may work on separate tasks, but must not edit the same files concurrently.
- Do not commit, push, discard user changes, or perform broad destructive operations without an explicit request.
