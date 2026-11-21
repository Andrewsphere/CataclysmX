# SDL inventory windows

`i` opens inventory, `u` opens inventory with use-selection enabled, and `/` opens
advanced inventory (all three entry bindings are configurable). The shared
`client/ItemUi` owns navigation, grouping, source/target identity, actions,
quantity validation, bulk transfers, and examination data. SDL handles the
`?` controls toggle and forwards the other modal chords to it, including
unbound chords. Contextual keys never become map movement, attacks, combat-style
changes, or world Examine input.

## Controls

- Up/Down select a row; Enter examines in ordinary inventory or transfers one
  item in advanced inventory.
- `e` examines; `w` wields; `d` drops; `u` uses; `a` manages an item.
- Advanced inventory initially opens Inventory | All nearby and retains panes on
  reopening. Left/Right focus the corresponding window. `I` selects inventory, uppercase `A`
  selects All nearby (lowercase `a` still manages contents), `o` enters a
  portable container, Backspace goes to its parent, and `f` toggles floor/object
  storage.
- `1`–`9` choose nearby storage using keypad geography (`7 8 9` north,
  `4 5 6` current row, `1 2 3` south). Physical keypad keys produce the same
  numeric chords. `5` means the player's tile. The panes swap when the focused
  pane already shows storage on that tile or the other pane shows its default
  storage. Focus stays on the same side.
- `m` enters a quantity and `M` transfers the selected stack. Escape backs out
  or cancels the unissued remainder of a batch. It does not refund paid actions.
- All nearby includes direct accessible floor/object items in the nearby 3×3,
  excluding the other pane's entire root world tile. Carried containers exclude
  nothing. Source-labelled stacks stay separate; aggregate totals have no combined
  capacity cap. Transfers into it choose a concrete Destination (including empty
  storage) before any quantity prompt, without changing the pane's mode.
- Up/Down scroll item examination. Descriptions and statistics wrap. Art keeps
  its fixed cell layout.
  In advanced inventory, examination and prompts open in the focused window,
  with the other list still visible. Escape backs out to the previous step.

Press `?` to show or hide the configurable bindings in the inventory footer.
In advanced inventory, the footer sits at the bottom of the focused pane and
transfer feedback stays near the top.
Entering/leaving a submodal resets the held key, so a held Enter cannot accept a
row and then repeat inside Examine or a quantity prompt. Quantity digits use
key chords rather than SDL text-input events, avoiding duplicate text delivery.
In the advanced lists, holding a number key selects or swaps once per press.
The modal identity `ItemInventory` is distinct from tile `Examine`.
Command queueing and cancellation also terminate a hold when the screen does
not change. The ready/paused snapshot clears held input at the end of paid
recovery, so a cancel held during recovery cannot close the window afterwards.

## Rendering API

`DrawInventoryScreen` consumes `ItemUiFrame`, immutable catalog/tileset assets,
text slots, output dimensions, UI scale, and optional `InventoryMapGeometry`.
The optional footer override supplies the text to measure and draw. A null
override uses the frame's bindings. This lets SDL change the layout with the
visibility toggle and keeps the shared inventory frame usable by both frontends.
`FitInventoryMap` reserves the player's 3×3 in render pixels. It reduces the
displayed zoom only as needed to fit the tiles and both side windows. The
advanced-inventory session uses a minimum view radius of one and retains normal
visibility rules. `DrawFrame` projects the pane anchors through
that transform and can return the layout so animation visibility uses the same
map bounds. `PlaceInventoryPopups` puts two equal-height windows outside the
reserved area, targeting 400 logical pixels in width and about 75% of the
space below the utility bar in height. Very large UI
scales can use the bottom HUD area to fit all three rows. A shared anchor tile
gets one outline and two connectors.

Both advanced panes have capacity lines and the focused pane has an active
border. Each item row uses a single line with right-aligned count, volume, and
mass columns. A narrow pane clips long names and fits wide numeric strings
within their columns. Advanced dialogs use the same window layout. Renderers
write only scroll offsets, and `ItemPaneScroll` keeps the selected row visible
through resizing and list changes.

Icons resolve a stable type-based tileset variant, use the first animation
frame, and fit the sprite's actual crop with its aspect ratio preserved. Atlas
extrusion is outside that crop. Map anchors and map zoom do not affect icons;
missing artwork falls back to `%` (`?` for an unknown type). Icons remain
present when an item has no text art. Art uses exact resolved `ArtColor` RGB
channels, using the normal examination foreground only when `useDefault` is
true. Legacy named colors retain their previous SDL RGB values. Art draws
validated single-cell UTF-8 codepoints on an explicit grid, preserving spaces
even with a user font. Oversized art is clipped horizontally, never wrapped.
Description, art, and provided statistics share vertical scrolling; absent
statistics add no invented values.

The window uses fixed text slots 64–127 for rows and numeric columns, 128–255
for the first 128 stable colored art runs, and 256–319 for a bounded codepoint fallback cache
(320 total `SdlText` slots). `SdlText::DrawCells` draws a full run at once when
the monospaced font's measured width matches its cell extent. Proportional
fonts or differently sized fallback glyphs use explicit cell advances and the
bounded glyph cache, rather than overwriting one text slot for every character.
Runs beyond the first 128 also use this glyph range with stack-only UTF-8
scratch, so 24–32-cell multicolored artwork never overwrites stable run slots
or silently drops excess runs. Cache size is independent of artwork run count.
Run origins advance by `ArtRun::cells`, including spaces. No per-frame cache
entries are created. Render scale and clipping are restored on return.
Excessive UI scale is capped for compact windows to retain usable pane content.

`DrawFrame` accepts an optional inventory frame after its existing HUD argument.
While SDL still owns a closing item modal, `retainClosed` keeps its inventory
visible until the modal closes. A standalone closed frame draws nothing.

## Asynchronous session ownership

Browsing is paused. `PauseForItems` acknowledges `Paused` with the exact request
ID only after any already-paid recovery reaches player readiness. A pending
open captures input and clears held keys until that acknowledgement arrives.
`PauseForAdvancedInventory` uses the same protocol and publishes at least a 3×3
view while the window is open. Resuming restores the configured view radius.

`ItemCommand` requires a paused session and the host player's identity, and
resumes just that admitted command and its recovery. It preserves ordinary
rate-limiting and temporarily enables idle pausing on the owner, preventing an
automatic wait beyond the paid recovery boundary even if gameplay idle pausing
is disabled. At readiness the owner restores the configured pacing and
publishes a paused snapshot. Pause/Resume or additional commands during this
interval are rejected, so cancelling a UI batch cannot freeze paid time or
replace its outstanding command. No extra acknowledgement is generated at this
boundary: the fresh paused snapshot records it.

The frontend tracks the command receipt ID and modal-control ID separately.
Only the matching final `Completed` calls `CommandFinished`; `Admission` is
provisional. Stale/cancelled command receipts call `CommandRejected`. Snapshot
publication precedes completion, and `Refresh` runs before another
`TakeCommand`. The next bulk item can issue only after the preceding receipt
and a ready paused snapshot. Advanced inventory persists; ordinary successful
actions close through the controller, with SDL retaining ownership until paid
recovery finishes before submitting the identified Resume request.

## Integration and validation

`src/sdl/inventory_screen.cpp` is part of the SDL library and
`tests/sdl_inventory_tests.cpp` is registered in the SDL-enabled test target,
alongside input/session tests. The demo SDL smoke
also visits inventory, item Examine, and the two-pane advanced screen, then
wields and unwields the carried demo item through modal keyboard input. Each
action waits for its final receipt and ready paused snapshot before continuing,
exercising localized result feedback and paid recovery with the screen retained.
It then reopens world Examine to check modal reentry.

The real-content render test covers both compact and wide layouts with bundled
sprites and authored descriptions/art. Set `CATACLYSMX_SDL_INVENTORY_PREVIEW` to
an output filename prefix to write Inventory, Examine and Advanced PNGs at both
sizes while running that test.
