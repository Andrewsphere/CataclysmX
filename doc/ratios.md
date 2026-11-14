# Integer ratios

`core/ratio.h` defines the project-wide scale for integer ratios, efficiencies,
and probabilities: `ratioScale == 10000` represents 1.0. Thus 1000 is 10%, 7500
is 75%, and zero is 0%. Use this constant rather than a local percentage scale.

`ValidUnitRatio(value)` accepts the inclusive range 0..10000. Content fields
representing probabilities and efficiencies validate this range. Rates and
multipliers may exceed 10000 when their domain allows values greater than one.

`ScaleRatioFloor(value, ratio)` computes `floor(value * ratio / ratioScale)` for
a uint64 value and uint32 ratio. It divides before multiplying to avoid an
overflowing intermediate product; an unrepresentable final value fails fast.
The rounding direction is explicit: one component at 7500 efficiency returns
zero components, whereas eight components return six.

Incremental work and billing carry their fractional remainder rather than
rounding each update independently. Crafting uses 1/10000 work units and scaled
energy rates, so its energy remainder has denominator `ratioScale * ratioScale`.
Those remainders are authoritative saved state. RNG ownership and draws remain
explicit simulation inputs; defining a probability scale introduces no global RNG.

# Bounded content lists

`core/static_list.h` supplies `StaticList<T, capacity>` for small, trivially
copyable records. Storage is an initialized inline array and a byte-sized count;
capacity must be 1..255. No allocation, capacity pointer, or uninitialized-storage
management is involved. The deliberate restriction suits packed recipe inputs
and trait requirements without adding a general replacement for game containers.

`PushBack` returns false without mutation at capacity, enabling typed loader
errors. `Size`, `Empty`, checked indexing, and `begin`/`end` expose only the used
prefix. Out-of-range indexing is a broken invariant and fails fast. Equality
compares the used prefix; unused storage and padding never define identity or a
persistence format.
