# D3: digital information and fabrication

**Status: Home-printer scope, ordinary information handling, machine libraries,
job capture and production learning accepted; detailed runtime design pending.**

This document develops D3-A/B from the
[crafting delivery plan](crafting_delivery_plan.md), using the accepted
[progression framework](crafting_progression.md) and
[W4 machine contract](workshop_design.md#7-autonomous-machine-contract-for-d3).
D3-C is deferred to a separate later hacking design; only its future integration
direction is recorded here.
The rules below are design decisions, not implemented or verified mechanics.
Schemas, API shapes, action costs and the first printing roster remain to be
reviewed before implementation.

### Current scope: an ordinary home printer

Use a contemporary home polymer 3D printer as the first equipment example. Most
of its recipes should be simple and directly available through supplied/built-in
designs, without acquiring a separate schematic. The first playable demonstration
uses those supplied designs and control software; it needs no USB import or
schematic-extraction route to become useful.

Ordinary file storage, copying and explicit import remain part of D3-A. Hacking,
design/schematic extraction and protected/damaged-data recovery wait for the
later hacking effort. The earlier proposed recovered-schematic demonstration is
deferred with them. Do not add temporary "craft a schematic/software package"
recipes or substitute unlock items merely to bridge that missing acquisition route.
Acquired intricate designs remain a later progression goal.

## 1. Information and carriers

Use **schematic** as the general player-facing term for technical instructions,
with domain-specific names such as *sewing pattern*, *chemical formula* or
*manufacturing blueprint* where appropriate.

Distinguish information by what it enables:

| Role | Meaning |
| --- | --- |
| Human-readable instructions | Digital equivalent of recipe instructions in a book; usable as a reference and through the relevant learning rules |
| Machine-readable design | Product/process information that a compatible machine can execute |
| Production software | Compatible control software and any required process/material support |

A recovered package can contain several roles. A human recipe does not
automatically supply a machine design, and machine instructions do not
automatically teach the character a manual technique.

Information can live on USB data sticks, phones, laptops and suitable machine
controllers. Usable unlocked information can be freely copied between compatible
carriers; transferring it does not consume the source copy or require hacking
again. Devices can store information they cannot execute: a phone carrying an
industrial package does not thereby become its machine controller.

Copies preserve information identity independently of the physical carrier.
Copying or importing does not itself memorize recipes, award study benefits or
reset once-per-character book use. Human-readable material follows the existing
book/reference learning model, including shared identity across equivalent copies.
Ordinary copying/importing should be quick, low-overhead and XP-free; exact time
and power costs remain to be specified.

### Device roles and ordinary transfers

| Device | Role in the current design |
| --- | --- |
| USB data stick | Passive portable storage; a compatible active device accesses its contents |
| Phone | Everyday information library: read instructions, copy files and transfer usable designs |
| Laptop/workstation | Ordinary information handling and supported production configuration; later hacking/recovery tools have their own design |
| Machine controller | Stores supplied/imported designs and compatible software, and starts supported jobs |

Operate on named packages/designs rather than a player-facing directory tree.
Initially omit gameplay byte-capacity management. Storage capability does not
guarantee execution compatibility.

An ordinary transfer is one short interruptible action. Participating active
devices need power; USB sticks need no separate battery. A copied package becomes
usable at the destination only on successful completion. Interruption preserves
the source and existing destination information without creating a partially
usable new package. Copying an already-present package creates no duplicate entry.
Exact action costs and power-loss handling remain for the runtime contract.

### Deferred: hacking, extraction and modified laptops

The later hacking effort owns recovering production software, human instructions
and machine designs from scavenged systems. It also owns hacking-oriented laptop
modifications, appropriate tools/programs, target-specific connections/adapters and
any supporting hardware work. These are future equipment/action hooks, not an
approved upgrade roster or a general modification framework in this pass.

Computers handles information access, protection bypass and recovery. Electronics
can handle actual interface reconstruction or hardware modification where needed.
An ordinary laptop remains useful for everyday file handling; future target
requirements determine when specialized modifications are necessary.

Retain the accepted distinction between source access, export protection, data
recoverability and equipment operability. A working installed package can remain
copy-protected; a mechanically broken machine can retain readable information.
Once an unlocked usable portable package is recovered, ordinary copying is
straightforward. Copying an already-readable file is ordinary transfer; obtaining
usable data through extraction is deferred. D3-C will decide source access,
required tools, costs/learning, interruption and source-changing outcomes.

## 2. Explicit import and a persistent machine library

Provide an action in the machine menu:

```text
Load designs -> choose carried USB / phone / laptop -> select files
```

The action represents connecting the device and transferring information. It
should not require separate inventory actions to insert, copy and eject the
carrier. Exact compatible interfaces and transfer costs remain for the detailed
design.

Imported designs remain in that machine's local library for future jobs, including
across save/load. Found machines can already contain supplied designs and usable
control software. Each machine has its own library; importing is not a global
unlock for every machine or for the character.

Autonomous machines do not discover usable information merely because a stick is
on nearby ground or a character with a phone passes by. Deliberate import from a
carried or explicitly connected device establishes local availability. Active
manual crafting can instead consult accessible digital references through the
character and an appropriate reader.

This makes workshop configuration meaningful while avoiding repeated USB handling
for each batch. Importing a design does not supply missing compatible production
software or improve the machine's physical capability.

## 3. Job startup and information lifetime

The machine menu presents design, quantity, required materials, duration, power
requirements and expected learning before Start. Use a short recipe-dependent
hands-on setup period followed by autonomous production. The completion reward
already represents the commissioned job; this setup has no second automatic XP
bonus. Exact setup timing, interruption and reservation/commit boundaries remain
for the W4/D3 runtime design.

For a selected batch:

1. Validate the machine's available design, compatible software/profile and
   operator setup requirements.
2. Reserve exactly that batch's physical inputs and sufficient output capacity
   under W4.
3. Capture the instructions and software/profile eligibility needed by this job.
4. Execute using the machine's ordinary funded work.

After successful startup, removing the original USB, leaving with the laptop or
deleting the library copy does not invalidate the existing job. Its captured
instructions survive Stop/Resume, power outages, offscreen operation and save/load.
Resume does not require the external source or re-import the design.

New jobs validate the machine's current information again. A persistent library
therefore supplies convenient repeated use, while the job's own capture protects
already-started work from later information changes. The detailed representation
of that capture remains an implementation-design question.

Information capture does not prepay electricity. Preserve D1/W4 paid slices,
power-waiting run intent and machine/source residency. W4 may accept a job while
unpowered and wait for supply; running still needs funded work. Commissioned-batch
completion grants learning under section 5; elapsed time alone is not its XP basis.

## 4. Reserved materials

The user reaffirmed W4's machine-owned input reservation:

- Reserve only the selected batch's inputs, not every item in nearby storage.
- Reserved originals are excluded from public transfers and other crafting jobs.
- Stop/pause retains both inputs and the output-capacity reservation.
- Completion replaces the inputs with the promised output exactly once.
- Cancel returns original inputs to the public machine bin, losing progress and
  paid work and refunding no electricity.
- Destruction follows W4: drop original inputs/public outputs and discard the job.

This prevents withdrawing or reusing ingredients while their job continues.
Restricting who may cancel a job or collect its output is a separate multiplayer
ownership decision; input reservation alone does not establish that policy.

## 5. Production modes and learning

The user accepted a distinction between continuous production and explicitly
commissioned batches. This supersedes the earlier blanket rule that autonomous
machine execution grants no operator XP.

| Mode | Example | Learning rule |
| --- | --- | --- |
| Continuous operation | A gasifier keeps producing fuel gas while supplied | No ongoing production XP; separate construction, repair or meaningful configuration can have their own learning |
| Commissioned batch | A character selects and starts "print 10 plastic cups" | Award the initiating character Computers learning when the selected batch successfully completes |

Classify the supported operation, not necessarily the machine hardware. Merely
switching a continuous process on does not make its subsequent output eligible
for batch XP. Continuous/repeating machinery still needs its separate process
contract; W4's first delivery remains one explicitly selected finite job.

### Authored learning budget

- Author learning per base recipe execution, scaling the nominal budget by the
  selected batch count. Different products can teach different amounts.
- The same number of completed executions has the same nominal budget whether
  commissioned together or separately. There is no flat Start bonus.
- Machine speed, elapsed waiting and power outages do not increase that budget.
- Repeated useful production remains valid practice. Apply the shared source
  ceiling `min(r + 1, skill.maxLevel)`: a training-level-0 cup recipe cannot teach
  beyond Computers 1.

Accepted initial tuning targets, subject to content balance:

| Recipe's authored training level | Automated learning fraction |
| --- | ---: |
| 0–4 | 100% of its authored machine-learning budget |
| 5+ | 80% of its authored machine-learning budget |

These fractions use `ratioScale` values 10000 and 8000. The advanced penalty is
reduced learning efficiency. This decision adds no production-time, material-loss
or failure penalty.

At learned noncombat levels 0–4, completion supplies ordinary XP. At learned
levels 5+, it supplies practice through the shared theory/practice model; it does
not supply both halves of advancement. Eligible study and other appropriate
theory sources remain useful. Distinguish the recipe's training level, which
selects the discount, from the character's learned level, which selects the
ordinary/advanced accounting.

Retain shared boosts, ceilings, cap cleanup and reserve/surplus rules. Detailed
integer scaling/remainders and batch award integration must prevent splitting
batches from inflating the nominal budget without changing those shared rules.
Actual realized advancement remains subject to the recipient's learning state.

### Recipient, completion and interruptions

- Preserve the initiating character's durable identity and the job's learning
  source/budget and batch count.
- Grant the completion award once, alongside successful output production.
  Collection grants nothing and does not change the recipient.
- Pause/resume and supply loss retain the pending award. Offscreen completion
  and save/load preserve the recipient and once-only accounting, including when
  the initiating character is parked or unloaded.
- Cancellation or destruction before completion gives no batch-completion XP.
- The first version has one completion event for the entire selected batch.
  Partial output collection and proportional completion awards need a later
  explicit extension.

Meaningful hands-on configuration can have its separately authored normal
learning. A completed-batch reward represents successful preparation and execution;
merely pressing Start has no separate bonus. The later hacking pass specifies
protected/damaged-source recovery work, rewards and appropriate learning sources.

## 6. Player example and acceptance scenarios

A player finds a working home polymer printer with supplied simple designs,
obtains compatible feedstock and connects power. They select a simple product
and batch quantity, perform setup and leave the machine to finish. The job reserves
its feedstock and output space. Completion awards the initiating player the
recipe's eligible Computers learning; another player can collect the product
without receiving that award. A subsequent batch uses the supplied design again.

Plastic cups illustrate batch selection; the final roster must choose products
with supported useful roles and authored costs. No extracted schematic is needed
for this example. Ordinary transfer/import behavior can be checked using already
readable information, without a temporary crafting-based acquisition substitute.

| Scenario | Expected behavior |
| --- | --- |
| Use a simple supplied home-printer design | No separate schematic, USB import or extraction prerequisite; normal operator/material/power requirements apply |
| Copy readable/unlocked information | Source and destination retain the same information identity; no repeated hacking or duplicated learning benefit |
| Interrupt an ordinary transfer | Source and existing destination data remain; no partially usable new package |
| Copy a package already present at the destination | No duplicate package or repeated learning benefit |
| Carry a design past an unconfigured printer | No automatic library import or machine eligibility |
| Import and remove the carrier | The local library retains the design for later jobs |
| Start, then remove carrier or delete local library entry | Existing job retains its instructions; later new jobs use current library eligibility |
| Pause, lose power, save/reload or leave the area | Captured information and reserved materials persist; execution follows D1/W4 funding and residency |
| Try to transfer/use reserved inputs | Inputs remain unavailable until completion, cancellation or destruction resolves the job |
| Complete one ten-cup batch or ten equivalent one-cup batches | Same nominal learning budget; no Start bonus or extra reward for machine waiting time |
| Complete while initiator is away, then collect as another character | Initiator receives the eligible award exactly once; collection and reload do not repeat it |
| Cancel or destroy an unfinished batch | No completion XP; originals resolve through W4 |
| Run/restart a continuous producer | Production remains XP-free |
| Complete an advanced commissioned recipe | Initial 80% budget applies for training level 5+; a learned-level-5+ recipient gets practice, subject to shared theory/reserve/ceiling rules |
| Copy/import ordinary files | No XP; information and once-per-character learning identities remain intact |

These are future acceptance scenarios, not test results.

## 7. Remaining design work

- **D3-A:** concrete carrier/package representation and identity, device access,
  compatibility, transfer power/time and interruption implementation,
  human reference access, software installation and library management.
- **D3-B/W4:** atomic startup and completion integration, durable job/recipient
  representation, batch bounds and integer learning accounting, actual setup
  work and reservation boundaries, the home-printer roster using supplied simple
  designs and its learning budgets,
  feedstock/operating budgets, persistence/LAN contracts and shared presentation.

**Deferred to the later hacking effort:** D3-C source access/protection/recovery,
design/schematic extraction, hacking laptop modifications and supporting
tools/interfaces. This pass records compatibility needs without designing those
actions or authoring temporary replacement routes.

Retain the accepted Computers-based printer-operation boundary, supplied simple
designs and found-equipment shortcuts. Acquired intricate designs, industrial
printing and process machinery follow their own later reviewed contracts.
