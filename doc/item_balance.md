# Item balance anchors

Use this guide when planning or revising an item. It defines the reference items,
encounters and tradeoffs to use alongside [item authoring](item_authoring.md).
[Combat experiments](combat_experiments.md) explains how to measure them;
[combat content](combat_content.md), [crafting content](crafting_content.md) and
[item presentation](item_presentation.md) define the supported fields and units.

## Design intent and measured power

**A reference item is a comparison point, not a promise that its current tuning
is ideal.** There are no universal approved damage tiers or win-rate bands.
Authored JSON is authoritative for current values. Update the reference values
below when deliberately changing an anchor, and rerun affected comparisons.
Keep measured rankings, spreadsheets and raw runs local under `/out`.

The design goals are:

- **A reason to choose the item.** At comparable access and training, a dedicated
  weapon should earn its equipment cost through a useful combat advantage.
  Utility tools can remain valuable through their crafting or other implemented
  functions. Everyday objects need not all become viable dueling weapons.
- **Progression with choices.** Compare opportunistic equipment, practical utility
  weapons and dedicated/specialized gear. A specialized item should have a useful
  situation, loadout or task; a broadly stronger upgrade should have a meaningful
  acquisition or usage cost. These are design relationships, not content enums.
- **Benefits paid for by actual tradeoffs.** Consider hand occupancy, defenses,
  stamina, action time, regional encumbrance, storage requirements and access.
  Judge mass and volume at the represented-unit scale. Count a runtime drawback
  only where a system actually consumes it; descriptive weight or flags do not
  automatically create fatigue, breakage or other penalties.
- **Combat results plus gameplay judgment.** A high duel score does not measure
  crafting access, convenience, readable feedback, escape tactics or tool value.
  Check those explicitly before calling an item an upgrade.

The current sword is a strong comparator, not a mandatory minimum for every new
weapon. Likewise, weak knife/baton/spear results are reasons to investigate their
roles, not targets that new members of those families must reproduce. Clothing
should be evaluated for both protection and the accumulated costs of an outfit.

## Main combat item anchors

Values below are **base melee profiles**, before move selection, grip modifiers,
skills, armor and actor speed. Damage is shown in HP for readability; JSON damage,
penetration, block capacity and armor protection use hundredths of HP. `attack_cost`
is a profile timing factor applied to the move, not a guaranteed elapsed duration.
Ratios use `ratioScale`: `10000` is 1.0 and `1000` is 10%.

| Anchor | Base damage (HP) | `attack_cost` | Supported grips | Main comparison |
| --- | ---: | ---: | --- | --- |
| `claw_hammer` | 12 blunt | 110 | One-handed | Early useful tool/weapon; `hammering` tier 1 |
| `chef_knife` | 8 slashing | 100 | One-handed | Compact cutting tool; knife handling and parry |
| `police_baton` | 9 blunt | 100 | One-handed | Compact dedicated blunt weapon; parry/block |
| `pry_bar` | 10 blunt | 125 | One-/two-handed | Utility-shaped blunt weapon and grip comparison |
| `arming_sword` | 18 slashing | 130 | One-/two-handed | Dedicated crafted weapon; strong current comparator |
| `fire_axe` | 17 slashing | 170 | Two-handed | Heavy edged weapon; commitment versus impact |
| `coil_spear` | 13 piercing | 150 | Two-handed | Crafted polearm-family weapon |
| `wooden_shield` | 4.5 blunt | 120 | One-handed | Defensive offhand; base block capacity 14 HP |

Sources: [household items](../data/items/household.json5),
[slice equipment](../data/items/slice_equipment.json5),
[low-tech items](../data/items/low_tech.json5) and
[combat items](../data/items/combat.json5).

Use the complete profile when choosing values:

- The sword's two-handed grip has damage ratio `11500`, cost ratio `10500` and
  rating bonus `25` (hundredths of a rating level). The pry bar's two-handed grip
  has damage/cost/block ratios `12000`/`11000`/`12500`. Hand occupancy matters in
  addition to these bonuses. Compare sword alone, sword/shield, two-handed sword
  and dual swords separately.
- Basic styles and Weapon Guard use shields defensively. Two held objects do
  not automatically make a useful paired attack. Check eligible moves, handling
  families, free-hand attacks, reactions and exhausted fallbacks in each style.
- Item names and flags grant no special mechanics: a spear profile alone grants
  no extra reach; coils grant no electrical damage; an axe or pry bar does not
  acquire a tool action from its name. The shotgun's profile measures melee use,
  not firing performance. Confirm the implemented behavior before pricing a niche.
- Items without an authored `melee` profile use the ordinary improvised fallback:
  4 HP blunt, attack cost 100, one-handed, no parry/block response. This is an
  incidental-use reference, not a suitable default for a dedicated weapon.
- `scrap_knife`, `metal_pipe`, `rock` and `work_trousers` are marked `obsolete`.
  They may appear in the loaded catalog and reports; use current peers and inputs
  for new progression. The obsolete pipe's power is not a current loot-tier anchor.

## Clothing and armor anchors

Start with these controls and change one garment at a time:

| Reference | Contents or role |
| --- | --- |
| `--outfit none` | No worn items; measures the total effect of adding clothing |
| `--outfit standard` | `leather_work_gloves` + `work_boots` |
| `--outfit work` | Standard outfit + `cotton_tshirt` + `denim_jeans` + `rain_jacket` |
| `work_shirt` | Separate normal-layer torso/arm garment; not included in `work` |
| `safety_goggles` | Local eye coverage; narrow protection should be assessed locally |
| `camping_backpack` | Storage with torso encumbrance; its main value is carrying capacity |

These are chiefly everyday clothing references; they do not establish a finished
heavy-armor progression. Read region coverage, layer and encumbrance alongside
protection. For scale, the T-shirt has blunt/slashing protection `30`/`50`
(0.3/0.5 HP), the work shirt `70`/`100`, and gloves `150`/`250`/`100` against
blunt/slashing/piercing. Coverage determines where protection can apply.
Definitions live in [slice equipment](../data/items/slice_equipment.json5),
[urban items](../data/items/urban.json5) and [combat items](../data/items/combat.json5).

Compare both passive exposure and active combat. Absorbing more damage can still
produce worse fights if handling, defense or timing costs accumulate. Examine
individual garments, the intended outfit, and overlapping layers. Keep passive
exposure short enough to compare equal observation windows; report deaths as well
as damage. Add relevant damage types and contact regions when the zombie panel
does not exercise the proposed protection. Weather, material names or future
environmental systems cannot stand in for currently implemented benefits.

## Physical, utility and acquisition anchors

| Reference | Current scale | What to compare |
| --- | --- | --- |
| `claw_hammer` | 600 g / 500 ml | Hand-tool size, melee plus hammering utility |
| `work_shirt` | 300 g / 1200 ml | Everyday garment scale and regional protection |
| `glass_bottle` | 200 g / 600 ml exterior; 500 ml capacity | Container exterior versus usable capacity |
| `camping_backpack` | 900 g / 2000 ml exterior; 20000 ml capacity | Storage value alongside worn costs |
| `aa_alkaline_battery` | 25 g / 10 ml; 1000 energy, initially full | Disposable power available immediately |
| `aa_rechargeable_battery` | 30 g / 10 ml; 1000 capacity, initially empty | Rechargeable supply needs a charging route |

Battery definitions are in [slice supplies](../data/items/slice_supplies.json5);
energy values are game units. For containers, tools, power stores and manuals,
compare supported capacity, task access, throughput, resource costs and usable
starting state. Use the [items API](items_api.md) and [skill content](skills_content.md)
for behavior. Food/medicine classifications alone do not implement consumption or
healing. Count recipe inputs and salvage returns by the declared object/packet
unit, including packaging where relevant.

Follow the **whole reachable acquisition chain**:

- Check pack inclusion, actual loot placement, encounter risk, prerequisites,
  recipe knowledge, consumed/retained tools, work, power and recycling returns.
  A definition or recipe's existence alone does not establish availability.
- The current-material sword route, `make_current_arming_sword` in
  [slice crafting](../data/crafting/slice.json5), uses 14 `steel_scrap`, two
  `wood_scrap`, one `copper_wire`, hammering/cutting tier 1, fabrication 2 and
  6000 work. It is an investment reference, not a universal price for weapons.
- The sword also has a [legacy route](../data/crafting/low_tech.json5) with older
  inputs and no explicit skill requirement. Consider every reachable route before
  claiming an item is gated by its most demanding recipe.
- `make_coil_spear` uses 1200 work, fabrication 1, retained hacksaw/pliers and a
  knowledge rule with fabrication-2 autolearn. Compare that access with the sword,
  rather than judging their combat scores as equally priced products.
- For a new upgrade, explain the advantage over a reachable peer and the cost of
  obtaining/using it. For a sidegrade, name the circumstances where each peer is
  preferable. Account for alternate crafting/recycling routes and shared utility.

## Encounter and character anchors

Use **`zombie_shopper`, `cellar_zombie`, `zombie_police`** as the default opponent
panel. They provide an ordinary low-training reference, the cellar baseline and
a higher-rating defensive opponent. Weight them equally for a diagnostic score;
also inspect each matchup. Their reference weight is not their spawn frequency.

| Comparison | Fixed reference |
| --- | --- |
| Early survival | Novice, hammer, one shopper; attack and cautious policies separately |
| Equipment | All combat skills 3, `basic_balanced`, standard outfit, each panel enemy |
| Progression | Skills 0, 3, 6; skill 10 checks the upper end and saturation |
| Style sensitivity | Same equipment/skills with Balanced, Aggressive and Defensive |
| Learned styles | Compatible equipment; owning art at 0 and 50000 raw XP |
| Crowd pressure | Three simultaneous shoppers; distinguish escapes from victories |
| Attrition | Five sequential shoppers; no healing actions or between-fight reset |
| Armor | Individual garment/outfit; passive 1000-tick exposure and active sword duels |

Use `--enemy-ai recipe` in direct runner commands. It retains normal monster
pursuit; `melee` instead wanders outside attack range and can distort knockback
and escape comparisons. The ranking driver already defaults to `recipe`.

The runner starts actors adjacent, fixes skills/training and uses a prescribed
policy. It does not use abilities or optimize tactics. A claim about reach,
positioning, special abilities or other omitted behavior needs an appropriate
additional gameplay comparison. Early survival should be assessed through injury
and disengagement as well as wins. The open arena's exits do not establish town
survival odds. When changing shared moves/styles, include their enemy users such
as `zombie_firefighter` and `zombie_longarms`.

## Using the anchors for a new design

1. **State the role and nearest peers.** Pick a current peer in the intended role
   and a stronger/weaker or alternative reference. Describe the expected advantage
   and cost before choosing stats. Check what the proposed mechanics actually do.
2. **Make a focused comparison.** Hold opponent, skill, style, outfit, policy,
   opening conditions, tick limit and seed range fixed. Change the item or grip.
   Start around skill 3, then examine the intended early/advanced use and relevant
   styles. Separate weapon value from a learned style or ability's contribution.
3. **Explain the result.** Inspect wins/deaths/escapes/timeouts, actual injury,
   win-only fight duration, stamina/exhausted actions, defense/armor absorption
   and move frequencies. Whole-fight elapsed time is not automatically kill time;
   low injury in a timeout is not a clean victory. Small differences need paired
   per-seed comparisons and uncertainty estimates, not just a sorted rank.
4. **Confirm the design judgment.** A 96-seed sweep is a useful screening run;
   confirm important differences on a larger separate interval, such as 384 new
   seeds, after choosing the tuning. These are working sample sizes, not acceptance
   thresholds. Review acquisition and a relevant in-game use case alongside the
   results. Use focused content/engine checks for changed behavior and integration.

A candidate has a convincing starting balance when its intended use is observable,
its important costs are real, and its relationship to peers makes sense in the
intended progression. If it broadly dominates a similarly accessible peer, or
cannot demonstrate its intended niche, explain and revise that relationship.
Avoid treating small rank changes or a single duel result as sufficient evidence.

Use this compact brief in the existing item/batch plan:

```text
Item and represented unit:
Role and expected acquisition stage/routes:
Current comparison items:
Expected advantage and meaningful tradeoff:
Supported mechanics and any unimplemented intended features:
Benchmark conditions and relevant gameplay use:
Observed result, uncertainty and design decision:
```

For a weapon sweep, after building the runner:

```powershell
python tools/balance_rankings.py out/build/win-msvc-release/cataclysmx_combat_experiment.exe --output out/item-balance --seeds 96 --groups weapons
```

The output directory must be new or empty. The sweep discovers authored melee
profiles in the loaded catalog. For focused comparisons use `--weapon ITEM
--grip one_handed|two_handed`, optional `--offhand ITEM`, or `--outfit none` with
repeated `--wear ITEM`; repeat the reference with the same remaining options.
See [experiment usage](combat_experiments.md) for builds, scenarios and exact flags.
Keep reports and exports under `/out`; maintain this guide when design rules or
anchors change, without adding a Markdown log for each run.
