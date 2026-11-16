# Authoritative inventory and equipment

`Actor::inventory` is the sole owner collection for directly carried items.
`Actor::wieldedItem` is one typed, generational, non-owning handle into that
collection. Wielding never adds a second mass contribution. Selecting a carried
nested item extracts it to direct inventory; the former weapon stays in direct
inventory. `World::WieldItem(actor, {})` unwields. Wield, swap and unwield return
100 base ticks on success; redundant selection and refusals return zero.

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
identity only. `stackable` is true only for unwielded leaves without capabilities,
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

`ItemMelee` supplies authored damage/cost/hands, or 4 HP blunt, 100 ticks and one
hand by default. Detailed actors need the required number of functioning hands.
Disability retains equipment but prevents its use; it never silently substitutes
unarmed damage. Weapon damage replaces actor unarmed damage. Plain timing scales
the existing locomotion-adjusted base by weapon cost; heavy windup/recovery scale
80/120 by weapon cost, rounding upward before actor-speed scaling.

Pending windup captures the weapon's persistent ID (`ActorActionState::weapon`),
including zero for unarmed. Removal causes existing capability-loss disruption;
execution also checks the captured identity. Saves store persistent equipment IDs
and rebind fresh handles only after restoring item ownership. See
`save_format.md`, `combat_api.md` and `networking.md` for the versioned boundaries.
