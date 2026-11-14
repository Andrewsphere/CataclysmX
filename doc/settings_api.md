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
Settings files are independent of the save schema. Save format 8 records residency
policy and upgrades version 7 transactionally at the next successful checkpoint;
see [save format](save_format.md). Seed and generation version remain authoritative
saved world data.

World files store **only explicit edits**, never a snapshot or a computed diff
against an old global configuration. An absent field inherits today's global
value; a present field overrides it. Reset removes the field. Explicit zero,
false, and empty binding lists are meaningful. Matching the global value does
not automatically remove an override.

```json5
// Global config/settings.json5
{format_version: 2, settings: {view_radius: 8, autosave_interval: 1000}}
// World settings.json5
{format_version: 2, settings: {view_radius: 10}}
```

Changing global values to `view_radius: 6, autosave_interval: 500` leaves this
world at radius 10 and changes its autosave interval to 500. Saving another
world edit never materializes the inherited interval.

New saved worlds create sparse local files. Unsaved worlds retain overrides in
memory; their World settings cannot be persisted without a selected directory.
Global files are written when explicitly saving Global settings.

## APIs and application

`message_history_size` is an Interface preference shared by both frontends.
It defaults to 10, accepts 1–100, and controls how many recent messages are
retained. Reducing it discards the oldest entries. See
[message history](message_history.md) for presentation and ownership details.

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
| `view_radius` | 64 | 0–64; terminal display preference, including LAN guests |
| `autosave_interval` | 1000 | Simulation turns (100 ticks each); 0 disables |
| `active_radius` | Runtime policy; legacy value 1 | 0–8 chunks; Square requires active ≤ resident |
| `resident_radius` | Runtime square policy; legacy fallback 5 | 0–8 chunks; setting it selects Square |
| `residency_mode` | Runtime policy; new servers use Circle | `square` or `circle` |
| `resident_radius_tiles` | Runtime circle policy; fallback 78 | 0–128 tiles; setting it selects Circle |
| `seed` | 42 | Creation default; never changes an existing world |
| `ticks_per_second` | 100 | 1–1,000,000 |
| `rate_limited` | false | Existing tick pacing behavior |
| `pause_when_idle` | true | Existing tick pacing behavior |

`ApplySettings(Server&, const SettingsStore&, Diagnostics*)` is the frontend
application API. With no residency override in either scope, it leaves the
server's policy and active radius untouched. New servers therefore retain Circle
78 tiles plus the square active radius of 1 chunk. Loaded servers retain their
saved circle or square, including small radii. Language, view, keybinding, pacing,
and autosave edits do not implicitly select a residency mode. Presence determines
explicitness; equality with a default never does. The next checkpoint records the
active runtime policy.

`ApplySettings(Server&, const Settings&, Diagnostics*)` remains a fully explicit
value application API. `Settings{}` deliberately retains legacy Square 1/5
semantics for existing callers. Its `residencyMode` chooses `SetBubbleRadii`
(chunk units) or `SetCircleResidency` (tile units). Both application overloads
validate before the fallible policy update, and only then set pacing/autosave.
A validation or world-update error preserves server preferences. Neither overload
sends commands, advances simulation, or changes the seed. The menu also stages
the store and localization before applying, retaining both on failure.

`ResolveSettings` / `SettingsStore::Effective()` are server-independent values
with legacy Square 1/5 fallbacks. For residency presentation or application, use
`ResolveSettingsForServer(server, global, world)`: it starts with current runtime
residency and overlays Global then World, while other fields retain their usual
built-in defaults. When switching away from the runtime mode, the previously
inactive radius starts at its legacy fallback (5 chunks or 78 tiles), unless a
layer supplies it. Unspecified active radius retains the current server value.
`HasResidencyOverride` tests presence of any active radius, chunk radius, tile
radius, or mode override. `ValidateOverrides` checks individual ranges and
same-layer selector conflicts; relationships are also checked after layering.
Active-only v2 settings can depend on runtime mode/radius, so that relationship
is finally validated at application. `Source(ResidencyMode)` includes mode
selection implied by a radius, while `HasOverride` reports actual field presence.

Explicit small bubbles remain supported and clip sight at residency boundaries. Default
sight is 64, independently of the viewport. The default display requests a
129×129 snapshot; terminal layout crops around the player to fit the allocated
panel. Explicit smaller overrides continue to work. LAN guests receive the
full radius-64 snapshot and apply their own display preference without changing
host perception. See [terminal view](terminal_view.md) and [sight defaults](sight_defaults.md).

### Settings schema v2 and v1 migration

Only **settings** now write `format_version: 2`. Keybindings independently use **2** (and accept v1); localization remains
at **1** and rejects version 2.
Settings accept versions 1 and 2 and reject all others. V1 remains strict and
rejects the new fields. Reading a v1 file with either legacy bubble field adds an
explicit Square mode to its in-memory overrides, even when its values equal
defaults. Saving migrates it to v2 with that mode retained. A v1 file containing
only language/pacing/etc. does not create a residency override.

```json5
// Global: circle in tile units; active simulation stays square in chunk units.
{format_version: 2, settings: {
  residency_mode: 'circle', resident_radius_tiles: 78, active_radius: 1,
}}
// World: explicitly select legacy square/chunk residency instead.
{format_version: 2, settings: {resident_radius: 2}}
```

Each layer can select mode using `residency_mode`, `resident_radius` (Square),
or `resident_radius_tiles` (Circle). Conflicting mode/units or both radius units
**within one layer** are errors. A World selector supersedes a Global selector;
values in the other mode remain inactive. Numeric values inherit per field, so a
world mode-only Circle override can reuse Global's tile radius. A v2 active-only
override changes the active square while inheriting the mode. Circle radius 0
is valid even with a larger active square: active demand implies residency.
Square still requires `0 <= active_radius <= resident_radius <= 8`.

Reset removes only the selected explicit field. Remaining selectors still apply;
removing a World selector restores a Global selector if present. Removing all
residency overrides preserves the current runtime policy, rather than resetting
to a new-server default. Sparse writes never materialize a saved/default circle
as configuration merely because an unrelated field was saved.

Configuration is re-resolved on startup, world changes, and explicit reload or
Apply; files are not watched. Headless runs apply preference files only when
`--user-dir` or `--world-dir` is explicitly provided. Scripted loads then
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

`KeyChord` contains a character or named key and `ctrl`, `alt`, and `shift` flags. Character
case is significant, including with Ctrl: `t`, `T`, `Ctrl+t`, and `Ctrl+T`
are distinct bindings. Alt is an explicit modifier; Shift is explicit for named
keys, while printable keys retain case-based spelling and Shift+Tab remains
`BackTab`. Text entry
uses produced UTF-8 characters without shortcut normalization.
Ctrl currently supports ASCII letters and the named keys; unsupported character
combinations are rejected during validation.

Keybinding format **v2** adds optional `alt` and `shift` booleans, for example
`{key: 'Up', alt: true}` and `{key: 'Left', shift: true}`. V1 files remain readable
with missing flags false; v1 rejects the new fields. Saving writes v2, which older
clients cannot read. All layers retain sparse overrides. Newly added default
chords yield to existing explicit bindings, so upgrading does not steal custom
keys; conflicts between explicit bindings still fail validation. Physical
terminal/desktop delivery of modified keys remains platform-dependent.

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
derive the character. Supported Alt/Shift navigation sequences retain their
modifiers; unsupported system modifiers are ignored. FTXUI
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

Enter edits a number, toggles a boolean or residency-mode draft, opens the language picker, or
captures a replacement binding. The first typed character replaces a number;
Backspace deletes, physical Enter accepts numeric text, and Escape cancels.
Individual numeric bounds are checked on acceptance; relationships such as
active/resident radii are checked on Apply so related drafts can be edited
together. A language selection remains a draft until applied.

World preferences show Active radius (chunks), Resident radius (square chunks),
Residency mode, and Circle radius (tiles), after Seed. The inactive mode's radius
is marked Inactive. Without explicit residency settings the displayed policy is
the server's current saved/default policy, with that source explained in details.
Editing either radius selects its units and removes the scope's incompatible
radius/mode overrides. Toggling mode removes an incompatible local radius.
These edits remain drafts until Apply; world selectors can mask global changes.

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
settings resolution and sparse inheritance remain in the client layer. In local play,
Settings and debug modals suspend elapsed updates and discard time spent open.
An open LAN host keeps pumping authoritative rounds while either modal is open
so guests remain connected and their inputs and deadlines continue to resolve.
Guest world settings apply to the retained local world; the remote host controls its
own pacing and saves. Interface settings, including view radius, apply to the
guest terminal immediately. The debug viewer is available only in the local world.
Save failures remain visibly unsaved.

## SDL held-key rate

`keyboard_repeat_rate` is an integer in [1,60], default 10. It follows existing
global/world override precedence and settings serialization. It controls only
SDL frontend repeat dispatch, with a 250 ms initial delay; simulation tick rate
and replay timing remain unchanged. SDL Save and apply combines runtime application
and persistence. Reopening clean options reloads files; save failures retain applied
unsaved settings. Terminal keeps its existing options workflow.
