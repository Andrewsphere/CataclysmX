# AI decision inspection

F11 retains the world-generation preview. `a` toggles the live AI inspector in
both SDL and terminal clients. Move the cursor onto an actor with arrows/hjkl;
Alt+Up/Down or `>`/`<` selects elevation. Home/c recenters on the player and
PageUp/PageDown scrolls details. These controls use the Debug keybinding context.

The inspector shows a 17x17 resident-world map, controller/profile, current
behavior, target memory and age, sensing timers/counters, action deadline, and
current cardinal/diagonal movement timing. Colored local probes describe the
**last actual decision**, whose origin may differ from the actor's current tile:

- Green: eligible candidate; red: rejected probe or search restriction.
- Orange: excluded previous position (anti-backtracking).
- Blue: decision origin; purple: remembered goal; yellow: chosen destination.

Details list all ten candidates (eight horizontal, two vertical), rejection
reasons, and squared geometric distances to the captured goal. Scores rank
pursuit only; idle/search use deterministic RNG and attacks bypass movement.
The current map and historical probes are intentionally labelled separately.
No decision exists immediately after controller creation/restoration. Debug
evidence is ephemeral, not saved or used to make simulation decisions.

## API and ownership

`AiDecisionInspection` in `actors/ai_controller.h` stores one fixed-size decision
summary inside the existing `AiInspection`. It stores ten one-byte probe statuses
and shared origin/goal coordinates rather than ten repeated destinations/scores.
`actorStepDirections` supplies stable indices, and `AiMovementScore` derives the
displayed scores using the same arithmetic as pursuit. This avoids per-decision
allocation, entity pointers, and a per-actor history buffer.

`ActorStepBaseCost` in `actors/movement.h` applies the diagonal factor
`ceil(base * 141 / 100)` after locomotion/injury scaling and before speed
conversion. Player and NPC execution share it. Stairs/cardinal steps retain
ordinary cost; combat and door opening keep their own costs. Movement retains
destination-only corner rules.

`CaptureAiDebugView(world, cursor, output)` in `client/ai_debug.h` fills owned
presentation values and reuses caller-owned storage. It never loads chunks,
teaches player memory, advances time, or invokes sensing/decision callbacks.
The explicit debug view is unfiltered resident-world inspection, separate from
ordinary player examination. `DebugMapView` is the shared tile-image carrier;
`WorldgenDebugView` remains its compatibility name for generated previews.

SDL requests `InspectAi` only while paused. The simulation owner captures the
view and returns `AiInspected` with owned values; the frontend never borrows
World or actors across threads. One capture request is outstanding at a time;
request/session identity, cursor and mode revision discard obsolete replies.
Terminal captures at opening/navigation and renders the cached value.
