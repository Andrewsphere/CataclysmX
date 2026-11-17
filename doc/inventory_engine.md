# Authoritative inventory and equipment

`Actor::inventory` owns directly carried items. Equipment holds non-owning left
and right hand and worn references to direct inventory children. A two-handed grip
occupies both hands with one item and never duplicates its mass. `wieldedItem` is
the left-first compatibility projection, not the combat source of truth.
`World::WieldItem` is an atomic replacement convenience; see
[equipment lifecycle](combat_lifecycle.md#equipment-mutations) for its selection
and rollback rules. Successful hold, release and regrip cost 100 base ticks;
garments use authored wear/remove costs (default 100). Refusals cost nothing.

`World::TransferInventoryItem(actor, id, source, destination)` is the gameplay
transfer boundary. `ItemStorage` uses persistent identity: Inventory means the
acting actor, Ground carries a position, Object/Container carry an ID. Unused
fields must be zero. An exact immediate source match prevents stale UI actions
from moving an item that has since changed parents. The method checks actor
readiness, ownership, reach, reservations, installation, ancestry and capacity
before ownership mutation. Moving a container and its tree is one 100-base-tick
action. Same-source transfers refuse without cost. There is no batch API.

These World methods return a cost without scheduling it; Server commits the
returned cost using actor speed. Failed commands consume no time. Administrative
`TransferItem` remains useful for setup and forced relocation; it also clears
equipment and cancels dependent windup after successful validation. Destruction
does the same. Explicit item behavior actions remain responsible for uninstalling
installed parts; inventory transfer never implicitly uninstalls them.

Ground storage requires supported visible floor within same-layer Chebyshev
distance one and unobstructed reach. Furniture storage requires explicit capacity
and visibility/reach. Machine internals are excluded. Other actors' possessions
and reserved descendants are inaccessible. `CanAccessStorage` is a query, never
an authorization token to retain across mutations.

The legacy `CanAccessItem` query keeps current-tile ground access for existing
item behaviors and callers. Nearby transfer authorization uses `CanAccessStorage`
instead, including bounded root traversal for nested containers and explicit
ownership checks; it does not broaden item-use reach.

## Snapshots

`WorldView::inventory` and `nearbyItems` are flattened, persistent-ID-sorted trees
with exact immediate `InventoryEntry::storage`. `groundItems` retains the legacy
current-tile projection. `wieldedItem` and per-entry `wielded` expose persistent
identity only; `heldHands` and `worn` expose actual equipment. `stackable` is true only for unequipped leaves without capabilities,
runtime state or reserved crafting-wrapper semantics.

`itemStorages` separates source metadata from entries. It includes Inventory,
all nine local Ground positions (including inaccessible positions without content
totals), accessible furniture and ordinary reachable containers. Mass is recursive;
volume is direct external volume. Inventory has its actual carry mass cap and no
volume cap. Floors have no fabricated caps. Furniture/container names are captured
values. A floor remains a separate source when furniture occupies its tile.
Object metadata also carries `objectPosition`, so number-key selection and floor
switching do not depend on map viewport radius or which Y layer is being drawn.
Other source kinds leave that metadata coordinate zero.

## Combat and persistence

Item melee profiles provide damage, explicit handling family, grips and response
capabilities; improvised items have a neutral one-handed fallback. Disability
retains equipment but can prevent its use. Committed attacks capture the actual
source and grip; removing a required item disrupts dependent work, not unrelated
actions. Armor supplies local coverage, protection and encumbrance. Saves rebind
persistent equipment identities after restoring ownership. Save format 14 and
network protocol 21 carry expanded equipment and combat state. See
[combat API](combat_api.md), [content](combat_content.md), and
[lifecycle](combat_lifecycle.md).
