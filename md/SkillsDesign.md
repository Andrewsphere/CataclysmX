# Skills design

The skill and learning core is implemented. See [runtime skills](../doc/skills.md)
and [skill content](../doc/skills_content.md) for the authoritative API, rules,
defaults, and schema; [crafting design](CraftingDesign.md) describes its job
integration. This document records the design direction and remaining scope.

Learned levels run from 0 to 10. Learned levels gate recipes and teaching;
effective levels can be modified above 10 for performance and cannot satisfy a
learned requirement. Noncombat advancement toward levels 1–5 uses ordinary XP;
from level 5, advancement toward 6–10 pairs theory with practice. Combat skills
use ordinary XP throughout. Activities stop teaching at their training level plus
one, capped at 10. Advanced reserves are capped by the current level's remaining
XP and do not carry across level-up.

The agreed noncombat domains are Cooking & Chemistry, Fabrication, Mechanics,
Electronics, Computers, Tailoring, Survival, and Medicine. These definitions and
the basic Melee, Unarmed, and Dodge combat skills ship in the main and FirstHouse
packs. Starter fabrication work and two manuals introduce the coil spear recipe,
including learning through disassembly; broader activity and book coverage remains
content and balance work. Weapon families, passive benefits and broader combat
balance remain future work.

The combat hooks now award one whole XP per valid, in-reach hostile strike attempt
to Melee, also to Unarmed when no weapon is wielded; defenders train Dodge on a
miss or non-parried strike and Melee on an actual parry. Raw damage packets and
cancelled or unreachable windups do not independently train skills. No combat skill
performance scaling is authored yet. See [runtime skills](../doc/skills.md#combat-training).

Ordinary books teach once per character and information identity, without being
destroyed. In the ordinary noncombat band, direct book XP is capped at 30% of a
level and the strongest multiplier lasts until that skill levels. Combat books
grant no direct or theory XP and at most a 1.2x multiplier. Advanced noncombat
books grant theory; crafting supplies practice and disassembly supplies theory.
Tuning the shipped XP curve, rates, book ranges and content coverage remains open.

Recipe knowledge is separate from skill and a usable book reference. Autolearn is
explicit; completed crafting and disassembly can permanently memorize recipes.
Study itself does not memorize the book's recipe list. Completed work trains
incrementally, while completion awards proportional recipe-learning credit to
contributors, including archived workers.

Martial-art acquisition, art XP, manuals with chance-based retries, moves and
active abilities remain design concepts, not implemented art progression. See
[combat design](CombatDesign.md) for that direction. Future work also includes
broader activity integrations, meaningful effective-level performance formulas,
and balance experiments; current UI and persistence details are in the runtime
documentation.
