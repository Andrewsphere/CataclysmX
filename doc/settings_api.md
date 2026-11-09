# Settings, input, and localization

Application configuration resolves **built-in defaults → global → world**.
There is no save-specific configuration layer. Every save opened with the same
world directory shares that world's preferences, bindings, and language.

## Files and inheritance

The terminal uses `%APPDATA%/CataclysmX` on Windows, `$XDG_DATA_HOME/CataclysmX`
on Linux, or `$HOME/.local/share/CataclysmX` when XDG is unset. `--user-dir <path>`
selects another root. Its `config/settings.json5` and `config/keybinds.json5`
are separate adjacent files. The bundled English catalog is `data/loc/en.json5`;
`--loc-dir` selects another catalog directory.

`--world-dir <path>` selects the shared world directory. Otherwise, a save at
`run.sqlite` uses `run.sqlite.world/`. World configuration consists of
`settings.json5` and `keybinds.json5` in that directory, beside the saves rather
than inside SQLite. Pass the same `--world-dir` when opening related saves.
Existing databases require no migration. Seed and generation version remain
authoritative saved world data.

World files store **only explicit edits**, never a snapshot or a computed diff
against an old global configuration. An absent field inherits today's global
value; a present field overrides it. Reset removes the field. Explicit zero,
false, and empty binding lists are meaningful. Matching the global value does
not automatically remove an override.

```json5
// Global config/settings.json5
{format_version: 1, settings: {view_radius: 8, autosave_interval: 1000}}
// World settings.json5
{format_version: 1, settings: {view_radius: 10}}
```

Changing global values to `view_radius: 6, autosave_interval: 500` leaves this
world at radius 10 and changes its autosave interval to 500. Saving another
world edit never materializes the inherited interval.

New saved worlds create sparse local files. Unsaved worlds retain overrides in
memory; their World settings cannot be persisted without a selected directory.
Global files are written when explicitly saving Global settings.

## APIs and application

`SettingsStore` owns typed global/world overrides and the resolved `Settings`
and `Keymap`. `Load` stages all files and preserves the store on failure.
`SetOverrides` validates the selected layer and its effective configuration
before replacing it. `Save(scope)` writes only that scope's explicit entries.
`Dirty(scope)` tracks applied changes awaiting persistence. An empty user root
disables global-file loading, useful for explicit world-only headless runs.

The small `SettingsOverrides` value type uses optional fields; this makes
inheritance explicit without a property registry, reflection, or variant map.
`ResetOverride` removes one field. Keybindings have the same per-action model.
`ResolveSettings` and `ResolveKeymap` are pure and do not access files.

| Field | Default | Rules |
| --- | --- | --- |
| `language` | `en` | Catalog identifier; missing translation falls back to English |
| `view_radius` | 8 | 0–12 |
| `autosave_interval` | 1000 | Simulation turns (100 ticks each); 0 disables |
| `active_radius` | 1 | 0–8, no larger than resident radius |
| `resident_radius` | 5 | 0–8 |
| `seed` | 42 | Creation default; never changes an existing world |
| `ticks_per_second` | 100 | 1–1,000,000 |
| `rate_limited` | false | Existing tick pacing behavior |
| `pause_when_idle` | true | Existing tick pacing behavior |

`ApplySettings(Server&, const Settings&, Diagnostics*)` validates first, then
uses the server's fallible bubble update before applying pacing/autosave.
It sends no command or elapsed time and never changes the world seed. Apply
failure preserves previous server preferences. On load, the terminal restores
SQLite state and applies current resolved preferences before accepting input.
The next checkpoint records those active runtime values. `Server::LoadSave` itself
preserves saved radii, including smaller ones; terminal application of effective
settings can replace them with inherited defaults or explicit overrides. Explicit
small bubbles remain supported and clip sight at residency boundaries. Default
sight is 64, independently of the bounded viewport. See [sight defaults](sight_defaults.md).

Configuration is re-resolved on startup, world changes, and explicit reload or
Apply; files are not watched. Headless runs retain their previous behavior
unless `--user-dir` or `--world-dir` is explicitly provided. Scripted loads then
stage the destination world's configuration before replacing the running server.
Headless output and command tokens remain stable English machine interfaces.

JSON5/UTF-8 parsing stays in the content boundary; public interfaces expose
EASTL values and `SettingsResult<T>` (`eastl::expected<T, SettingsError>`).
Loading/validation accepts optional structured diagnostics. Unknown fields,
duplicates, wrong types, invalid values, and unsupported versions are errors.
Files are limited to 4 MiB. Writes use exclusively created temporary siblings,
flush, and atomic replacement. Each file is independent: a settings/keybinding
pair is not a filesystem transaction. A partially failed Save remains dirty.
Malformed existing files are never overwritten by the configuration writer.

## Keybindings

```json5
{
  format_version: 1,
  bindings: [
    {context: 'gameplay', action: 'wait', keys: [{key: 't', ctrl: true}]},
    {context: 'gameplay', action: 'pick_up', keys: [{key: 'T', ctrl: true}]},
    {context: 'debug', action: 'west', keys: []}, // Explicitly unbound.
  ],
}
```

`KeyChord` contains a character or named key and one `ctrl` flag. Character
case is significant, including with Ctrl: `t`, `T`, `Ctrl+t`, and `Ctrl+T`
are distinct bindings. Shift and Alt are not configurable modifiers. Text entry
uses produced UTF-8 characters without shortcut normalization.
Ctrl currently supports ASCII letters and the named keys; unsupported character
combinations are rejected during validation.

Contexts are `gameplay`, `interaction`, `menu`, `debug`, and `items`. A local entry
replaces the entire inherited list for that context/action; removing it restores
inheritance. Conflicting chords within one context are rejected. Escape always
cancels; `Ctrl+c` always exits. `Ctrl+C` remains a separate enhanced binding.
Action names and defaults are explicit in `client/input.cpp`.

`use` defaults to **u** in Gameplay and Interaction (open item selection), and
in Items (use the selected inventory item). Items `apply` defaults to **a** and
opens battery/storage management. Items bindings take precedence over Menu
bindings while browsing inventory, preserving Menu `unbind` **u** in Settings.
Submodal navigation, accept, direction selection and cancel use Menu bindings.
The Settings binding editor includes the Items category.

`Localization::ItemName` takes an `ItemDefinition` or a catalog/type pair. It
looks up optional `item.<stable-id>` and falls back to the definition's name;
it has no enum/name table. `ItemActionText` and `ItemRefusal` localize typed
capability actions and refusals with English helper fallbacks. `ResultText`
uses the specific item refusal in `CommandResult::itemOutcome`, otherwise the
ordinary outcome text. Headless rendering uses content definition names.

`DecodeTerminalInput` is a pure frontend adapter. Traditional terminals send
uppercase text without reporting whether Shift or Caps Lock caused it. They also
collapse `Ctrl+t` and `Ctrl+T` into one control byte. The adapter never invents
uppercase Ctrl input from that byte. Basic mode marks unavailable bindings,
including control-byte aliases such as `Ctrl+i`/Tab, rather than aliasing them.

`TerminalKeyboard` queries for the [Kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/)
after entering the alternate screen. Supporting terminals enable disambiguation
and alternate-key reporting (flags 1 and 4); mode state is popped before exit.
CSI-u Ctrl-letter events retain case, using reported Shift/Caps Lock only to
derive the character. Alt and other modifier combinations are ignored. FTXUI
continues to assemble input sequences and handle ordinary text entry.
Unsupported terminals remain usable in basic mode. No terminal capability can
recover modifier information that the host does not transmit.

## Localization and editor

Catalogs use `{format_version: 1, messages: {...}}`, with stable keys mapping to
UTF-8 strings or `{one: '...', other: '...'}`. Complete templates use named
placeholders, allowing argument reordering. `{{` and `}}` escape literal braces.
Placeholders must match across plural forms and translations. English is
required; missing translated keys fall back to English, then `[message.key]`.
An invalid existing catalog is an error and leaves the old catalog active.
The initial plural rule is English's one/other rule; additional language rules
can be added explicitly when translations need them.

`Localization` owns catalogs and returns owning strings, avoiding dangling views
when changing language. Simulation outcomes and item kinds remain typed; the
client translates them. `TerrainName` looks up `terrain.<id>.name`, falling back
to the content-defined name. Persistence IDs and developer diagnostics remain
unchanged. Default English text helpers are retained for existing headless APIs.

F2 opens the editor with the setting list focused. Three cascading tab rows
select Global/World scope, Preferences/Keybindings, and a category. Preferences
are grouped into Interface, World, Timing, and Saving; keybindings are filtered
by Gameplay, Interaction, Menu, and Debug context.

Tab/BackTab moves focus through scope, section, category, list, and details.
Left/right selects within a focused tab row; Enter returns to the list. Up/down
selects a list row or scrolls the focused details pane. Each scope/section/category
retains its selection and independent list/details scroll positions. Navigation
preserves drafts. These controls follow the existing Menu bindings.

Enter edits a number, toggles a boolean draft, opens the language picker, or
captures a replacement binding. The first typed character replaces a number;
Backspace deletes, physical Enter accepts numeric text, and Escape cancels.
Individual numeric bounds are checked on acceptance; relationships such as
active/resident radii are checked on Apply so related drafts can be edited
together. A language selection remains a draft until applied.

`+` adds a binding, `u` unbinds, and Backspace resets to inherited. `a` applies,
`s` applies and saves the selected scope, `R` reloads saved configuration for both
scopes, and `i` opens key inspection. The compact footer and contextual details
show relevant bindings. Text entry, capture, and inspection retain fixed Escape
cancellation; numeric text retains physical Enter acceptance. Unapplied drafts
are discarded on close; applied unsaved changes remain active.

The details pane explains each field/action, effects, constraints, source, what
Reset restores, and any applied value differing from the draft. World seed is
read-only after creation; Global seed remains an editable creation default.
Generation metadata and keyboard limitations appear only where relevant.
At 100 columns or wider, an 18-column category sidebar sits beside the list and
details. Thin rules separate the three panes, with the remaining content width
split approximately 45%/55% between list and details. Narrower screens show
categories as a horizontal strip above stacked list/details panes, keeping the
controls and footer visible at 80×24. Actual allocated layout boxes determine
this arrangement on the first frame and resizing; no physical terminal query is
needed. Selection, focus, drafts, and independent viewport positions survive
resizing. Up/down select categories while the wide sidebar is focused; left/right
remain available at both widths. The narrow category strip uses left/right only.
Tabs and footer hints are grouped at the left with fixed spacing, rather than
distributed across the terminal width. Footer separators keep each key and its
action together; status feedback stays beside the Settings title.

Text and backgrounds follow the terminal theme. Yellow marks drafts/unsaved
states or unavailable bindings, green marks success, and red marks errors.
Selected controls use brackets/bold; selected list rows use a separate `>` marker
and bold. Only the focused selection uses inverse video. The details header stays
visible and uses inverse video while that pane is focused, including during
scrolling and key inspection. Status labels plus `*` (draft), `=` (override), and
`!` (unavailable) markers keep the meaning readable without color. Full error
diagnostics remain in scrollable details rather than expanding the footer.

Pane headings, consistent padding, right-aligned list values, and rules between
description, value/source information, and constraints provide hierarchy without
a fixed color palette. List previews are limited to 12 columns to protect labels
at the wide layout's minimum width; full values and bindings remain available in
scrollable details. All new labels use the localization catalog. The design uses
FTXUI's explicit DOM composition and menu focus styling while retaining the local
scroll pane: replacing it with a focus-centering frame would change viewport
restoration. These presentation choices require no public API or file changes.

Presentation metadata and viewport state stay private to the terminal module;
settings APIs, sparse inheritance, and file formats are unchanged. In local play,
Settings and debug modals suspend elapsed updates and discard time spent open.
An open LAN host keeps pumping authoritative rounds while either modal is open
so guests remain connected and their inputs and deadlines continue to resolve.
Guest settings apply to the retained local world; the remote host controls its
own pacing and saves. The debug viewer is available only in the local world.
Save failures remain visibly unsaved.
