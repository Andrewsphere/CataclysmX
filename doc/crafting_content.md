# Crafting content

Crafting uses the existing JSON5 content document (`format_version: 1`), with
entries in `definitions`. All sources are parsed before references are resolved;
forward and cross-file references work. Unknown or duplicate fields are errors.
IDs match `[a-z][a-z0-9_]*` and are globally unique across every definition kind,
including generated recycling recipes and the injected unfinished-craft item.
Loading is transactional: an invalid definition produces `InvalidData` and no
catalog. Optional diagnostics retain source/path information.

## Tool traits and items

```json5
{type: 'tool_trait', id: 'cutting', name: 'Cutting'}
{type: 'item', id: 'powered_saw', name: 'Powered saw', mass_g: 1200, volume_ml: 800,
 tool_traits: [{id: 'cutting', tier: 2}],
 recovery_allowed: true,
 capabilities: [
   {id: 'battery', type: 'energy_store', capacity_energy: 1000, rechargeable: true}
 ],
 crafting_power: {store: 'battery', energy_per_work: 10000}}
```

`tool_trait` requires `id` and `name`. Item `tool_traits` is optional (default
empty), with at most four distinct trait IDs and integer tiers 1–255. Item
`recovery_allowed` is a boolean defaulting to true. A false value prevents that
item type from being recovered by recycling, rather than preventing it from
being used as a crafting input or recycling target.

Item `crafting_power` is optional. When present, both fields are required:
`store` names an energy-store capability **on that item**, and `energy_per_work`
is an integer 1–UINT32_MAX. The rate uses the project integer-ratio convention:
10000 means one energy per base work, 1000 means 0.1 energy per base work.
The loader resolves the stable capability name after lexical capability sorting.
There is no implicit energy-store selection or default billing. Existing behavior
registration is sufficient; crafting fields introduce no capability type.

## Crafting recipes

```json5
{type: 'recipe', id: 'make_panel', name: 'Make panel', crafting_enabled: true,
 inputs: [
   {item: 'sheet', count: 11},
   {item: 'glue', count: 1, recoverable: false},
   {item: 'clamp', count: 1, retained: true}
 ],
 tool_traits: [{id: 'cutting', tier: 2}],
 result: {item: 'panel', count: 1}, work: 300,
 recycling: {
   id: 'recycle_panel', work: 100, efficiency: 1000,
   inputs: [{item: 'clamp', count: 1, retained: true}],
   tool_traits: [{id: 'cutting', tier: 1}]
 }}
```

Required fields are `id`, `inputs` (an array, possibly empty), `result`, and
`work`. Optional `name` defaults to the stable ID. `crafting_enabled` defaults
to true; false preserves the definition and any requested recycler while marking
the crafting recipe disabled.

Each input requires an item ID and integer count 1–255. `retained` defaults to
false and `recoverable` defaults to true; both must be booleans when supplied.
Retained inputs are required tools, rather than consumed material. At most eight
distinct input item types are allowed, **including retained tools**. An item type
cannot occur twice, even with different flags. `tool_traits` defaults to empty
and uses the same four-entry, distinct-ID, tier-1–255 schema as items.

`result` requires `item` and `count`. Output counts are initially restricted to
1–255, despite the runtime output field being uint32, to bound atomic products.
`work` is an integer 1–UINT32_MAX in base-work units. Every referenced item must
resolve to a nonzero 20-bit item ID. Several crafting recipes may produce the
same output item.

### Generated recycling

Optional `recycling` requests a separate recycling definition. Its `id`, `work`,
and `efficiency` are required; the ID is explicitly authored for stability and
participates in global uniqueness checks. `efficiency` is an integer 0–10000
(10000 = 100%, **1000 = 10%**). Generation requires `result.count == 1`.

For each crafting input that is consumed, recoverable, and whose item permits
recovery, the generated return count is:

```text
floor(input.count * efficiency / 10000)
```

Zero counts are omitted. Retained tools, inputs with `recoverable: false`, and
items with `recovery_allowed: false` are omitted. An empty return list is valid,
including at zero efficiency. No result-count division or floating-point math
is involved. Recycling additions in `recycling.inputs` default to empty and use
the ordinary input schema; they are independent requirements and do not
contribute to generated returns. Recycling `tool_traits` also defaults to empty
and is independent of the crafting recipe's traits.

## Explicit recycling recipes

```json5
{type: 'recycling_recipe', id: 'salvage_panel', name: 'Salvage panel',
 target: 'panel', inputs: [], tool_traits: [],
 returns: [{item: 'sheet', count: 2}], work: 100}
```

Required fields are `id`, `target` (an item ID), `returns` (possibly empty), and
`work`. `name` defaults to the ID. `inputs` and `tool_traits` default to empty
and follow the schemas above. Returns require item/count pairs, with at most
eight distinct item types and counts 1–255. Duplicate return types and explicitly
returning an item with `recovery_allowed: false` are errors. Explicit recipes
do not reference crafting recipes or apply an efficiency calculation.

An item target has **at most one recycler**, across explicit and generated
definitions. A conflict rejects the entire load, with diagnostics naming both
recycler IDs, the target, and both source locations. Crafting alternatives do
not automatically choose which recycler wins.

## Runtime catalog and internal item

`FindCraftingRecipe`, `FindRecyclingRecipe`, and `FindToolTrait` resolve authored
IDs. Their `TryGet...`, `Get...`, and `...Count` companions follow existing catalog
conventions: zero is invalid, `TryGet` returns null for invalid IDs, and `Get`
aborts on an invalid ID. `FindRecyclingFor` maps a target item to its unique
recycler, or zero when absent. Numeric IDs are assigned by lexical authored-ID
order within each category, independent of source order; trait IDs are 1–255.
Inputs, traits, and returns are canonicalized by resolved numeric ID. Runtime
recipe references are typed numeric IDs, with no JSON or runtime string lookup
needed for execution. Strings remain for content identity and presentation.

If any crafting or recycling definition exists, loading injects `unfinished_craft`
and exposes its ID through `UnfinishedCraftItem`. Its name is `unfinished craft`,
mass is zero, volume is one, and `unfinishedCraft` is true. It has one `container`
capability with capacity UINT32_MAX and canonical metadata matching ordinary
container capabilities; it has no energy stores or behavior bindings. Runtime
code owns its special state. No user-authored unfinished-craft flag is accepted.

The injected ID is globally reserved only when injection is needed: a collision
then rejects the load. Without recipes/recyclers, no internal item is injected,
`UnfinishedCraftItem` is zero, and existing empty catalogs retain their counts.
Trait-only content does not trigger injection. A user item named
`unfinished_craft` remains an ordinary item when no recipes/recyclers exist.

These limits keep the packed inputs and fixed-capacity lists
small and checked at the content boundary. References resolve once after all
sources are available, allowing immutable runtime definitions without retaining
parser objects or adding behavior-registration machinery.
