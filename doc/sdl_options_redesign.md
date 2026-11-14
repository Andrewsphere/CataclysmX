# Planned SDL options UI redesign

Status: implementation specification for a later visual rebuild. Current options
remain keyboard-only with one Save and apply operation. This document does not
claim that the redesign below has shipped.

## Layout

Use the existing SDL renderer and text subsystem, without adding a UI library.
At logical width >= 850 pixels, use a full-window options screen with a fixed
header, left category column, center settings list, right details/editor pane,
and fixed bottom action bar. At smaller sizes, replace the category column with
a single category selector above the list and stack details below the list.
Keep header and action bar visible; each list/details region scrolls independently.
The minimum supported window remains 480x360 physical pixels at UI scale 1.

Header contains Global/World scope and Preferences/Keybindings section. Show the
selected scope and unsaved state in text, not color alone. Preferences categories
remain Interface, World, Timing, Saving. Keybindings categories are input contexts.
Interface includes the SDL keyboard repeat rate. Show each row's name, effective
value, inheritance/override marker, and read-only/inactive status where applicable.

## Navigation and editing

Tab/Shift+Tab cycles header scope, section, category, list, details/editor, and
Save and apply action. Arrows change the focused selector or list selection.
Enter in a row opens its inline editor in the details pane. Number edits use SDL
text input; booleans toggle; language uses the discovered catalog list; bindings
capture one translated chord. Escape cancels the active editor, then closes the
screen from browse mode. No autosave on close; unapplied drafts are discarded.

Keep selection and scroll when changing focus or resizing. Scope changes retain
both drafts and each scope's selection. Section/category changes remember their
last selection. Clamp selections when their list changes. The selected row must
remain visible. Details include description, constraint, inherited/effective value,
and application timing. Current-world seed is read-only; inactive residency fields
remain visibly marked and editable under the existing settings rules.

Use the existing shared keybinding meanings for navigation/editing. Save and apply
uses the Save binding and the focused action bar control; do not display separate
Apply or Reload controls. Reset, append binding, unbind, and inspect remain
contextual operations in the relevant section. All operations support the central
held-key mechanism; capture consumes one chord. Keep text input separate from
shortcut interpretation while editing.

## State and failure behavior

Keep SettingsStore and session ownership unchanged. OptionsUi owns per-scope drafts
and focus; ScreenFrame owns presentation values. Rendering never reads Server.
The save operation validates the selected draft and key conflicts, stages language,
requests session application, installs acknowledged frontend state, and saves the
selected scope. Disable duplicate submission while this operation is in flight.

Validation and application errors keep drafts and focus on the failing field.
Persistence failure keeps applied settings and shows Applied, unsaved with a retry
operation using the same Save and apply control. Temporary worlds show Applied for
this session. Other-scope drafts survive saving. Reopening clean options reloads
persisted settings; a previous save failure preserves the unsaved applied state.
Show feedback in a reserved status area so it cannot hide the selected setting.

## Acceptance criteria

Verify wide and compact layouts, UI scale, long labels, empty lists, keyboard-only
navigation, focus visibility, resize preservation, and independent scrolling.
Verify invalid numeric input, binding conflicts, language-load failure, session
application failure, persistence failure/retry, temporary-world feedback, and
per-scope draft preservation. Extend existing SDL tests and smoke coverage; add no
tests for documentation tooling.
