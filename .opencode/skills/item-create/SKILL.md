---
name: item-create
description: Item creation and upgrades; use ONLY when asked to author or revise game item content.
---

# Create or upgrade an item

Follow `doc/item_authoring.md`; check `doc/content_format.md`,
`doc/item_presentation.md`, `doc/crafting_content.md`, and `doc/content_pack.md`
for the current authoritative schemas. Check `doc/definition_flags.md` and
`data/flags/core.json5` for shared flags.

- [ ] Identify the intended use, setting, and represented unit (single object,
  packet, handful, etc.). Search definitions and references across packs for the
  same item and ID. Reuse existing stable IDs and upgrade in place; choose a new
  globally unique ID only for a genuinely distinct item.
- [ ] Set `mass_g` and external `volume_ml` for that whole unit; compare existing
  canonical peers and explain departures.
- [ ] Write one to three concise sentences whose **primary purpose is atmosphere
  and vibes**: the life an object belonged to, a small human detail, an obsolete
  promise, or what finding it feels like after the collapse. Visual description
  and practical explanation are supporting details; avoid merely restating the
  name, illustration or stats. Keep the object's identity clear and do not imply
  unsupported mechanics. Ordinary comfort and quiet familiarity are valid tones;
  every item need not be tragic or mention the apocalypse explicitly.
- [ ] Consider **a survivor's perspective** as an alternative to humor: a small
  spark of survival ingenuity that hints why this object matters now, what makes
  it worth scavenging, or how someone resourceful might repurpose it. Tie one
  concrete possibility to life among the dead: scarcity, preparation, repairs,
  shelter or keeping something useful going. Let it suggest an idea rather than
  become a list of uses or a tutorial. Ground actionable hints in supported
  behavior; do not present an imagined use as an available game action or effect.
- [ ] Use occasional understated humor, irony or satire rooted in the object and
  the world's end. Follow the batch's shared humor budget (roughly 2–3 items in
  a 60-item generation), rather than adding a punchline to every item. Avoid
  repeated jokes, forced quips and fourth-wall references.
- [ ] Assign applicable shared category and property flags, checking meaning,
  domain and kind, and include their definitions in the pack. Use only
  implemented capabilities, tool traits, crafting power, and melee; check exact
  fields, units, limits, and defaults. Do not invent materials, armor, ranged, or
  other unsupported fields. Report intended unsupported mechanics as metadata-only.
- [ ] Compare melee and progression with shipped examples and the default melee
  profile; explain tradeoffs. Favor abundant advanced pre-collapse salvage, with
  historical/simple gear accessible through plausible low-tech crafting, not
  primarily museum finds.
- [ ] Complete the item illustration using `text-art-create` unless the task explicitly
  scopes out presentation: uncolored complete grid and
  silhouette review first, then color without changing layout. Follow the canonical
  external path and RGB format in the authoring guide.
- [ ] Integrate the definition into intended manifests and, where appropriate,
  recipes, recycling, and plausible loot/placement. Verify prerequisite access,
  stable references and current recipe constraints; a definition alone does not
  spawn an item.
- [ ] Validate affected loadable packs with available project checks; inspect art
  in the target renderer if feasible. Report checks actually run, new versus reused
  IDs, integration, stat reasoning, and pending or metadata-only functionality.
