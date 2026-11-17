# Zombie showcase

Start any frontend with `--scenario zombie-showcase` (optionally `--seed 42`).
The default pack is `data/zombie_showcase.json5`. SDL with the default Gen1
32x32 tileset shows all 64 authored zombie sprites, once each, plus the cellar zombie
and the doctor with fallback art, and a long-armed combat-type exhibit sharing the
existing construction-worker sprite. Terminal and headless frontends show the same named actors.
Use ordinary movement and tile inspection to browse the gallery.

`Server(content, seed, NewGameScenario::ZombieShowcase)` builds a square,
walled floor gallery centered on the origin. It places every undead recipe in
stable textual-ID order, three tiles apart, and starts the player in the north
aisle with a backpack and bandage. All exhibits have their AI controller removed,
so they neither move nor attack as time passes. They retain ordinary health and
combat statistics and can be attacked. Empty rosters produce an empty gallery.

The dedicated pack uses display recipes in `data/monsters/showcase.json5`.
Each recipe copies its production type's statistics and names both the gameplay
type and visual appearance. Optional Gen1 mappings bind each display recipe
directly to a sprite. This guarantees coverage without relying on randomized
variant selection, adding simulation appearance state, or changing normal town
spawns. The cellar zombie retains its production ID and shared civilian variant art;
the doctor retains its production ID and missing sprite mapping. The added
`zombie_longarms` retains its production ID, reach 2, aggressive style, native
damage, training ranks/ratings, and explicit inexhaustibility. There are **67
exhibits** in total: 64 original art exhibits, two fallback exhibits, and the
long-arms combat-type exhibit.

The Gen1 monster mapping for `zombie_longarms` uses the existing
`sprite.character.zombie.construction_worker` visual in both production and the
showcase. Shared `visual` references are the supported tileset alias mechanism;
monster `looks_like` is not a supported content field (terrain has a separate
appearance-inheritance field of that name). No bitmap or sprite metadata is
generated or replaced. Tests retain the 64 unique sprite checks and separately
verify the new exhibit's shared identity and matching production recipe.

The gallery's generated layout already enumerates every undead recipe, so the
additional definition automatically receives a deterministic, three-tile-spaced
placement. With 67 entries the nine-column layout remains sufficient. The new
reach-2 exhibit is also explicitly exercised next to the player in the harmless,
stationary-gallery regression test.
When adding zombie art or changing production stats, update these display recipes
and mappings; focused content and tileset coverage tests catch roster drift.

Explicit `--content` or `--content-pack` overrides the default pack. With custom
content the gallery contains one exhibit per undead recipe; full Gen1 variant
coverage comes from the bundled showcase pack. Other tilesets use their usual
fallbacks for display recipes without mappings. No new persistence or networking
fields are introduced: positions, identities and absent controllers use existing
serialization. To reload a showcase save, select `--scenario zombie-showcase` or
supply its pack explicitly so the content fingerprint matches.
