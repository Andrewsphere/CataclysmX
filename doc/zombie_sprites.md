# Zombie sprite batch

Gen1 includes all 64 transparent 32x32 sprites from ComfyUI's
`sprite_variations/final_blood_batch`. Native RGBA pixels and alpha are preserved;
lossless PNG re-encoding removes embedded generation/workflow metadata. Source
filenames, descriptions and monster assignments are recorded in
[zombie_sprites.json](zombie_sprites.json). The imported PNGs are editable assets
under `assets/tiles/gen1_32x32/sprite/character/zombie`.

Related occupations and environmental appearances share existing specialist
types through local variant definitions. For example, chef, kitchen porter and
baker use `zombie_cook`; environmental names imply no special attacks or effects.
Distinct roles without an existing match add 23 definitions in
`data/monsters/specialists.json5`, with ordinary sighted zombie behavior, speed
100, damage 10 and accuracy 9000. Existing monster statistics are unchanged.

The manifest maps 34 monster types, including varied civilian appearances for
`cellar_zombie`. The batch contains no doctor sprite: `zombie_doctor` retains the
generic NPC fallback. All 23 new roles spawn through suitable existing town
groups: cashiers and dealers among shoppers, paramedics and lab technicians in
medical sites, professors and librarians in schools, maintenance workers and
miners in workshops, and civilian or traveling roles in residential, outdoor,
office and warehouse populations. Existing entry weights are multiplied by four
before adding lower-weight new roles, keeping established specialties common.
Marker placement, spawn chances, counts and existing monster stats are unchanged.
Species selected for a seed change with the updated content. Adding definitions
and updating groups changes the content fingerprint; persistence and network
formats are unchanged.
