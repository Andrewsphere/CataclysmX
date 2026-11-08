# UI navigation controls

This is the baseline for keyboard navigation in menus, dialogs, lists, tabs,
pickers, and inspection panes. New UI should follow these rules; changes to
existing screens should move toward them with explicit behavior changes and
focused tests. The compatibility notes below describe current exceptions.
Gameplay movement, action costs, and world interaction are outside this standard.

The aim is to make navigation predictable across screens while keeping every
essential operation available from the keyboard. Mouse support, if added, should
complement these controls.

## Default controls

Bindings are defaults, not hardcoded character checks. Resolve actions through
the active input context and show the effective bindings in help. The action
names below are the existing names in `client/input.cpp`.

| Control | Existing action | Baseline behavior |
| --- | --- | --- |
| Up / `k` | `north` | Previous row or choice; scroll upward in a focused text pane. |
| Down / `j` | `south` | Next row or choice; scroll downward in a focused text pane. |
| Left / `h` | `west` | Previous tab in a focused horizontal tab row. |
| Right / `l` | `east` | Next tab in a focused horizontal tab row. |
| Tab | `next` | Move focus to the next focus area. |
| Shift+Tab | `previous` | Move focus to the previous focus area; decoded as `BackTab`. |
| Enter | `accept` | Activate the selected control, enter an editor, or accept a choice. |
| Escape | `cancel` | Cancel the innermost edit or picker; otherwise close the current modal. |

Left/right must not change a tab row while another area is focused. Directions
without a meaning in the focused control do nothing. A map or spatial inspector
uses directions to move its inspection cursor while that view has focus.

PageUp/PageDown and Home/End are optional conveniences. When a list offers them,
they move by a visible page and to the first/last row respectively. In a text
pane they scroll by a visible page and to the start/end. Adding these bindings
requires explicit context actions and conflict validation; their decoded key
names alone do not make them supported menu controls.

Space is not a universal accept key. Backspace is not a back-navigation key:
editors use it to delete text, and Settings uses it to reset inheritance.
Screen-specific shortcuts must appear in that screen's help.

## Focus, selection, and scrolling

- Open a screen with its primary list or control focused. Show focus separately
  from the selected tab or row; only one area receives navigation input.
- Tab order follows the visible structure, top to bottom and left to right.
  Tab and Shift+Tab wrap within the current modal. Static labels and hidden
  areas are excluded. A read-only details pane remains focusable when scrollable.
- Arrows move within the focused area. Lists, pickers, and tab rows wrap at
  their ends, matching Settings. Text scrolling and spatial cursors clamp to
  their valid bounds. Empty lists have no selection; navigation and Enter do
  nothing, and the UI shows an empty-state explanation.
- Keep the selected row visible. Changing rows starts the new row's details at
  the top. Scrolling details does not change the selected row.
- Returning to a page during the same screen session restores its selection and
  independent list/details scroll positions. Switching pages preserves drafts.
  After filtering or removing rows, retain the selected entry if present;
  otherwise select the nearest remaining row and clamp the viewport.
- Resizing preserves focus, selection, and drafts, and adjusts scrolling to
  keep the active control visible. Focus order stays consistent when panes stack.

Enter on a selected row edits or activates that row. Enter on a focused tab row
enters its primary content area. A read-only row may expose details but must not
enter an editor. Navigation alone must not apply, save, reset, or activate a row.

## Editing, cancellation, and input ownership

An editor or picker owns input until accepted or cancelled. Typed characters
are text while editing, including `h`, `j`, `k`, and `l`; they must not trigger
menu shortcuts. Binding capture treats the next supported chord as input data.

Escape always remains available, even if the user unbinds `cancel`. It leaves
the innermost mode first and preserves the value from before that edit. A second
Escape can close the containing screen. A close event is consumed by that screen
and must not also cancel or quit the screen underneath it.

For Settings, accepting an edit changes a draft. Apply activates drafts; Save
applies and persists the selected scope. Closing discards unapplied drafts while
applied unsaved changes remain active. Other editors must state their commit
boundary and what cancellation preserves. Invalid input keeps the editor open
with an explanation and a working cancel path.

Physical Enter accepts numeric text, and physical Escape cancels numeric entry,
binding capture, and key inspection. These fixed controls must be shown even
when menu actions are rebound. `Ctrl+c` remains the application's reserved exit
shortcut; it is separate from UI back navigation.

Only the active modal receives UI input, including unknown keys. Events must
not fall through to gameplay or another context. Opening, navigating, and
closing a UI modal send no gameplay commands and advance no simulation time.
An explicit gameplay commit, such as dropping a selected item from Inventory,
uses the normal command pipeline and its existing action cost. Browsing alone
must never commit that action.
Discard wall-clock time spent in the modal when resuming, including its opening
and closing events, so closing it cannot cause a catch-up update.

## Hints and terminal behavior

Show concise hints for focus movement, activation, and cancellation in a stable
footer. Update hints for the active mode and resolved bindings; label unbound
actions explicitly. Put longer explanations in a scrollable help/details area.
Use localized action labels and the shared chord formatting.

Focus and selection must remain recognizable without color, using markers,
inverse video, bold text, or borders. Keep essential navigation hints and the
active control usable at 80x24 and after resizing; wrap translated text rather
than allowing it to hide the controls.

Preserve character case: `j`, `J`, `Ctrl+j`, and `Ctrl+J` are distinct chords.
Use the existing decoder and capability checks. Basic terminals cannot report
every distinction, so mark unavailable bindings and keep baseline navigation
usable without enhanced keyboard support. Shift+Tab is the named `BackTab` key;
Shift and Alt are not general configurable modifiers.

## Current compatibility notes

- Gameplay normally hides Inventory, Players, Controls, and Quick actions. Their
  configurable defaults are `i`, `P`, F1, and `a`. These centered modals use Menu navigation and
  consume Escape without passing it to gameplay. Inventory's accept action
  drops the selected directly carried item. Quick actions uses up/down with
  wrapping and accept to activate the selected command; see [Gameplay UI](gameplay_ui.md).
- Settings (F2) is the reference for focus traversal: Scope, Section, Category,
  List, Details, then Scope again. It opens with List focused. Enter from any
  non-list area currently returns focus to List. At 100 columns or wider,
  categories form a sidebar and accept up/down as well as left/right. Below that
  width they form a horizontal strip accepting left/right only. This orientation
  comes from the last allocated layout, including the first frame and resizing;
  directions always resolve through the active Menu bindings. Both layouts keep
  the same focus order, selection, drafts, and independent scroll state.
- Settings follows the terminal's default foreground/background, with semantic
  colors only for pending/warning, success, and error states. Selected controls
  remain bold and marked while unfocused; inverse video identifies the focused
  selection or the fixed details heading. The footer stays outside scrolling
  content and displays resolved shortcut keys in bold.
- The world-generation debug viewer (F11) uses the `debug` context. Tab and
  Shift+Tab cycle preview layers, PageUp/PageDown scroll details by one line,
  and Home recenters the cursor. F11, `q`, or Escape close it. These are existing
  specialized controls, not defaults to copy into ordinary menus.
- Escape during gameplay currently cancels a direction prompt or exits when
  no prompt is active. This document defines cancellation inside UI screens;
  changing the gameplay exit behavior requires a separate explicit change.
- Page navigation and Home/End list navigation are not currently Menu actions.
  The optional conventions above are guidance for implementing them later.

## Implementation and review

Keep terminal event decoding and rendering in `src/terminal`; reuse the explicit
contexts and actions in `src/client/input.h`. Navigation state belongs to the
screen, independently of world entities. These conventions do not require a
widget framework, a new dependency, or a simulation API.

For behavior changes, add focused tests for focus order in both directions,
wrapping and empty lists, selection/scroll restoration, nested cancellation,
text input isolation, rebound controls and hints, and modal event consumption.
Check that UI navigation leaves simulation ticks and world state unchanged.
Exercise small layouts and resizing when changing focus or viewport behavior.
Existing examples are in `tests/terminal_settings_tests.cpp`,
`tests/terminal_debug_menu_tests.cpp`, and `tests/terminal_input_tests.cpp`.

See [Settings, input, and localization](settings_api.md) for configuration,
binding validation, terminal capabilities, and the Settings editing contract.
