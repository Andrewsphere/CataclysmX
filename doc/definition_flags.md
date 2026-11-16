# Definition flags

`content/definition_flags.h` provides shared flag vocabulary types and sparse,
definition-owned sets. Catalog storage, JSON parsing, name resolution, and
publication belong to `ContentCatalog` and its loader.

## JSON schema

```json
{
  "type": "flag",
  "id": "electrically_conductive",
  "name": "electrically conductive",
  "description": "The defined material or entity conducts electrical current.",
  "domains": ["item", "terrain", "object"],
  "kind": "property"
}
```

Require `id` matching `[a-z][a-z0-9_]*`, nonempty strings for `name` and
`description`, a nonempty array
of unique domain strings, and an explicit `kind`. Domain spellings are exactly
`item`, `monster`, `terrain`, and `object`; kinds are `category` and `property`.
Definitions reference vocabulary with an optional `"flags":
["electrically_conductive"]` array. Absence or an empty array means no flags.
Only item attachment is initially integrated; the domain vocabulary already
supports future monster, terrain, and object integration.

A category classifies a definition (for example `armor`); a property describes
a characteristic (for example `electrically_conductive`). Both are descriptive
metadata until an explicit consumer uses them. Neither creates capabilities,
changes stats, nor promises an implemented simulation effect. There is no
implication, inheritance, category exclusivity, or automatic propagation.
Future consumers should document which exact flags they interpret.

The bundled vocabulary is `data/flags/core.json5`, with 37 entries. All are
currently descriptive. Absence means the property is not asserted; it does not
establish a measured inverse property. New mechanics must define their fallback
and audit older, unclassified content before treating missing flags as false.
Authoring checklists should review each relevant property, including decisions
to omit it. Mixed-material flag meanings describe the qualifying constituent
explicitly rather than implying uniform whole-object behavior.

The item-only category `obsolete` (display name **Obsolete**) marks legacy
definitions retained for an existing recipe, machine, or demo/server dependency.
It does not hide items, filter loot, disable recipes, or migrate saves. New
authoring should use current definitions. See [item slice integration](item_slice_integration.md)
for the remaining dependencies and deliberately retained compatibility loot.

`ContentCatalog::FindFlag(name)`, `TryGetFlag(id)`, `GetFlag(id)`, and `FlagCount()`
follow the existing catalog lookup contracts. For example:

```cpp
const auto conductive = catalog.FindFlag("electrically_conductive");
const bool exposedConductor = itemDefinition.flags.Contains(conductive);
```

`Contains` returns false for invalid ID zero. `TryGetFlag` returns null for
zero/out-of-range IDs; `GetFlag` fails fast on invalid IDs. Item equality includes
the canonical flag set. Network fingerprints include vocabulary meaning, allowed
domains, kinds, and item attachments by stable name. No new per-instance save
field is needed for definition-owned flags.

## Identity and loader contract

Flag strings occupy a **flag-specific namespace**, shared across all four
domains. Reject duplicate flag IDs across all sources, but allow a flag ID to
match an item or other definition ID. Existing catalog-wide definition-name
uniqueness must not absorb flags: names such as `container` and `armor` are
natural vocabulary and may also name items. Do not create per-domain copies
of the same flag.

1. Parse `FlagDefinition` public fields: `String id`, `String name`,
   `String description`, `FlagDomains domains`, and `FlagKind kind`.
2. Convert each domain string explicitly to `FlagDomain::{Item, Monster,
   Terrain, Object}`. Call `domains.Add(domain)` and reject its typed
   `FlagDomainError::{InvalidDomain, DuplicateDomain}` failure. Reject
   `domains.Empty()` before publication. Map kind explicitly to
   `FlagKind::{Category, Property}`; the C++ default is not a JSON default.
3. Validate unique flag names separately, sort vocabulary by stable string ID,
   and assign one-based `FlagId{index + 1}`. Check count against `UINT32_MAX`
   before narrowing. Zero means invalid/not found. Publish catalog definitions
   through const access and never reorder or mutate this vocabulary afterward.
4. Resolve attachment strings after all flag definitions are available. Reject
   unknown names and reject any flag whose `domains.Contains(ownerDomain)` is
   false. `Contains` also returns false for invalid enum values. Reject wrong
   JSON types and duplicate attachment names; never silently deduplicate.
5. Sort resolved `Vector<FlagId>` by `.value`, then call
   `FlagSet::FromSorted(eastl::move(ids))`. Handle its
   `eastl::expected<FlagSet, FlagSetError>` result and move the value into the
   owner definition. Errors are `InvalidId` (zero), `DuplicateId`, and
   `UnsortedIds`. Sorted duplicate IDs are rejected, not removed.

Map these typed errors to loader content errors with source/field diagnostics.
The set itself validates representation only: it cannot detect unknown nonzero
IDs, foreign-catalog IDs, or domain-invalid references. The loader must do so
before constructing it. Failed construction consumes the supplied by-value
input, but never publishes a partially valid set.

Numeric IDs are immutable only within their originating catalog. They can
change when vocabulary changes; save/network/content identities must use the
stable string ID and resolve it in the destination catalog. No global registry
or runtime catalog-identity token is needed.

## Set API and memory layout

`FlagSet` defaults empty and exposes `Contains(FlagId)`, `Empty()`, `Size()`,
`Ids()` returning `eastl::span<const FlagId>`, and equality. The explicit static
factory `FromSorted(Vector<FlagId>)` takes ownership of validated storage.
There is no element mutation API; iteration order is ascending catalog ID.
Borrowed spans must not outlive or cross replacement/movement of their owner.

The set stores a sorted `Vector<FlagId>`: four bytes per member plus vector
bookkeeping/capacity. Membership is binary search. A `StaticList` would impose
an unjustified content bound; a `SmallVector` would add inline capacity to every
definition and move overflow elements individually. Definition counts are much
smaller than runtime entity counts, occupancy is not yet measured, and cheap
ownership transfer during loading favors `Vector` without speculative inline
capacity. Empty sets allocate nothing. There is no 64-flag limit or catalog-wide
bitset allocation per definition. `FlagDomains` uses one private byte because
the four allowed domains are a genuinely closed set; this does not limit the
number of vocabulary entries.

Runtime items access flags through their item definition; they carry no copy
of the set. Future domains can reuse the same types and loader checks.

`tests/definition_flags_tests.cpp` covers checked domain membership, rejected
set inputs, empty/sparse lookup, value semantics, and more than 64 members.
`tests/item_flags_content_tests.cpp` additionally tests namespace collisions, unknown
references, disallowed domains, duplicate names, and source-order independence.
