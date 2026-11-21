# SDL Settings layout

F2 opens a full-window Settings screen backed by the shared keyboard editor.
The header shows the active scope and the Preferences and Keybindings tabs.
The list has labeled horizontal section bands and a separate column for values
or chords. A selection band and a position marker identify the current row.
At 850 logical pixels and wider, the details panel takes about one third of the
body. It groups keys, scope and reset information, and effects with horizontal
dividers. Smaller windows stack details beneath the list. The footer prioritizes Save, Close, and tab
navigation when font size and window height constrain the display.

`OptionsUi` adapts `SettingsEditor` into the owned `ScreenFrame`.
`DrawScreen` measures visible wrapped rows and keeps the selected row in view.
The list and detail scroll offsets are restored when switching tabs and
adjusted to the current viewport after a resize. Settings and Help share this
structured row presentation.

Save applies the active scope through the session owner, then writes its
configuration files. The UI keeps the current language, font, and bindings until
application is acknowledged. Persistence errors remain visible and retryable.
Temporary worlds show session-only status. [Settings API and controls](settings_api.md)
