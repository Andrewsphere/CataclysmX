# SDL inspection, interaction, and menus

## Controls and movement

`x` opens a small Examine window beside its highlighted tile. The eight gameplay directions move its cursor; the camera follows within the
filtered snapshot. Alt+Up/Down and the existing layer bindings change its viewed
Y level within world bounds without moving the player. `z`/`Z` zoom also works. Ctrl+Up/Down or PageUp/PageDown scroll the description.
Escape resumes. Terrain and overlays use their independent projection depths.
Remembered appearances are labelled and do not expose live actors or contents.
Stored object contents require immediate access. Inspection neither teaches
memory nor acquires chunks.

`e` (or `o`) highlights visible doors and objects on the player's tile and eight
neighboring tiles, always on the same Y level. Target selection appears beside
the player; action choices appear beside the selected tile. A sole nearby target tile is
selected automatically. A direction immediately opens that tile's actions; `.`
or Home selects the player's tile. Exactly one available action executes
immediately. Otherwise Up/Down selects an action and Enter executes it. Unavailable
actions remain gray with refusal reasons. Escape returns to target selection,
then resumes. Loose items use the shared inventory interfaces described below.

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

## Inventory and equipment

**i** opens inventory; **u** opens use-selection; **/** opens advanced inventory.
Item lists include scaled sprites, names, counts, volumes and masses. **e** (or
Enter in ordinary inventory) examines the item, **w** wields/unwields, **d** drops,
**u** uses and **a** manages contents. Examination includes optional descriptions
and colored UTF-8 art, plus the item's relevant stats.

Advanced inventory initially shows Inventory | All nearby and keeps both panes
open through paid transfers. Uppercase **A** selects All nearby; lowercase **a**
still manages contents. All nearby includes direct accessible floor/object items
in the same-layer 3×3, excluding the opposite pane's entire world tile (including
the root tile of nested containers). Carried containers exclude nothing. Source
labels identify separate stacks. Transfers into All nearby choose a concrete
destination, including empty storage, before a quantity prompt. **Tab** switches
panes; **I** selects inventory; **1–9** selects the current/neighboring tile in
keypad order. **f** toggles floor/object storage; **o** opens a portable container
and **Backspace** returns to its parent. **Enter** transfers one, **m** prompts for
an amount, **M** transfers the selected stack. Escape cancels future work or backs
out. See [shared controls](inventory_ui.md) and [SDL rendering/timing](sdl_inventory.md).

## Expanded combat

**C** opens Combat: Up/Down chooses a learned/basic style, granted ability, or
accessible art manual; Enter accepts. Unavailable rows remain visible with their
shared refusal, cost, cooldown, and prerequisite details. PageUp/PageDown or
Ctrl+Up/Down scroll details. Escape closes without issuing a combat command.
Manual entries start/resume an attempt or pause active study at its paid boundary.

**4** uses Focus, **5** opens Leap targeting, and **6** opens Directional Sweep
targeting. These resolve the stable content IDs `focus`, `leap`, and `sweep` and
choose the first available grant in the same order as the menu. Missing or blocked
abilities explain their unavailability in Combat. Menu and hotkey choices use the
same pending ability command, retaining its exact grant source.

Sweep uses one of four cardinal arrow directions and outlines all three affected
tiles. Leap uses a landing cursor within two tiles of the player on the same Y;
arrows and the gameplay diagonal bindings move the preview. Enter accepts the
preview, at which point the server validates and pays the action. Escape cancels
targeting back to Combat; a second Escape closes. Previewing, inspecting, and
cancelling issue no combat command and spend no action resources. Local menus
pause the world; LAN menus leave the shared simulation live.

**@** opens Player combat information. Up/Down, PageUp/PageDown, or Ctrl+Up/Down
scroll the continuous wrapped shared detail text, including timing, player costs,
defense rates, training/blocked XP rules, hands, arts, and regional encumbrance
(eyes, mouth, and each foot included). Enter/Escape closes.

In inventory and item inspection, the **Items** bindings are **L** hold left,
**R** hold right, **H** two-handed, **G** regrip, **W** wear, **T** remove worn,
and **U** release the selected held item. The SDL footer includes these configured
bindings. These uppercase chords are distinct from gameplay and legacy item keys.

While dead, **C** remains usable: select **Create new character** and press Enter.
This uses the existing session and world; belongings remain with the corpse.
Character identity changes clear old pending UI commands and target previews.
Combat snapshot events enter ordinary message history once per session-local
serial: hits/misses/defenses, invalidations, XP suppression, death, and successful
or failed study attempts. Routine `ActionCommitted` events do not flood the log.

### Frontend implementation

`sdl::CombatUi` in `sdl/screens.h` owns snapshot-only menu selection and a pending
command. `BeginChoice`/`MoveTarget` only prepare presentation; `Accept` returns the
exact command after checking that its menu/grant choice still exists and is
enabled. The app submits it through `ResumeAndCommand`. `PresentCombatInfo` uses
`ScreenFrame::scrollText` for continuous wrapping and text-line scrolling.
`CombatEventCursor` scopes deduplication to session generation, retaining the cursor
across character replacement. Session snapshots copy the entire `WorldView`, so
combat information, events, and character identity survive presentation cropping.

Focused regression coverage is in `tests/sdl_combat_tests.cpp` (add that source to
the SDL-enabled test target when integrating).

## Debug and options

F9 opens [Multiplayer settings](sdl_multiplayer.md), also available from F2's
Interface page. Arrows/Tab and Enter select hosting/joining actions and edit host
tick rate, rate limiting and idle pause; Apply pacing changes applies the draft.
P opens Players using the shared
binding. The default gameplay save binding is **S** (Shift+s), when a local or
host save is open. Guests cannot save or change host pacing.

F11 ports the terminal's world-generation preview: arrows pan, Shift+arrows jump a
region, Home/c recenter, 1–5 or Tab/Shift+Tab select the five layers, +/- zoom,
and PageUp/PageDown scroll details. Debug is available locally and to hosts, not
guests. The surface preview excludes live entities
and saved edits. Unsupported generation versions show an explicit warning.

Within F11, `a` toggles between world generation and the **AI inspector / live
resident map**. Entering AI mode resets the cursor to the player's actual X, Z,
and Y, including when the surface worldgen preview had flattened Y to zero.
Arrows move one tile; Alt+Up/Down or `>`/`<` change Y within world bounds.
Home/c resets to the player; PageUp/PageDown scroll capture details. Escape or
F11 resumes. Each new F11 opening starts with the world-generation preview.

Within F11, **i** opens the catalog item browser. Menu navigation selects a row;
labels contain the localized name and stable content ID. **/** edits a search,
**e** opens the ordinary item examination sheet (including colored art), **Enter**
gives one item, and **m** edits a batch amount from 1 to 1000. Search and amount
editors use SDL committed text; Enter accepts and Escape returns to the list.
Escape from examination also returns to the list; Escape from the list returns
to world-generation debug. **F11** closes the entire debug modal at any layer.
Browser input is exclusive: while editing, printable q/a/i bindings are text,
and F9 cannot open another screen. Outside text editing, the configured Debug
shortcut also closes the entire modal; F11 always closes it.

Giving is local/host-only administrative work. The owner checks carry capacity
for the complete batch before creating anything and reports typed failures in
the browser. Local browsing and grants remain paused and cost no ticks; hosting
continues running. A pending grant blocks another grant even after closing and
reopening the browser, until its identified reply arrives.

The AI inspector displays the captured 17×17 resident map and the actor under
the cursor. It is explicit debug access to current resident state, independent
of player knowledge. It does not load chunks, update memory, or run AI scans.
Decision overlays and descriptions are the selected actor's **last actual
decision snapshots**, which can predate its current position. Overlay colors:
green candidates, red blocked, yellow chosen, blue origin, purple goal, and
orange backtracking. Wide windows show map and details alongside each other;
narrow windows stack the map above independently scrollable details.

F2 opens the keyboard-only preferences and binding editor. Tab/Shift+Tab select
scope, section, category, list, or details. Arrows navigate; Enter edits. The
Save binding performs Save and apply. Reset, append binding, unbind, and key
inspection remain available. Number editing uses SDL text input; binding capture
uses translated key events. Wide layouts place details alongside the list;
compact layouts stack them. World overrides remain memory-only for temporary
SDL worlds. Seed is a future-world default, never a live-world mutation. Guests
can edit presentation preferences and bindings; world fields are host-owned.

These screens pause local simulation. Closing discards paused wall time. While
hosting or joined, menus own local input but the shared world continues running.

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

`InspectAi` returns `AiInspected` with an owned `AiDebugView` captured on the
session owner thread, while locally paused or hosting. The UI uses its `DebugMapView` image and detail
strings directly, with no World access. One capture request may be in flight;
cursor changes coalesce to the latest selection. Session ID, request ID,
cursor, and selection/mode revision gate acceptance, so returning to the same
cursor after a toggle cannot revive an obsolete capture. Changing selection
clears the displayed capture while its replacement is fetched. Shared debug
map presentation keeps live inspection and generated previews on the same
renderer without coupling either capture to SDL.

`ScreenFrame` contains presentation values; `OptionsUi` owns drafts and editing
state. SDL rendering consumes those values without a server dependency or a new
widget library. See [settings](settings_api.md) for keybinding format v2 and
[network presentation](network_presentation.md) for protocol 18 inspection and compatibility.

## Zoom and held input

Map zoom is a multiplier of native tileset dimensions: default 1x, steps of
2^(step/4), bounded to 1/4x–8x. Resizing does not change tile scale. UI scale remains
independent. The main view shows compact Help, Settings, and Multiplayer shortcuts,
session status, and an SDL-only health doll with labeled limb HP. Narrow
windows omit lower-priority bar entries; all bindings remain in Help.

All keyboard actions support holding through the central HeldKey controller,
including screen commands and confirmations. Repeats start after 250 ms at the
Keyboard repeat rate preference (default 10 Hz, range 1–60 Hz). Native repeat
events are ignored; release/focus loss stops repeats, with no catch-up burst.
Opening/closing a modal or switching interaction steps resets held repeat, so
confirmation and cancellation do not leak into the next context. See
[SDL UI behavior and APIs](sdl_ui.md).

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
