# SDL inspection, interaction, and menus

## Controls and movement

`x` opens Examine. The eight gameplay directions move its cursor; the camera follows within the
filtered snapshot. Alt+Up/Down and the existing layer bindings change its viewed
Y level within world bounds without moving the player. `z`/`Z` zoom also works. Ctrl+Up/Down or PageUp/PageDown scroll the description.
Escape resumes. Terrain and overlays use their independent projection depths.
Remembered appearances are labelled and do not expose live actors or contents.
Stored object contents require immediate access. Inspection neither teaches
memory nor acquires chunks.

`e` (or `o`) highlights visible doors and objects on the player's tile and eight
neighboring tiles, always on the same Y level. A sole nearby target tile is
selected automatically. A direction immediately opens that tile's actions; `.`
or Home selects the player's tile. Exactly one available action executes
immediately. Otherwise Up/Down selects an action and Enter executes it. Unavailable
actions remain gray with refusal reasons. Escape returns to target selection,
then resumes. Loose items and inventory retain their existing interfaces.

Alt+Up ascends and Alt+Down descends, using the ordinary stairs checks. Ctrl+Left
and Ctrl+Right move southwest/southeast; Shift+Left and Shift+Right move
northwest/northeast. Plain arrows retain their original meanings. Diagonals are
single atomic horizontal steps: adjoining walls do not block them. Their base
locomotion cost is `ceil(base * 141 / 100)`, followed by the existing speed
conversion. Destination occupancy, gravity and residency rules still apply.
Melee and object access use the same eight horizontal neighbors. Interaction
never crosses Y levels. Adjoining walls add no extra diagonal reach restriction.

Bumping closed openable terrain opens it at the normal opening cost without
entering. A later movement enters. This authoritative behavior also applies to
terminal/headless clients. An attempted diagonal door opening uses opening
cost, rather than diagonal locomotion cost.

## Debug and options

F11 ports the terminal's world-generation preview: arrows/hjkl pan, HJKL jump a
region, Home/c recenter, 1–5 or Tab/Shift+Tab select the five layers, +/- zoom,
and PageUp/PageDown scroll details. The surface preview excludes live entities
and saved edits. Unsupported generation versions show an explicit warning.

F2 opens the keyboard-only preferences and binding editor. Tab/Shift+Tab select
scope, section, category, list, or details. Arrows navigate; Enter edits. The
Save binding performs Save and apply. Reset, append binding, unbind, and key
inspection remain available. Number editing uses SDL text input; binding capture
uses translated key events. Wide layouts place details alongside the list;
compact layouts stack them. World overrides remain memory-only for temporary
SDL worlds. Seed is a future-world default, never a live-world mutation.

All these screens pause local simulation. Closing discards paused wall time.
Existing map sprite activation clicks remain blocked while a screen owns input;
these menus add no mouse controls.

## API decisions

`core/interaction.h` defines `InteractionRequest`, `InteractionChoice`, and
`QueryInteractions(world, actor, position, output)`. Requests retain the absolute
tile, persistent object ID or expected terrain ID, and exact action. They never
substitute the action appropriate to a later state. The server re-queries and
validates before mutation, including visibility, reach, occupancy, and capacity.
The legacy directional interaction command remains available.

`ObjectBehavior::QueryActions` supplies supported actions and typed refusal
reasons through caller-owned output. `ObjectReadContext` provides read-only input
and collection checks. `World::UseObject` runs discovery's eligibility checks
before invoking mutation. This keeps machine rules in their behavior and gives
all frontends the same answers without a UI switch on machine state. Simple
objects and terrain contribute their own action families; state-unavailable
alternatives remain visible.

`client/tile_inspection.h` formats owned descriptions from an authoritative
filtered `WorldView`. It is intentionally a client helper, not a simulation
text dependency. The session captures it; render code never borrows World.

SDL control requests add Inspect, ApplySettings, and ResumeAndCommand. Queries
carry request/session identity; the frontend coalesces cursor requests and
ignores obsolete results. Settings are staged and language loading completes
before requesting application. `ApplySettings(server, global, world, diagnostics)`
preserves runtime residency when neither scope overrides it. Only a successful
owner acknowledgement installs frontend preferences/bindings/localization;
Save and apply reports filesystem failures as Applied, unsaved and retains the
current settings for retry. Reopening clean options reloads persisted settings. ResumeAndCommand discards paused
wall time and submits the selected command in one owner operation.

`ScreenFrame` contains presentation values; `OptionsUi` owns drafts and editing
state. SDL rendering consumes those values without a server dependency or a new
widget library. See [settings](settings_api.md) for keybinding format v2 and
[networking](networking.md) for protocol v12 compatibility.

## Zoom and held input

Map zoom is a multiplier of native tileset dimensions: default 1x, steps of
2^(step/4), bounded to 1/4x–8x. Resizing does not change tile scale. UI scale remains
independent. The main view shows only status and an F1 Help reminder for controls.

All keyboard actions support holding through the central HeldKey controller,
including screen commands and confirmations. Repeats start after 250 ms at the
Keyboard repeat rate preference (default 10 Hz, range 1–60 Hz). Native repeat
events are ignored; release/focus loss stops repeats, with no catch-up burst.

## Demo door

The door at X=3, Z=0, Y=0 starts closed and unlocked. Its action list contains
Open, Close, Lock, Unlock. Lock requires a closed door; Unlock leaves it closed.
Neither requires a key. Both cost 100 base ticks on success. Refusals cost no time.
Locked doors refuse bump opening and timed door-controller opening. The separate
lockable terrain family leaves ordinary generated doors unchanged.

`TerrainDefinition::locksTo` and `unlocksTo` resolve optional content references.
Locked terrain is impassable without open/close transitions; its unlock target
must be a closed openable door that locks back. An open lockable variant points
to the same locked state but refuses Lock until closed. Terrain variants preserve
lock state through existing saves and retain damage during action transitions.
