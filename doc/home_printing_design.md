# Home printing: accepted first playable slice

**Status: Pass 3 implemented and functionally verified; seeded balance playtesting remains.**

The user accepted the first printer, useful products, feedstock, costs,
acquisition and setup/batch behavior together, building on the
[D3 foundations](digital_fabrication_design.md). Numerical budgets are accepted
initial tuning targets. [Runtime APIs](digital_runtime.md) and the
[content/evidence record](pass3_content.md) describe the implementation, save
payloads, protocol 28 and verification. Seeded balance playtesting remains part
of the integrated workshop/city package.

## 1. Accepted delivery target

Find an ordinary home polymer printer, bring prepared filament and power, print
useful storage or assembly components, and earn Computers learning when a selected
batch finishes. The machine supports six supplied designs at Computers 0–2.
Its operating competence is independent of the trades that use printed components.

Coverage targets:

- One found, portable printer and one counted feedstock.
- Six supplied print recipes: three immediately usable storage products and
  three components with concrete consumers.
- Three alternative manual assembly recipes producing existing useful equipment.
- Useful practice sources at Computers 0, 1 and 2, teaching through 1, 2 and 3.
- Acquisition in electronics retail, a school and residential hobby/workshop
  settings; no requirement to complete D4 city generation first.
- One complete interruption/offscreen/save/LAN demonstration using actual content.

This is **seven new item identities**, one new machine object, six machine recipes,
three manual recipes and four recyclers. Reuse four existing product identities:
`plastic_storage_box`, `canvas_duffel`, `salvaged_machete` and
`workshop_battery_pack`. No existing item definition needs a stat upgrade.

The small roster follows the specific first-printer scope. USB/phone/laptop
content belongs to ordinary information access and is not counted as printer
content. Hacking, schematic extraction, filament manufacturing and printer
construction remain their own later work.

## 2. Equipment and feedstock

### Printer

Item `home_polymer_printer` places object
`home_polymer_printer_station`; the distinct IDs respect the shared definition
namespace.

| Property | Accepted initial value / rule |
| --- | --- |
| Represented equipment | One contemporary desktop filament printer with its controller and ordinary supplied designs |
| Packed mass / external volume | 8,000 g / 65,000 ml |
| Installed footprint | One blocking, sight-transparent tile; adjacent same-layer interaction |
| Shared input/output bin | 12,000 ml |
| Supply | D1 grid electricity; no built-in battery or free initial energy |
| Processing demand | 1 energy per machine tick |
| Initial state | Idle, empty bin, no job, supplied designs available |
| Idle demand | Zero in this first gameplay model |
| Acquisition | Found working equipment; no temporary construction recipe |

The bin is a staging/output allowance, not a geometrical build-volume simulation.
The supported recipe list decides which individual products the printer can make.
Batch volume is checked against the bin; there is no bed-packing puzzle.

Packing requires no job and empty public storage. Preserve the original protected
equipment item and any per-instance library state, so moving/replacing the printer
cannot reset information, duplicate equipment or regenerate resources. Portable
machine backing/packing is part of the W4 integration, not an assumed existing
capability of every legacy machine.

### Filament

`polymer_filament` represents **one 10 g portion**, external volume
**15 ml**. Quantities describe usable filament portions from a spool; a 1 kg find
contains 100 portions. No empty-spool item, arbitrary byte-like material store or
fractional ingredient consumption is needed.

- Start with one compatible general-purpose polymer feedstock.
- Whole portions are reserved as ordinary leaf items and consumed on completion.
- Authored input excess covers supports, trimming and process waste.
- Found filament is the first supply route. Mixed `rigid_plastic_scrap` is not
  directly printable; producing suitable filament needs a later deliberate process.
- Polymer grades, nozzle sizes, wear, failed prints and thermal simulation are
  unnecessary for these first curated outputs.

## 3. Supplied products and practical roles

Selected IDs were checked against the current catalog. New items need normal
authoring, canonical text art and external SDL handoff during implementation.

| Output | Identity | g / external ml | Useful role | Acquisition / progression |
| --- | --- | ---: | --- | --- |
| Plastic cup | New `plastic_cup` | 40 / 400 | Ordinary 300 ml small-item storage | Supplied Computers-0 print; drinking benefits belong to food gameplay |
| Small parts case | New `small_parts_case` | 120 / 850 | Ordinary 600 ml storage for small supplies/components | Supplied Computers-1 print |
| Plastic storage box | Existing `plastic_storage_box` | 300 / 2,500 | Existing 2,000 ml storage | Supplied Computers-1 producer plus existing found route |
| Plastic buckle set | New `plastic_buckle_set` | 40 / 100 | One set of four buckles, consumed by the duffel assembly below | Supplied Computers-1 print; no generic equipment-repair effect |
| Printed machete grip | New `printed_machete_grip` | 80 / 180 | One fitted pair of grip scales, consumed by the machete assembly below | Supplied Computers-1 print |
| Printed electronics case | New `printed_electronics_case` | 180 / 900 | One fitted shell, consumed by the workshop battery-pack assembly below | Supplied Computers-2 print |

The cup, case and box use existing container behavior. A component's practical
role is its actual recipe consumer; it needs no invented tool trait. Products are
ready to use as represented, including ordinary support removal. Printing these
products introduces no hidden Fabrication requirement.

Every print in this slice is supplied with the printer. Selecting a built-in
design requires neither a separate schematic item nor a human recipe-learning
step. The later information model can also carry imported designs, but this
roster does not manufacture temporary design files as an acquisition substitute.

## 4. Printing, setup and batch budgets

One base recipe execution produces one output item. The quantity selector chooses
how many identical executions belong to this one commissioned job.

| Machine recipe ID | Output | Filament portions (g) | Computers gate / training source | Machine ticks / energy | Base learning per execution | Max batch in empty bin |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `print_plastic_cup` | `plastic_cup` | 5 (50) | 0 / 0 | 1,200 | 10 XP | 30 |
| `print_small_parts_case` | `small_parts_case` | 14 (140) | 1 / 1 | 2,400 | 20 XP | 14 |
| `print_plastic_storage_box` | `plastic_storage_box` | 34 (340) | 1 / 1 | 6,000 | 30 XP | 4 |
| `print_plastic_buckle_set` | `plastic_buckle_set` | 5 (50) | 1 / 1 | 1,000 | 10 XP | 51 |
| `print_machete_grip` | `printed_machete_grip` | 10 (100) | 1 / 1 | 1,800 | 15 XP | 25 |
| `print_electronics_case` | `printed_electronics_case` | 21 (210) | 2 / 2 | 3,600 | 30 XP | 12 |

A level-0 gate means no explicit learned-skill requirement; the introductory
training source is still explicitly level 0.

Accepted initial hands-on setup cost for each selected batch:

```text
base setup work = 200 + 20 * quantity
machine ticks  = recipe machine ticks * quantity
machine energy = machine ticks * 1
nominal XP     = recipe learning * quantity
```

Setup scales with actor speed; machine duration and electricity do not. The fixed
setup overhead makes larger batches convenient without giving repeated Start
commands a learning bonus. Setup represents loading, arranging and selecting the
prepared job. The printer's electrical preparation/finishing cost is folded into
the processing budget; there is no separately charged heating or idle phase.

These are compressed game budgets, comparable to existing recipes, not measured
real-world printer throughput or watts. The simulation uses 100 ticks per game
second. Runtime wall-clock pacing and idle pause are independent.

### Quantity and capacity

Preserve W4's eight distinct input types and positive counts bounded at 255.
Each **expanded** consumed-input count and total output count must fit 255, not
just the unscaled base recipe. Use checked arithmetic for multiplied work,
learning, mass and volume. The selected quantity is fixed until the job ends.

For an empty bin:

```text
maximum quantity =
  min(255,
      floor(255 / each input count),
      floor(255 / output count),
      floor(bin capacity / max(input volume per execution,
                               output volume per execution)))
```

For a nonempty bin, preflight actual selection first. Public items selected as
inputs move out of the unreserved contents; other contents retain their volume.
Then apply W4's reservation:

```text
unreserved public volume + max(total reserved input volume, total output volume)
```

The UI offers a valid range and explains the limiting material/count/capacity.
Execution rechecks it; it never silently reduces a stale requested quantity.
Later insertions honor the reservation, including when the job is stopped.

### Ten-cup example

An empty printer accepts quantity 10 at Computers 0:

- Reserve 50 filament portions: 500 g and 750 ml.
- Reserve 4,000 ml for the final ten cups; they contain 400 g of polymer.
- Perform 400 base setup work: 4 game seconds at normal actor speed.
- Process for 12,000 ticks: 120 game seconds and 12,000 energy.
- Produce ten cups and nominally award 100 Computers XP to the initiator.

At the existing D1 values this processing uses two generator fuel packets' worth
of delivered energy. A full 4,000-energy workshop battery pack funds one third of
it. Mains still supplies it normally while available. This is a budgeting
comparison; shared loads and actual stored charge still govern execution.

## 5. Component consumers

Add alternative producers rather than rewriting old recipes or saved unfinished
jobs. These recipes produce the existing canonical items and retain the relevant
trade's learned skill gates.

| Manual recipe | Consumed inputs | Retained tools / traits | Gate | Work | Input g -> product g |
| --- | --- | --- | --- | ---: | ---: |
| `stitch_canvas_duffel_with_printed_buckles` | 5 `canvas_patch`, 1 `nylon_webbing`, 1 `sewing_thread`, 1 `plastic_buckle_set` | `retractable_utility_knife`, `sewing_supplies` | Tailoring 1 | 1,800 | 940 -> 800 |
| `fit_machete_with_printed_grip` | 10 `steel_scrap`, 1 `steel_wire`, 1 `printed_machete_grip` | `claw_hammer`, `hacksaw`, `metal_file` | Fabrication 1 | 2,200 | 1,280 -> 1,000 |
| `assemble_workshop_battery_pack_in_printed_case` | 4 `aa_rechargeable_battery`, 1 `electrical_lead_set`, 1 `printed_electronics_case`, 1 `solder_wire` | Soldering 1, electrical testing 1 | Electronics 2 | 3,500 | 800 -> 750 |

All three are default-known techniques. Thread and solder remain nonrecoverable
consumables. The duffel uses less webbing and less work; the fitted grip and case
reduce preparation work. The machete's steel blade is still made through manual
metalwork. The printed battery case contains no cells, charge or working circuit.

Preserve compatible actual cell charge during battery assembly under the current
crafting rules; neither printing the shell nor paid soldering creates charge.
The existing three final-product recyclers remain their sole recyclers. Their
current returns fit the new inputs and product masses as well as the old routes.

## 6. Accepted job lifecycle

Use the existing World command/event boundaries and D1 funding. The necessary
extension is one explicit manual setup phase followed by the existing autonomous
processing phase, not a generic graph of arbitrary job phases.

### Select and begin

Preview is read-only. Begin revalidates same-layer adjacent access, an enabled
unbroken idle machine, learned requirements, current supplied/imported information,
actual available leaf inputs and output capacity. Accessible input sources follow
W4/crafting rules and persistent-ID selection, including the machine's public bin.

Commit one machine-owned job containing:

- Actual reserved original inputs and the capacity claim.
- Stable recipe identity, fixed quantity, work/learning budgets and information
  eligibility captured for this job.
- Initiating character's durable identity.
- Setup progress and any paid actor slice, followed by machine progress and paid
  grid ticks when that phase begins.

The machine is occupied as soon as setup begins. Another Start cannot compete for
its inputs or overwrite the pending job. The initiating actor must be available
for the setup action.

### Manual setup

Setup is interruptible manual work, using the existing paid-work convention:
already paid actor work is preserved; recheck access and worker eligibility before
another unpaid slice. Moving away or switching activity pauses setup. Completed
setup work is retained. Stop/Resume and save/load retain paid timing rather than
charging the same slice twice.

Allow setup without electrical supply, preserving W4's ability to accept an
unpowered job. The physical preparation is unpaid electrically; a ready
controller's initial powered work is included in the processing demand.
Importing new information still follows its separate powered-transfer rule.

During setup there is no autonomous workshop hold: progress requires an eligible
active worker. A stopped/inactive setup can remain stored with reserved ingredients.
Another qualified player may resume setup, but the initiating character remains
the agreed completion-learning recipient.

### Autonomous processing

When setup finishes, automatically enter Processing with run intent. D1 buys
bounded grid slices, waits for supply as needed and maintains machine/source
residency. The operator can leave. Paid machine ticks survive source loss.
Manual Stop freezes them immediately; Resume requests execution without repeating
setup, reserving ingredients again or rechecking the external data source.

One selected batch completes as a whole. There is no automatically repeating job,
partial output collection or automatic material collection for a later batch.
After completion the machine becomes idle; remaining output occupies its bin.
Progress follows elapsed simulation time, including offscreen activity; closing
the game does not create wall-clock catch-up.

Cancel at either phase returns the original inputs, discards work and refunds no
spent electricity or actor time. Destruction drops originals/public outputs under
W4 and grants no completion reward. A machine with a job cannot be moved or packed.

### Shared operation

Use ordinary shared-workshop access for Start/Stop/Resume/Cancel/Collect.
Reserved materials cannot be taken through normal inventory transfers; cancelling
deliberately ends the job and makes its originals public again. This is the
existing shared-operation direction, not a new private-machine permission system.
Collection and a helper's Resume never transfer the completion-learning recipient.

## 7. Learning and durable completion

Use recipe-authored learning budgets, not machine ticks disguised as character
work. Apply the shared learning helpers to the recipient's state at completion,
including current boosts, source ceilings, capped surplus and theory/practice.
This roster stops at training level 2; the accepted 80% discount at training 5+
remains a general D3 rule for later recipes.

The nominal budget is additive across quantities, while realized advancement can
be clipped by shared level boundaries. Printing a component does not memorize
its separate manual assembly recipe or create a new information package.

### Practice route without books

The current ordinary XP thresholds are 100, 200 and 400 for levels 1, 2 and 3.
One illustrative route with an empty bin between batches:

| Advance | Useful practice | Filament | Machine energy / ticks | Nominal learning |
| --- | --- | ---: | ---: | ---: |
| 0 -> 1 | 10 cups in one batch | 500 g | 12,000 | 100 |
| 1 -> 2 | 10 small parts cases in one batch | 1,400 g | 24,000 | 200 |
| 2 -> 3 | 14 electronics cases in two batches, 12 + 2 | 2,940 g | 50,400 | 420; 20 surplus capped |

Total: 4,840 g filament, 86,400 processing ticks/energy, 1,480 base setup work and
700 realized unboosted XP. This is checked arithmetic, not a playtest result.
Players can choose other useful level-1 products or use existing eligible study
benefits. This slice deliberately leaves Computers 3+ practice to later content.

### Completion ownership and failure

Output creation, input consumption and once-only learning must share one logical
completion transaction. Resolve the initiator by persistent identity across live,
parked, resident/unloaded and archived state; do not activate their simulation
solely to award learning. Destroyed characters receive no reward or replacement
character grant, while the product can still complete.

Current archived **recipe-learning** credit and its retry tests are a useful
integration reference; they do not already implement archived skill-XP awards.
Extend that boundary deliberately for this reward.

Preflight/stage a recoverable recipient-storage operation before publishing
completion. A storage failure must preserve pending completion and surface a
typed error/retry path, not lose XP after delivering output or recreate output
when retried. Pause/save/reload must not pay setup, electricity or XP twice.
Order same-tick order-sensitive completions by persistent machine identity.

## 8. Acquisition and generation

Accepted initial targets use inspected existing building contexts:

| Context | Printer target | Filament target |
| --- | --- | --- |
| `electronics_store` display/repair area | One placed, idle operational home printer | Two dedicated draws of 100–200 portions each: 2–4 kg |
| `school` teaching/work area | One placed, idle operational home printer | One dedicated draw of 100–200 portions: 1–2 kg |
| `duplex` selected `hobby_pantry` basement | One placed, idle hobby printer | One dedicated draw of 50–100 portions: 0.5–1 kg |
| `furnished_house` selected `workshop_cellar` | One separate 30% ground-loot opportunity for a packed printer | One independent draw of 50–100 portions |

A town need not contain a qualifying site. These are content targets per selected
site, not guarantees that every generated settlement has a printer. FirstHouse
loads the shared roster and uses the existing cellar selection, so its printer is
optional rather than another guaranteed starter cache.

Put bulky packed printers in a dedicated ground-only pool. Keep filament in
dedicated draws too: common pools have existing count/volume bounds. A maximum
200-portion filament draw is 3,000 ml and 200 item instances. Review exact placement
cells, approach paths and all aggregate loot bounds during content authoring.
Printer and filament are separate draws/placements, not alternatives in one pool.

Retain generation algorithm version 7 for these ordinary authored placements,
subject to integration against the then-current version. Content/fingerprint
changes affect newly generated chunks; existing saved contents remain authoritative.
Never repopulate an old site, refill filament or reset an installed library on load.

## 9. Salvage, physical conservation and presentation

Accepted recyclers:

| ID / target | Returns | Work / prerequisites |
| --- | --- | --- |
| `salvage_small_parts_case` / `small_parts_case` | 1 `rigid_plastic_scrap` (100 g from 120 g) | 150 work, cutting 1 |
| `salvage_plastic_storage_box` / `plastic_storage_box` | 2 `rigid_plastic_scrap` (200 g from 300 g) | 250 work, cutting 1 |
| `salvage_printed_electronics_case` / `printed_electronics_case` | 1 `rigid_plastic_scrap` (100 g from 180 g) | 200 work, cutting 1 |
| `dismantle_home_polymer_printer` / packed `home_polymer_printer` | 30 `steel_scrap`, 1 `microcontroller_board`, 1 `heating_element`, 1 `cooling_fan`, 1 `insulated_power_cable`, 4 `electrical_connector`, 2 `copper_wire`, 6 `rigid_plastic_scrap` | 2,000 work, Electronics 1, retained screwdriver and pliers |

Use the existing `house_cutting` trait for those cutting requirements. The printer
recycler has eight return types and recovers 4,810 g from 8,000 g. Recheck its
component masses against the integration catalog before authoring. Dismantling is
physical salvage and does not extract software or schematics.
Containers must be empty before recycling under the ordinary leaf-input rules.

Cup, buckles and grip each weigh less than the existing 100 g plastic-scrap unit;
give them no recycler in this first roster. They do not become one whole scrap
each. None of these recyclers returns printable filament, operating energy or
already-consumed supplies.

The print input/output mass pairs are 50->40, 140->120, 340->300, 50->40, 100->80
and 210->180 g. Excess is abstracted process waste, keeping the single-output
contract. New containers are empty; printed shells contain no battery or charge.

Authoring ownership:

- Seven new item definitions/art briefs in `items/digital_fabrication.json5` and
  `art/items/<id>/art.txt`; existing reused items retain their identities/art.
- One object in `objects/digital_fabrication.json5`.
- Machine recipes, manual alternatives and recyclers in
  `crafting/digital_fabrication.json5` after the W4 schema exists.
- Dedicated acquisition pools in `worldgen/loot/digital_fabrication.json5`,
  referenced by the named building files.
- Include shared definitions in both principal packs; placement coverage differs.

Descriptions should emphasize familiar household/hobby objects and useful
survivor repurposing. No deliberate humor quota is needed for these seven items.
Artwork remains pending: the printer's visual brief is a tabletop gantry, spool,
bed and small controller; actual printing benefits must work with terminal/glyph
presentation before the external SDL artwork pass.

## 10. Implementation handoff and acceptance

The implementation pass settles content/API/save/LAN shapes together, including
D3-A library/carrier integration, against the accepted gameplay rules.
Do not invent a temporary item-energy filament store or human-recipe alias just
to express missing W4 machine fields.

Bounded delivery:

1. W4 selectable recipe/quantity, reservations, manual setup, autonomous execution,
   original backing-item lifecycle and once-only completion learning.
2. Ordinary supplied/imported information integration under the D3 contract;
   first home-printer recipes use supplied designs.
3. The seven-item/six-print roster, three real assembly consumers, salvage and
   acquisition, with shared frontend/headless operation.

Preserve legacy fixed-recipe machine behavior and snapshots. New mutable job and
information state needs deliberate payload/save/LAN compatibility decisions;
allocate actual version numbers against the implementation baseline, not alongside
concurrent work. Changed content budgets must never silently reinterpret reserved
originals in existing unfinished jobs.

Focused future acceptance:

- Both principal packs load; selected IDs are unique, recipe references resolve,
  container limits fit and each existing target still has at most one recycler.
- Source/count/capacity refusals are atomic; race two Starts for one machine;
  expanded counts above 255 fail; output room stays reserved while stopped.
- Demonstrate ten cups using actual filament and a finite D1 supply; assert
  material mass, actual energy, setup progress and eligible completion learning.
- Interrupt/save/reload manual setup and prepaid processing; finish offscreen;
  packing rejects jobs and preserves empty machine identity/library state.
- Preserve the initiator when another player resumes or collects; cover parked,
  archived and destroyed recipients plus recoverable storage failure/retry.
- Same outputs give equal nominal credit when split; shared boosts/caps and
  advanced practice behavior retain their own rules.
- Execute all three downstream manual alternatives; battery output charge is
  bounded by the actual compatible input charge.
- Exercise generated printer/filament contexts and approach paths on a named seed
  panel, including repeated saves without replenishment.
- Terminal/SDL/LAN views agree on quantity, supplied design, setup versus processing,
  missing resources, reserved inputs, remaining energy/work and expected learning.

Relevant existing references include `object_content_tests`, `object_behavior_tests`,
`object_server_tests`, power world/network tests and archived-credit scenarios in
`skill_save_tests`. They were inspected for contracts; no runtime tests or new
content loads were run for this design/acceptance documentation.

## 11. Accepted decisions

The user accepted this package:

1. Seven new items, six useful supplied prints and three existing-product consumers.
2. Found 10 g filament portions initially; no generic scrap-to-filament shortcut.
3. A 12 L bin, expanded 255-count bounds, `200 + 20 * quantity` setup and the
   initial per-product work/energy/learning table.
4. Reserve at setup start; resumable manual setup, then autonomous execution;
   shared operation with the original initiating character receiving completion XP.
5. The four acquisition contexts, conservation/salvage choices and existing
   content-based generation compatibility policy.

Detailed API/schema/persistence/network decisions belong to implementation.
