# Crafting system design

Crafting and recycling are implemented as world-owned persistent jobs. See
[runtime crafting](../doc/crafting.md), [content](../doc/crafting_content.md),
[commands](../doc/crafting_commands.md), and [frontend controls](../doc/crafting_ui.md)
for contracts. Recycling, disassembly and uncrafting refer to the same operation.

Recipes define inputs, retained tools, tool traits, output and work; several
recipes can produce the same item. Each target has at most one explicit or
generated recycler. Generated returns use the authored efficiency on eligible
consumed inputs; explicit returns and recycling requirements are independent.
Loading resolves references transactionally and rejects conflicting recyclers.

Jobs reserve actual inputs in an unfinished item, fund slices atomically, and
persist through save/load. Pause completes the paid slice; cancellation releases
reserved items and discards job work without refunding power. Workers may change,
and credited work belongs to the worker who performed it.

## Skill and knowledge integration

[Skills and learning](../doc/skills.md) are implemented: learned skill
requirements gate crafting and recycling, independently of recipe knowledge.
Crafting needs a known recipe or an accessible reference; recycling needs no
recipe reference. Completed slices grant skill XP, retained even on cancellation.
Completion awards recipe memorization in proportion to each worker's contribution.
Default combined learning is two full crafts or three full disassemblies, with
mixed contributions accumulating deterministically. Autolearn is explicit.
Required skills default to training at the required level; authored training can
override the rate. Advanced noncombat crafting supplies practice and disassembly
supplies theory, subject to teaching ceilings and remaining-level reserve caps.
The starter chain uses level-0 Fabrication work toward level 1, a level-1 coil
spear available through manuals or level-2 autolearn, and a level-2 sword. Ungated
spear disassembly also teaches its recipe. See [shipped content](../doc/skills_content.md#shipped-starter-content).

Recipe knowledge and skill gates do not replace materials, tools, reach, power or
the paid-slice job model. Richer item quality, stations, speed bonuses, alternative
requirements and broad activity/content coverage remain future design and tuning.
