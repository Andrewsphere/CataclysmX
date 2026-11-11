# Machine interaction through Server

Directional `interact` keeps the existing terrain priority: doors open or close
before an object on the same tile is considered. Unavailable tiles and closed
vertical faces retain their existing failure outcomes. World validates machine
access again when `UseObject` executes.

For an object with a `DescribeObjectState` value, Server selects one action:

* Running job: `Stop`.
* No job and nonempty object contents: `Collect`.
* Otherwise: `Start`, including resuming paused work.

Successful actions use the behavior's `baseCost`, scaled by actor speed through
the existing action scheduler, and report `Outcome::toggled`. Refused actions
spend no time: `NoContents` and `NoJob` report `nothing_here`; other machine
failures report `blocked`. Untyped objects have no state snapshot and keep their
enabled toggle interaction. Typed None objects have a None snapshot and their
use is refused as an invalid action.

Start consumes direct carried leaf inputs. Work advances with simulation time;
completed output stays in object contents until collected. Stop preserves work
and energy, and resuming requires power but no replacement inputs. Progress and
energy inspection use the direct `World::DescribeObjectState` API. Reusing the
existing command and outcome keeps this integration small and compatible with
the current wire format.

Direct `world.UseObject(actor, object, action)` calls return a positive `baseCost`
on success but do not schedule it; a caller with a ready actor must call
`world.ScheduleActorAction(actor, result.baseCost)`. `SetObjectEnabled` freezes
advancement and events independently of machine `running`; completed work needs
an explicit `world.ProcessObjectEvents()` at the zero-time boundary to produce
output. Reacquire World read borrows after mutations.

Player movement calls the shared checked `World::TryActorStep` after staging
the destination residency footprint. The shared check enforces paired stairs;
the server retains rollback paths, bubble commits, and action scheduling.

`tests/object_server_tests.cpp` covers standalone Server start/completion/
collection, stop/resume costs, power failures, direct-input eligibility,
collection capacity, vertical access, and door priority. The bundled
`scrap_press` definition is in `data/terrain.json5`. Save v9 and the legacy v7/v8
upgrade are described in the
[save format](save_format.md#version-78-compatibility-and-checkpoint-upgrade).
