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
the nine combat skills Melee, Unarmed, Knives, Swords, Clubs & Hammers, Axes,
Polearms, Shields and Dodge ship in the main and FirstHouse packs: 17 skills total.
Starter fabrication work and two manuals introduce the coil spear recipe,
including learning through disassembly; broader activity and book coverage remains
content and balance work. Explicit weapon families and combat performance scaling
are implemented; broader activity coverage and balance remain tuning work.

Combat work is split between Melee and the explicit source family, with separate
defensive skill work and per-skill opponent difficulty gates. An opponent two
levels below gives one tenth credit; three or more below gives none, with a
per-encounter notice when an award is fully suppressed. Packets do not multiply work. See
[runtime skills](../doc/skills.md#combat-training).

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

Martial-art acquisition, art XP, repeatable chance-based manual attempts, moves
and active abilities are implemented. Manuals appear in ordinary loot; acquisition
does not automatically switch style. See [combat learning](../doc/combat_learning.md)
and [combat design](CombatDesign.md). Broader activity integrations, progression
coverage and measured tuning remain work; current UI and persistence details are
in the runtime documentation.
