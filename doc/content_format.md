# Terrain content, format version 1

`content/content.h` provides an immutable, owning `ContentCatalog`. The loader
accepts JSON5, including comments, unquoted keys, single-quoted strings, and trailing
commas. `data/terrain.json5` is the bundled four-terrain catalog.

```json5
{
  format_version: 1,
  definitions: [
    {
      type: 'terrain',
      id: 'closed_door',
      name: 'closed door',
      movement: {passable: false},
      blocks_sight: true,
      appearance: {symbol: '+', foreground: 'light_gray'},
      opens_to: 'open_door',
    },
    {
      type: 'terrain',
      id: 'open_door',
      name: 'open door',
      movement: {passable: true},
      blocks_sight: false,
      looks_like: 'closed_door',
      appearance: {symbol: '/'},
      closes_to: 'closed_door',
    },
  ],
}
```

## Schema and validation

The root has exactly `format_version` and `definitions`, both required. Version
must be an integer; an unsigned integer other than 1 returns `UnsupportedVersion`.
Definitions is an array (possibly empty). Every definition requires:

| Field | Value |
| --- | --- |
| `type` | Exactly `terrain` |
| `id` | Nonempty ASCII `[a-z][a-z0-9_]*` |
| `name` | Nonempty string without embedded NUL |
| `movement` | Object with the required boolean `passable` |
| `blocks_sight` | Boolean |

Optional fields are `appearance`, `looks_like`, `opens_to`, and `closes_to`.
Unknown fields and duplicate keys are errors at every object level, even when
duplicate values agree. Unknown definition types and duplicate IDs across any
input sources are errors. `movement.cost` is intentionally unsupported until its
gameplay meaning is defined. No ID prefixes are reserved or required.

### Appearance inheritance

`appearance` accepts only `symbol`, `foreground`, and `background`. `looks_like`
names another terrain by stable string ID and inherits **only unspecified
appearance fields**. Explicit fields override inherited fields individually.
Name, passability, sight blocking, and transition references are never inherited;
required gameplay fields must always be present locally.

All sources are collected before resolving references, so forward and cross-file
references work. Unknown references and any `looks_like` cycle are errors, even
if the terrain already specifies every appearance field. Resolution uses an
iterative chain walk rather than the C++ call stack.

Every resolved appearance needs a symbol and foreground. A root without a
background defaults to black only after inheritance resolution reaches that
root: an intermediate missing background cannot mask an ancestor's explicit
background. A terrain may omit `appearance` entirely when inheritance supplies
its required fields.

`symbol` is exactly one valid UTF-8 Unicode scalar, excluding C0 controls,
DEL, and C1 controls (U+0000–001F and U+007F–009F). Invalid UTF-8, surrogates,
and multiple scalars are rejected. Space, combining scalars, and wide scalars
are accepted: terminal display width and fallback belong to the frontend, not
the simulation/content schema.

### Colors

Colors are case-sensitive names below or exactly `#RRGGBB`; hexadecimal digits
may use either case. Short hex, alpha channels, whitespace, and undocumented names
are not accepted. These are fixed RGB values, independent of terminal palette settings.
`brown` is an explicit alias for `yellow`, both RGB (128, 128, 0), for compatibility
with traditional readable terrain palette names.

| Name | RGB hex | Name | RGB hex |
| --- | --- | --- | --- |
| `black` | `#000000` | `dark_gray` | `#808080` |
| `red` | `#800000` | `light_red` | `#FF0000` |
| `green` | `#008000` | `light_green` | `#00FF00` |
| `yellow` | `#808000` | `light_yellow` | `#FFFF00` |
| `blue` | `#000080` | `light_blue` | `#0000FF` |
| `magenta` | `#800080` | `light_magenta` | `#FF00FF` |
| `cyan` | `#008080` | `light_cyan` | `#00FFFF` |
| `light_gray` | `#C0C0C0` | `white` | `#FFFFFF` |
| `brown` | `#808000` | | |

### Terrain transitions

`opens_to` and `closes_to` are optional stable terrain IDs. Both on one terrain
are an error. Targets must exist, including targets in other sources. These
references are resolved directly to runtime `TerrainId`s, without traversing
their graph. Reciprocal closed/open doors and self-references are legal; they
are transitions, not inheritance. Absence becomes the invalid ID zero. The
loader does not impose extra gameplay rules on target passability or appearance.

## API, ownership, and errors

- `LoadContent(eastl::span<const ContentSource>, Diagnostics* = nullptr)` parses
  named memory sources. Source names and texts are borrowed **only during the
  call**; successful catalogs and diagnostic frames own their strings.
- `LoadContentFiles(eastl::span<const eastl::string_view>, Diagnostics* = nullptr)`
  reads all files and loads them as one transaction. File path views need not be
  NUL-terminated. Empty paths and embedded NUL are I/O errors.
- `LoadContentFile(const char*, Diagnostics* = nullptr)` is the single-path
  convenience entry point. A null path returns `Io`.
- All return `ContentResult<ContentCatalog>` (`eastl::expected`). Expected
  failures are `Io`, `InvalidData`, and `UnsupportedVersion`; allocation failure
  is fatal. Stdio files and parser documents use RAII. Read errors and close
  failures are checked. There are no exceptions or partial catalog returns.
- Diagnostics are optional and appended without clearing existing frames.
  Validation frames carry source file and schema path; syntax failures also
  carry one-based line and byte-column. Frames follow the shared inner-first
  convention: field failure first, followed by terrain and content-source/catalog
  context. Unsupported definition types are dispatched and reported at the
  document layer's `type` field. Missing-reference diagnostics name both target
  and referring terrain; cycle diagnostics name an involved terrain. Duplicate-ID
  failures name the stable ID and carry the later definition's location first,
  then the prior definition's source/path as a separate context frame. A null
  pointer avoids diagnostic strings/frames/path allocations, including these
  contextual messages.
- `FindTerrain(string_view)` returns zero when absent; `TryGetTerrain(TerrainId)`
  returns null for zero/out-of-range; `GetTerrain(TerrainId)` aborts on an invalid
  ID as an always-on invariant check. `TerrainCount()` counts definitions.
- Numeric IDs are the lexicographically sorted stable IDs' ordinal plus one.
  Input source and definition order cannot change them. IDs are **catalog-local**:
  adding/removing IDs can renumber them, and an in-range ID from another catalog
  cannot be detected. Persist stable string IDs, not numeric ordinals.
- Returned pointers/references remain valid while the owning catalog lives and
  is not moved from or replaced by assignment. Copies own independent storage.
  The catalog has no public mutators, hidden defaults, or global instance.

The API exposes only typed owned values, keeping yyjson inside content loading.
Sorted contiguous storage gives deterministic IDs and simple binary-search
lookup without hash iteration semantics or an additional indexing dependency.
Appearance-only inheritance lets content share presentation while keeping
gameplay definitions explicit and reviewable. World, save, and renderer adapters
decide how to retain the catalog and use these typed values.
