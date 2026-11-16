# Item descriptions, text art, and melee content

`ItemDefinition` owns an optional authored `description` string (empty when
absent), parsed `TextArt art`, and `eastl::optional<MeleeProfile> melee`.
Existing definitions need none of these fields. They are independent of item
capabilities and terrain appearance.

```json5
{
  type: 'item', id: 'metal_pipe', name: 'metal pipe', mass_g: 1500, volume_ml: 1000,
  description: 'A length of heavy metal pipe, scarred by rust and old tool marks. A two-handed swing puts considerable weight behind it.',
  art: '/art/items/metal_pipe/art.txt',
  melee: {damage: [{type: 'blunt', amount: 2000, penetration: 0}],
          attack_cost: 150, hands: 2},
}
```

## Descriptions

`description` must be a string without embedded NUL; empty strings are valid.
Prefer one to three concise sentences describing the object and its practical
use. This is editorial guidance, not a sentence-count validator. The authored
text is the fallback for the standard localization key
`item.<stable_item_id>.description`; localization is resolved by the shared
client rather than content loading. Art is authored presentation, not localized
prose. Description text is plain text, not color-tag markup.

## Colored UTF-8 art

Both art parsers return an owning `ContentResult<TextArt>`. A
`TextArt` has `Vector<ArtLine> lines` and maximum cell `width`. Each `ArtLine`
contains `ArtRun{text, color, cells}` records. Text is valid UTF-8 without markup;
each allowed scalar occupies exactly one logical cell. Adjacent text of the
same resolved color is merged, even across different palette names or redundant
tags; empty runs are omitted.
Empty lines contain no runs. Source storage can be discarded after parsing.

`ArtColor` stores canonical `uint8_t red, green, blue` and `bool useDefault`.
`ArtColor::Rgb(r, g, b)` selects explicit RGB; `ArtColor{}` selects the renderer's
context color. Names are resolved while parsing, so runtime equality and rendering
do not depend on palette names or a UI library. Explicit RGB remains distinct
from the context default even if a renderer happens to display them identically.

### External text-art files

The item's `art` string points to a file below the pack's data root. See
[content asset loading](content_assets.md) for file/memory APIs, path rules,
diagnostics, ownership, and packaging. New illustrations use external files.

`ParseTextArtFile(eastl::string_view, Diagnostics* = nullptr)` borrows the source
while parsing a versioned UTF-8 asset:

```text
@text-art 1
@default steel
@color steel #AAAAAA
@color edge #5A5A5A
---
<edge>╔═<steel>════════════<edge>═╗
<edge>╚═<steel>════════════<edge>═╝
```

The first physical line must be exactly `@text-art 1`, without a BOM, leading
whitespace, or trailing whitespace. Until the required exact `---` separator,
only `@color name #RRGGBB` and an optional single `@default name` are accepted.
Directive spacing is exactly as shown; blank header lines and comments are not
supported. Hex digits may be upper- or lowercase. Names match `[a-z][a-z0-9_]*`,
are at most 64 bytes, and cannot be `default` or `reset`. Duplicate names are
errors, even for identical RGB. At most 64 palette entries are accepted. These
small, bounded declarations borrow source views rather than copying a palette.

`@default` may precede the referenced declaration, but must resolve to a local
color. Without it, each body row starts in the context default. With it, each
row starts in the declared RGB. `<default>` and `<reset>` both restore that
file default. Other tags resolve **only** against the file's local palette;
legacy built-in colors and aliases are not implicitly available. A monochrome
file needs no declarations:

```text
@text-art 1
---
  /\\
 /__\\
```

The body must contain at least one row, but all-empty rows are valid. LF and
CRLF are accepted, including mixed line endings; bare CR is invalid. A final
newline terminates the preceding row without creating an extra row. To preserve
an intentional final blank row, add another newline. Leading/trailing spaces,
blank rows, and unequal row lengths are retained. Header-like text after `---`
is ordinary body text, subject to the same tag and character rules.

Malformed input returns `InvalidData`. A syntactically valid positive version
other than 1 returns `UnsupportedVersion`: version syntax is `[1-9][0-9]*` with
value at most 4294967295. Zero, signs, leading zeros, fractions, overflow, and
extra whitespace are `InvalidData`.

File limits are 147456 total raw bytes, 16384 header bytes (including first
line, separator, and their line endings), and 4096 bytes per physical line
(excluding LF/CRLF). The body allows 128 rows, 256 cells per row, and 131072
source bytes summed over rows, excluding line endings but including tags.
The total file budget also counts all line endings. These independent budgets
bound parsing and output without weakening the legacy body limits.

Diagnostics use one-based **physical file line and UTF-8 byte-column**, including
header lines. File parser locations have empty filename and path; the asset
loader adds the filename. Missing rows or separators are located at the next
expected line. Whole-file size failures are located at line 1, column 1.

### Legacy inline compatibility

`ParseTextArt(eastl::span<const String>, Diagnostics* = nullptr)` continues to
parse the optional inline `art` array, one string per row. An empty array means
no art. Newline characters inside strings are invalid: use separate array
entries. Legacy names resolve to the original SDL RGB values, now stored
directly in the canonical runs:

| Tag | Alias | RGB |
| --- | --- | --- |
| `<default>` | `<reset>` | Context default |
| `<black>` | `<k>` | 0, 0, 0 |
| `<red>` | `<r>` | 220, 83, 83 |
| `<green>` | `<g>` | 88, 194, 132 |
| `<yellow>` | `<y>` | 225, 204, 89 |
| `<blue>` | `<b>` | 100, 140, 230 |
| `<magenta>` | `<m>` | 200, 110, 210 |
| `<cyan>` | `<c>` | 90, 200, 210 |
| `<white>` | `<w>` | 245, 245, 245 |
| `<gray>` | `<grey>` | 170, 170, 170 |
| `<darkgray>` | `<darkgrey>`, `<dark_gray>` | 90, 90, 90 |

Each inline row starts in the context default independently. In both formats,
tags set the subsequent text color with no stack or nesting. Names are
case-sensitive. `<<` produces a literal `<`; `>` is ordinary text. Closing tags,
unknown names, whitespace in tags, nested tags, and unterminated tags are errors.

### Fixed single-cell policy and limits

Allowed scalars are precisely these inclusive ranges:

- U+0020–007E: printable ASCII.
- U+00A1–00AC and U+00AE–024F: Latin-1 printable characters and Latin extended
  letters, including precomposed accented Latin; soft hyphen U+00AD is excluded.
- U+0391–03A1 and U+03A3–0481: Greek letters/symbols and Cyrillic through U+0481.
- U+048A–052F: further Cyrillic, excluding combining U+0483–0489.
- U+2500–259F: box drawing and block elements.

This conservative allowlist makes art layout reproducible without locale-based
`wcwidth` or a UI dependency. These ranges are treated as one cell even on a
host configured to give ambiguous-width glyphs another width. Frontends must
honor the content cell policy. Combining marks (use precomposed accents),
variation selectors, zero-width characters, bidi controls, wide CJK, emoji,
tabs, all control characters, and ANSI escapes are rejected. Invalid UTF-8,
including overlong sequences, surrogates, and out-of-range scalars, is rejected.

Inline limits are 128 lines, 256 rendered cells per line, 4096 decoded source UTF-8
bytes per line, and 131072 decoded source bytes total. Source byte counts include
markup but not JSON quoting/escapes. Tags that produce no cells still consume
the source budget. Failures report one-based art line and source byte-column,
plus the zero-based array path; when loaded from JSON, the file and full
definition path are added. These are positions within decoded art, not JSON
document coordinates. Both parsers return `InvalidData` for invalid body data.
Neither constructs diagnostic strings or allocates diagnostics when diagnostics
are omitted.

## Melee profile

`melee` accepts only `damage`, `attack_cost`, and `hands`. It is independent of
unarmed combat stats. `damage` is required and contains one to seven packets:

- `type`: exactly one of `blunt`, `slashing`, `piercing`, `ballistic`, `heat`,
  `chemical`, `choking` (lowercase enum names). Duplicate types are errors.
- `amount`: required integer 1–1000000, in **hundredths of HP**: 400 means 4 HP,
  2000 means 20 HP. Every packet must contribute positive damage.
- `penetration`: optional integer 0–1000000, also hundredths of HP, default 0.

`attack_cost` is an optional integer 1–1000000 base ticks, default 100; combat
applies move/speed modifiers. `hands` is an optional integer 1 or 2, default 1.
Fractions, booleans, negative values, unknown fields, duplicate keys, nulls,
empty damage lists, and out-of-range values are errors. Packet order is retained
because damage application can be order-sensitive. The bounded amounts leave
headroom for combat arithmetic rather than accepting arbitrary uint32 values.

`ItemMelee(const ItemDefinition&)` returns the authored immutable profile or a
shared immutable default: one blunt packet of 400 (4 HP), penetration 0, base
cost 100, one hand. Its one-packet inline buffer requires no allocation per query.
The returned reference borrows the definition or the process-lifetime fallback.

Definition equality includes description, canonical parsed art, and optional
melee values. Art aliases/redundant tags compare equally when they produce the
same runs. Explicit melee defaults compare equally to omitted fields within an
authored profile; an absent profile remains distinct from an authored profile,
even when `ItemMelee` gives the same effective values. This retains author intent
without adding runtime state or a generic presentation framework.
