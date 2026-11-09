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
| `inventory` | `i` | Carried descendants and accessible ground items, selected by persistent ID |
| `use` | `u` | Same item list with Enter-to-use |
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
Selection and scrolling survive resize and reopening. Carried descendants and
ground items on the player's tile follow the carried list, with location and
parent/installation IDs. Enter drops only directly carried roots. Dropping a
container drops its contents with it.

Accept is a commit boundary: successful submission, including a queued command,
closes Inventory. Escape cannot undo a submitted command. Rejected submissions
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
(default **u**) open item selection, where Enter invokes use instead of drop.

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

The selected item's mass (including descendants), volume, ordinary storage
usage/capacity, charge per capability, activity mode and remaining ticks are
shown above the list; the header shows total carried mass/carry capacity.
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

`Open` accepts Inventory, Use, Players, Help and QuickActions. `PausesLocal` identifies an open modal
or its opening action; the application combines it with existing modal and host
timing rules. `HandleEvent` consumes all events while open and accepts a
`bool(Command)` submission callback for drops, item use and storage. Return true only for an
accepted command. Its optional `void(InputAction)` activation callback handles
Quick actions choices outside this UI. The modal closes before this callback
runs; omitting it keeps external choices open with feedback. Typed actions avoid
synthetic keyboard events, re-decoding, and a second elapsed-time update. The
application owns local/host/guest routing and diagnostics. No server,
networking object, clock, or entity pointer is retained by this UI. The optional
final catalog pointer to `HandleEvent` is retained for call-site compatibility;
generic item selection does not inspect definitions.
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
