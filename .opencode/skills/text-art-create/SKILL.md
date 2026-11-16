---
name: text-art-create
description: Item text art creation; use ONLY when asked to draw or revise an item text-art illustration.
---

# Create item text art

Read `doc/item_authoring.md` for the external file contract and
`doc/item_presentation.md` for the glyph allowlist, palette rules and limits.
Box-drawing and block elements are approved.

- [ ] Identify the item's recognizable form and choose a shape-specific rectangle,
  targeting roughly 24–32 cells on its dominant dimension. Account for cells being
  taller than wide; preserve deliberate spaces and stay within documented limits.
  A shirt can use 32 columns by 16 rows: doubling its row count makes it resemble
  a long coat in the actual renderer. Judge physical proportions, not square grids.
- [ ] **First draw the complete uncolored glyph grid**, including all silhouette
  and interior details. Use only supported single-cell Unicode. Review the grid at
  actual display scale for silhouette, proportions, visual clarity and recognizable
  form; revise the uncolored grid until it works.
- [ ] **Then colorize the approved grid without modifying any glyph or spacing.**
  Choose locally meaningful color names and roughly 6–12 colors as a guideline,
  fewer when appropriate. Use restrained contrast and consistent lighting.
- [ ] Make one canonical finished file at
  `data/art/items/<id>/art.txt`: `@text-art 1`, `@color name #RRGGBB` declarations,
  optional `@default name`, `---`, then colored art using `<name>`, `<reset>`,
  and `<<` for a literal `<`. Names are local; color resets each line. The
  item reference is `art: '/art/items/<id>/art.txt'`, relative to the manifest
  directory as data root.
- [ ] Compare the markup-stripped art to the approved uncolored grid,
  decoding `<<` and preserving whitespace. Preview
  in the target renderer if available, checking font, cell aspect, clipping and
  contrast. Keep the external asset as the canonical finished illustration.
- [ ] Validate allowed glyphs and dimensions, local color references, and exact
  layout; report what was actually previewed or validated and any blockers.
