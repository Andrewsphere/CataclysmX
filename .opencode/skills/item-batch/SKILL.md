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
  concise plain descriptions, existing stat anchors, supported capabilities,
  tool traits and melee, shared flags, and canonical external RGB art. Flag
  intended unsupported behavior as metadata-only.
- [ ] Plan and review descriptions as a collection, with **plain factual prose
  as the default**: identity, represented unit, construction and useful distinctions.
  Do not give ordinary entries a backstory, mood, survivor monologue or punchline.
  Check practical statements against actual mechanics and obtainable recipes.
- [ ] Treat atmosphere and humor as separate, optional exceptions: **1–3
  atmospheric and 1–3 humorous descriptions per 100 items at most**, with zero
  in either category acceptable and preferable to weak writing. These are ceilings,
  not quotas. Coordinate the allowance across the whole collection, including
  existing exceptions when extending it; never restart or round it up per subagent
  or small sub-batch. Count a description using both tones against both limits.
- [ ] Have the **strongest available writing-capable model** draft and review
  any exception candidates; resolve the available models when authoring rather
  than hard-coding a model version or leaving this to a cheaper batch worker.
  Keep an exception only if it is natural, specific to the object and fully
  in-world. Reject forced sentiment, feigned profundity, stock apocalypse quips
  and fourth-wall references. If quality or review is uncertain, use plain prose.
  Record selected atmospheric and humorous IDs separately (or explicitly none)
  in the batch record, then review the whole collection for tone drift.
- [ ] Respect actual recipe constraints including input and trait bounds, retained
  tools, quantities, work and unique recycling targets. Connect requested loot to
  plausible locations and placements independently of batch membership; include
  source files in the intended manifests without duplicate definitions.
- [ ] Validate affected content packs and review renderer previews where available.
  Update tracker status and coverage actuals against quotas. Report new and
  upgraded counts, incomplete entries, integration and checks actually run, plus
  metadata-only mechanics and unresolved blockers.
