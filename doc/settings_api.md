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

`text_size` is a global-only SDL Interface preference: integer font points from
8 to 48, default 14. `Settings` and `SettingsOverrides` expose `textSize`, with
`SettingsField::TextSize` for editing and Reset. Settings files keep format 2;
older files without this field use the default. World overrides are rejected
with `SettingsError::InvalidData`, including when loading configuration, while
pure resolution reads this field from Global only. The terminal preserves it
in shared configuration but does not display an editor or resize terminal text.
LAN guests can change it independently of the host.

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

The frontends read configuration files at startup and apply in-game edits through
Settings. External file edits take effect on the next launch. Headless runs apply preference files only when
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

The `shared` context stores directions and shortcuts whose default key and
action repeat across contexts. `ResolveKeymap` applies each binding to the
contexts where that shortcut is shared. The store checks for conflicts in
each affected context. `shared_layer` stores the alternate vertical shortcuts
that are shared outside Items. World values layer over Global values.
Contextual alternate entries can use `inherit_shared: true` to retain the
inherited shared chords without storing copies. Existing context-specific
entries still load. Editing or resetting a cardinal direction replaces older
entries for that direction in the edited scope.

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

## In-game settings

F2 opens **Preferences** in **Global** scope. Tab and Shift+Tab switch between
**Preferences** and **Keybindings**. Up/Down selects a row, Ctrl+Up/Down jumps
between section dividers, and PageUp/PageDown scrolls details. Press `g` or
select the scope button and press Enter to switch between Global and World.
The controls follow the active Menu bindings. Switching tabs retains each
selection and viewport. Switching scope or closing discards uncommitted edits.

Left/Right adjusts the selected value. Enter opens numeric or language entry
and toggles two-choice preferences. Typing replaces a numeric value. Physical
Enter accepts the number and physical Escape cancels the active editor.
Numeric bounds are checked when entering the value. Cross-field validation
runs at Save so related fields can be edited together.

Keybindings start with Shared directions and Shared shortcuts. The same key
can still perform different actions by context, and context-specific alternate
keys remain under their own headings. Left/Right selects a chord or the
**Add** slot. Enter captures a key, `+` appends, `u` removes the selected
chord, and Backspace resets the action to its inherited bindings. Removing
the last chord leaves an explicit unbound action. `i` inspects a key.
New default shortcuts defer to existing explicit bindings. The scope button
is reachable through ordinary row navigation if its shortcut is unbound.

Press `s` to validate, apply, and save the active scope. Runtime application
precedes persistence. An application failure keeps the edits available for
correction. A disk failure leaves the accepted values active and dirty. Save
retries persistence without reapplying identical values. World settings with
no world directory apply for the session. F9 opens Multiplayer separately.

Global values are cyan, World values are violet, and pending edits are yellow.
The `G` and `W` markers identify value sources. Details group key lists, scope
and reset information, and gameplay effects under dividers. In wide layouts
the panel uses about one third of the body. Compact layouts stack it below the
list. [SDL Settings layout](sdl_options_redesign.md)

The local world pauses in Settings. A LAN host continues serving players.
Terminal guests edit their retained local world configuration. SDL guests
can edit display preferences and bindings, and the host controls runtime
world settings. The live world seed is read-only.

## Editor and application API

`client/settings_editor.h` provides `SettingsEditor` and `SettingsMenuFrame`
for both frontends. The editor owns one sparse settings/keybinding draft and
two tab viewports. `Key` returns a Save or Close event to the frontend.
`Prepare` validates the draft against the other layer and fills a candidate
`SettingsStore`. `Applied` takes the accepted store and runtime snapshot.
Presentation carries owned labels, values, and style hints, so input and
inheritance behavior is shared across SDL and FTXUI without sharing a renderer.

`ResolveSettingsWithResidency` lets a frontend use the current residency
snapshot as its fallback before overlaying explicit settings. SDL uses the
snapshot because the live server belongs to another thread. The apply path
uses the effective mode and active radius to skip reapplying unchanged
residency, pacing, and autosave settings. Changes to either radius still
select its corresponding mode and clear incompatible local selectors.

The frontend installs settings only after a successful runtime apply. SDL
stages changed language and font resources before sending an owner request.
Its pending save records the request, session generation, scope, and screen
so acknowledgement goes to the correct editor, including F9 host pacing.

## SDL held-key rate

`keyboard_repeat_rate` is an integer in [1,60] with default 10. Global and
World settings resolve it through the usual inheritance rules. It controls
SDL repeat dispatch after a 250 ms initial delay.
