# SDL HUD and contextual windows

The SDL frontend uses a compact utility bar, an always-visible health/status HUD,
and tile-anchored Interact and Examine windows. Terminal health presentation is
unchanged. This is presentation state; it does not change combat, persistence,
network protocols, or simulation timing.

## Geometry and text

`MakeGameplayLayout` returns `GameplayLayout`: logical-pixel top-bar and HUD
rectangles, plus a render-pixel map viewport and `MapLayout`. Map cells retain
their native aspect ratio and zoom. An optional cell offset centers Examine's
cursor. `DrawFrame` passes the resulting geometry to `DrawScreen`, so sprites,
tile highlights, and popup anchors use the same transform. `MakeMapLayout`
remains a compatibility wrapper for callers needing only the default map.

`ScaleRect` converts coordinate spaces at the SDL boundary. `MapCellRect` resolves
a column/row through the map transform. Neither operation reads world entities.
The HUD uses the right side at 700 logical pixels and wider; compact windows put
health and status/history alongside each other beneath the map.

`SdlText::Width`, like `Height`, measures the current cached text slot. Call `Draw`
offscreen to prepare a slot before measuring it. The top bar measures translated
labels and resolved chords rather than allocating equal-width boxes.
`MakeTopBarLayout` removes the project title, Debug, then Settings as space runs
out; Help and the truthful Local session label have priority. SDL currently has
no LAN session mode.

## Popup presentation

`ScreenFrame::presentation` explicitly selects `Large` or `Anchored`. Help,
Options, and Debug retain large layouts. The cursor is the popup's tile anchor;
targets are highlighted, and a short connector links the popup to the tile.

`PlacePopup` accepts logical-pixel viewport, target, desired dimensions, and an
optional rectangle to avoid (the player). It tries right, left, below, above,
clamps to the viewport, and prefers keeping the target and player uncovered.
When a centered tile leaves insufficient room, a side window may narrow to at
least 180 pixels. Geometry is a pure calculation, independently testable without
a renderer.

Popups render only when their body contains nonempty text. Examine keeps the
last description visible while a new inspection reply is pending, so moving
the cursor does not hide the panel. On first opening, only the cursor highlight
and camera are shown until the first description arrives.
Popups prefer a 320-pixel width and cap height at 60% of the map viewport. Content
wraps to the actual available width. Title/footer continuation lines yield to a
visible body row on compact windows. Examine scrolls rendered text lines, so a
single long description remains readable. Action lists keep the selected row
visible; refusal text gets a separate region, or joins the selected row in tight
layouts. PageUp/PageDown and Ctrl+Up/Down also scroll longer refusal details.
`ScreenFrame::rowHeights` reuses measurement storage across redraws.

## Modal ownership

`ModalController` owns the active/requested screen, transition state, cursor,
action selection, and Interact's parent/child scroll restoration. It borrows the
frontend's `HeldKey`; both live on the SDL main thread. Explicit methods replace
independent screen/pending assignments:

- Submit Pause successfully, then `BeginOpen`; its acknowledgement calls `Paused`.
- Submit Resume successfully, then `BeginClose`; acknowledgement calls `Closed`.
- A successful ResumeAndCommand enqueue closes the UI through `Closed`.
- `EnterActions` and `BackToTargets` preserve the parent and same-tile action
  selection/scroll. Changing target resets action selection.

`CapturesInput` includes pending transitions. Only the active context receives
input; unknown modal keys cannot fall through to gameplay. Every transition
clears held repeat. This avoids a held confirmation or Escape acting again in
the next context. The session owner still controls pause timing and discards
paused wall time. A single available interaction still executes immediately.

## Health presentation

`DrawHealth` consumes only the existing owned `CombatView`. It draws a segmented,
front-facing silhouette, with the character's left on the viewer's right. The
upper limbs represent the existing hand zones; labels do not invent arm health.
Absent zones are omitted, and an empty view draws nothing.

`HealthColor` uses current/effective-maximum HP: green above 75%, yellow above
50%, orange above 25%, red above zero, dark at zero. Broken parts also have a
cross and explicit text. `HealthPartText` retains hundredths precision, omitting
only a zero fractional suffix. Values and labels convey health without color.
The doll and labels scale together when necessary to fit all parts, including
broken states. The helper restores renderer scale after drawing.

`CombatSummaryText` is SDL-specific: it retains style, stamina, action state, and
accuracy penalties without repeating limb values. Combat feedback remains in
the status area; message history has a separate clip and reserved space.

## Validation

`[sdl][ui]` covers packing, popup placement, map transforms, modal restoration and
repeat reset, HP thresholds/formatting, and compact/wide rendering. The preview
test optionally writes four window sizes using `CATACLYSMX_SDL_UI_PREVIEW` as a
filename prefix. The existing `sdl_frontend_smoke` exercises actual Vulkan
rendering, unknown-key isolation, nested cancellation, and paused tick stability.
