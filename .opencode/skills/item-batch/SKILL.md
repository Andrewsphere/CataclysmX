---
name: item-batch
description: Item batch planning and authoring; use ONLY when asked to create or upgrade a coordinated batch of game items.
---

# Author an item batch

Read `doc/item_authoring.md`, `doc/content_format.md`, `doc/item_presentation.md`,
`doc/crafting_content.md`, `doc/content_pack.md`, and relevant location/loot docs.
Use `item-create` and `text-art-create` for individual items and illustrations.

- [ ] Agree on a roster and measurable coverage quotas before editing: location,
  practical role, acquisition and progression stage, along with any requested
  categories. Record targets and counts; make the roster diverse and coherent.
  Default to batches of 15–25 items unless the request specifies otherwise.
- [ ] Search the catalog and references. Maintain a tracker with ID, represented
  unit, role, location, progression, new or existing/upgrade, art, recipes, loot,
  status and blockers. Count new items separately from upgrades; avoid duplicate
  IDs and duplicate concepts. A batch is a work roster, not a loot pool.
- [ ] The five pilot IDs `wood_scrap`, `claw_hammer`, `glass_bottle`, `work_shirt`,
  and `arming_sword` already exist; see `doc/item_pilot.md`. Preserve existing
  stable IDs and references and check the intended pack's sources.
- [ ] Cover abundant advanced pre-collapse salvage alongside practical everyday
  equipment. Build historical/simple gear progression primarily through plausible
  low-tech crafting possible without civilization, not primarily museum loot.
  Trace obtainable inputs and tools from early to later items; record gaps and
  unsupported steps instead of fabricating a mechanic.
- [ ] Apply the item checklist to each roster entry: units and physical values,
  concise atmosphere-first prose, existing stat anchors, supported capabilities,
  tool traits and melee, shared flags, and canonical external RGB art. Flag
  intended unsupported behavior as metadata-only.
- [ ] Plan and review the descriptions as a collection. Their primary purpose
  is atmosphere and vibes, with visual/practical details serving that purpose.
  Vary implied history, everyday familiarity, loss, survival ingenuity and dry humor;
  avoid a catalog of specifications or the same apocalypse observation repeated
  for every item. For a 60-item generation, aim for roughly **2–3 deliberately
  humorous or satirical descriptions**, leaving the rest room for other moods.
  Coordinate that target across subagents/sub-batches, not separately per agent;
  a smaller batch need not force a joke. Record the selected humor IDs in the
  batch report and check that prose never promises unsupported mechanics.
- [ ] Give some descriptions **a survivor's perspective**: why an ordinary
  object is worth carrying now, or a resourceful possibility its finder might
  notice in a zombie-filled world. This is another atmospheric approach, not
  another joke quota. Prefer an item-specific survival observation to a forced
  punchline, and vary the needs it evokes instead of making everything a weapon.
  Review practical hints against actual mechanics and obtainable crafting routes.
- [ ] Respect actual recipe constraints including input and trait bounds, retained
  tools, quantities, work and unique recycling targets. Connect requested loot to
  plausible locations and placements independently of batch membership; include
  source files in the intended manifests without duplicate definitions.
- [ ] Validate affected content packs and review renderer previews where available.
  Update tracker status and coverage actuals against quotas. Report new and
  upgraded counts, incomplete entries, integration and checks actually run, plus
  metadata-only mechanics and unresolved blockers.
