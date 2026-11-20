# Digital information and machine runtime

This describes the runtime interfaces and headless commands for the current
digital fabrication implementation. The accepted gameplay direction and roster
are in [digital fabrication](digital_fabrication_design.md) and
[home printing](home_printing_design.md); those documents also discuss deferred
ideas and should not be read as evidence that every design is implemented.

## Content authoring

`definitions` accepts two additional typed records:

- `information`: `id`, `name`, optional `books` (existing book identities),
  `designs` (machine recipe identities), and `software` (boolean).
- `machine_recipe`: `id`, optional `name`, consumed `inputs`, one `result`,
  positive `duration` and `energy_per_tick`, optional `skills` and
  `required_software`, and required
  `learning:{skill,level,xp_per_execution}`. `setup_work` defaults to 200 and
  `setup_work_per_unit` to 20; total setup is the fixed part plus quantity times
  the per-unit part. Learning is nominal whole XP per recipe execution, rather
  than a work-rate modifier. Batching expands inputs, output, duration and XP.

Items use `digital_device:'storage'|'reader'|'controller'` and optional
`supplied_information`. Readers name a local energy-store capability through
`reading_power`; storage is passive. Selectable objects use
`behavior:'selectable_machine'`, `selectable_recipes`, `capacity_volume_ml` and
a `packing_item` that is a controller with the reciprocal `placed_object`.
The supported recipe list is separate from the instance's installed designs.

See `data/items/digital_fabrication.json5`,
`data/information/digital_fabrication.json5`,
`data/crafting/digital_fabrication.json5` and
`data/objects/digital_fabrication.json5` for complete examples. Expanded counts
must fit 1–255 per input/output; arithmetic overflow and insufficient storage
are refusals before reserving any originals. Effective catalog export includes
`information` and `machine_recipes`.

## Ownership and information transfers

`ItemRuntime` owns a nullable `InformationLibrary` for a digital device. It
contains package IDs and an optional in-progress `InformationTransferState`.
Ordinary items therefore do not each pay for a library allocation. For an
installed portable machine, the original protected equipment item is the sole
owner of the controller library: the object endpoint resolves to that item.
Packing and replacing the equipment preserves its library instead of copying it
or recreating supplied packages. Defaults are created with the item; an
explicitly empty saved library remains empty.

An `InformationEndpoint` contains exactly one nonzero persistent item or object
ID. Transfer state belongs to the destination library and records the worker,
source, optional host, package name and remaining ticks, rather than retaining
entity pointers. No new usable package is published until completion.
`InformationSystem` maintains a sparse registry of destinations with active
transfers, plus reusable due-work storage; this avoids scheduling every idle
device. Due completions are ordered by persistent item ID.

`InformationSystem::Copy`, `Remove` and `Cancel` return typed results;
`Collect`, `CollectPackages` and `HasPackage` support inspection. The ordinary
copy uses 100 base work scaled by actor speed and awards no XP. The source and
destination must be accessible and powered as applicable. Two passive storage
devices require a carried, powered reader as host. Access and power are
rechecked while copying; interruption or failure leaves existing packages
intact. An already-present destination package is a free no-op. Deletion
requires a writable, powered destination and schedules 100 base work.

## Machine jobs

`MachineSystem::QueryStart` previews availability; `Start` and `Use` return
`MachineResult`, distinguishing normal outcomes from world errors. `Collect`
produces machine views and recipe availability for an actor. Setup, processing
and control are authoritative simulation operations, not frontend state.

At start, `MachineJobState` captures recipe and output identities, expanded
input and output counts, processing duration, energy, setup, skill and learning
budgets, and the original reserved input IDs. The capacity claim is the greater
of original input volume and complete output volume. This makes the job's
budgets durable across later recipe tuning and prevents reserved inputs or output
space being spent elsewhere. Restore validates the captured state and actual
ownership; see `machine_job.h` for serialization and validation entry points.

Jobs have two explicit phases:

1. **Setup** consumes actor work in paid slices. Stop requests a pause after
   the current paid slice; saved state retains pending work and timing remainder.
2. **Processing** uses machine time and funded power. Stop freezes prepaid
   ticks immediately; resume continues the job.

Completion consumes reserved originals, publishes output and awards the
captured production learning to the eligible recipient under the learning
rules. Completion preflights archived recipient storage before publishing output
or XP. A storage failure leaves completion pending and retries after
`machineCompletionRetryTicks` (100 ticks), without charging extra work or
electricity. Cancel and collect are explicit machine actions; their outcome
must be checked rather than assumed.

## Server and presentation boundary

`core/server.h` exposes `begin_machine`, `machine_action`,
`copy_information`, `remove_information` and `cancel_information` through
`Command`. `ValidCommandShape` validates the command shape, while authoritative
execution checks current game state. `CommandResult` includes
`machineOutcome` or `informationOutcome` alongside the general outcome and
possible storage error.

`WorldView` owns `machines` and `informationDevices` vectors. `MachineView`
includes job progress, reservation and learning information, and recipe
previews; `InformationDeviceView` includes endpoint, capabilities, transfer
progress and packages. These are presentation DTOs with persistent identities,
not borrowed runtime handles. Shared client presentation consumes these values
for local and LAN frontends; the server remains authoritative. Digital devices
cannot form UI stacks because their libraries are mutable instance state. The network
wire protocol constant is `0x43584e1c` (protocol 28); peers must use a matching
protocol and compatible content.

## Headless commands

The following commands are supported in local `--script` input and LAN
`--command` arguments. `machines` and `information` print shared menu and
learning summaries. IDs below are numeric: object and item IDs are persistent
world identities; recipe and package IDs are catalog-local numeric IDs, not
content-name strings. Obtain current IDs from the relevant views/content; do not
assume example numbers identify the same entities in another game.

```text
machines
information
begin-machine OBJECT RECIPE QUANTITY
stop-machine OBJECT
resume-machine OBJECT
cancel-machine OBJECT
collect-machine OBJECT
copy-information SOURCE DESTINATION PACKAGE [HOST_ITEM]
import-information SOURCE DESTINATION PACKAGE [HOST_ITEM]
export-information SOURCE DESTINATION PACKAGE [HOST_ITEM]
delete-information DESTINATION PACKAGE
cancel-information
```

`SOURCE` and `DESTINATION` are `item:ID` or `object:ID`, with a nonzero ID.
`HOST_ITEM`, when supplied, is a numeric persistent item ID. Copy, import and
export use the same transfer command and endpoint rules; their names do not
implicitly reverse the source and destination. For example, with suitable
devices and package IDs in the current game:

```text
copy-information item:123 object:456 1
copy-information item:123 item:124 1 125
```

The second example supplies reader item `125` as the host for two passive
storage endpoints. These examples describe syntax, not a guaranteed valid
world state. `cancel-information` applies to the issuing actor's transfer.
See [server API](server_api.md) and [networking](networking.md) for broader
session and transport usage.
