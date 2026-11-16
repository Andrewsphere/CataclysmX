# Item descriptions, text art, and melee content

`ItemDefinition` owns an optional authored `description` string (empty when
absent), parsed `TextArt art`, and `eastl::optional<MeleeProfile> melee`.
Existing definitions need none of these fields. They are independent of item
capabilities and terrain appearance.

```json5
{
  type: 'item', id: 'metal_pipe', name: 'metal pipe', mass_g: 1500, volume_ml: 1000,
  description: 'A length of heavy metal pipe, scarred by rust and old tool marks. A two-handed swing puts considerable weight behind it.',
  art: ['<darkgray>╔═<gray>════════════<darkgray>═╗',
        '<darkgray>╚═<gray>════════════<darkgray>═╝'],
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

`art` is an optional array of strings, one per line. An empty array means no
art. Empty lines, leading/trailing spaces, and unequal line lengths are retained.
Newline characters inside strings are invalid: use separate array entries.

`ParseTextArt(eastl::span<const String>, Diagnostics* = nullptr)` returns an
owning `ContentResult<TextArt>`, with `InvalidData` for invalid input. A
`TextArt` has `Vector<ArtLine> lines` and maximum cell `width`. Each `ArtLine`
contains `ArtRun{text, color, cells}` records. Text is valid UTF-8 without markup;
each allowed scalar occupies exactly one logical cell. Adjacent text of the
same color is merged, even across redundant tags; empty runs are omitted.
Empty lines contain no runs. Source storage can be discarded after parsing.

Tags set the color of subsequent text, with no stack or nesting:

| Tag | Alias |
| --- | --- |
| `<default>` | `<reset>` |
| `<black>` | `<k>` |
| `<red>` | `<r>` |
| `<green>` | `<g>` |
| `<yellow>` | `<y>` |
| `<blue>` | `<b>` |
| `<magenta>` | `<m>` |
| `<cyan>` | `<c>` |
| `<white>` | `<w>` |
| `<gray>` | `<grey>` |
| `<darkgray>` | `<darkgrey>`, `<dark_gray>` |

Names are case-sensitive. `<reset>` restores `ArtColor::Default`; each line
starts in that color independently. `<<` produces a literal `<`; `>` is ordinary
text. Closing tags, unknown names, whitespace in tags, nested tags, and
unterminated tags are errors. `ArtColor` is a named palette, independent of any
UI library; renderers choose its visual mapping and the context's default color.

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

Limits are 128 lines, 256 rendered cells per line, 4096 decoded source UTF-8
bytes per line, and 131072 decoded source bytes total. Source byte counts include
markup but not JSON quoting/escapes. Tags that produce no cells still consume
the source budget. Failures report one-based art line and source byte-column,
plus the zero-based array path; when loaded from JSON, the file and full
definition path are added. These are positions within decoded art, not JSON
document coordinates. No diagnostic strings are constructed when diagnostics
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
