# Gameplay screen and modals

Gameplay uses Settings' light borders, one-column horizontal padding, bold
headings and terminal-default interface colors. Terrain and entity glyphs keep
their content colors. Inventory, Players, Controls and Quick actions are hidden until requested;
ordinary gameplay has no shortcut footer.

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
rescales a glyph. The latest message can occupy up to three lines.

## On-demand windows

| Action name | Default key | Window |
| --- | --- | --- |
| `inventory` | `i` | Directly carried items, selected by persistent ID |
| `players` | `P` | Player connection, readiness, queue and grace status |
| `help` | `F1` | Shortcut reference, interaction bindings and fixed LAN keys |
| `quick_actions` | `a` | Select common gameplay commands and windows with arrows |

These actions exist in both Gameplay and Interaction and are editable in
Settings. Lowercase `p` still toggles idle pause. The initial welcome message
shows effective opening bindings. Existing key-conflict validation applies;
new bindings never silently steal a user-configured key.

Windows overlay a subdued gameplay screen. Their height follows the content,
so empty or short lists do not produce a nearly empty full-height window.
Preferred widths are 72 columns for Inventory, 80 for Players, 84 for Controls,
and 64 for Quick actions; all clamp to the terminal with at least two cells of
exterior margin. A narrow clear gutter separates the modal from background
borders. Titles, instructions and two-row footers stay fixed while content
scrolls. Inventory and Quick actions show selection counts; Players shows its
roster count. Up/down markers indicate content outside the viewport:

```text
        ┌─────────────────────────────────────────────────┐
        │ Inventory                                1 / 2  │
        │ Select a carried item to drop it.                │
        ├─────────────────────────────────────────────────┤
        │ > 7:backpack (1 inside)                          │
        │   12:rock                                        │
        │                                                 │
        ├─────────────────────────────────────────────────┤
        │ Up/k/Down/j select | Enter drop                  │
        │ Escape close                                    │
        └─────────────────────────────────────────────────┘
```

Inventory uses Menu up/down to select with wrapping and Menu accept to drop.
Selection and scrolling survive resize and reopening. Empty inventory has no
selection. Only directly carried items are actionable; contained items appear
as a count. Dropping a container drops its contents with it.

Accept is a commit boundary: successful submission, including a queued command,
closes Inventory. Escape cannot undo a submitted command. Rejected submissions
keep the modal open with feedback. If the selected ID disappears between drawing
and activation, no command is sent; review the reconciled selection and activate
again. Updates never substitute a neighbor at activation.

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

Opening, browsing and cancelling send no simulation commands. Local time is
discarded while a modal is open, including opening and closing events, so there
is no catch-up update. Open LAN hosts continue serving guests and guest snapshots
keep refreshing. Explicit drops use the ordinary authoritative command pipeline
and action costs. A network endpoint transition closes the modal to avoid
carrying selected IDs into a different world.

The F9 LAN interface occupies the World panel, retaining its existing commands
and editors. Its controls appear within that panel and in Controls.

## Frontend API and ownership

`terminal/gameplay_screen.h` provides `GameplayUi`, `GameplayPresentation`, and
`GameplayWelcome`. `GameplayUi::Render` accepts a `WorldView` snapshot, catalog,
localization, effective keys, and presentation values. Its DOM owns rendered
text and cells, but scroll panes reference UI state: draw the returned element
before the next render or destruction of that `GameplayUi`.

`Open` accepts the four opening actions. `PausesLocal` identifies an open modal
or its opening action; the application combines it with existing modal and host
timing rules. `HandleEvent` consumes all events while open and accepts a
`bool(Command)` submission callback for inventory drops. Return true only for an
accepted command. Its optional `void(InputAction)` activation callback handles
Quick actions choices outside this UI. The modal closes before this callback
runs; omitting it keeps external choices open with feedback. Typed actions avoid
synthetic keyboard events, re-decoding, and a second elapsed-time update. The
application owns local/host/guest routing and diagnostics. No server,
networking object, clock, or entity pointer is retained by this UI.

This split lets synthetic terminal tests exercise real allocation, resizing,
selection and callbacks without coupling presentation to simulation. The small
allocation-aware DOM nodes follow Settings' existing approach rather than
introducing a widget framework. The `--render` text snapshot remains unchanged.
