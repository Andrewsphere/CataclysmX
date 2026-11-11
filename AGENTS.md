# Project instructions

Build a small, deterministic C++23 roguelike inspired by Cataclysm: Bright Nights. Gameplay and clarity take priority over realism or speculative optimization.

## Code

- Follow style guide where available. Prefer explicit, grep-friendly C++: small `.h` interfaces, `.cpp` implementations, composition, qualified calls, and simple control flow.
- Compile **engine code without exceptions**. Return typed results for expected errors, distinguish gameplay outcomes, and fail fast on broken invariants. OOM is fatal.
- Prefer value ownership and RAII. Use EASTL for game containers; use generational, typed, non-owning handles for world objects. Do not keep entity pointers across deletion or uncontrolled mutations.
- Keep dependencies and abstractions minimal. Do not add a library, generic framework, allocator, or clever metaprogramming without a demonstrated need.

## Style guide

- Use **C++23** and prefer straightforward, grep-friendly code. Keep public interfaces small and explicit; put non-template implementations in `.cpp` files.
- Prefer explicit control flow and qualified calls. Do not use SFINAE, CRTP, clever metaprogramming, implicit ADL-based customization, hidden registration, or global mutable state. Use simple templates and `constexpr` when they clearly reduce complexity.
- Prefer composition and free functions over inheritance. Use `switch`/explicit dispatch for small closed sets; allow virtual functions only for small, stable subsystem interfaces. Avoid deep class hierarchies and RTTI-based dispatch.
- Compile project code with **exceptions disabled**. Use `eastl::expected<T, Error>` for recoverable failures; distinguish normal gameplay outcomes from errors, and use always-on assertions for broken invariants. Treat out-of-memory as fatal.
- Separate error codes from human-readable diagnostics. Use an optional `Diagnostics*` output parameter for operations that need detailed reports (loading, parsing, validation), avoiding allocations when it is null. Do not thread diagnostics through routine simulation code.
- Prefer values and explicit ownership. Store runtime game entities in generational `SlotMap`s owned by the world; handles are typed, non-owning references. Use `try_get` for potentially stale handles, and do not retain element pointers/references across structural mutations. Prefer deferred entity destruction at defined simulation boundaries.
- Use **EASTL** for game-oriented containers (including small/fixed vectors), a vetted flat/dense hash map for hash tables, Prefer EASTL containers, strings, views, and vocabulary types (expected, optional, etc.). Use standard library facilities when EASTL has no suitable equivalent or when interfacing with external APIs. Avoid mixing EASTL and STL equivalents within internal interfaces; perform conversions at integration boundaries. Prefer contiguous storage for small collections; do not introduce containers or allocators without a concrete need.
- Prefer stack/value ownership; use RAII for resources, `eastl::unique_ptr` only when dynamic unique ownership is useful, and `eastl::shared_ptr` only with explicit justification.
- Keep simulation deterministic and independent of UI, networking, and wall-clock time. Pass RNG/time explicitly, define iteration/order semantics, and support headless tests and replay.
- Avoid unnecessary abstractions, macros, implicit conversions, and framework-like code. Each change should preserve module boundaries and include focused tests; optimize only after measurement.
- Keep third-party libraries behind narrow subsystem boundaries. Prefer EASTL for internal containers and vocabulary types. Use yyjson only in content loading, ENet only in networking, and terminal libraries only in frontends. Never expose third-party implementation details through simulation APIs unless explicitly justified.
- Use PascalCase for types, methods, functions and enum members.
- Use camelCase for variables, parameters, and fields.
- Use a trailing underscore (camelCase_) for private and protected data members.
- Do not use trailing underscores for public fields, including fields of simple value types and component structs.
- Consider ankerl hashmaps/sets when designing memory layout if ordering is not required.
- Be careful we allocations, do not design APIs that return disposable vectors.

## Architecture

- This is game, not a simulator. We employ determinism while it's beneficial for simplicity/design/testing/development in general. If stricked determinism requires too much machinery, rule relaxation is acceptable.
- Keep subsystem boundaries clear. Content is parsed/validated into typed data; runtime entities are owned by the world. Preserve headless execution and replayability.
- Do not silently change gameplay timing, public contracts, persistence formats, or dependency versions.

World generation must be reproducible for the same seed, coordinates, generation version, and content. Simulation should reproduce results from the same logical state, RNG state, and authoritative inputs within a game/content version, independently of runtime handle allocation. Keep storage traversal unordered where order is irrelevant; impose stable ordering at order-sensitive execution boundaries. Prefer reusable due-work batches over repeatedly sorting all entities. Async preparation is compatible with this policy, but simulation activation timing must be defined or treated as an explicit input. Preserve practical replayability without introducing elaborate machinery solely for stronger determinism guarantees.

## Workflow

- Do not add, maintain, or run tests for tooling.
- Read the relevant module and existing tests before editing. Make small, focused changes; add focused tests for changed behavior and edge cases.
- Run the narrowest applicable CMake/CTest preset when available; report precisely what ran, passed, failed, or could not run. Never claim unrun tests passed.
- A coordinating agent owns integration. Subagents may work on separate tasks, but must not edit the same files concurrently.
- Do not commit, push, discard user changes, or perform broad destructive operations without an explicit request.
- Use LSP queries if helpfull.

## Documentation
- Document new APIs inside `/doc` directory.
- Include brief insides into dicision making and reasoning for API shapes.
