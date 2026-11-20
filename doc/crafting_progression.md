# Crafting progression framework

**Status: Reviewed progression outlines and delivery direction accepted; crafting protection uses ordinary traits.**
The user accepted the planning approach, shared skill meanings, independent recipe
requirements and a future Cooking/Chemistry split. Sections below distinguish
accepted capability/product bands from proposals and queued subsystem designs.
Cooking's level-5 ceiling and the supplied/recovered printer-design direction are
accepted. The [dependency and delivery plan](crafting_delivery_plan.md) consolidates
the branches, remaining decisions and proposed mechanics/content passes.
This framework informs [urban workshop progression](urban_workshop_progression.md)
and later content passes. It does not replace accepted power/workshop runtime
contracts or decide the pending digital-fabrication mechanics.

## 1. Agreed planning direction

- Discuss the overall skill/tool/station progression before authoring another
  bounded item roster.
- Establish rough crafting possibilities and acquisition roles first. Separate
  content passes then develop recipes, equipment and useful outputs within that
  outline.
- The game need not have a complete or immediately winnable progression after
  each pass. Later tiers and routes may remain planned and explicitly incomplete.
- Repeated crafting for practice is acceptable. More content should provide a
  variety of useful practice choices; eight crafts is not inherently excessive.
- The earlier [W3 roster](workshop_content.md) is paused. Its low station and sword
  skill gates are not approved targets.

Initial user anchors, developed in the accepted outlines below:

- A scavenged rail/steel striking surface can provide anvil capability 1; a
  simple anvil can provide 2; a professional anvil can provide 3.
- A proper sword should require at least anvil 2 and roughly skill 4 or higher.
- A makeshift welder should be craftable, at more than novice skill.

Accepted bands guide later authoring; exact recipes and the remaining ladders
still need review.

## 2. Shared rules

### Independent requirements

Describe a recipe through separate requirements:

1. **Skill:** the worker's competence.
2. **Tools/stations:** which operations the available equipment can perform.
3. **Stock/components:** appropriate materials or salvaged subassemblies.
4. **Knowledge:** an ordinary technique, learned design or accessible reference.
5. **Operating resources:** labor, electricity, fuel and consumables.
6. **Production software, where needed:** compatible control software for selected
   sophisticated equipment/processes, acquired independently of human knowledge.

These requirements should have recognizable reasons. Usually one primary skill
and, where justified, one supporting skill are sufficient. Technological-looking
equipment does not automatically need Computers; shaping metal, assembling a
mechanism and designing its electrical control are different work.

### Found, assembled and manufactured are different routes

Finding usable equipment skips its construction. Using it for a recipe still
requires the recipe's competence, information, material and operating resources.
Placing a found anvil should not require the skill needed to manufacture one.

Distinguish:

- **Scavenge:** acquire an operational item or a useful component.
- **Assemble:** build with existing functional components or suitable stock.
- **Commission/rebuild:** make a salvaged assembly operational through an explicitly
  supported future recipe/action.
- **Manufacture:** produce the critical working parts, a more demanding route.

For example, assembling electronics from recovered chips does not imply a
semiconductor factory. Constructing a support for an anvil-shaped steel block
does not imply manufacturing a professional anvil.

The user explicitly prioritizes assembling useful equipment from scavenged parts.
Scavenging carries risk, so finding working equipment or critical assemblies must
be a worthwhile shortcut. Manufacturing complex working parts belongs to later
skill bands, often 5+ and sometimes 7–8+. Use real manufacturing difficulty where
it gives players reasonable expectations without requiring a detailed simulation.

Recipes can support assembly/rebuild when their actual inputs/outputs express it.
Generic equipment repair, calibration and commissioning actions are not assumed
to exist merely because the outline names them.

### Capability tiers

Tool tiers express usable capability, not character skill or a universal item
quality score. Three meaningful anvil tiers do not require ten tiers of every tool.
Higher tiers need an example operation/output that benefits from the distinction.

Accepted set-coverage simplification: screwdriving 1 is a suitable individual
screwdriver and screwdriving 2 is a screwdriver/bit set; wrenching 1 is a suitable
individual wrench and wrenching 2 is a wrench/socket set. Simple recipes can use
one tool, while more complex assemblies require the set. Exact fastener dimensions
and individual heads do not need separate simulation.

The current matcher accepts any tier at least as high as the requirement. A higher
tier must therefore retain the lower tier's applicable capability. A heavier,
powered or more expensive tool is not automatically a higher-tier substitute for
a precision hand tool. Use a distinct operation where capabilities are not nested.
Differences in mass, energy or supported work may distinguish equipment without
adding another trait tier.

The user explicitly accepts that power alone does not raise a tool's tier. A
powered screwdriver is nevertheless intended as tier 2 because its authored
equipment includes the usual interchangeable-bit set. Its electricity and its
head coverage are distinct properties. Electrically assisted measurement tools
may genuinely enable additional capabilities; discuss specific devices in the
measurement/electronics pass.

Station capabilities apply while installed. Portable equipment items do not
grant anvil/forge capabilities merely by being carried.

### Information and learning

Ordinary improvised techniques are candidates for default knowledge/autolearn.
Specialized designs are candidates for manuals, recovered schematics and archives.
Knowledge does not replace skill, and skill does not automatically supply every
specialized design. Human recipe knowledge and machine design-file eligibility
remain separate questions for D3, alongside executable production software and
machine/process profiles. Selected intricate printer recipes explicitly require
the appropriate schematic; the printer's control software is a separate
requirement.

Do not change the XP curve merely to close a temporary content gap. Each future
skill band should eventually have multiple useful activities and, for advanced
noncombat bands, theory sources. Incomplete coverage can be recorded for a later
pass instead of lowering every recipe gate.

### Accepted direction: production software

User-requested direction: selected high-tier components across multiple trades
can require production software to control sophisticated equipment. Factory
computer systems can be hacked to acquire that software. This adds a digital
acquisition objective to the existing equipment, material and knowledge routes.
It is broader than Chemistry and printer design files.

Distinguish three useful information roles:

- Human knowledge explains how to perform the work.
- A design/process specification describes the desired part or production process.
- Production software enables a compatible controller/machine to execute the
  relevant operation, potentially with suitable process/material profiles.

An acquired package may supply more than one role. These distinctions describe
what the information does; they do not require three separate loot objects or
three extra gates on every recipe.

Representative consumers:

| Equipment/work | Possible software requirement |
| --- | --- |
| Controlled chemical production | Reactor-control software and a suitable process profile |
| Sophisticated CNC machining | Compatible machine-control/toolpath software for the required operations |
| Advanced electronic component/module production | Equipment-specific production and test software |
| Advanced additive manufacturing | Printer-control software and suitable material/process support, alongside the product design |

Accepted acquisition/use direction:

- Recover useful software from factory control terminals, engineering computers
  and industrial archives. Hacking is a valuable acquisition route; exact target
  access, time, hardware and outcomes belong to the future hacking design.
- Export acquired packages to portable media and deploy them to a compatible
  local controller or workstation. Prefer machine-family/process compatibility
  over a detailed operating-system/version dependency simulation.
- Computers governs access, recovery and any meaningful installation/configuration
  work. The actual production task retains the relevant trade requirements where
  the operator performs that work. Software possession does not impose a blanket
  high Computers gate on every later user of the configured equipment.
- Found working equipment can already contain its appropriate control software.
  Recovering a complete functional setup remains a worthwhile shortcut; finding
  a bare/incomplete controller can instead create a software acquisition goal.
- Treat usable software as reusable information. Copying, installation and
  machine access need explicit rules; software is not consumed per produced
  component. Repeated use and permission to make a working copy are separate.
- Preserve accessible basic printing through suitable ordinary supplied software.
  Advanced production packages can unlock selected demanding processes. Printing
  retains the accepted Computers-based operation boundary.

#### Copy protection and recovery from broken equipment

Accepted additions:

- Software can be copy-protected. Making a usable copy can require hacking even
  when the installed software is functional and the original machine can use it.
- Broken machines can retain functional software. Sufficiently skilled characters
  can extract it when the relevant controller/storage remains recoverable.
- Factory-terminal access, software copy protection and damaged-hardware recovery
  are distinct obstacles. A source can present one or several of them.

Accepted action/role direction for D3 and the hacking design:

| Source condition | Useful recovery route |
| --- | --- |
| Accessible, readable, unprotected software | Ordinary export/copy to suitable media |
| Working installed software with copy protection | Hacking to produce a usable portable copy |
| Mechanically broken machine with readable controller/storage | Access the surviving controller or recover its storage, then extract software |
| Damaged interface/controller with recoverable data | Appropriate reader/interface and, where needed, Electronics work to establish access; Computers performs data recovery |

Protection can survive physical machine failure: extracting a protected package
from a broken machine may still leave the copy-protection step. A raw backup is
not necessarily an installable package. Conversely, mechanical failure does not
automatically corrupt the stored program, and a working machine is not necessarily
an unrestricted software source.

Readable controller/storage salvage can yield software without restoring the
whole machine to operating condition. Electronics handles actual
hardware access/reconstruction where needed; Computers handles recovery,
protection bypass and package preparation. Individual actions only require the
skills relevant to that source and method.

Accepted: once the player has produced an unlocked portable package, ordinary
subsequent copying should normally be straightforward. This makes successful
recovery a lasting acquisition reward; it does not add hacking work to each manufactured
component. Exact package portability/protection rules remain for D3.

The future design must distinguish equipment operability, data recoverability,
access and export protection. These are gameplay distinctions, not a prescribed
new damage model or schema. Define readable versus unrecoverable sources, required
readers/media, work/learning, interruption and any source-changing outcomes
explicitly; failure is not implicitly destructive. Machine destruction/salvage
must have an authored relationship to its information-bearing parts. Recovering
software does not by itself repair the source equipment.

D3 owns information identity/carriers, compatibility, local deployment and
software eligibility with W4. The [accepted D3 foundations](digital_fabrication_design.md)
use freely transferable information on USB sticks, phones and laptops, explicit
imports and persistent machine-local design libraries. Jobs validate and capture
their required instructions at startup; later carrier removal or library deletion
does not invalidate existing work, including resume and save/load. Detailed
representations/actions must preserve paid-work and ingredient accounting.
D4 supplies acquisition contexts; the separate hacking design supplies access
and copy-protection actions. This outline does not implement those mechanics.

## 3. Accepted shared skill scale

These are accepted content-authoring anchors for the existing 0..10 range, not new
runtime categories or approved thresholds for every example.

| Level | Competence | Typical scope |
| --- | --- | --- |
| 0 | Untrained | Sorting, crude preparation, simple bindings and obvious salvage |
| 1 | Beginner | Basic hand work and simple assemblies; mounting found equipment |
| 2 | Competent amateur | Practical tools, simple stations and straightforward mechanisms |
| 3 | Trained craftsperson | Reliable workshop equipment and multicomponent constructions |
| 4 | Professional | Proper weapons, durable equipment and demanding ordinary trade work |
| 5–6 | Specialist | Precision work, complex systems and specialized material processes |
| 7–8 | Expert | Advanced industrial/futuristic equipment within a prepared workshop |
| 9–10 | Exceptional specialist | Selected frontier designs; not a mandatory endpoint in every domain |

Higher skill still needs the relevant infrastructure and materials. Futuristic
salvage can be common before the character can reproduce its internal components.

Current learning already distinguishes ordinary noncombat advancement at learned
levels 0–4 from theory-plus-practice advancement at learned levels 5–9. A task at
training level r teaches only through r+1. Thus moving swords to Fabrication 4
eventually requires useful level-2 and level-3 work; repeatedly making level-1
machetes alone cannot reach 4. See [skills](skills.md).

### Accepted direction: skill-specific ceilings

The user suggested a shorter progression for secondary domains such as Cooking.
Accepted direction: use one learning model with a maximum level per skill, rather than
separate "simple" and "complex" skill systems.

- Cooking: accepted maximum **5**.
- Chemistry and the other current skills: retain **10** as the planning default;
  their actual content coverage can still be incomplete.
- Preserve the shared meaning of a level; do not rescale Cooking 5 to represent
  another skill's level 10.
- Stop advancement at the skill's ceiling and present that ceiling clearly.
  Recipes and learning sources must respect it.
- With the current advanced-learning boundary, reaching Cooking 5 uses ordinary
  progression from levels 0–4. It would have no 5→6 theory/practice step.

M-Skills implements this policy through `SkillDefinition::maxLevel`, bounded
fixed-array curves, cap-aware learning/content validation/presentation and versioned
migration. See the [implementation contract](skill_caps_and_split_design.md).

## 4. Skill ownership and rough output families

All examples below describe possible future content, not claims of implemented
actions or finalized recipes. Bands overlap intentionally; exact recipes need
their own acquisition/operation review.

| Skill | Owns | Early possibilities (0–2) | Workshop possibilities (3–4) | Specialist possibilities (5+) |
| --- | --- | --- | --- | --- |
| Fabrication | Shaping and joining structural materials; woodworking and metalwork | Handles, improvised weapons, simple furniture, mounting scavenged stock, crude forge | Forming tools, fabricated anvil, racks, proper blades, structural equipment | Precision tooling, dies, advanced fabricated structures and armor components |
| Mechanics | Mechanisms, fit, alignment, mechanical integration and future vehicle internals | Fastened assemblies, hand-operated mechanisms, straightforward dismantling | Pumps, drives, vehicle mechanisms, powered-tool assemblies and rebuilding machines from major components | Precision mechanisms and industrial/vehicle system integration |
| Electronics | Wiring, soldering, electrical tools, circuits and instrumentation | Leads, connectors, soldering-iron assembly and simple electrical assemblies | Makeshift welder, chargers, microcontroller circuits and devices assembled from salvage | Precision instruments, complex control systems and advanced electronics |
| Computers | Software, information, digital control and future hacking | Ordinary file handling, basic device setup and preparing simple software tools | Data recovery, technical archives, hacking-tool preparation/use, machine setup and design-file preparation | Advanced automation, specialized hacking systems and fabrication software |
| Tailoring | Textile/leather construction and fit | Patches, bindings, bags and simple garments | Work clothing, load-bearing equipment, reinforced clothing and fitted protection | Technical fabrics, complex protective garments and specialized gear |
| Survival | Field materials, low-infrastructure production and post-cataclysm biological resources | Cordage, simple field tools, basic shelter components | Durable outdoor equipment and prepared natural materials | Recognizing/preparing useful monster-derived materials and specialized field equipment |
| Cooking | Food preparation and preservation | Basic preparation and meals | Demanding preparation and preservation | Selected specialist work at accepted cap 5 |
| Chemistry | Chemical processing and material preparation | Simple material mixtures | Controlled workshop chemical processes | Specialized compounds and industrial material preparation |
| Medicine | Medical preparation, treatment and future surgery | Simple dressings and basic care supplies | Prepared medical kits and clinical supplies | Surgery, including installing bionics in other characters, and specialized medical work |

Combat skills govern use of weapons and combat learning. A proposed sword recipe
belongs primarily to Fabrication rather than requiring Swords merely because its
product is a sword. Manufacturing medical equipment may require Electronics or
Mechanics alongside relevant medical knowledge; it is not all Medicine.

**Implemented mechanics:** Cooking and Chemistry have separate shipped identities.
Legacy `cooking_chemistry` progress migrates to both, capped at Cooking 5, under the
[bounded migration contract](skill_caps_and_split_design.md). Food and chemical
activities still need their own content passes.

### Agreed future-domain hooks

- **Vehicles:** Mechanics owns interaction with car internals and mechanical
  systems when vehicles arrive. Electrical and software subsystems can use their
  respective skills; exact action requirements belong to the vehicle design.
- **Electrical tools:** making a soldering iron and assembling microcontroller
  hardware belong to Electronics. Programming/configuring software belongs to
  Computers where the task demonstrates that need.
- **Hacking:** Computers supports preparing and using hacking tools. The user's
  example is finding software and preparing a USB device for ordinary store PCs;
  a military turret instead requires appropriate military-grade hardware/software.
  Factory systems also provide production software for sophisticated equipment
  across trades; acquiring it is a useful progression objective.
  This is a deliberately game-oriented direction. Targets, effects, access,
  costs and outcomes need a separate hacking design. That later effort also owns
  hacking laptop modifications, supporting interfaces/tools and design/schematic
  extraction; these are explicitly outside the current D3-A/B design scope.
- **Post-cataclysm resources:** advanced Survival must include useful resources
  that appeared after the cataclysm, including possible monster loot. Candidate
  hides, shells, fibers or secretions are illustrative, not an approved creature
  roster. Survival can own identification/preparation while Fabrication, Tailoring
  or Chemistry owns subsequent product construction/processing.
- **Bionics:** sufficiently skilled medical characters can become surgeons and
  install bionics in others. Self-operation is a separate future design;
  an autodoc/medical robot is the intended equipment direction to explore.
  This does not assume a currently implemented surgery or autodoc action.

## 5. Tool/capability discussion inventory

This is a review inventory, not a request to add every named trait. Use a trait
where interchangeable providers or meaningful capability tiers justify it.
Determine operation, providers, tiers, construction skill, acquisition and useful
consumers together.

| Group | Operations/capabilities to discuss | Important distinction |
| --- | --- | --- |
| General hand work | Cutting, hammering, screwdriving, wrenching, holding/clamping | A kitchen knife cannot satisfy every material-cutting task |
| Woodworking | Sawing, chopping, carving/chiseling, drilling and joinery support | Powered equipment may improve throughput without replacing every hand-tool role |
| Metal preparation | Metal sawing/shearing, drilling, filing/grinding | Cutting capacity and precision are different capabilities |
| Forging | Anvil, forge heat, metal holding and forming hammers | Rail → fabricated anvil → professional anvil is the user's proposed station ladder |
| Welding | Welding equipment, fixtures and consumables | Makeshift versus found equipment needs meaningful operating/capability differences |
| Precision mechanics | Turning, milling, measurement and assembly support | Machine tools are distinct operations, not simply higher-tier anvils |
| Electrical work | Soldering, circuit assembly and electrical testing | A multimeter's implemented grid inspection is distinct from future diagnostic work |
| Textile/leather work | Sewing, textile cutting, leather preparation and supported machine work | Hand sewing and industrial sewing need useful, explicit consumers |
| Field production | Material cutting/shaping, cordage and field preparation | Distinguish skillful low-tech production from manufacturing industrial tools |
| Cooking/chemistry/medicine | Process heat, vessels, separation and sterile/controlled work areas | Generic forge heat is not automatically every process's heat source |
| Digital fabrication | Computer access, files, polymer/metal printer capability and post-processing | Printers manufacture curated outputs; machine permission is not human memorization |

For each accepted operation, record:

```text
Operation / trait
  tier -> concrete capability -> representative providers
       -> skill to make provider -> skill to use it in example recipes
       -> found / assembled / manufactured route
       -> stock, information and operating needs
       -> useful output families -> planned content/engine pass
```

The current four-trait/eight-input recipe limits are implementation constraints,
not reasons to invent a generic "advanced workshop" trait that hides every
operation. Use meaningful component recipes where appropriate. Review actual
limits if a later accepted recipe demonstrates a need.

## 6. Discussion and delivery sequence

1. Shared skill scale, requirement separation and Cooking's level-5 ceiling
   accepted; finish remaining domain boundaries as needed.
2. General hand-tool set tiers and rough construction progression accepted;
   settle remaining provider details with their first content consumers.
3. Metal preparation, forging, welding and substantial metal-product bands accepted.
4. Mechanical/electrical equipment and diagnostics outlines accepted.
5. Textile, field-production, Cooking/Chemistry and Medicine outlines accepted.
6. Computers, information/software acquisition and printer progression accepted;
   D3 still owns the detailed runtime contract.
7. Review the [dependency and delivery plan](crafting_delivery_plan.md), then take
   bounded mechanics and content passes against the accepted subsystem contracts.

Possible later content passes are hand tools/woodwork, metalworking bootstrap,
forged tools/weapons, electrical assembly, mechanical equipment, textiles/field
gear, process supplies and digital fabrication. Final scopes follow the accepted
outline rather than an arbitrary item count.

Each pass declares the band/capabilities it fills, representative useful outputs,
acquisition/knowledge dependencies, required engine support and remaining gaps.
Its delivered recipes must function as described and must not accidentally bypass
approved gates. It need not fill every later band or provide a complete endgame.

### First capability discussion: hand tools and woodworking

Accepted here: screwdriving/wrenching set tiers, the distinction between power
and capability, and the rough construction progression. Remaining individual
provider details are working proposals; exact recipes are deferred. Later
accepted metal-preparation and workholding sections supersede the preliminary
entries for those operations below.

| Operation | Starting providers / capability | Construction outline |
| --- | --- | --- |
| General cutting | A serviceable crude edge or ordinary knife; one basic tier initially | Early crude edge/handle work; manufacturing a reliable steel blade belongs to later metalworking |
| Hammering | Tier 1 general impact; tier 2 controlled metal forming with a suitable smithing hammer | Early mounting of a found head; making the steel head requires trained metalworking |
| Wood sawing | Hand wood saw or framed salvaged blade; one basic tier initially | Frame/handle assembly around an existing blade can be early; producing the blade is harder |
| Metal sawing | Hacksaw; distinct from general cutting and wood sawing | Early assembly from a recovered blade; blade manufacture is specialist work |
| Screwdriving | Tier 1 suitable individual screwdriver; tier 2 screwdriver/bit set, including the intended powered-driver kit | Found tools available early; good steel tool manufacture around Fabrication 3–4, subject to metalworking review |
| Wrenching | Tier 1 suitable individual wrench; tier 2 wrench/socket set | Found tools available early; good steel tool manufacture around Fabrication 3–4, subject to metalworking review |
| Wood drilling | Suitable manual drill; one basic tier initially | Crude field drill or assembly from recovered parts; no implied metal-drilling capability |
| Metal drilling | Suitable drill and bit; one basic tier initially | Found tool or mechanical assembly; making cutting bits has separate material/workshop needs |
| Wood carving / chiseling | Suitable carving knife or chisel | A recovered cutting edge can be fitted early; manufacturing a good chisel is later metalworking |
| Clamping / workholding | Clamps and secured bench/vise where a recipe needs them | Simple wooden support/clamps before reliable metal mechanisms; distinct from hot-metal tongs |

Recommend ordinary manufactured hand tools as common useful salvage. A player
need not manufacture each tool before using it; its capability tier does not
equal the skill required to make it. Add extra tiers only with concrete consumers,
such as greater cutting capacity or repeatable precision. Powered variants are
not automatically higher-tier versions.

Accepted rough woodworking outputs: level 0–1 handles, stakes and simple bindings; level 2
basic boxes, supports, benches and assembled tool frames; level 3 reliable
furniture, bellows structures and workshop fixtures; level 4 demanding joinery and
precise wooden components. Later content passes choose representative recipes.

### Accepted outline: metal preparation and forging

Accepted as a rough progression outline; exact items, recipes and balance remain
for later content passes.

| Capability | Tiers/providers | Progression purpose |
| --- | --- | --- |
| Metal sawing/cutting | 1: hacksaw; 2: equipment suitable for larger/heavier stock | Broader stock capacity; distinguish precision requirements when needed |
| Metal drilling | 1: suitable drill and metal bit; 2: rigid drill-press/fixture setup | More demanding, repeatable drilling; a handheld motor alone does not imply that capability |
| Filing | 1: suitable metal file | Keep one tier until a useful consumer demonstrates more |
| Grinding | A separate operation where appropriate, rather than automatically filing 2 | Powered shaping does not substitute for every fine hand-finishing task |
| Hammering | 1: ordinary suitable hammer; 2: smithing hammer | Basic work before substantial controlled forging |
| Metal holding | 1: suitable pliers; 2: forging tongs | Bootstrap small/long stock, then larger/hotter forging work |
| Anvil | 1: mounted rail/block; 2: fabricated workshop anvil; 3: professional anvil | Follow the user's proposed capability ladder |
| Forge heat | 1: a usable charcoal forge capable of ordinary steel forging | A later tier needs a named material/process or work-capacity requirement |

Accepted manufacturing anchors: first hand-blown forge and tongs at Fabrication
2; smithing hammer and fabricated anvil at Fabrication 3; first proper sword at
Fabrication 4. Found equipment can be used earlier for recipes whose own skill
requirements are met. A professional anvil's manufacturing route belongs to a
later specialist pass rather than being required to bootstrap tier 2.

Accepted first proper sword requirements: anvil 2, hammering 2, metal holding 2,
forge heat 1, Fabrication 4 and suitable blade stock. This keeps the four-trait
shape and makes skill, forming equipment and material the meaningful progression
gates. A basic charcoal forge is sufficient; a better forge opens later work
rather than being mandatory merely because the sword is a higher-tier output.
Exact blade-stock identities and recipe quantities remain to be authored.

The intended chain is rail/block + ordinary hammer + pliers → forge and tongs →
forming tools and fabricated anvil → proper sword. Recipes that manufacture an
upgraded provider must use earlier available providers or a stated found-component
route. Welding equipment and its Electronics/Mechanics construction gates are
recorded in the accepted outline below.

### Accepted outline: soldering and welding

Accepted as rough capability and construction bands. Exact advanced processes,
recipes and balance remain for later passes. The user's additional welding
protection requirement is developed below.

| Capability | Tier 1 | Tier 2 | Tier 3 |
| --- | --- | --- | --- |
| Soldering | Basic contact iron; wires, connectors and simple connections | Temperature-controlled fine-work station; delicate circuit assemblies | Complete precision/rework bench, including ordinary soldering equipment; dense electronics and advanced recovery |
| Welding | Makeshift/basic arc equipment; brackets and simple steel frames | Regulated workshop equipment; more demanding structural and vehicle work | Specialized complete welding station; selected advanced material/process requirements |

The tier-3 examples describe complete equipment arrangements that retain lower
tier capabilities. A hot-air tool alone is not a replacement for every contact
soldering operation. Exact advanced processes/consumers need further review before
the corresponding content is implemented.

Accepted equipment construction anchors:

| Equipment | Primary skill | Supporting skill / dependency |
| --- | --- | --- |
| Assembled basic soldering iron | Electronics 2 | Salvaged heater and ordinary hand tools; no soldering prerequisite |
| Controlled soldering station | Electronics 3 | Basic soldering equipment and recovered control components |
| Precision/rework bench | Electronics 5+ | Found specialist components/instruments; exact supporting gates later |
| Makeshift welder | Electronics 3 | Mechanics 2, basic soldering, salvaged power components; no welding prerequisite |
| Regulated workshop welder | Electronics 5 | Mechanics 3 and more demanding electrical tools/components |
| Specialized welding station | Later specialist scope | Found equipment first; reconstruction/manufacturing route decided with its useful consumers |

Manufacturing a welder trains the relevant equipment-building skills; fabricating
a welded product primarily uses Fabrication. For example, a simple rack might
require Fabrication 2 and welding 1, while demanding structural work uses
Fabrication 3–4 and welding 2. The equipment's construction skill does not become
a blanket requirement to operate a found example.

Makeshift equipment should have a concrete drawback, such as greater power
consumption, bulk or narrower capability. Precision and regulated equipment need
their actual operating resources and consumables. Numerical rates remain for
content balance; this outline does not add random failure or durability mechanics.

Implementation dependency: D1 already bills powered handheld crafting tools, but
passive W1 station traits alone do not bill grid electricity. Any powered placed
manual bench needs an explicit funding design/integration before its operating
costs can be considered functional.

Accepted rough Electronics output bands:

- 0–1: basic salvage, leads, connectors and wiring assemblies.
- 2: simple powered tools and devices assembled around functional recovered parts.
- 3–4: makeshift workshop equipment, chargers, sensor/microcontroller assemblies
  and reliable control devices.
- 5–6: precision instrumentation, regulated power systems and complex circuits.
- 7+: selected advanced electronic/robotic hardware, coordinated with the other
  trades and later technology designs.

Electronics owns microcontroller hardware; Computers applies when the task
includes programming or preparing software. Reusing an already functional control
module does not automatically require writing firmware. Diagnostics/measurement
equipment and broader mechanical/electrical product families are recorded below.

### Welding protection

Accepted: welding requires capability `welding_protection` supplied through
ordinary accessible tool traits. Proper use of the equipment during crafting is
implicit. Tier 1 is welding glasses, craftable from three sunglasses; tier 2 is a
welding mask. These are game crafting abstractions. No matching sunglasses,
welding glasses or mask identity currently exists in `data/`.

Accepted rough content outline:

| Equipment | Capability | Construction anchor |
| --- | --- | --- |
| Ordinary sunglasses | No welding protection | Scavenged ingredient for the improvised route |
| Improvised welding glasses/goggles | Welding protection 1 | Three sunglasses plus simple frame/binding materials; Fabrication 1 |
| Welding mask | Welding protection 2 | Found, or assembled from a rigid shell/frame, straps and a suitable welding-filter component; Fabrication 2 |

Exact additional ingredients and item identities remain for a content pass.
Several unmodified sunglasses or several tier-1 items do not add tiers;
the recipe produces a dedicated protective item. A powered/auto-darkening mask
does not automatically need a third protection tier.

Accepted runtime direction:

- Author protection as an ordinary `tool_traits` requirement. Accessible inventory
  (including accessible container contents), nearby ground and reachable storage
  qualify under the existing tool-selection rules. Equipment need not be worn or
  held personally; normal handheld-tool sharing is accepted. Another actor's
  inventory remains subject to the existing access exclusion.
- Specify the required protection for the work: tier 1 for
  welding-1 jobs and tier 2 for welding-2/3 jobs. Using a better welder for the
  same simple job does not itself increase the protection requirement.
- Reuse ordinary tool checks on start and before new funding, including resume
  that buys another slice. Missing protection uses `MissingTools`. Losing access
  after funding preserves that paid slice; explicit reassignment/resume of prepaid
  work follows the existing tool-service rule without a new protection check or
  repayment. New work needs accessible protection again.
- Removing a mask while it remains accessible does not interrupt crafting.
  Capability matching does not change the actor's equipped state.
- Protection is a crafting prerequisite; an unprotected-welding injury system
  is outside this direction.

Example requirement fragment:

```json5
tool_traits: [
  {id: 'welding', tier: 1},
  {id: 'welding_protection', tier: 1},
],
```

This abstraction applies to crafting-specific protective equipment generally:
acquiring the appropriate equipment matters, while manual equipping adds little
decision-making. Character-facing armor, environmental protection and encumbrance
continue to use actual equipment state under their owning systems.

**M-Worn is retired.** This decision supersedes the earlier worn-only rule and its
draft schema, dedicated outcomes, selection changes and save/LAN version proposals.
Existing crafting mechanics support the accepted protection requirement.
Protection consumes one of the four ordinary recipe trait slots; revisit that
limit only if a concrete revised recipe demonstrates a need.

W3B owns the item identities, recipes and acquisition routes. Its content review
must check the required protection tier for each operation that actually performs
welding, including recyclers. Constructing a welder or dismantling a welded
product does not automatically perform welding; each recipe declares its actual
operations independently.

### Accepted outline: measurement and diagnostics

Accepted as rough capabilities, consumers and construction/acquisition bands.
Measurement precision and diagnostic access supply concrete equipment gates for
later work without conflating them with worker skill. Exact items and recipes
remain for future content passes.

| Capability | Tier 1 | Tier 2 | Tier 3 |
| --- | --- | --- | --- |
| Dimensional measurement | Ruler/tape and square; ordinary layout and sizing | Measurement kit adding calipers/micrometers; close fits and precise components | Metrology bench with suitable references/fixtures/probes; selected specialist precision work |
| Electrical testing | Multimeter; basic circuit checks | Electronics test bench including a meter, oscilloscope and bench supply; circuit behavior and controlled testing | Specialized diagnostic station including lower-tier equipment; advanced control/electronic systems |

Higher tiers represent complete useful equipment coverage, not a single instrument
that fails to provide ordinary lower-tier operations. A digital caliper does not
automatically exceed an equally capable mechanical one. Powered equipment can
enable genuinely new measurements or controlled testing; its tier follows those
capabilities rather than merely having a battery.

Representative consumers:

- Measurement 1: ordinary frames, housings and woodworking layouts.
- Measurement 2: fitted shafts, bearing seats, accurate tool components and
  demanding mechanical assemblies.
- Measurement 3: selected precision machine parts, dies and advanced actuator
  components; exact tolerances are gameplay capability gates, not a proposed
  numerical tolerance simulation.
- Electrical testing 1: wiring checks, simple electrical assemblies and the
  proposed makeshift welder.
- Electrical testing 2: regulated power supplies/welders, more demanding sensor
  and microcontroller circuits.
- Electrical testing 3: selected advanced controllers, robotic or bionic
  electronics. It does not replace Medicine for installation or Computers for
  software work.

High diagnostic tier does not grant universal access to proprietary systems.
Target-specific interfaces/software, particularly the agreed military hacking
requirements, remain separate acquisition gates.

Accepted rough construction/acquisition anchors:

- Simple measuring/layout tools: Fabrication 1–2.
- Manufacturing accurate calipers/micrometers: roughly Fabrication 4 with suitable
  precision tools and reference standards. Bundling found instruments into a kit
  is a much simpler acquisition route.
- Multimeter assembled around recovered measurement components: Electronics 3.
- Manufacturing bench-test instruments: Electronics 4–5; arranging found
  instruments into a useful bench is simpler than making their working parts.
- Specialist precision/diagnostic equipment: found industrial/laboratory
  equipment first; later reconstruction/manufacturing around specialist skill
  bands and explicitly acquired components/standards.

Using a found instrument follows the actual work's skill requirement, not its
manufacturing requirement. Many Electronics/Mechanics recipes will naturally
require competence to interpret results; a universal high skill gate just to
possess or use a meter is unnecessary.

Implementation boundary: the current multimeter's D1 grid-inspection role already
works. Generic measurement/testing requirements and any new diagnostic actions
are future content/runtime scope. Passive crafting traits could express some
recipe gates; powered benches need the funding integration noted above.

### Accepted outline: mechanisms and machine tools

Accepted as rough capability, equipment assembly and activity bands. Mechanics
owns fitting, integrating and servicing a working mechanism; Fabrication owns
making its structural and shaped parts. Electrical drives/controllers introduce
Electronics where that work is actually performed.

| Capability | Tier 1 | Tier 2 | Useful output families |
| --- | --- | --- | --- |
| Clamping/workholding | Suitable ordinary clamps | Rigid workshop bench/vise arrangement | Supported assembly and demanding shaping |
| Turning | Basic workshop lathe | Precision lathe setup | Bushings, spacers, shafts and selected close-fitting components |
| Milling | Basic milling setup | Precision milling setup | Slots, mating surfaces, housings and selected gear/tool components |
| Pressing | Bench/arbor press | Larger hydraulic workshop press | Press fits, bearings and heavier component assembly/forming |
| Grinding | Ordinary powered shaping/grinding | Precision surface-grinding setup | Tool preparation and selected accurate mating surfaces |

As with earlier capabilities, a higher-tier arrangement must cover relevant
lower-tier operations. Power alone does not supply accuracy, and computer control
is not automatically a higher machining tier. Manual operation and future
file-driven/autonomous machine work remain distinct workflows.

Accepted equipment assembly anchors, assuming explicitly scavenged functional
components:

| Equipment | Assembly band | Important supplied component |
| --- | --- | --- |
| Hand-cranked drill or simple hand-operated pump | Mechanics 2 | Recovered chuck/gears or pump internals |
| Fabricated bench vise | Fabrication 3, Mechanics 2 | Suitable screw/jaws and stock |
| Bench grinder or drill press | Mechanics 3, Electronics 2 for the electrical assembly | Motor, usable wheel or spindle/chuck, and suitable supports |
| Simple workshop lathe | Mechanics 4, Electronics 2 for the drive assembly | Usable precision bed/guides and spindle components |
| Milling machine or demanding machine rebuild | Mechanics 5, Electronics 3 where required | Suitable precision mechanisms and control/drive components |
| Precision machine-tool construction | Mechanics 6+, with relevant Fabrication/Electronics work | Appropriate measured parts, reference standards and industrial components |

These are assembly/reconstruction routes, not recipes that make a machine from
an arbitrary pile of scrap. Manufacturing a recovered component's replacement can
be a separate, harder recipe in its owning trade. Useful frames, guards and other
subassemblies can have their own Fabrication recipes.

Found working machines skip these assembly requirements. Making a shaft on a
found lathe uses the shaft recipe's Fabrication/measurement requirements; fitting
that shaft into a working drive primarily uses Mechanics. No universal
Mechanics-4 gate applies merely because the lathe's assembly recipe uses it.

Accepted Mechanics activity bands, including future vehicle roles:

| Level | Rough work families |
| --- | --- |
| 0–1 | Straightforward dismantling, bolted assemblies and simple linkages |
| 2 | Hand-operated mechanisms, simple pumps/drills and accessible component replacement |
| 3 | Powered workshop assemblies, drives and ordinary vehicle-system service |
| 4 | Engine/transmission rebuilds from appropriate parts and demanding mechanical integration |
| 5–6 | Precision mechanisms, major machine reconstruction and specialist vehicle systems |
| 7+ | Selected advanced actuators, robotic/bionic mechanisms and futuristic machinery |

These bands reserve space for vehicles without specifying a vehicle simulation
now. Lifting/support equipment, access rules and individual maintenance actions
need the vehicle design. Advanced electrical/software vehicle systems retain
their respective skill and component requirements.

Implementation boundary: passive station capabilities can express some future
recipe gates; [M-Bench](powered_manual_stations_design.md) supplies explicit
station-local grid funding for powered manual providers. A
file-controlled unattended machine belongs to the W4/D3 job/file contracts.
Generic machinery repair and vehicle service are not currently implied by a
crafting tool trait.

### Accepted outline: electrical equipment and power

Accepted as rough product bands, including early salvaged solar,
chemistry-specific batteries and later complex manufacturing. Preserve D1's
common grid and charge accounting. Power
progression comes from storage capacity, available output, demand, fuel and
acquisition rather than a generic "power tier" that arbitrarily excludes devices.

Early building mains, found batteries and generators remain useful. Player-built
packs, generators and future renewables provide alternate supply routes. Larger
workshops can combine sources and storage for demanding equipment; found
industrial equipment can be used before the player can manufacture it.

Accepted product-construction anchors:

| Product family | Skill band | Route / useful role |
| --- | --- | --- |
| Leads, wiring harnesses and simple connection assemblies | Electronics 0–1 | Ordinary stock and hand tools; preserve existing starter wiring/connector routes |
| Basic powered tools/lights and compact battery packs | Electronics 2 | Assemble recovered functional parts/cells and appropriate protection components; soldering iron retains its accepted level-2 anchor |
| Chargers and workshop battery banks | Electronics 3 | More substantial assemblies around compatible cells and charging/control components |
| Sensor/microcontroller devices | Electronics 3–4 | Hardware assembly from available modules; software work belongs to Computers where required |
| Rebuilt small electric motors | Electronics 4, Mechanics 2 | Windings/electrical work plus usable mechanical parts; precision part manufacture remains separate |
| Generator assembled from engine and generator-capable motor/alternator | Mechanics 4, Electronics 3 | Explicit recovered functional assemblies, chassis and controls |
| Basic solar assembly from recovered solar elements and suitable controller components | Electronics 2, Mechanics 1 | Affordable early energy route using scavenged working elements |
| Larger managed solar array | Electronics 3, Mechanics 2 | More substantial installation and control assembly |
| Regulated power equipment and demanding controllers | Electronics 4–5 | Better soldering/testing capability and appropriate power/control components |
| Crude/simple lead-acid cells and dependable workshop batteries | Chemistry 2–3 | Accessible stationary storage; heavy and bulky for its capacity |
| Lithium-ion/polymer cell manufacture | Chemistry 6–7 | Specialist controlled material processing; later than assembling packs from found cells |
| Advanced storage, drives and electronic power systems | Specialist bands, roughly 6+ | Selected future industrial/futuristic products and recovered critical components |

These are construction bands, not new global skill gates for operating or
installing found D1 equipment. A simple pack built from recovered cells does not
require the Chemistry needed to manufacture those cells.

Important boundaries:

- Assembling storage must not create charge. Preserve only compatible recoverable
  input charge under explicit capacity/conservation rules; crafting does not fill
  the completed pack for free.
- A newly manufactured rechargeable storage product needs an authored initial
  charge policy consistent with energy accounting; an empty product is the
  default manufacturing target. Do not reconstruct fuel from generator assembly.
- Cell manufacture follows the actual chemistry and product quality, not the
  rechargeable flag. The earlier blanket Chemistry-5+ gate is superseded by
  chemistry-specific progression.
- Recovered chips, controllers, motors and panels can support modest-skill
  assembly. Producing semiconductors or photovoltaic cells is a separate industrial
  capability, not implied by assembling useful electronics.
- A ready functional controller can be reused without custom programming.
  Firmware/software preparation and hacking tools follow the Computers discussion.
- Solar generation is proposed future engine/content work, not a shipped power
  source. Its output/activation rules need an explicit power design before use.
- New electronic item names do not grant light, sensing, switching or charging
  behavior automatically. Later passes must connect each useful product to its
  actual implemented function or record the required engine work.

Electrical testing 1 supports basic assemblies and the makeshift welder;
testing 2 supports regulated/diagnostic products; selected advanced systems use
testing 3. Soldering and other tool requirements are selected for the actual
operation rather than automatically matching the product's skill number.

#### Early solar and complex manufacturing

Accepted direction: recovered solar elements plus modest skill should provide
an affordable early energy option. A complete found working unit skips its
construction requirements. The Electronics-2/Mechanics-1 basic assembly
uses functional recovered elements and control components; it does not imply
manufacturing photovoltaic material.

Accepted specialist manufacturing anchors:

- Manufacturing suitable individual engine parts: relevant Fabrication/Mechanics
  work around 5–6, depending on the part and available equipment.
- Manufacturing a complete reliable engine from stock: Mechanics around 7–8,
  with appropriate precision-fabricated parts and industrial process equipment.
- Photovoltaic-cell manufacture: specialist material processing around Chemistry
  6–7 and the appropriate electrical expertise/equipment; exact technology may
  move the band. Assembling recovered cells into useful panels remains much earlier.

These do not raise the accepted Mechanics-4 engine-rebuild band: rebuilding with
appropriate existing parts and manufacturing those parts are distinct activities.
Likewise, combining a recovered engine and suitable electric machine into a
generator is substantially easier than manufacturing the engine.

#### Battery roles

Accepted rough battery ladder:

- Chemistry 2: crude lead-acid cell, a simple and inefficient but useful stationary
  storage option.
- Chemistry 3: dependable lead-acid workshop/vehicle battery.
- Electronics 2–3: packs/banks made from compatible recovered cells, including
  lithium cells, with suitable electrical/control components.
- Chemistry 6–7: manufacturing ordinary lithium-ion/polymer cells with controlled
  processes and specialized equipment.
- More advanced chemistries: later specialist/industrial scope, chosen when their
  actual gameplay role is defined.

Lead-acid should remain useful through accessible materials/construction and
stationary storage. Lithium's advantage is compact, lighter storage, with harder
cell manufacture; finding usable lithium cells is therefore valuable. Represent
these differences through mass, volume, capacity, inputs and acquisition.
Preserve D1's battery accounting and absence of a battery discharge-rate limit;
chemistry does not silently add a new power simulation.

### Accepted outline: Tailoring and textile equipment

Accepted as rough capabilities, product bands and equipment-construction routes.
Tailoring constructs textile/leather products and fitted soft equipment. Recovered
fabric, clothing, webbing and fasteners support useful early work; manufacturing
specialist fibers is a separate process.

| Capability | Tier 1 | Tier 2 | Tier 3 |
| --- | --- | --- | --- |
| Sewing | Serviceable needle/basic hand-sewing tools | Complete sewing kit with suitable needles and accessories | Specialist textile workstation, including appropriate heavy-duty/technical sewing equipment and ordinary hand tools |
| Leatherworking | Awl, suitable knife and basic leather tools | Leatherworking set with punches, riveting/finishing tools | Leave open until a specific process demonstrates a need |

Use the existing general cutting capability for simple textile cutting.
Ordinary measuring/layout tools support garment patterns and fitting. A high
Tailoring level does not automatically require higher metrology tiers intended
for precision machine components.

Ordinary clothing, backpacks and useful padded/leather protection remain
achievable with a tier-2 hand kit and suitable materials. Reserve sewing 3 for
selected demanding technical materials/seams, not every high-skill garment.
An ordinary domestic sewing machine is not automatically tier 3. Any proposed
time/efficiency benefit needs an explicit recipe/runtime implementation; current
capability tiers alone do not alter work speed.

Accepted Tailoring output bands:

| Level | Rough output families |
| --- | --- |
| 0–1 | Patches, bindings, crude sacks and basic textile preparation |
| 2 | Simple garments, belts, straightforward bags and basic leather goods |
| 3 | Reliable everyday/work clothing, durable backpacks, sleeping gear and useful padded protection |
| 4 | Fitted protective clothing, layered soft protection, armor carriers and load-bearing rigs |
| 5–6 | Technical protective garments, demanding constructions and specialized material combinations |
| 7+ | Selected advanced technical fabrics and post-cataclysm material equipment |

A straightforward item made from recovered advanced material can remain below
the specialist bands. Skill follows the actual construction difficulty; unusual
material does not automatically make every simple bag an expert recipe.

Accepted tool/equipment acquisition anchors:

- Assemble a usable sewing kit around found needles/accessories at Tailoring 1–2.
  Manufacturing each cutting or piercing tool is separate Fabrication work.
- The current `sewing_supplies` identity is a natural candidate for sewing 2;
  keep thread and other spent materials explicit recipe inputs.
- Assemble/rebuild an ordinary sewing-machine arrangement from a recovered
  mechanism at Mechanics 3, with Electronics 2 where electrical work is needed.
- Assemble a specialist textile workstation around industrial equipment at
  Mechanics 5 / Electronics 3 as appropriate; the exact process can change this
  band. A found working station skips construction.

Material and trade boundaries:

- Survival can harvest/prepare natural or post-cataclysm hides and fibers.
- Tailoring turns suitable prepared materials into textile/leather products;
  spinning/weaving can be reviewed as a later textile-production branch.
- Chemistry owns demanding material treatments and synthetic-material production
  where a recipe actually performs those processes.
- Fabrication supplies rigid armor plates/frames; Tailoring supplies carriers,
  padding and straps. Complex finished equipment can combine separately made
  components rather than requiring every trade for every incidental step.

Balance through practical roles: carried volume, mass, coverage, protection and
encumbrance, plus real supported utility. Higher-tier clothing need not simply
have a larger armor number.

Current-content boundary: `sewing_supplies`, `sewing_thread`, cloth/leather stock,
tool-belt/duffel recipes (Tailoring 1) and a padded-jacket recipe (Tailoring 2)
already exist. The broader bands above are future authoring targets, not silent
changes to those recipes. Armor/storage can use existing systems; new repair,
refitting, weather or special-material effects need their own implemented support.

### Accepted outline: Survival and field production

Accepted as rough capabilities, activity bands and resource roles. Survival owns
field resource knowledge, low-infrastructure preparation and selected primitive
production methods. Its later bands extend into post-cataclysm biological resources.

| Capability | Tier 1 | Tier 2 | Tier 3 |
| --- | --- | --- | --- |
| Biological harvesting | Suitable knife/basic cutting tools | Butchering/skinning kit with appropriate knives, scrapers and saws | Specialized extraction kit including fine instruments and ordinary harvesting tools |
| Wood chopping | Hatchet or suitable crude axe | Full-size axe or equipment appropriate to larger timber | Leave open until a useful consumer demonstrates a need |
| Hide preparation | Scraper and suitable stretching/drying arrangement | Equipped hide-processing station | Advanced material processes reviewed individually rather than adding an automatic tier |

These are capability groups to refine, not final schema IDs. Complete higher-tier
kits retain lower-tier tool coverage. A fine instrument alone does not replace
the larger cutting tools needed for ordinary butchering.

Accepted Survival activity/output bands:

| Level | Rough families |
| --- | --- |
| 0–1 | Basic field preparation, tinder, simple bindings/cordage, crude field cutting tools |
| 2 | Useful primitive tools, shelter components, ordinary hide/fiber preparation and charcoal production |
| 3–4 | Reliable field-material processing, durable low-infrastructure equipment and the first useful unusual-creature materials |
| 5–6 | Specialized harvesting/preparation of tough hides, shells, strong fibers and other difficult post-cataclysm resources |
| 7+ | Delicate or uncommon biological components for selected advanced material, chemical and medical applications |

Potential resource roles, pending actual creature/item design:

- Unusual hides or membranes → Tailoring's protective garments.
- Hard shell plates → Fabrication's armor components, then tailored carriers.
- Strong fibers/sinews → cordage, bindings and specialist textile components.
- Specialized secretions or biological reagents → Chemistry/Medicine processes.

These examples define useful downstream roles without approving creature species
or special effects. Advanced Survival is not a universal substitute for biological
research, Chemistry or Medicine.

Accepted harvesting direction:

- Ordinary useful resources remain available at modest skill with suitable tools.
- Higher skill/tools unlock particular difficult parts or preparation routes,
  rather than only adding a universal loot-quantity multiplier.
- The player's harvesting skill does not change the requirement to kill or
  otherwise acquire the source; resource extraction is a separate activity.
- Finding already prepared material skips the harvesting/preparation gate.
  Tailoring/Fabrication recipes need not also demand the Survival skill that
  would have been needed to obtain that ingredient personally.

Skill ownership follows the work/method: preparing a crude natural-material
field tool can use Survival; making a reliable steel tool belongs to Fabrication.
Harvesting and field preparation belong to Survival, preparing meals belongs to
Cooking, and specialized material/reagent processing belongs to Chemistry.

Future foraging, tracking and field use of traps can draw on Survival as their
systems are designed. Constructing an engineered trap may instead involve
Fabrication/Mechanics; operation and manufacture remain separate activities.

Charcoal should be a useful Survival-2 production route from wood when its process
is implemented. It must not require a charcoal-fueled forge to produce the first
charcoal. Found charcoal remains an independent acquisition route. Existing W2
forge ignition retains its accepted abstraction; a future field-fire system does
not silently add new ignition prerequisites to it.

Implementation boundary: harvesting/depletion, camp processes, shelter benefits,
food/water use and unusual material effects require explicit content/runtime
support. This outline reserves progression roles; a new trait or descriptive item
flag alone does not implement those actions.

### Accepted outlines: Cooking and Chemistry

Cooking's output bands, equipment direction and maximum level of 5 are accepted.
Cooking owns meals and food preservation; Chemistry owns chemical processing,
reagents and material production. Chemistry retains the default maximum of 10.
Chemistry's rough product and expanded equipment outline is also accepted,
including centrifuges and pH/pressure meters. Exact recipes, provider tiers and
process runtime remain for later design/content passes.

#### Accepted outline: Cooking

| Level | Rough output families |
| --- | --- |
| 0 | Assemble ready-to-eat ingredients; simplest boiling/roasting preparations |
| 1 | Reliable simple meals, soups and straightforward cooked ingredients |
| 2 | Ordinary bread/baking, varied everyday meals and basic drying/salting/pickling |
| 3 | More demanding meals, controlled food fermentation and dependable preserved travel food |
| 4 | Professional cooking/baking, complex preparations and well-planned ration recipes |
| 5 | Selected specialist cuisine, demanding preservation and unusual-ingredient preparations |

These are overlapping authoring bands, not a rule that every preservation method
needs level 3 or every unusual ingredient needs level 5. Survival can provide a
prepared ingredient; the meal's skill follows the actual cooking work.

Food preparation 1 can come from suitable basic cookware and utensils; tier 2
is a complete kitchen set, including ordinary cookware and baking/preparation
equipment. Useful meals remain possible with camp equipment. No third generic
kitchen tier is needed until a specific operation justifies it.

Preservation equipment follows the method: a drying/smoking arrangement, suitable
sealed vessels or a pressure-canning setup are distinct capabilities, rather
than interchangeable rungs on one preservation ladder. Recipes state which
equipment and operating heat they need. Found cookware is an early acquisition
route; manufacturing vessels or building powered appliances belongs to the
appropriate construction trade.

Food variety, ingredient use, portability and shelf life should give Cooking
practical roles. Nutrition, spoilage, taste/morale and other benefits need explicit
gameplay support before content promises them. Extended fermentation/drying can
be considered in the process-machine pass: separate active preparation from
elapsed processing, without granting practice simply for waiting.

#### Accepted outline: Chemistry and laboratory equipment

| Level | Rough output families |
| --- | --- |
| 0–1 | Simple mixtures from prepared ingredients and basic material preparation |
| 2 | Simple soaps/adhesives and crude lead-acid cells |
| 3 | Dependable lead-acid batteries, ordinary controlled distillation and useful material treatments |
| 4–5 | More demanding reagents, resins/polymers, synthetic fibers and workshop chemical products |
| 6–7 | Lithium-ion/polymer cell manufacture, photovoltaic material processing and other specialist controlled processes |
| 8–10 | Selected advanced industrial/futuristic compounds and post-cataclysm material applications |

These rough product bands include the earlier battery and photovoltaic anchors.
Exact chemistry matters: using recovered resin
or mixing prepared compounds can be much easier than synthesizing their inputs.
Specialist skill still needs the appropriate materials, equipment and knowledge.

Accepted equipment direction: advanced Chemistry needs a collection of concrete
instruments and process equipment. Centrifuges, pH meters and pressure meters are
explicit user examples. The expanded operation families below are accepted as
an equipment outline.

This is an equipment inventory, not a decision to add one trait per instrument.
Choose interchangeable providers and useful tiers around representative recipes.

| Operation | Representative equipment | Useful work |
| --- | --- | --- |
| Measuring and dosing | Balances, graduated vessels, pipettes, burettes; later analytical balances and precise volumetric equipment | Repeatable reagent quantities, solutions and formulations |
| Acidity/conductivity measurement | Indicators, pH meter with suitable probe, conductivity meter | Selected treatments, process monitoring and solution checks |
| Pressure/vacuum measurement | Pressure gauges, vacuum gauges and appropriate sensors | Monitoring pressure/vacuum processes; measurement is distinct from producing those conditions |
| Thermal control and mixing | Thermometers, hotplates, stirrers, heating mantles, controlled baths, drying ovens and chillers | Controlled reactions, mixing, drying and temperature-sensitive work |
| Centrifugation | Bench centrifuge; later controlled/refrigerated models with suitable rotors | Separating suspended solids or biological fractions, including delicate reagents |
| Filtration and extraction | Funnels, filters and separatory vessels; later vacuum filtration and appropriate membrane equipment | Clarifying products, separating phases and recovering useful fractions |
| Distillation | Simple still/condenser; controlled fractionating setup | Solvent recovery and suitable chemical separations |
| Pressure/vacuum processing | Suitable pumps, vessels, vacuum chambers and controlled reactors | Selected syntheses, treatment and drying processes |
| Controlled workspaces | Fume extraction, inert-atmosphere glovebox, clean workspace or dry enclosure as required | Processes needing particular containment, atmosphere or contamination control |
| Chemical analysis | Appropriate test kits, microscopy, chromatography or spectrometry equipment | Checking particular reagents, identifying useful fractions and verifying demanding products |
| Electrochemical processing | Controlled supply, compatible vessel and appropriate electrodes | Selected deposition, material preparation and cell-manufacturing processes |

Different instruments measure different properties. A pH meter does not stand in
for a pressure gauge, and a gauge does not provide a pump or pressure-capable
vessel. Likewise, a clean workspace, fume extraction and an inert enclosure serve
different processes; they are not automatically ascending tiers of the same trait.
Analysis equipment must support the particular test rather than confer universal
identification of every substance.

An integrated apparatus can supply several relevant capabilities when it actually
includes those instruments. A higher tier retains applicable lower-tier coverage;
capacity, precision and temperature control only create tiers when useful recipes
justify the distinction. Numerical rotor speeds, pressures and pH simulation are
not required to make these equipment gates meaningful.

Recipes require the relevant subset, not every instrument in the laboratory.
Representative consumers:

- A selected resin formulation can need accurate dosing, controlled heat and
  stirring; acidity measurement applies when its particular process needs it.
- Preparing an unusual biological reagent can need centrifugation, temperature
  control and an appropriate analysis step. Survival obtains the source;
  Chemistry processes it; Medicine may use the prepared product.
- A selected lithium-cell material process can need precise preparation, drying
  and controlled-atmosphere equipment. The exact chemistry determines the
  apparatus, preserving the accepted Chemistry-6–7 manufacturing band.

Rough acquisition progression: basic vessels, filters and simple measurements at
Chemistry 0–2; useful instrument-equipped processes around 3–4; increasingly
specialized control, separation and analysis at 5+. These are illustrative work
bands, not universal skill gates for using a found instrument. A centrifuge or
meter can be useful before the character has an entire advanced laboratory.

Suggested acquisition/construction anchors:

- Collect a basic usable chemistry kit from found equipment around Chemistry 1–2.
- Assemble a simple still from suitable recovered vessels and condenser parts
  around Fabrication 2 / Mechanics 1; the process's Chemistry requirement remains
  separate from the apparatus construction.
- Assemble a basic centrifuge from an appropriate recovered rotor, drive and
  enclosure around Mechanics 3 / Electronics 2. Manufacturing precision rotors or
  rebuilding demanding high-speed/refrigerated equipment is separate later work.
- Assemble a meter around a recovered functional sensing module around Electronics
  3–4; making the sensor itself can be much harder. Its measurement range and
  suitable probe remain part of the actual supplied capability.
- Found laboratory glassware, instruments and process equipment provide valuable
  shortcuts. Laboratories, hospitals and industrial facilities are candidate
  acquisition contexts. Manufacturing glassware, pumps, heaters and electronic
  instruments uses the relevant trades and equipment rather than Chemistry alone.

Appropriate filters, probes, reference materials and other supplies can matter
where a process consumes or specifically needs them. Exact consumable requirements
belong to the selected recipes. Powered instruments need explicit operating-energy
integration; a capability trait alone does not charge their power use. Suitable
timed separation/processing jobs can use the queued unattended-machine design.
A forge's heat trait does not automatically provide controlled chemical heating.
Selected sophisticated chemical equipment can additionally need the
[production software](#accepted-direction-production-software) described above.
Hardware capabilities, appropriate software and the process's Chemistry skill
remain distinct requirements.

Chemistry prepares chemical products and treated materials; Tailoring/Fabrication
turns them into finished equipment, Electronics assembles cells into packs, and
Medicine governs clinical use. Familiar food fermentation/preservation belongs to
Cooking; producing a chemical feedstock belongs to Chemistry. Similar equipment
can support both skills without making both mandatory for every recipe.

Gasification and fuel-gas machinery retain their separate queued design; exact
construction/setup skills are still open. Chemical processing supplies a useful
application of selected unattended machines, with actual setup work distinct from
elapsed runtime.

Implementation boundary: shipped Cooking and Chemistry are separate, with Cooking
capped at 5 and validated legacy proficiency migrated into both.
Food preservation and chemical processes need their own material, container,
heat and action support; this discussion does not specify a general chemistry
simulation or change the existing power/workshop contracts.

### Accepted outline: Medicine, clinical equipment and bionics

Accepted as rough activity bands, equipment roles and bionic-installation anchors.
Medicine owns clinical preparation, diagnosis, treatment and surgery. Manufacturing
the drugs, instruments or implants uses the relevant material/equipment trades.
Found supplies and working medical equipment remain valuable shortcuts. Retain
the default Medicine ceiling of 10.

| Level | Rough activity/output families |
| --- | --- |
| 0–1 | Basic care with suitable found supplies, simple dressing preparation and organizing first-aid supplies |
| 2 | Field-care kits, routine wound care and using appropriate immobilization supplies |
| 3 | Field-medic preparation, more demanding wound care and prepared procedure supplies |
| 4 | Professional clinical care, supported diagnosis and selected minor procedures |
| 5–6 | Surgery, specialist clinical preparation and selected simpler bionic installations in others |
| 7–8 | Demanding surgery, complex bionic installation and specialized biological treatments |
| 9–10 | Selected frontier reconstructive/bionic procedures and futuristic medical applications |

Accepted anchors: ordinary surgery around Medicine 5–6; installation of a
relatively straightforward prepared bionic around 6; complex/neural-interface
procedures around 7–8. These are difficulty bands, not one gate for every implant.
These anchors apply to installing bionics in others; self-operation and robotic
assistance retain their separate future design.

Preparing a supply, manufacturing its ingredients and treating a patient are
different activities. Cutting cloth into a bandage can remain simple textile
work; its use follows the treatment action. Manufacturing a rigid splint can use
Fabrication, while applying it belongs to Medicine. Preparing a first-aid kit
must not impose its assembly requirement on every use of a found dressing.

Accepted equipment inventory:

| Operation | Representative equipment | Useful role |
| --- | --- | --- |
| Medical instruments | Basic scissors/tweezers; complete surgical instrument set; specialist fine-instrument set retaining ordinary coverage | Increasingly demanding manipulation and clinical procedures |
| Diagnosis and monitoring | Thermometer, stethoscope, blood-pressure gauge, patient monitors and appropriate sample tests | Particular examinations and monitoring during supported procedures |
| Sterilization | Suitable medical sterilizer/autoclave, compatible packaging and process supplies | Preparing instruments and materials that require sterile handling |
| Supported clinical work | Illuminated treatment surface; equipped operating station for demanding procedures | Patient access, positioning and procedure support |
| Magnification and imaging | Surgical magnification, ultrasound or other appropriate imaging equipment | Selected precise procedures and diagnostic tasks |
| Bionic interfacing | Compatible implant interfaces, testing/configuration equipment and required software | Preparing and checking selected installations |
| Robotic assistance | Autodoc or surgical robot with appropriate tools, supplies and procedure software | Separate future assisted/autonomous medical design |

Medical-instrument tiers 1/2/3 can follow basic hand instruments, a complete
surgical set and a specialist set respectively, provided higher-tier equipment
retains ordinary coverage. Diagnosis, sterilization, imaging and implant
interfacing are separate operations. A monitor does not replace every diagnostic
instrument. Basic care only requires instruments where the actual action needs
them; applying a found simple dressing need not require a complete medical kit.

Scavenged sterile supplies provide an early route independent of building a
sterilizer. Authored preparation processes must distinguish ordinary clean
materials from sterile outputs. The eventual medical design decides how much
cleanliness/sterility state is useful; this outline does not prescribe a detailed
contamination simulation.

Acquisition and trade boundaries:

- First-aid supplies and useful portable instruments come from household,
  pharmacy and clinical salvage. Hospitals and specialist facilities supply
  advanced instruments, prepared implants and relevant information/software.
- Assembling a basic first-aid kit from appropriate found supplies can sit around
  Medicine 1–2; assembling/preparing more demanding procedure supplies around 3–4.
- Tailoring/Fabrication supplies dressings, supports and instruments. Chemistry
  manufactures chemical medical supplies and pharmaceutical ingredients;
  Medicine owns clinical preparation/use where those tasks need medical knowledge.
- Electronics/Mechanics constructs monitors, powered equipment and bionic
  hardware. Installing a complete prepared implant does not automatically require
  the skills used to manufacture its circuitry or mechanisms.
- Computers can recover, unlock and configure equipment software. Selected
  sophisticated medical systems may need compatible packages, while human
  surgical competence remains a separate requirement for human-performed work.
- Building an autodoc, operating one and receiving treatment from one need
  distinct requirements in the future design. Self-operation and the amount of
  skill a robot can substitute for remain deferred.

Medicine should have useful preparation recipes and later practice through
actual care/procedures. Higher training bands need appropriate study sources.
The medical-action design defines learning from performed work and its training
ceilings; exact patient-action XP rules remain for that pass.

Current-content boundary: `cloth_bandage`, `sterile_gauze` and `antiseptic` exist,
and `cut_cloth_bandage` trains Tailoring 0. The `medicine` category flag explicitly
does not imply a treatment effect. These identities and existing clinic/hospital/
pharmacy acquisition contexts are starting points for later functional content.

Implementation dependency: treatment and surgery need patient-state, resource,
duration, interruption and outcome rules of their own. Ordinary item crafting
and autonomous manufacturing contracts do not establish surgical behavior.
Sterilization, monitoring/imaging, bionic effects and robotic treatment require
explicit functional support. This is an accepted progression outline awaiting
the relevant runtime design.

### Queued design: selected unattended process machines

Accepted direction: a limited set of machines consumes specified inputs,
performs elapsed work without a player continuously operating it, and produces
specified outputs. This supplements the predominantly manual tool-based crafting
game. Reserve a separate process-machine design pass alongside W4/D3.

Required candidate processes:

| Process | Rough inputs/conditions | Intended useful output |
| --- | --- | --- |
| Coal gasification | Coal + water + process heat | Synthesis/fuel gas for an adapted gasoline engine |
| Wood-gas production | Wood + heat in an oxygen-excluding arrangement | Wood fuel gas for an adapted gasoline engine |

The oxygen-free wood route is a pyrolysis/retort process. Its relationship to
charcoal production is useful: a later design should decide whether charcoal and
other byproducts are explicit useful outputs or abstracted waste. This terminology
does not require a detailed chemistry simulation.

Fuel-gas use should involve a modest engine conversion/adapter rather than
manufacturing a new engine. The exact conversion work, gas cleanup abstraction,
usable output and engine tradeoffs belong to the queued design.

That pass must settle:

- Represented coal/wood/water units, gas storage/transfer and generator fuel
  compatibility; current generator definitions have one authored fuel item.
- Heat source, startup versus running costs, throughput and mass/energy accounting.
  A Ready manual forge does not automatically become a free autonomous heat supply.
- Whether a process has one or several useful products, output capacity and input
  reservations, cancellation and interrupted work.
- Whether jobs remain explicit single batches or optionally repeat while supplied.
  The current W4 contract specifies one job with no implicit repeat queue;
  unattended operation alone does not change that contract.
- Offscreen progress, residency, save/LAN state and frontend operation.

W4's planned selectable jobs and D1 funding/residency are the integration starting
points. Thermal/fuel-driven work, stored gas and possible multiple outputs require
explicit contract review: the present W4 recipe shape has one output and positive
grid-energy demand. No gasifier, retort or new process runtime is implemented here.

### Accepted printer progression direction for D3

The user explicitly separates operating a printer from manufacturing it:

- Basic printing makes simple plastic parts with very little Computers skill.
- Higher Computers skill enables more demanding/durable results with suitable
  designs, material and equipment.
- Advanced printers produce metal parts and potentially more advanced materials.
- **Printing itself requires Computers, not Fabrication.** Machine capability,
  appropriate feedstock/parts, information and operating supply remain meaningful
  requirements; accepted rough Computers bands are recorded below.
- Selected intricate printer recipes require the appropriate schematic, distinct
  from having the printer's production/control software.
- Constructing a printer can be difficult and require appropriate
  Mechanics/Electronics and manufactured parts. These construction requirements
  do not become requirements for using a found operational printer.
- Basic operational printers should be reasonably easy to scavenge in this
  futuristic setting. Advanced machines retain meaningful specialized acquisition
  contexts to be chosen with D3/D4.

Accepted first scope: a contemporary home polymer printer with mostly simple,
supplied/built-in recipes requiring no separately acquired schematic. Its first
playable example uses those supplied designs. Ordinary file copying/import stays
in D3-A; design/schematic extraction and hacking laptop modifications wait for a
separate hacking effort. Do not bridge that gap with temporary schematic/software
crafting recipes or placeholder unlock items. The earlier recovered-schematic
demonstration is deferred; intricate designs remain a later progression goal.

Printer recipes are deliberate alternative production routes. A curated printable
part can bypass its manual Fabrication/tool gates through Computers, the right
machine and a usable design supplied with the equipment or acquired separately.
Auditing legacy crafting bypasses must not
accidentally impose the hand-forging skill gates on this accepted machine route.
Using the printed part in a larger mechanism can still require that separate
assembly recipe's Mechanics/Electronics skills.

D3 must distinguish ready-to-use printed products from parts requiring subsequent
assembly. Any necessary processing for a promised ready-to-use printed product
must not introduce a hidden mandatory Fabrication gate. Feedstock preparation and
machine construction can have their own trades, with found/prepared inputs
available to the printer operator.

The [accepted D3 learning distinction](digital_fabrication_design.md#5-production-modes-and-learning)
supersedes the blanket no-passive-operator-XP rule: explicitly commissioned batches
award the initiating character Computers learning when they finish; continuous
production grants none. Nominal credit follows recipe executions, not Start count
or elapsed waiting, and advanced recipients receive practice under the shared
learning model. D3 defines setup/preparation work, file access/transfer, curated
designs and material tiers; the later hacking effort owns extraction/recovery.
Selected advanced printers may also need compatible production software and
material/process profiles, under the shared software direction above. Product
designs and control software have different roles even when acquired together.
This is an accepted progression direction, not a completed printer runtime design.

#### Schematics for intricate prints

Accepted requirement: some intricate printer recipes need a specific usable
schematic. Here a schematic is machine-readable product-design information for
the supported print route. Control software enables the machine/process; the
schematic supplies the design for the particular product. A package can include
both, while other acquisitions supply only one.

Accepted acquisition/eligibility outline:

- Simple prints can use supplied/built-in designs, preserving the early
  low-skill printer route. Later authoring identifies which recipes need a
  separately recovered schematic.
- Intricate designs can become loot in factory engineering archives, research
  systems, portable media or surviving machine/controller storage. Finding an
  operational printer with useful designs already loaded remains valuable.
- Illustrative consumers include a specialized gear set, precision actuator
  housing or bionic component casing. These are candidate printed parts, not an
  approved item roster or a claim that printing a casing creates a complete bionic.
- A schematic is reusable information. Acquiring it can enable repeated supported
  prints while material and operating costs are paid normally.
- Access/recovery shares the D3 information workflow. Explicit import retains the
  design in the machine's local library; startup captures the job's instructions,
  so resume does not need the carrier or library copy. Source protection and
  design/schematic extraction remain deferred to the separate D3-C/hacking effort.
- Higher Computers skill supports preparation and operation; it does not
  automatically supply a missing design. Knowing a manual crafting recipe is
  distinct from having the machine-readable schematic.

For a gated print, check the schematic alongside machine capability, appropriate
control software/profile, operator setup skill, feedstock and operating resources.
The schematic requirement preserves the Computers-only printer-use boundary.
Exact eligibility, transfer and persistence rules belong to D3/W4.

### Accepted outline: Computers and digital production

Accepted as rough numerical bands and equipment/construction anchors, alongside
the software acquisition, copy-protection/recovery and printer-use directions
above. Computers owns information handling, software preparation, recovery and
digital equipment setup; retain the default ceiling of 10.

| Level | Rough activity/output families |
| --- | --- |
| 0–1 | Read accessible digital information, copy ordinary files and run prepared simple polymer prints |
| 2 | Install/configure ready compatible software, prepare ordinary software-tool media and set up more useful polymer prints |
| 3–4 | Recover software from readable controllers, use suitable tools against selected ordinary copy protection, configure workshop production systems and operate prepared metal-printing setups |
| 5–6 | Demanding data recovery/protection bypass, adapt complex production packages and prepare specialized manufacturing jobs |
| 7–8 | Selected advanced industrial/military systems, specialist controller integration and demanding frontier-material workflows |
| 9–10 | Selected exceptional software adaptations and futuristic system integration |

Hacking difficulty follows the actual source/protection. Illustrative anchors:
ordinary store/household systems around Computers 2–3 with the right acquired
tool software; suitable protected production-package recovery around 3–5;
demanding industrial recovery around 5–6; selected advanced/military work around
7–8. These are content targets, not an automatic success rule based on location.
Military targets retain the agreed specific hardware/software requirements.

Copying a readable unprotected package from a mechanically broken machine can
remain low-skill work once access is established. Damage does not itself mandate
expert Computers; recovery difficulty follows the surviving interface/data and
protection. Physical access or controller reconstruction can require Electronics
as a distinct step.

Accepted digital-production anchors:

| Work | Computers band | Equipment/information assumption |
| --- | --- | --- |
| Simple polymer parts | 0–1 | Working basic printer and a ready compatible design/profile |
| Durable functional polymer parts | 2–3 | Suitable feedstock, capable printer and appropriate design/settings |
| Ordinary metal parts on a prepared industrial printer | 3–4 | Compatible complete control software, ready design/process profile and suitable feedstock |
| Specialized print preparation or production-software adaptation | 5–6 | Appropriate hardware and acquired technical information/software |
| Selected advanced-material/custom-control work | 7+ | Specific machine/process capability and specialist packages |

Skill follows the setup/preparation actually performed. A ready industrial
workflow can be easier to operate than creating or adapting its process profile.
Metal printing can remain an advanced acquisition objective through the machine,
feedstock, information and power requirements while using a moderate operation
skill band. Computers skill alone does not make weak material or an unsuitable
design produce a durable part.

Accepted equipment inventory:

| Operation | Representative equipment | Boundary |
| --- | --- | --- |
| Information access | Compatible reader or computer with the relevant archive | An electronic reader's reading role does not imply general software execution |
| General software work | Working laptop/workstation or suitable terminal | Supports compatible installed tools; acquisition can precede hardware manufacture |
| Transfer and recovery | Portable media, appropriate readers/adapters and data connections | Access depends on the actual source/interface |
| Controller service | Compatible programming/service interface and appropriate software | Hardware-specific access remains meaningful |
| Production preparation | Workstation or integrated machine controller with the needed preparation/control package | A machine may include its own usable interface; no universal separate-PC requirement |
| Specialized target access | Target-appropriate hardware and software, including military examples | A faster ordinary computer does not automatically provide the missing interface or package |

These are functional roles, not a universal ascending computer/hacking-tool tier.
Some actions use ordinary hardware with valuable software; others genuinely need
a particular interface or processing capability. Use curated software operations
and supported production designs rather than requiring player-authored code or
an open-ended CAD/programming simulation.

Accepted rough hardware-assembly anchors, assuming suitable recovered functional parts:

- Ordinary computer assembly around Electronics 2, with simple software setup
  around Computers 1 as a separate task.
- Basic printer assembly around Mechanics 4 / Electronics 3, using suitable motion
  components, controller and print mechanism.
- Industrial/advanced printer reconstruction around Mechanics 6+ / Electronics
  5–6, with the actual technology determining the recipe and critical components.
- Manufacturing processors, precision motion parts or print heads is separate
  specialist work. Found working machines skip their construction requirements.

Learning comes from meaningful software preparation/recovery/configuration and
successful commissioned batches under D3. Continuous production and ordinary
copying/importing are XP-free. Completed batches credit the initiator once,
including offscreen; advanced bands still need appropriate theory sources.
The initial automated-learning targets are 100% of the authored budget for
training levels 0–4 and 80% for 5+, subject to content tuning. Shared teaching
ceilings, boosts and theory/practice accounting remain in force.

Implementation boundary: the existing ebook reader/electronic books and computer
salvage are starting content. General software access, copying, installation,
protected recovery, hacking and printer operation require D3 and related runtime
work. Exact file/media representation, job eligibility, power/access interruption,
failure outcomes and persistence remain for those subsystem designs.

## 7. Current evidence and next discussion

Read-only audits found eight noncombat skills and nine combat skills in
`data/skills/core.json5`. Most authored crafting is ungated or at levels 1–2;
salvage has broader coverage than manufacturing.

The only authored tool traits in the shipped catalog are `house_cutting`,
`hammering` and `soldering`, all currently provided at tier 1. W1/W2 implement
placed capabilities and fueled manual work, but the W3 anvil/forge/welding roster
has not been authored. Digital fabrication remains pending.

The shared skill meanings, Cooking/Chemistry split, hand-tool set tiers, rough
construction progression, metalworking and soldering/welding outlines are agreed
directions. Welding protection tiers, ordinary accessible/shared-tool semantics
and existing paid-work behavior are accepted, as is the measurement/diagnostics
outline. Mechanisms, machine-tool capabilities and Mechanics bands are also
accepted. Electrical/power product bands, early solar, chemistry-specific batteries
and specialist manufacturing anchors are accepted, as is the Tailoring/textile
outline. Survival/field production is accepted. Selected process automation
(coal gasification and oxygen-free wood-gas production) is queued for its own
design; Computers-only printer operation and accessible scavenged printers are
recorded requirements for D3. Cooking's bands, equipment and level-5 ceiling are
accepted, as is Chemistry's expanded equipment/product outline. Cross-domain
production software, package/compatibility/use direction, copy protection and
recovery from broken equipment are accepted. Detailed access/recovery actions
belong to D3 and the hacking design; ordinary copying after obtaining an unlocked
portable package is accepted. Medicine's activity/equipment bands and bionic
installation anchors are accepted, as are the Computers/D3 numerical and equipment
bands. Selected intricate prints require schematics; the supplied-versus-recovered
design outline is accepted. The [delivery plan](crafting_delivery_plan.md) now
maps cross-domain prerequisites and separates mechanics/content passes,
including a W3A forging / W3B electrical-welding revision. The user accepted that
delivery direction and retired M-Worn in favor of ordinary crafting traits.
The [W3A/W3B implementation record](workshop_rosters.md) now owns the authorized
rosters, concrete budgets, learning/acquisition links and verification.
Remaining capability details are assigned to their first consumers.
This document remains the progression framework rather than the implementation log.
