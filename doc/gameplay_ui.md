# Gameplay screen and modals

Gameplay uses Settings' light borders, one-column horizontal padding, bold
headings and terminal-default interface colors. Terrain and entity glyphs keep
their content colors. Inventory, Players, Controls and Quick actions are hidden until requested;
ordinary gameplay has no shortcut footer. The top bar exposes Help (F1),
Settings (F2), LAN (F9), and Debug (F11) as keyboard shortcut labels. Their labels
follow configured bindings. See [message history and top bar](message_history.md).

## Visual overview

Previously the screen was one vertical stream: title and every shortcut,
session, message, optional LAN controls, position/pacing, map, inventory and
players. Wrapping shortcuts and long inventories competed with the map and
could push information below the terminal.

At **100 columns or more**, status occupies a 32-column sidebar. Labels sit on
the left and bold values align on the right. Action readiness, location and
pacing are separated into short groups, keeping long numbers intact. Busy,
queued and pending states also have explicit text and a yellow accent. The map
gets the remaining body width. Schematics omit extra blank map space:

```text
┌──────────────────────────────────────────────────────────────┐
│ CataclysmX  ·  Local / Hosting / Guest                         │
├─────────────────────────────────┬────────────────────────────┤
│ World                           │ Status                     │
│ ─────────────────────────────── │ ────────────────────────── │
│                                 │ Action               Ready │
│               @                 │ Cooldown           0 ticks │
│                                 │ Speed                  100 │
│                                 │ Location                   │
│                                 │ X 0  Z 0                   │
│                                 │ Tick                     0 │
│                                 │ Pacing                     │
│                                 │ Tick rate           100 /s │
│                                 │ Rate limit             off │
│                                 │ Idle pause              on │
├─────────────────────────────────┴────────────────────────────┤
│ Latest outcome or interaction direction prompt                │
└──────────────────────────────────────────────────────────────┘
```

Below **100 columns**, status becomes a compact strip above the map, including
the first rendered frame. An **80×24** terminal keeps the message visible:

```text
┌───────────────────────────────────────────────────────┐
│ CataclysmX  ·  Local                                   │
├───────────────────────────────────────────────────────┤
│ X 0  Z 0 | Tick 0 | Ready                              │
│ rate / limit / idle pause / speed / delay              │
├───────────────────────────────────────────────────────┤
│ World                                                 │
│ ───────────────────────────────────────────────────── │
│                         @                             │
│ Map cropped around player          (only when needed)  │
├───────────────────────────────────────────────────────┤
│ Latest outcome or interaction direction prompt         │
└───────────────────────────────────────────────────────┘
```

The map centers naturally when it fits. Otherwise its viewport follows the
player, clamped to the supplied view edges, with a crop indicator. Cropping
changes presentation only: it never requests a smaller simulation view or
rescales a glyph. Message history retains the latest 10 grouped entries by default,
configurable under Interface settings. The newest rows remain visible when
the terminal is small; history uses at most half the gameplay height.

## On-demand windows

### Power workshop (terminal and SDL, including LAN)

Open **Crafting** from its gameplay binding or Quick actions, then press **p**
for the **Power workshop**. The workshop lists carried reusable wiring kits and
placeable batteries/generators, including carried container contents. Installed
components are excluded. Commands always refer to the selected persistent item
IDs; the server rechecks ownership, materials, reach, layer, occupancy and costs
when the action executes.

- **Tab** cycles carried wiring kits; **g** cycles carried placeable equipment.
- **p** switches between wiring and placing equipment.
- **w / W** increases/decreases width; **d / D** increases/decreases depth,
  wrapping through the configured range. The initial size is the server's
  maximum (default 16×16, configurable from 1 to 64 per axis); smaller rectangular
  regions down to 1×1 are supported.
- **Arrows** select the inclusive rectangle's minimum corner, or the placement
  destination. **Home** returns to the player's tile. Wiring remains on the
  actor's layer; placement is on the current or an adjacent tile.
- **Enter** submits the selected installation. Wiring consumes accessible ordinary
  material leaf items: `ceil(newly covered tiles / kit.wiringTilesPerMaterial)`.
  The selected kit remains reusable and has no consumable energy store. Its
  material type and tiles per item are shown in the workshop. A battery
  installation costs four tile equivalents, so the default 16-tiles-per-material
  kit consumes **one material item**. **Escape** returns to crafting;
  **PageUp/PageDown** scroll the workshop, including its coverage map.

Carry a **multimeter** to see the current grid's present mains availability,
battery energy/capacity, available generator output, machine counts and a
viewport-bounded coverage map. Each map symbol represents a grid ID modulo 36;
the exact IDs are listed below the map. The map uses the viewed layer, while
the current inspection describes the player's own tile. Losing carried access
to the meter removes both inspection and coverage. Normal player snapshots
contain no future mains cutoff information.

Host-only **Debug (F11) → p** opens power/chunk diagnostics. Press **p** to
refresh the read-only snapshot, and **a** to return to world generation. It
shows coverage and full grid IDs, power workload counters, and a 17×17 chunk
column map centered on the player. `P` means player demand, `M` machine demand,
`B` both, and `O` other demand; uppercase is active, lowercase resident, and `.`
unloaded. Counts distinguish loaded/active/player/machine/machine-only/shared
columns and loaded/archived chunks. Below the map, every machine-only loaded
column is listed by X/Z coordinates and active/resident state, with machine-owner,
machine-dependency and other-request counts, including columns outside the map.
SDL captures diagnostics on the session
owner thread before presenting them; the UI never queries the live world.

Headless verification commands:

```text
power
power-debug
wire-grid <kit-id> <min-x> <min-z> <max-x> <max-z> <y>
place-power <equipment-id> <wiring-kit-id-or-0> <x> <z> <y>
stations
place-station <equipment-id> <x> <z> <y>
pack-station <object-id>
```

`power` uses normal meter eligibility; `power-debug` is administrative inspection.
See [power commands and presentation](power_presentation.md) for the shared DTO,
wire validation and session-thread API design.

| Action name | Default key | Window |
| --- | --- | --- |
| `inventory` | `i` | Carried descendants and accessible ground items, selected by persistent ID |
| `use` | `u` | Same item list with Enter-to-use |
| `advanced_inventory` | `/` | Inventory / All nearby panes and quantity transfers |
| `players` | `P` | Player connection, readiness, queue and grace status |
| `help` | `F1` | Shortcut reference, interaction bindings and fixed LAN keys |
| `quick_actions` | `a` | Select common gameplay commands and windows with arrows |
| `combat_menu` | `C` | Stances, granted abilities, accessible art manuals, and new character after death |
| `combat_info` | `@` | Shared character, equipment, combat rules and learning details |

These actions exist in both Gameplay and Interaction and are editable in
Settings. Lowercase `p` still toggles idle pause. The initial welcome message
shows effective opening bindings. Existing key-conflict validation applies;
new bindings never silently steal a user-configured key.

Windows overlay a subdued gameplay screen. Their height follows the content,
so empty or short lists do not produce a nearly empty full-height window.
Preferred widths are 90 columns for Inventory, 116 for advanced inventory, 80 for Players, 84 for Controls,
and 64 for Quick actions; all clamp to the terminal with at least two cells of
exterior margin. A narrow clear gutter separates the modal from background
borders. Titles, instructions and two-row footers stay fixed while content
scrolls. Inventory and Quick actions show selection counts; Players shows its
roster count. Up/down markers indicate content outside the viewport:

```text
        ┌─────────────────────────────────────────────────┐
        │ Inventory                                1 / 2  │
        │ Carried mass / capacity                         │
        ├─────────────────────────────────────────────────┤
        │ > 7:backpack (1 inside)                          │
        │   12:rock                                        │
        │                                                 │
        ├─────────────────────────────────────────────────┤
        │ Enter examine | d drop | u use | a contents      │
        │ Escape close                                    │
        └─────────────────────────────────────────────────┘
```

Inventory uses the shared `client/item_ui.h` controller. Menu up/down selects with
wrapping; Menu accept or Items Examine opens examination. Items Drop drops only
directly carried roots, including their contents. Trees, stack grouping, physical
columns, descriptions, colored art, wielding and advanced inventory are described
in [shared item UI](inventory_ui.md). Selection and scroll survive resize and
submodal cancellation.

Each pane has a fixed **Name / Count / Volume / Mass** heading above its scrolling
rows. Numeric columns are right-aligned using widths shared by every row and the
heading; the name column takes the remaining space. Names retain persistent IDs,
contents/parent annotations and the shared controller's ground marker. The
terminal does not add a second ground suffix.

The final authoritative successful result closes ordinary Inventory; admission
and queued receipts leave it awaiting resolution. Escape cannot undo a submitted command. Rejected submissions
keep the modal open with feedback. If the selected ID disappears between drawing
and activation, no command is sent; review the reconciled selection and activate
again. A vanished selection stays invalid until explicit navigation, even if
another snapshot is rendered before activation. Updates never substitute a
neighbor at activation.

## Item use and storage

In Inventory, `items/use` (default **u**) invokes the selected item's functional
actions selected by `ItemActionInfo::category == Operate`. Contents-management
actions do not turn a single-function device into a multi-use menu. One
functional action submits immediately, or enters its argument prompt; multiple
functional actions open a selectable menu. `gameplay/use` and `interaction/use`
(default **u**) open item selection, where Enter invokes use instead of examination.

`items/apply` (default **a**, labelled Manage) opens actions categorized as
`ManageContents`, followed by **Store in container** and **Extract to direct inventory**.
An action's `argument` selects no prompt (`None`), an accessible-item selector
(`Item`), or a cardinal-direction prompt (`Direction`). Item selectors list all
authoritative carried/ground entries, including installed items and the source;
the server validates whether the selected target is suitable for that action.
The frontend does not infer target compatibility from an action name, category,
or concrete behavior configuration. Store prompts for an ordinary container and sends `store_item` with its
ID; Extract sends the same command with destination zero. The server validates
capacity, ancestry, access and installation rules.

Direction prompts use Menu movement bindings. Ground-list items can also be
selected and used; deployment requirements are validated by the server.
Escape returns from any item submodal to inventory; a further Escape closes it.
Menu up/down and accept navigate action and target lists. Target IDs remain
fixed for that prompt: disappearance does not select another target. Navigate
explicitly to another listed target, or cancel and reopen for a refreshed list.

The selected item's storage usage/capacity, charge per capability, activity mode
and remaining ticks are shown in examination; rows show mass/volume/count and
the header shows total carried mass/carry capacity.
Action labels, optional activity status, and refusal messages use `ItemText`
localization keys with their authored fallback text. Availability refusals are
localized and shown before submission. Actual server
refusals appear in the latest game message; failed submissions keep selection
open. No local item state, charge, ownership or activity is predicted.

Items bindings take precedence over Menu bindings while browsing inventory;
Menu navigation and cancellation still apply. The separate `items` context
keeps Settings' existing **u** unbind behavior. All bindings are editable in
Settings, and the Controls window includes the item bindings.

Players uses a bold name/ID heading and a separate connection/readiness line for
each player. Controls groups label/key rows by windows, actions, movement,
pacing, system, and direction-prompt bindings. Both are read-only: Menu up/down
scrolls with clamping and retains independent offsets.

Quick actions uses Menu up/down with wrapping and Menu accept to activate.
Its selection and scroll position survive resizing and reopening. Inventory,
Players and Controls open directly; other choices dispatch the existing typed
input action to the application exactly once. Available choices include pickup,
interaction, wait, Settings, save, pacing controls and debug. Movement directions
remain navigation inside the menu. Opening or navigating the menu commits no
gameplay action; accepting a gameplay command uses its ordinary action cost and
local/host/guest routing. A selected interaction action enters the normal
direction prompt. Host-only actions retain their existing session checks.

All modals consume unknown keys and close with Menu
cancel or physical Escape. Closing never also cancels a direction prompt or
quits gameplay. Ctrl+c remains the application's reserved exit shortcut.

Opening, browsing and cancelling send no simulation commands. Uncommitted browsing
discards local elapsed time, including opening and closing events, so there is no
catch-up update. Item UI `NeedsTime` allows ordinary timing during committed work
and through the final paid action delay. Open LAN hosts continue serving guests and guest snapshots
keep refreshing. Explicit drops use the ordinary authoritative command pipeline
and action costs. A network endpoint transition closes the modal to avoid
carrying selected IDs into a different world.

The F9 LAN interface occupies the World panel, retaining its existing commands
and editors. Its controls appear within that panel and in Controls.

## Debug item browser

In the local or hosting game, F11 opens the debug view. From either world
generation or AI inspection, the Debug inventory binding (default **i**) opens
the item catalog. Up/down selects a catalog entry; **/** edits a search, **e**
examines the selected item, Enter gives one, and **m** prompts for a count.
Enter confirms a search or count. Escape returns from an editor or examination
to the list, then returns to the previous debug view; F11 closes the entire menu.
The list and examination scroll to keep the selection or inspection position
visible. Feedback reports invalid counts and grant results.

`CreateWorldgenDebugMenu` accepts an optional final
`std::function<DebugGiveResult(DebugItemGrant)>` callback. The frontend drains
the shared browser's request on the UI thread and supplies the result to the
browser; omitting the callback leaves the catalog read-only with feedback.
The terminal application invokes the authoritative grant for the host player on
that same thread. Guests cannot open this local debug menu. The world and catalog
must outlive the component, as must any localization and keymap pointers.

## Frontend API and ownership

### Combat windows and targeting

Combat uses `CollectCombatMenu` entries, including their exact typed commands,
grant sources, target kinds, costs and refusal details. Menu up/down selects and
Menu accept activates. Disabled entries remain readable. The information window
uses `CombatDetailsText`; Menu up/down scrolls it. Combat bindings also appear in
Controls. After death, **C** offers **Create new character** even while the old
snapshot reports busy or queued work. Acceptance submits `new_character` through
the ordinary local/host/guest callback.

Gameplay **4**, **5**, **6** resolve the stable authored ability names `focus`,
`leap`, and `sweep`. They select the first available matching snapshot grant in
snapshot order, retaining its exact source. Missing grants show feedback in the
combat window. A disabled grant is never replaced with an unrelated ability.
Hotkeys and menu acceptance use the same pending-command path.

Sweep enters cardinal targeting: choose one direction, inspect the three-tile
inverted map preview, then use Menu accept. Leap starts a tile cursor at the
character; Menu directions move it within Chebyshev distance two on the same Y
level (gameplay diagonal bindings also work). Its selected tile is inverted on
the map. Accept requires a non-origin destination. Escape or Menu cancel closes
either prompt without submission. Previewing changes no world state; the server
checks occupancy, perception, obstruction and final ability availability. The
exact grant is checked again against the latest snapshot before acceptance.
Prompts expire on avatar/player changes, movement, changed input opportunity,
death, or busy/queued state, preventing a stale target from becoming a new action.

Inventory **L/R/H/G/W/T/U** map to shared `ItemUiAction` values for hold left,
hold right, two-handed hold, regrip, wear, remove worn, and release held item.
The inventory footer displays their effective Items-context bindings. Ordinary
item navigation and submodal key handling continue through the shared controller.

`GameplayUi::CombatAction` handles ability shortcuts with a snapshot and the
same admission callback used by `HandleEvent`. `RecordCombatEvents` appends
`CombatEventText` to application history once per increasing session-local
`CombatEvent::serial`. It advances over suppressed `ActionCommitted` events to
avoid commitment spam and duplicate messages from repeated snapshots.
`ResetItems` resets the entire transient UI and serial cursor at an actual
connection change. Replacing `WorldView::characterId` within a stable player
session clears selections and pending prompts while advancing the cursor to the
latest snapshot serial, so old-avatar events are not replayed. The application
also clears pending receipt labels and interaction/grab prompts on avatar change.

`terminal/gameplay_screen.h` provides `GameplayUi`, `GameplayPresentation`, and
`GameplayWelcome`. `GameplayUi::Render` accepts a `WorldView` snapshot, catalog,
localization, effective keys, and presentation values. Its DOM owns rendered
text and cells, but scroll panes reference UI state: draw the returned element
before the next render or destruction of that `GameplayUi`.

`Open` accepts Inventory, AdvancedInventory, Use, Crafting, CombatMenu, CombatInfo,
Players, Help and QuickActions. `PausesLocal` identifies an open modal
or its opening action; the application combines it with existing modal and host
timing rules. `HandleEvent` consumes all events while open and accepts a
`bool(Command)` submission callback for drops, item use and storage. Return true only for an
accepted command. Its optional `void(InputAction)` activation callback handles
Quick actions choices outside this UI. The modal closes before this callback
runs; omitting it keeps external choices open with feedback. Typed actions avoid
synthetic keyboard events, re-decoding, and a second elapsed-time update. The
application owns local/host/guest routing and diagnostics. No server,
networking object, clock, or entity pointer is retained by this UI. The optional
final catalog pointer to `HandleEvent` can supply the immutable item catalog;
otherwise the catalog from the last render is used. `RefreshItems` refreshes the
shared controller and drains at most one ready request on timer events as well
as input. `ItemCommandFinished` forwards exact final results from local sends,
host updates and guest receipts. `ResetItems` clears state on endpoint changes.
`Render` refreshes the controller while the snapshot still contains its cells,
before transferring map cell ownership into the viewport. Network test harnesses
must mirror the timer path: pumping transport and forwarding matching receipts
alone cannot dispatch a command captured while the viewed player was busy.
Readiness in a received snapshot does not guarantee admission: its input
opportunity can expire while another player resolves a round. A final
`stale_input` leaves ordinary inventory open with feedback and retains the exact
selection. Refreshes do not retry refused actions; the player deliberately
activates the action again. A loopback success test that switches from the host
to the guest must first receive the guest's current ready opportunity after the
host's round, rather than act on its previous ready snapshot.
Selections, action choices and pending commands are owned values. On execution,
the exact source/target IDs and capability/action pair are checked against the
current snapshot before dispatch. This keeps snapshot refreshes independent of
the modal's navigation state.
Inventory selection belongs to the viewed player ID. Rendering another player's
view resets the selection and pending item prompt; a player change between the
last frame and an input closes the prompt without sending a command. This allows
the same UI to serve local, host and guest views without carrying one player's
item selection into another player's inventory.

This split lets synthetic terminal tests exercise real allocation, resizing,
selection and callbacks without coupling presentation to simulation. The small
allocation-aware DOM nodes follow Settings' existing approach rather than
introducing a widget framework. Text rendering retains the historical root-only
`Inventory:` line with contained counts, and appends item detail inspection only
when stateful items are present. `client/text.h` also exposes `ItemDetails` for
explicit inspection and `ParseItemCommand` for the shared headless grammar;
parsing validates command shape and assigns the output only on success.

`client/localization.h` exposes `Localization::RenderItemText(const ItemText&)`,
which resolves a key from the active/English catalogs when present, otherwise
returns the fallback (including an empty fallback). `ItemActionText(const
ItemActionInfo&)` renders `label`; `ItemRefusal(ItemUseOutcome, const
ItemUseReason&)` renders `reason.message`, with generic outcome text when no
message is available. `ResultText(const CommandResult&)` passes both
`itemOutcome` and `itemReason` for item-use failures. Reason codes remain machine
identifiers and do not select localized strings. Legacy builtin label/reason
keys remain available because behavior factories author those keys.

These metadata interfaces let custom behavior-local action IDs work without
frontend dispatch changes. Pending commands retain the exact string ID and
capability; refreshing a menu cannot substitute a different local action.
`ParseItemCommand` accepts `use <item-id> <capability-id> <action>
[item-id|n/s/e/w]`: it validates action-ID syntax and generic argument shape,
while the server validates the behavior's declared schema. `ItemDetails` prints
the local action ID, fallback label, generic availability and typed reason
code/message, plus optional activity status fallback text.
