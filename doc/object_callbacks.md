# Object callbacks

`ObjectMutationContext` is borrowed for one `Use` or `ProcessEvent` callback.
Only `World` constructs it, and it cannot be copied or moved. It carries the
object handle explicitly, so mutation services do not depend on an ambient
current object. The world rejects reentrant object callbacks and requires its
private callback transactions to run inside a callback scope.

`ConsumeInputs`, `CollectContents`, and `Produce` execute synchronously: their
results are available to the behavior immediately and their existing atomic
validation and failure semantics are preserved. Item notifications are held
until the behavior returns. The callback guard is then released and queued
notifications are drained; these can mutate world storage, so callers reacquire
object pointers afterwards.

`RequestDestroy` records a callback-local, idempotent request. After the callback
returns and notifications drain, the world destroys that object before continuing
callback bookkeeping. The behavior remains alive throughout its invocation.
Gravity remains batched through notification draining, destruction, and the
remaining bookkeeping, then settles at the same spatial boundary. Object event
batches retain their persistent-ID order and revalidate each due handle.
