# Crafting progression: dependencies and delivery

**Status: Delivery direction accepted; W3A/W3B, I1, pass-2 renewable power and pass-3 W4/D3-A/B functionally verified.**
The [framework](crafting_progression.md) records skill bands, capability families
and acquisition roles. This document consolidates their dependencies and proposes
bounded mechanics/content passes. The user accepted ordinary accessible/shared
traits for crafting protection, retiring M-Worn and its W3B dependency.
Later gameplay/roster drafts need review before their implementation/content
passes; technical representation belongs to implementation. This plan does not replace
the accepted [power](power_design.md) and [workshop](workshop_design.md) contracts.

The [W3A/W3B implementation](workshop_rosters.md) records the accepted item/operation
coverage, learning links, acquisition contexts and concrete authored budgets.
The [I1 source map and verification](content_pack.md#source-organization) establish
semantic catalog ownership with identical per-pack content fingerprints.
The [pass-2 renewable contract](renewable_power_design.md) now owns C-Energy's
recovered-solar/wood-gas budgets, nine-item roster and fixed installation policy.
Implementation and focused Debug/SDL verification are complete; seeded balance
playtesting remains part of the integrated package.

The accepted [D5 integration direction](urban_workshop_progression.md#d5--integration-contract-and-content-roster)
adds craftable basic-tool fallbacks, recovered-panel solar and an integrated
wood-gas generator to the implementation/playtest package. Use multiple acquisition
routes; guaranteed progression loot/site coverage is deferred. Implement and
playtest the agreed workshop/energy/printing/city package before hacking and later
progression.

Existing W1–W4 and D1–D5 names retain their meaning in
[urban workshop progression](urban_workshop_progression.md). Additional labels
below identify proposed follow-on slices, not new implemented subsystems.

## 1. Starting point

- D1 electricity and W1/W2 placed stations/fueled manual work have prior verified
  implementations; evidence remains in their subsystem documents.
- Ordinary traits, item-powered crafting, skill/knowledge gates, retained inputs
  and recycling already support substantial manual content. M-Bench now funds
  selected powered placed traits from their station-local grids.
- Crafting protection uses ordinary accessible tool traits, including existing
  handheld-tool sharing. Correct use during the job is implicit; this is supported
  by existing selection and paid-work mechanics.
- W4 selectable machine jobs are implemented with the first home printer.
  Its present scope is one explicitly started job, bounded consumed inputs, one
  output type and positive grid demand. Thermal processing and repeating jobs are
  later contract questions.
- M-Skills implements per-skill ceilings and separate Cooking (maximum 5) and
  Chemistry (maximum 10), with the shared curve and versioned legacy migration.
- Manual recipes currently allow eight distinct input types including retained
  tools and four required traits. The accepted sword requirements fit that shape.
  Protection uses an ordinary trait slot; the revised welding roster must fit the
  limit or demonstrate a concrete need to revisit it.

These observations were checked against the current definitions and durable
runtime documents. No new runtime verification is claimed by this planning pass.

## 2. Cross-domain dependency map

Every branch permits found equipment and prepared inputs where authored.
Construction skill requirements apply to the route that actually builds the device.

| Branch | Main dependency chain | Important alternate route / boundary |
| --- | --- | --- |
| Manual metalworking | Ordinary tools + recovered stock → forge/tongs at Fabrication 2 → forming equipment/anvil 2 at 3 → proper sword at 4 | Found stations/tools skip construction; fuel, blade stock, recipe skill and knowledge remain |
| Electrical/welded work | Recovered heater/control/power parts → soldering equipment → makeshift welder at Electronics 3 / Mechanics 2 → useful welded products | Found welder still needs job-appropriate accessible protection, consumables and D1 supply; products primarily use Fabrication |
| Precision mechanisms | Found measurement/tools and precision assemblies → fitted parts and workshop mechanisms → specialist machine construction | A found lathe or spindle bypasses manufacturing that equipment; powered manual use needs funded station support |
| Energy independence | Found supplies → recovered-panel solar, storage and integrated wood-gas generation → later manufactured cells/complex internals | D5 includes functional solar and prepared-wood-to-electricity assembly around a recovered engine; engine/photovoltaic manufacture is later |
| Textiles and field materials | Found fabric/leather or prepared natural materials → sewing/leather tools → useful clothing/storage/protection | Harvesting can later supply materials; Tailoring need not wait for a complete creature/harvesting system |
| Food and chemistry | Suitable ingredients + process equipment → meals/preservation or chemical products → specialist materials | The skill split precedes content using the new skill identities; found prepared chemicals can bypass their manufacture |
| Medicine and bionics | Found care supplies/instruments → clinical preparation and treatment → surgery/implant installation | Complete implants bypass manufacture, not installation requirements; patient actions require a medical runtime contract |
| Digital production | Working printer/controller + supplied software/designs → simple prints → acquired software/schematics and capable equipment → intricate/advanced parts | Basic printing need not wait for hacking; protected recovery is one way to obtain advanced packages |
| Specialist manufacture | Appropriate skill + measured/process equipment + acquired parts/materials/information → selected complex internals | Software gates apply to the actual controlled process; they do not make Computers a universal replacement for other trades |

Acquisition and learning run alongside these chains. A factory expedition may
yield a controller, a software package or a schematic rather than a whole machine.
Working finds and recoverable broken equipment both have value. Existing sites can
host a bounded acquisition slice before the larger D4 city overhaul, with explicit
placement and generation-compatibility decisions.

### Learning dependencies

A training source at level r teaches only through r+1. A proper sword at
Fabrication 4 therefore needs suitable level-2 and level-3 practice opportunities;
repeated level-1 work cannot supply the whole path. Found stations grant no
construction XP, so the found-equipment route also needs useful practice.

The assembled iron at Electronics 2 and makeshift welder at Electronics 3 /
Mechanics 2 similarly need suitable earlier practice. A found iron can support
electrical work before the player can construct one. The revised electrical
roster must identify its level-1/2 Electronics and early Mechanics learning links.

Noncombat advancement from learned level 5 onward needs theory and practice.
Include appropriate study/recovery sources with selected advanced routes.
Repeated useful work remains acceptable. Missing later practice/content can be
recorded rather than lowering accepted skill gates or changing the XP curve.

## 3. Near-term mechanics slices

Each runtime slice includes content validation, player-facing reasons/actions,
headless and shared frontend support, applicable persistence/LAN changes and
focused verification. API shapes and version numbers belong to its design.

| Slice | Bounded scope | Dependency / completion example |
| --- | --- | --- |
| **M-Bench: powered manual stations** | [Implemented contract](powered_manual_stations_design.md): station-local grid costs, provider-owned rates, atomic station/handheld/heat payment and crafting-area source residency | Builds on D1/W1/W2; useful connector production demonstrated with a powered fixture. Shipped station content is selected separately |
| **M-Skills: split and ceiling** | [Implemented contract](skill_caps_and_split_design.md): per-skill maximum, Cooking/Chemistry split, validation/presentation and versioned migration of existing progress | Available for content using separate identities. Cooking stops at 5; other default ceilings remain 10; shared learning model preserved |
| **W4: selectable grid machines** | Implemented single-job reservation, output-capacity, setup/processing, paid-slice and durable recovery contract | Reuses D1 ownership/residency; [pass-3 evidence](pass3_content.md) covers shared use, interruption, offscreen completion and persistence |

M-Worn is retired: the [accepted protection rule](crafting_progression.md#welding-protection)
uses existing tool traits. M-Bench is required only when a selected provider is a
powered placed manual station. A carried grid-funded welder can use D1 item-power
rules. Forging content can use W1/W2 without waiting for either electricity
extension or digital fabrication.

M-Skills is independent of the initial metalworking roster. Its migration copies
the validated old combined proficiency to Chemistry and Cooking, capping Cooking
at 5. Book/recipe identities and paid work are preserved.

## 4. Digital-production slices

These are subdivisions of D3/I4, sharing one information model. Pass 3 implements
D3-A/B; D3-C is explicitly deferred to the later hacking effort.

| Slice | Bounded scope | What it enables |
| --- | --- | --- |
| **D3-A: ordinary information access** | Usable package/design identity, compatible devices, supplied information, ordinary copying/export and local availability | Accessible archives and transferable unlocked information, including supplied basic printer designs |
| **D3-B: first functional printing** | Contemporary home polymer printer, supplied simple designs/software, W4 integration, setup, completion learning, feedstock and useful outputs | Useful basic printing without an acquired schematic; acquired intricate designs and advanced printing follow later |
| **D3-C: deferred hacking and extraction** | Later separate design for access/protection/recovery, design/schematic extraction, hacking laptop modifications and target-specific tools/interfaces | Factory software/design acquisition and broken-equipment recovery; hardware work retains its Electronics role |

The [accepted D3 foundations](digital_fabrication_design.md) establish freely
transferable usable information on USB sticks, phones and laptops, explicit import
into persistent machine-local libraries, and startup capture of a job's required
instructions. Removing a carrier or deleting a library copy does not invalidate
the existing job; new jobs use current library eligibility. D3-A/B implementation
owns representations, APIs and W4 persistence/network integration. Installed use, export
permission, software integrity and machine operability remain distinct. Ordinary
copying of an unlocked portable package is not another hacking challenge per product.

The accepted learning distinction grants the initiating character an authored,
batch-scaled reward on successful commissioned-job completion, including offscreen.
Continuous production and ordinary copying/importing are XP-free. Training-level-5+
automated recipes initially use 80% of their authored budget; advanced recipients
gain practice under the shared learning model. These balance targets and the
once-only recipient/completion contract are recorded in the D3 design.

Most home-printer recipes should be simple and available from the supplied
catalog without separately acquired schematics. The first example uses that
route. Ordinary transfer of readable information remains D3-A; extraction and
the earlier proposed recovered-schematic example wait for hacking. Do not add
temporary schematic/software crafting recipes or placeholder unlock items to
substitute for that future acquisition gameplay. Specific laptop modifications,
military interfaces and recovery outcomes belong to the later hacking design.

The [accepted home-printing contract](home_printing_design.md) covers six supplied
prints, seven new physical item identities, three existing-product assembly
consumers, feedstock/costs, setup and batch ownership, and acquisition together.
The accepted package is implemented; [runtime APIs](digital_runtime.md) and
[functional evidence](pass3_content.md) record schemas, saves and networking.
Hacking and extraction retain their later gameplay scope.

## 5. Energy implementation and later independent feature designs

D5 accepts the first energy expansion below for implementation/playtesting.
The other tracks need bounded gameplay rules before functional content;
implementation settles their runtime representation. They are not prerequisites
for the whole manual workshop.

| Track | First useful design target | Important dependencies/questions |
| --- | --- | --- |
| Energy supply expansion — accepted D5 scope | Recovered-panel daylight solar and one integrated prepared-wood-to-electricity generator using a recovered gasoline engine | Concrete output/fuel budgets and activation under D1; zero nighttime solar, existing battery accounting, internal gas cooling/filtering and no separate gas-cylinder economy |
| Later thermal/process machinery | Standalone coal/wood/retort process products beyond the integrated generator | Heat source, startup/running costs, represented gas units/storage, useful byproducts, capacity and interruption; review W4 extensions explicitly |
| Field resources | A useful harvesting/preparation route feeding existing material consumers; charcoal production as a separate bounded process | Source/depletion semantics, output handling and learning; first charcoal must not require an already charcoal-fueled forge |
| Food/preservation | One usable food/preparation or preservation loop at a time | M-Skills for separate Cooking; define actual consumption/preservation benefits and elapsed processing where needed |
| Chemical processes | A selected separation/reaction/material-production loop with its relevant instruments | M-Skills for Chemistry, actual supplies/containers, heat/power or machine jobs; analysis and control effects need explicit support |
| Medicine | First care/treatment loop, followed later by surgery/bionics | Patient state and action outcomes, costs, interruption and learning; an item-crafting job is not a surgical contract |
| Vehicles and robotic/self treatment | Dedicated later subsystem designs | Preserve accepted skill roles; vehicle access/support and autodoc skill substitution remain open |

Limit each implementation to its chosen useful operation and downstream consumer.
For example, an authored centrifuge needs a supported separation job; an item's
name/category alone does not provide the process. Thermal supply is not free
because a nearby manual forge is Ready. Optional repeat queues and multi-output
processes are explicit additions, not assumptions inherited from W4.

## 6. Content passes after their prerequisites

These are proposed batch boundaries. Each gets its own revised roster, identities,
quantities, learning/knowledge sources, operating costs, salvage and acquisition
review. They do not prescribe a fixed item count or require complete endgame
coverage.

| Content slice | Representative coverage | Runtime dependency |
| --- | --- | --- |
| **C-Hand: hand tools and stock preparation** | Default-known basic-tool fallbacks without circular prerequisites, useful sets, handles/frames and selected stock conversions | Existing crafting/traits; settle provider details with the roster |
| **W3A: forging progression** | [Authored roster](workshop_rosters.md#3-w3a-forging-stock-and-storage): stock, stations, tongs/hammer, both sword routes and useful intermediate storage/tool work | W1/W2; concrete budgets and verification in the roster |
| **W3B: electrical/welding progression** | [Authored roster](workshop_rosters.md#4-w3b-electrical-assemblies-and-useful-welding): iron, leads/pack, makeshift/found welder, protection and welded products | D1 + ordinary manual crafting; follows W3A shared definitions. Selected handheld providers avoid M-Bench |
| **C-Textiles: useful soft equipment** | Sewing/leather tools, garments, bags, padding and carriers from available prepared materials | Existing supported storage/armor/crafting behavior; defer additional weather/repair effects to their actual feature passes |
| **C-Mechanisms: workshop assemblies** | Vises, supported tooling and selected fitted mechanisms from recovered assemblies | Existing passive capabilities where adequate; M-Bench and any new actions for powered/function-specific equipment |
| **C-Energy: supply/storage assemblies** | Useful compatible packs/banks, recovered solar and integrated wood-gas generation | D1 plus the accepted D5 source behavior and its implementation |
| **C-Field/Food: field and meal routes** | Selected natural/post-cataclysm materials, charcoal, meals or preserved supplies | Relevant field/process/food implementation; split into bounded rosters rather than shipping all domains together |
| **C-Chemistry: instruments and materials** | A chosen chemical product chain with concrete instrument providers and downstream use | M-Skills plus the selected manual/process-machine/analysis capabilities |
| **C-Digital: printing and information rewards** | First: home polymer printer and useful supplied-design prints. Later: acquired intricate schematics and production packages | W4 + D3-A/B first; extraction content waits for the separate hacking/D3-C effort |
| **C-Medicine: care and clinical supplies** | Functional early supplies/instruments, then later surgery and prepared-implant routes | Corresponding medical actions; powered/software-driven equipment follows its own integration |
| **C-Specialist: selected advanced components** | Complex engine parts, lithium cells, photovoltaic manufacture or advanced bionic/material components | Only the relevant accepted specialist process/equipment contracts; choose one useful chain per roster |

C-Hand can be folded into W3A/B when a small provider upgrade directly serves those
recipes. It is not a mandatory all-tools catalog expansion. Found prepared inputs
let C-Textiles and other assembly branches proceed before upstream manufacture.
I1 catalog reorganization is verified; use the
[semantic source map](content_pack.md#source-organization) for later authoring.
The large-city overhaul retains its separate D4 scope.

### Revised W3 boundary

The earlier [W3 proposal](workshop_content.md) is historical source material.
Its nine-item count, level-2 sword, level-1 anvil equivalence, ungated station
assembly and found-welder-only route are not current targets. Runtime dependencies
follow the revised roster's actual providers.

Requirements applied while authoring the revised roster:

1. Keep the accepted forge/tongs 2 → forming equipment 3 → proper sword 4 chain.
   Both sword producers need anvil 2, hammering 2, metal holding 2 and forge heat 1.
2. Choose actual suitable blade stock and meaningful fabrication work at levels
   2 and 3. Verify both crafted-equipment and found-equipment learning paths.
3. Keep iron assembly at Electronics 2 and makeshift welder assembly at Electronics
   3 / Mechanics 2, with their earlier practice links. A found iron/welder skips
   that construction requirement, not the chosen product recipe's requirements.
4. Include acquired/crafted provider routes without requiring the upgraded tool
   to build its first usable instance. Anvil/forge/iron/welder bootstrap each
   needs this check.
5. Declare job-appropriate `welding_protection` in ordinary `tool_traits` for each
   welding operation: tier 1 for welding 1, tier 2 for welding 2/3. Accessible/shared
   equipment qualifies; a stronger welder does not change the job's requirement.
   Check each recipe/recycler's actual operations and combined trait budget.
6. Recalculate quantities, fuel/work, mass/salvage and knowledge for the revised
   roster. Preserve stable existing IDs and review every producer/recycler of
   affected outputs, including deliberately alternative printer routes later.
7. Select real acquisition contexts and an explicit generation-compatibility
   policy. The historical per-site guarantees and version proposal need review
   against the newly selected roster.

The [implementation record](workshop_rosters.md) records the selected budgets.
Its compatibility policy requires completing/cancelling old unfinished sword jobs
with the previous catalog before upgrading, or starting a new world.

## 7. Remaining decisions and recommended order

Resolve decisions with the first pass that consumes them:

| Decision | Owning next pass |
| --- | --- |
| Selected provider upgrades, practice products, learning gates and knowledge policy | Authorized in [W3A/W3B](workshop_rosters.md); broader C-Hand distinctions stay with later consumers |
| Concrete recipe quantities, work/fuel/XP budgets, loot weights and affected-save policy | Authored in W3A/W3B; pacing remains subject to playtesting |
| Placed electrical-provider funding grid, rate and mixed payment | Implemented in [M-Bench](powered_manual_stations_design.md) |
| Combined-skill migration and capped learning/content validation | M-Skills design |
| File/package representation, compatibility, import actions and job-capture integration | D3-A/B and W4 implementation; [library/capture behavior](digital_fabrication_design.md) and [first printing slice](home_printing_design.md) accepted |
| Source access, schematic extraction, protection, recovery and hacking laptop modifications | Later separate D3-C/hacking design; outside the current D3-A/B effort |
| Basic solar/wood-gas source budgets and assembly content | [Pass 2 / C-Energy](renewable_power_design.md); implemented and functionally verified |
| Standalone gas products/heat/byproducts/repeat behavior; medical/field/vehicle action semantics | Respective later feature design |

Accepted delivery direction:

1. **W3A/W3B roster implementation is authorized.** W3A
   uses W1/W2; W3B uses D1 and ordinary tool traits for protection. The proposed
   providers fit existing mechanics; reopen that assessment if scope changes.
2. **Deliver the accepted W3 slices**, including only the hand-tool/learning
   coverage they need. Any demonstrated mechanics prerequisite gets a separate
   design/implementation pass first.
3. **Design/implement M-Bench and selected mechanical/electrical content**, then
   **W4 with D3-A/B** for the first functional printing route. These are independent
   tracks where their shared interfaces are settled.
4. **Implement the accepted D5 basic-workshop/energy/printing/city package and
   playtest it.** Solar and integrated wood-gas generation are included. Advance
   to hacking and later progression after that loop is successful.
5. **Schedule M-Skills before Cooking/Chemistry content**; other later field,
   process, chemical and medical tracks follow their useful consumers.

The user accepted this direction and authorized the initial design work.
Later gameplay rules and content rosters follow their own review; schemas, APIs,
saves and networking belong to implementation. Later feature-track priority
remains consumer-driven.

## 8. Evidence expected from each later pass

- Demonstrate the claimed player action and useful output with the actual authored
  inputs/providers; a descriptive item or flag is not functional evidence.
- Record selected skill/knowledge/source requirements, teaching ceilings and
  any intentionally missing progression links. Content-only work uses existing
  runtime contracts rather than inventing behavior.
- Check represented resource conservation, provider substitution and relevant
  acquisition/recycling cycles. Preserve paid work and originals under the owning
  job's accepted interruption/cancellation rules.
- Run the narrowest relevant implementation/content checks and report what
  actually ran. Include manifest, save/LAN and frontend coverage when affected;
  keep tooling tests out of scope per project workflow.
- Update the owning durable document and coordinating status. No complete/winnable
  progression, broad rebalance or finished external SDL artwork is implied by
  completing one bounded functional slice.
