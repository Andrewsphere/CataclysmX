# Combat content

## Catalog and integration

`ContentCatalog` owns immutable `CombatMoveDefinition`, `CombatStyleDefinition`,
`CombatArtDefinition`, and `AbilityDefinition` values. Each has the contract's
`Find…`, `TryGet…`, `Get…`, and `…Count` methods. IDs are one-based and assigned
lexically after collecting all sources; zero means absent. `Get` aborts on an
invalid ID, while `TryGet` returns null. Persist names, never indices.

`GetCombatSkills()` returns `CombatSkillIds` by value. Canonical names are
`melee`, `unarmed`, `knives`, `swords`, `clubs_hammers`, `axes`, `polearms`,
`shields`, and `dodge`; authored combat catalogs require all nine, each with
`category:'combat'`. No runtime combat-style enum crosses this API boundary.
Consumers resolve `basic_balanced`, `basic_aggressive`, or `basic_defensive`.

When **no** `combat_move`, `combat_style`, `combat_art`, or `ability` definition
is supplied, the loader installs a typed 15-move/3-style fixture repertoire.
It never inserts skills: available canonical skill names bind normally and
missing bindings remain zero. All three basic styles exclude the Shields family
in the fallback when that skill is available. The runtime treats zero neutrally. Any authored
combat category disables this fallback; missing basic roles are errors.
An authored catalog must provide every baseline role in every basic style.
The baseline moves cannot impose learned unlocks or handling-family restrictions
of their own. An authored catalog must preserve each role's source, trigger,
execution, and paired/exhausted/opening purpose. Costs and
positive weights remain tunable. Missing save references are still errors.

`CorpseItem()` identifies the reserved generated `combat_corpse` ordinary
container present in **every** catalog. It has one `container` capability with
`UINT32_MAX` volume capacity, 60000 ml exterior volume and 60000 g mass. It is
not an unfinished craft. External definitions cannot claim its ID. World death
transfers must bypass ordinary capacity refusal, since even this capacity is
finite. Adding this baseline changes item counts and lexical item indices.

### Build/network handoff

* `src/content/combat_content.h` is an internal implementation fragment included
  once by `content.cpp` inside its anonymous namespace; no new engine `.cpp`
  registration is required. It shares strict readers and source diagnostics.
* `tests/combat_content_tests.cpp` is registered with `cataclysmx_tests`; its focused
  Catch2 tag is `[combat-content]`. It uses the existing
  `CATACLYSMX_CONTENT_PACK` definition to locate fixture data.
* `WriteCombatContent(const ContentCatalog&)`, implemented in the existing
  `content_pack.cpp`, returns a versioned canonical `DataObject` of effective
  combat semantics, including explicit defaults and fixture definitions.
  `ContentFingerprint` in `src/network/network.cpp` now includes
  `HashData(hash, WriteCombatContent(content))` through coordinator integration.

The serializer includes complete moves, requirements, sources, unlocks, strikes,
contacts, unique damage packets, defenses, effects, costs, training, weighted
styles, art ownership, grants, abilities, item melee/grips/armor, book manuals,
monster style/ranks/ratings/reach/inexhaustibility, skill bindings and corpse ID.
References are names; vectors retain their defined order and styles' weighted
moves are normalized to lexical move order. Numeric aggregate lists are private
to fingerprint schema version 3, not a save format or JSON authoring schema.
The fingerprint includes `requiresNaturalApparatus` on each primary/secondary
source and each style's sorted `exclude_attack_handling_skills` list.

## Definition format

All files retain `{format_version:1,definitions:[…]}` JSON5 envelopes. Unknown
fields, duplicate keys, duplicate definitions/references, bad enum spellings,
fractional/negative unsigned numbers, and unresolved references are rejected.
Diagnostics identify the source and nested field when available; passing null
diagnostics does not change validation. Definitions resolve across files and
forward references in a two-pass identity/reference load.

### Moves

`type:'combat_move'` requires `id`, `name`, `requirements.primary`, and a
nonempty `strikes` array for strike execution. Optional fields:

* `trigger`: `attack` (default), `incoming_strike`, `counter_opening`.
* `execution`: `strikes` (default), `dodge`, `parry`, `block`. Reactions must use
  `incoming_strike` and have no strikes. Counter attacks must require an opening
  and cannot generate another counter opening.
* `requirements`: `primary`, paired-only `secondary`, `paired`, unique
  `handling_skills`, `requires_awareness`, `requires_opening`,
  `allowed_during_recovery`, `allowed_during_windup`, `exhausted_only`.
  Exhausted-only moves must be zero-stamina attacks. Basic exhausted roles also
  prohibit a cooldown so they remain usable fallbacks.
* Sources: `kind` is `held_item`, `empty_hand`, `kick`, or `natural`; `hand` is
  `either`, `left`, `right`, or `both`. `free_hands`, `functional_hands`, and
  `functional_legs` use `none`, `left`, `right`, `both`. An optional `grip` is
  `one_handed` or `two_handed` and requires a held-item source. Kick sources
  must require functional legs. Kick/natural sources cannot select hands.
  Paired sources require distinct one-handed held or empty-hand bindings;
  secondary strikes without paired sources are rejected. Impossible occupied
  hand/free-hand combinations are rejected.
  Natural sources additionally accept optional boolean
  `requires_natural_apparatus` (default true). Explicit use on any other source
  kind is rejected, including a redundant true value. Null, strings, numbers,
  and duplicate keys are errors; false explicitly permits an alternate natural
  attack without the attack-apparatus gate. Other body/source checks still apply.
* `unlock`: unique `skills:[{skill,level}]`, levels 0..10, and unsigned `art_xp`
  in runtime art-XP units. All requirements are ANDed by the runtime.
* `cost:{windup,recovery,stamina}`: defaults 0/100/5; time 0..1000000,
  recovery at least 1, stamina 0..100. `cooldown` is 0..UINT32_MAX ticks.
* Each strike supports `source` (`primary`/`secondary`), `reach`, unique weighted
  `contacts:[{region,weight}]`, optional unique `damage:[{type,amount,penetration}]`,
  `damage_ratio`, `rating_bonus100`, `intrinsic_miss`, and unique `defenses` names
  (`dodge`, `parry`, `block`). Empty omitted damage borrows the committed profile.
  Explicit damage arrays must be nonempty. Amount is 1..1000000 hundredths HP,
  penetration 0..1000000; `damage_ratio` is 1..100000; miss is 0..10000 and
  defaults to 500. Contact and move weight sums must fit uint32.
* `reach:{distance,height,regions}` defaults to 1/`any`/all regions. Distance
  is 1..2; height is `any`, `lower`, or `upper`; explicit regions are nonempty
  and unique. Contacts must be included in the strike's region mask.
* `defense:{rating_bonus100,block_capacity,uses_source_capacity,permits_armed_attack}`
  belongs to reaction moves; its kind is derived from execution. Block capacity
  is 0..1000000 hundredths HP. Bare-handed parry does not implicitly permit an
  armed incoming attack.
* `preparation:{expose_opening}` and
  `follow_through:{condition,stagger,knockback_tiles,counter_opening}` use explicit
  supported fields. Conditions are `always`, `miss`, `defense_succeeded`,
  `contact`, `damaging_contact` (default), `killed`; knockback is 0..2 tiles.
* `training:{work,melee_share,handling_share,art_xp_per_work}` defaults to
  10000/4000/6000/10000. Shares are unit ratios and must sum to 10000; work is
  0..UINT32_MAX. Signed rating bonuses are bounded to ±1000000 hundredths levels.

Damage names are `blunt`, `slashing`, `piercing`, `ballistic`, `heat`, `chemical`,
and `choking`. Duplicate types in one strike or melee profile are errors, making
shared blocking/protection unambiguous.

### Styles, arts, abilities

`combat_style` requires `id`, `name`, nonempty unique weighted
`moves:[{move,weight}]`, optional owning `art`, and optional positive
`switch_cost` (default 20). Weights default to 1 and range 1..UINT32_MAX.
Weights are relative within the eligible moves for the current trigger. Scaling
every reaction weight equally does not reduce the chance of attempting a defense;
the same is true of a counter trigger with only one eligible move. Style tradeoffs
must come from the selected move mix and its actual costs/effects.
Optional `exclude_attack_handling_skills:['shields']` prevents either held-item
source of a strike from using the listed combat handling families. It composes
with the move's `handling_skills` allowlist and applies to ordinary, paired,
counter, and exhausted moves, as well as Sweep in the active style. Block and
parry still use the item's normal response rules. Leave a family out of this
list to let otherwise eligible moves attack with it; for example, a shield
specialist can use moves with `handling_skills:['shields']`.

`combat_art` requires `id`, `name`, nonempty unique `styles`, and optional
`abilities:[{ability,unlock}]`. Style/art ownership must agree in both
directions. Every art must expose an attack starter with no skill/art-XP gate,
opening requirement, or exhausted-only requirement. A catalog of permanently
locked advanced moves is rejected.

`ability` requires `id`, `name`. Supported executions are `focus`, `leap`, and
`sweep`. Common fields are `targeting`, `cost`, `cooldown`,
`interrupt_on_damage`, `interrupt_on_forced_movement`, and `training`.

* Focus uses `self`, positive `focus_duration` and positive
  `focus_rating_bonus100`.
* Leap uses `tile` and `leap_distance` 1..2 (default 2).
* Sweep uses `cardinal_direction` and a `sweep_move` reference to an ordinary
  non-exhausted strike attack without an opening requirement. That move cannot
  appear in any weighted style. Ability costs/training replace borrowed move costs.

Effect-specific fields on other ability kinds are errors. Unknown effects cannot
become inert placeholders.

### Items, armor, manuals, monsters

`melee` adds `handling_skill`, nonempty unique `grips`, `block_capacity`, and
`allowed_responses`. Every grip requires `kind` and can override `damage_ratio`,
`cost_ratio`, `stamina_ratio`, `block_ratio` (default 10000) and signed
`rating_bonus100`. Positive cost/damage/stamina ratios range 1..100000; block
ratio can be zero. Legacy `hands:1`/`hands:2` creates **exactly one** neutral grip;
`hands` and `grips` are mutually exclusive. Missing legacy handling uses `melee`
when present; damage type never selects a skill. Unauthored improvised melee
retains its neutral one-handed fallback and zero skill for runtime Melee binding.

Optional `armor` has `layer` (`under`, `normal`, `outer`, `strapped`), nonempty
unique `regions:[{region,coverage,encumbrance,overlap_exempt}]`, `protection`,
optional `rigid_regions`, positive `wear_cost`/`remove_cost` (default 100).
Coverage is 0..10000, default full; encumbrance is 0..65535. Protection is a
damage-name keyed object of 0..1000000 hundredths HP values. Rigid regions must
have a region entry with positive coverage. Garment coverage is rolled by the
runtime once per physical strike and shared by that garment's damage packets.

Contacts: `head`, `eyes`, `mouth`, `ears`, `torso`, `left_arm`, `right_arm`,
`left_hand`, `right_hand`, `left_leg`, `right_leg`, `left_foot`, `right_foot`,
`attack_apparatus`, `locomotion`, `core`. Contacts remain distinct even when
they share an injury pool.

Books add independent `art_manuals:[{art,skill,level,success_chance,base_duration}]`.
Chance is 1..10000, duration is positive through UINT32_MAX ticks, skill is a
combat skill, level 0..10, and duplicate arts are rejected. Ordinary benefits
and recipes remain independent of repeatable art study; manuals are not consumed.

Monsters add `combat_style` (default `basic_balanced`),
`authored_training_ranks` (default true), `offensive_training_rank`,
`defensive_training_rank` (0..10), signed `attack_rating100` and
`defense_rating100`, explicit `inexhaustible` (default false), and `reach`.
Styles requiring an ungranted art are rejected. Faction does not determine stamina
policy. Legacy `accuracy` validation/conversion is retained for compatibility.
These fields match `World::CreateMonster`'s current explicit copies from the
definition. Omitted fields resolve to a simple undead body, speed 100, native
damage 12 HP, legacy accuracy 9000, authored ranks enabled with both ranks and
ratings zero, inexhaustibility false, and `ReachProfile{}` (reach 1, any height,
all contacts). The loader binds the basic style before publishing the catalog.

## Shipped identities and starter tuning

`data/combat/core.json5` contains 20 moves, five styles, two arts and three
abilities. All four shipped manifests load the authored combat core and skills;
main/first-house also load combat items and manual books.

* Basic moves: `basic_weapon`, `basic_fist`, `basic_kick`, `basic_heavy`,
  `basic_paired`, `basic_natural`, `basic_bite`, `basic_dodge`, `basic_parry`,
  `basic_block`, `basic_counter`, `basic_exhausted_weapon`,
  `basic_exhausted_fist`, `basic_exhausted_kick`, `basic_exhausted_natural`.
* Basic styles: `basic_balanced`, `basic_aggressive`, `basic_defensive`.
  All three keep Shields-family items out of strikes, counters, paired attacks,
  exhausted attacks and Sweep; normal shield blocking remains available.
  Aggressive favors held weapons over spare-hand punches/kicks, including during
  exhaustion. It retains a larger share of heavy and paired attacks than Balanced,
  trading more committed attacks for higher stamina demands and recovery costs.
  Dual weapons retain both paired source orientations. Natural attackers use their
  own eligible move mix, so player equipment tuning must also be checked against
  monsters sharing the style. The typed fallback is a small fixture repertoire;
  production balance weights live in the authored core.
* `basic_natural` requires a functional natural attack apparatus.
  `basic_bite` explicitly sets `requires_natural_apparatus:false`, remains an
  ordinary positive-stamina weighted attack, and may be chosen alongside a
  healthy native strike. `basic_exhausted_natural` is the emergency weak bite:
  it also explicitly disables that gate, costs zero stamina, has no cooldown,
  and uses authored piercing damage 700 scaled by 5000 (350 hundredths HP).
  Both use explicit bite damage rather than borrowing a disabled native profile.
  This policy is identical in the typed fixture and authored JSON; the existing
  exhausted-only eligibility rule still selects fallback attacks only when no
  eligible ordinary attack is affordable.
* Art `boxing`, style `boxing_guard`, starter `boxing_jab`, advanced
  `boxing_cross` (Unarmed 2 and 50000 art XP). Grants `focus` immediately and
  `leap` at the same advanced threshold.
* Art `weapon_forms`, style `weapon_guard`, starter `weapon_cut`, advanced
  `weapon_drive` (Melee 2 and 50000 art XP). Grants `focus` immediately and
  `sweep` at the advanced threshold. Sweep borrows `weapon_sweep_strike`.
* Abilities `focus`, `leap`, `sweep` cost 8/12/18 stamina, respectively.
* Items `brass_knuckles`, `wooden_shield`, `boxing_manual`,
  `weapon_forms_manual`, `cloth_face_wrap`, `work_boots`. New simple items use
  the existing missing-art presentation fallback; no generated bitmap assets.
* Books `boxing_manual_text`, `weapon_forms_manual_text` have repeatable
  chances 7000/6000 and base durations 600/800 ticks, respectively.
  Their item carriers, plus `brass_knuckles` and `wooden_shield`, appear with
  positive weights in `house_storage`, reached by furnished-house cellar chest
  placements in both Town and FirstHouse. Manuals additionally appear in
  `loot_reading_room_supplies` (bookstores, libraries, schools, and residences)
  and `loot_hobby`; knuckles additionally appear in `loot_outdoors` (including
  sporting-goods stores). These additions preserve existing pool maximum item
  counts and volumes, including aggregate bounds of parent pools.
* `zombie_longarms` has explicit reach 2 and appears at weight 1 in
  `monsters_outdoors`. Natural moves allow reach 2, bounded by the actor's
  authored reach; ordinary actors retain reach 1. Main-pack undead explicitly
  opt into inexhaustibility and have authored rank/rating values. Ratings are
  separate from rank, even when initial tuning uses matching numbers.
  The showcase pack also contains its production-identical recipe, bringing the
  generated gallery to 67 exhibits. Its Gen1 mapping reuses the existing
  construction-worker visual; see [Zombie showcase](zombie_showcase.md).

Combat rules own the approved 40/60 ratings, 40/40/60 defense bases with 5-point
skill-difference scaling and 5..90% clamp, 5% intrinsic miss, bounded 20% stamina
efficiency, and opponent XP suppression. JSON only supplies move-level tuning;
it does not duplicate these arithmetic policies.

## Verification

The content sources and focused tests are registered in the integrated build.
The complete SDL-enabled suite passes; see [combat verification](combat_verification.md)
for commands, counts, and the separate gameplay experiment baseline. Catalog
checks preserve authored-item and capability assertions while accounting for the
generated corpse: the demo pack has 17 items, Town 98 items, and Town 18 skills.
