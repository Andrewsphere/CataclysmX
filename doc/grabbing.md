# Grabbing and moving furniture

**G** (Shift+g) asks for one of the eight horizontal directions. If the adjacent
tile contains visible, active, movable furniture, the player takes hold of it.
The log names the object: “You take hold of the table.” **G** again releases it:
“You let go of the table.” Escape cancels direction selection. Lowercase **g**
continues to pick up ground items. Bindings are configurable in both frontends.
Headless scripts use `grab n|ne|e|se|s|sw|w|nw`, ordinary `move`, and `ungrab`.

While holding furniture, a horizontal step translates both actor and object by
the same vector. This handles pushing, pulling, side-stepping and diagonals
without changing their relative offset. Both destinations are checked before
either relocation, so a blocked actor or object leaves both in place. The pair
can enter each other's vacated tile, but cannot displace another actor or object.
The actor may walk over nonblocking furniture as usual; the dragged object needs
an object-free destination. Both destinations need passable, supported terrain.
Diagonal steps follow ordinary actor movement's destination-based collision rules.
Dragging does not bump-open doors or bump-attack; release or interact/attack
explicitly. Changing elevation requires releasing first.

## Authoritative API and timing

- `CommandKind::grab` selects an adjacent tile through `dx,dz`; `dy` is zero.
- `CommandKind::release_grab` has zero direction.
- `World::QueryObjectStep(actor, object, direction)` reports eligibility and base
  cost without mutation. `TryObjectStep` atomically commits the same checked pair
  relocation; invalid handles/directions return typed errors, ordinary obstacles
  return `ObjectMoveOutcome`. Neither method schedules the actor.
- Server owns a typed, non-owning object handle and the original offset per
  player session. Each move revalidates both. Deletion, unloading, displacement
  by another player or an external actor move cannot redirect the grip to a
  replacement object: the next move reports a lost grip instead.
- `WorldView::grabbedObject` exposes the persistent ID, never the runtime handle.
  Grip is transient session state: disconnect and save/load release it. Furniture
  positions and stored contents persist normally.
- `CommandResult::objectName` carries captured object text for grab/release/
  blocked feedback. `CommandResultMessage` returns an owning `String`;
  `Localization::ResultText` formats localized named messages.

Object definitions accept `move_cost` in base ticks, 1..1,000,000; default 100.
The cardinal cost is the greater of this value and the actor's injury-adjusted
locomotion cost. Apply the ordinary diagonal distance rounding, then actor speed,
exactly once. Example base costs: chair/lamp 100, table 200, sofa/bed 300, washing
machine 400. These are gameplay tuning values, not a weight simulation.
Grab, release and refused steps spend no simulation time. Successful movement
uses normal action scheduling and residency-bubble updates.

The small World API keeps pair relocation and storage/lighting notifications in
one place. Keeping the grip on the session avoids persistent entity coupling and
requires no save-schema change. LAN protocol 16 transports the new commands,
named feedback and grip field. The older cardinal `QueryObjectMove`/`TryMoveObject`
API remains available to direct callers; Push/Pull no longer appear in interaction
choices and old menu requests are refused.
