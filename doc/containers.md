# Sequence containers

`core/containers.h` defines `Vector<T>` and `SmallVector<T, inlineCapacity>`;
`core/static_list.h` defines `StaticList<T, capacity>`. Evaluate them in the
order StaticList, SmallVector, Vector, accounting for the element type and the
number and lifetime of owners. An exact fixed extent still belongs in
`eastl::array<T, N>`.

## StaticList: a small hard bound

`StaticList` holds initialized trivial values inline, with a byte-sized used
count. Capacity must be 1..255. It supports `Size`, `Empty`, checked indexing,
iteration, equality, and `PushBack`; the latter returns false without mutation
when full. Handle invalid external counts as typed loader errors and internal
bound violations as always-on invariant failures.

This deliberately small interface suits recipe records and generated plans.
All slots consume space and are initialized even when the list is empty; copies
and moves copy the inline array. A valid maximum alone does not justify placing
that maximum in every item or snapshot entry. Serialize the used prefix, never
the container's object representation.

## SmallVector: a common-case inline buffer

`SmallVector<T, N>` aliases `eastl::fixed_vector<T, N, true, FatalAllocator>`.
It supports nontrivial elements and ordinary vector mutations. N is an inline
capacity, not a gameplay/content limit: larger collections retain their existing
validation rules and allocate through the same fatal-on-OOM policy as `Vector`.

Choose a small N from expected occupancy and element weight, rather than copying
the largest permitted count. Per-item energy and behavior lists commonly need
zero or one element, while key bindings commonly need one or two alternatives.
Every owner pays for the inline buffer, including empty owners. Strings and
other owning elements can still allocate their own payloads.

Once growth exceeds inline capacity, the sequence moves to contiguous heap
storage; further growth may reallocate. In the installed EASTL implementation,
move construction and move assignment are element-wise even for overflowed
vectors. They do not transfer the source allocation. This matters for nested
collections and buffers repeatedly moved between snapshots or channels.

## Vector: bulk storage and cheap ownership transfer

`Vector<T>` remains appropriate for map-sized buffers, catalog-wide collections,
large reusable work batches, and storage whose allocated buffer should transfer
cheaply between owners. Preserve retained capacity when scratch buffers are
reused. For sparse per-entity lists, weigh the total inline footprint against
the allocations saved before increasing inline capacity.

Read-only sequence consumers should accept `eastl::span<const T>` when they do
not need the owning container. Mutable append APIs use an explicit shared list
type where required; do not add a generic container framework just to hide a
storage choice. Returning a tiny inline value is reasonable, while repeated
heap-vector construction should favor caller-owned reusable output storage.
