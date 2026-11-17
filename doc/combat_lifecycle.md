# Combat equipment and character lifecycle

## Equipment mutations

`Actor::equipment` owns equipment references, not items. All held and worn items
are direct inventory children, so an item's mass and ownership are counted once.
The two slots are left and right. A two-handed grip occupies both with the same
identity; releasing either releases both. `wieldedItem` is refreshed left-first
as a compatibility projection.

`World::QueryEquipment(actor, request)` performs a read-only eligibility check.
`HoldItem`, `ReleaseHand`, `RegripItem`, `WearItem`, and `RemoveWornItem` validate
before mutation and return a base cost without scheduling. The server schedules
successful equipment commands once. Holding, releasing, and regripping cost 100;
wearing and removing use the garment definition. Failures spend no time.

Holding checks the actual selected hand, its capability, authored grips, other
occupancy, carried ownership, installation and crafting reservations. Extracting
an owned nested item is part of the successful operation. A two-handed item
requires both functional, available hands. Disability never releases equipment.
Worn and held are exclusive; same-layer intersecting rigid regions conflict even
for overlap-exempt garments. Clothing must cover contacts present on the body.
Furniture grabbing is a separate session interaction and does not free hands.

`WieldItem` is an explicit inventory-UI replacement operation. It tentatively
releases only the old left-first selection, then tries authored grips in
definition order, left before right. Other held and worn items remain occupied.
If no supported binding succeeds, it restores the original equipment and leaves
the selected item's location intact. Unwield releases the old selection only.

Transfers and destruction clear all affected held and worn references. Losing an
item disrupts an action only when that persistent item is a required committed
source (or the legacy committed weapon). An unrelated held item cannot cancel a
kick. Equipment invariants check ownership, grip support, duplicate occupancy,
worn exclusivity, rigid conflicts, and the legacy projection without requiring
currently functional limbs.

## Death boundary

Combat applies terminal injury and calls `MarkActorDead(actor, killerId)`.
Marking immediately removes occupancy, marks life Dead, cancels the current
combat action and ordinary/art study, suspends the actor deadline, and emits the
one-time lifecycle death event. A player is parked but stays owned by the world.
Funded crafting jobs lose their worker and retain their paid remainder in
WorkerUnavailable state. This uses existing typed crafting-state capture/read
and item initialization APIs because the crafting coordinator has no explicit
worker-death API; it neither completes work nor cancels the job.

`FinalizeActorDeaths()` runs at safe simulation, command, reconciliation, and
save boundaries. It processes terminal actors by persistent ID. It creates the
catalog's ordinary corpse container, records its persistent identity in
`CombatState::corpse`, moves inventory roots directly into it without capacity or
reservation refusal, and preserves every nested tree. Equipment references are
cleared. NPC storage is retired after transfer. Dead players remain parked and
nonoccupying, including after character replacement. A nonzero corpse identity
is terminal even if the corpse later moves or is destroyed; reconciliation never
creates another one.

Dead actors are excluded from active/ready batches, NPC decisions, actor event
deadlines, learning credit, and pacing. Due player cohorts recheck readiness
before each command, so a friendly sweep can kill or disrupt a later queued
participant. Its queued input receives a terminal/not-ready receipt rather than
executing or becoming an automatic wait. With no living active sessions, pacing
is idle, including continuous pacing mode. When the last active player dies
inside simulation, `AdvanceUntilInput` returns `NoActivePlayers` after the current
batch and corpse finalization, before advancing to any future NPC/object event.
Dead views retain access to character status and remembered maps.

## Sessions and replacement

`PlayerSlot::id` and the host's cached session ID remain stable. `character`
identifies the current actor; zero in an old slot means the original session ID.
`WorldView::playerId` is the session, `characterId` is the actor. Status carries
both identities and life. Parked, unloaded guests use their last captured combat
projection for life; parked characters do not simulate while unloaded.

`CreateFreshCharacter(sessionId)` requires an active session whose current actor
is dead. It searches deterministic nearby rings for an active, supported,
unoccupied tile and creates an ordinary new player in the same world. Inventory,
skills, art knowledge, and map memories start fresh. The old character and corpse
remain. Reconnect token, session ID, and input sequence survive; buffered intent
and furniture grip are cleared and opportunity increments. `new_character` is
admitted outside live pacing and cannot replace a living character.

`SessionState::player` remains the host **actor** for spatial save validation.
Roster entries save the current actor plus stable `SavedPlayer::session`; zero
session migrates to actor. World validation permits a dead parked host and
unrostered finalized dead player actors, while requiring every living player in
the roster. Loading derives the stable host session from its roster entry.

## Persistence and presentation boundaries

World archives use catalog-aware combat state, `CaptureEquipment`/`ReadEquipment`,
and `CaptureCharacterCombat`/`ReadCharacterCombat`. Equipment references bind only
after all items exist. Empty legacy equipment falls back to the old wielded item
using its first authored grip. Restoring terminal state clears active work;
zero-vital legacy actors transition through the death boundary once.
Legacy armed windup binds its committed hand identities and authored grip after
equipment restoration, preserving the funded deadline, recovery, and stamina.
Expanded v3 commitments are restored verbatim. Terminal finalization also runs
idempotent worker cleanup after restored items exist, so migrated death cannot
leave a crafting job assigned to a dead character.

Attack-without-target uses `ChooseAdjacentTarget` and simulation RNG. Sighted AI
receives a perceived target's `Combat.CanReach` affordance. New command envelopes
reject extraneous legacy fields; older commands reject every appended combat
field. Style, ability, and art-study coordinators schedule their own commitments;
the server does not schedule those a second time.

The server drains combat events at update/round boundaries into a global
128-event presentation history. Views include only events involving their
current character. `Combat.CollectInfo` provides combat/equipment/art/ability
information; inventory entries derive `worn` and `heldHands` from equipment.

## Focused regression coverage

`combat_equipment_tests.cpp` covers hand occupancy, atomic replacement, layer-local
rigid conflicts, disability retention, equipment rebinding, legacy paid two-handed
windup migration, and strict command envelopes. `combat_death_tests.cpp` covers
death participation, NPC retirement, replacement sessions, and dead-host saves.
Its oversized-possession regression transfers 6,000,000,020 ml into one corpse,
then checks ownership, nested contents, invariants, save/load, and chunk archival.
Ordinary container capacity refusal remains covered before the death transfer.

Death events belong to the world boundary. Runtime ownership checks and shared
item-snapshot validation exempt corpse contents from ordinary capacity refusal.
On-disk validation accepts the dead parked host and retained finalized characters;
stable roster session identity remains separate from actor identity.
