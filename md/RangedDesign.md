# Ranged Combat and Firearm Design

## Status and goals

This is a design for future work, not a description of implemented firearm
behavior. The core direction is agreed; sections marked **proposal** give a
starting point for unresolved details. See [Combat Design](CombatDesign.md) for
the shared combat model.

- Make routine shooting fast in multiplayer without holding up other players.
  Keep a detailed aiming panel for deliberate choices and inspection.
- Give firearms depth through decisions about when to shoot, how many rounds to
  commit, what to carry, and how to reload. Routine actions should not require
  working through a menu.
- Let the ammo load determine what a hit does. Let the gun determine how readily
  the shooter can land that hit, follow it up, and get ready for the next fight.
- Prefer readable outcomes and deterministic simulation over physical detail that
  doesn't change decisions.

## Quick targeting and detailed aiming

Holding a usable ranged weapon means the character is always ready to aim.
**Fire** takes a snap shot when the character can act; raising the weapon or
entering a targeting mode is not a required action first.

For now a character can hold only one firearm at a time. A two-handed grip
still counts as one gun, and spare firearms can be carried in inventory. There
is no dual-wielding or active-gun selector; normal hand and grip requirements
still apply.

- **Aim Direction + movement key** selects an eight-way sector without moving the
  character. It highlights a visible hostile, favoring a clear shot near the
  chosen bearing. The sector is a search shortcut, not a restriction on facing
  or movement.
- **Next Target** cycles visible hostiles in that sector. When several targets
  share a sector, show direct choices such as `1 Runner · 2 Spitter · 3 Brute`.
  Keep those assignments stable during selection; if `2` disappears, it becomes
  unavailable rather than silently pointing at another actor. A selected actor
  stays selected as it moves while still visible, even out of the sector.
- **Fire** uses the selected actor or tile without opening a menu. Losing an
  actor target never causes the game to pick a replacement for the shot.
- The **aiming panel** opens on the same selection and uses the same Fire action.
  It adds a tile cursor, a preview of the intended path, and an explanation of
  the major factors affecting the shot. A line such as
  `Fair shot · 7 tiles · partial cover · unsettled from sprinting` is more useful
  than an unexplained percentage. Deliberate body-region targeting is not
  required for the first pass.

Selection, cycling, and inspection are local UI operations, not simulation
commands. Directions, target selection, firing, and mode switching should be
rebindable. Opening an aiming panel neither pauses the session nor extends input
grace. There is no special aiming hold that makes teammates wait.

## Steadiness, accuracy, and multiplayer time

If a player submits no command and the server advances through an automatic
wait, a character standing ready with a gun **steadies the weapon**. The same
elapsed simulation time under the same conditions gives the same improvement
during an explicit wait. The deadline expiring awards nothing by itself; paused
simulation, menu time, and repeated targeting inputs don't build aim.

- **Settled spread** is the gun's precision floor under favorable conditions.
  **Current stability** reflects how far the shooter is from that floor.
- Moving, firing, or being disrupted makes the weapon less steady. It settles
  over eligible simulation time at a rate affected by the gun and the shooter.
  Time spent moving or reloading isn't a substitute for standing ready.
- Stamina and the condition and encumbrance of the limbs used for the grip
  affect achievable steadiness and handling. Sight-related encumbrance can
  affect a shot when relevant. Don't apply unrelated encumbrance globally.
- Range changes how much the same spread matters. The same shooter and gun can
  make a good close shot and a poor distant shot without changing hit damage.
- Passive steadiness belongs to the shooter and held weapon, not the currently
  highlighted actor. It carries over when you switch targets. A separate paid
  **Steady** action for extra precision on one actor or tile is optional; test
  passive steadiness alone first. If added, that extra precision does not
  transfer when changing targets.

The server owns action timing and the shot result. Queue an explicit Fire
request at the next valid action opportunity if the shooter is still completing
an automatic wait; don't backdate it or change its target. The current
[pacing system](../doc/tick_scheduling.md) makes an expired opportunity an actual
wait. If this still feels like a lockout in playtests, consider making *automatic
idle only* interruptible at a simulation boundary. Don't shorten reload or fire
recovery, award extra aim for a timeout, or block another player to solve it.

## Ranged skills and training

Follow the [melee skill model](../doc/skills.md#combat-training): **Aiming** is
the general ranged skill, and each gun has one explicit handling category.
Start with **Handguns**, **Submachine Guns**, **Rifles**, and **Shotguns**. A
gun's category is independent of its ammo: the Glock 17 and MP5 use the same
load but train different handling skills.

Aiming governs shot placement across categories. Category skill governs
handling the gun, including controlling recoil and recovering between shots.
Exact bonuses and whether category skill also shortens reloads are balance
decisions. Use effective skill levels for performance and learned levels for
any unlocks, as in melee.

Split authored combat work between Aiming and the relevant weapon category,
using the existing per-skill opponent-difficulty rules. Divide a firing
string's work over its planned rounds, as melee does for multi-strike actions;
pellets and damage packets don't multiply training. Browsing targets and
passively settling during a wait don't award combat practice by themselves.

## Shot paths, obstacles, and body contact

Ordinary firearm rounds resolve immediately **on their firing tick**, within
the game's roughly 100-tile interaction envelope. The first version supports
**same-level shooting only**. No ordinary bullet flight time, aiming ahead for
travel time, distance-based damage or penetration loss, or separate range cutoff
per gun is needed by default. Reserve simulated travel for projectiles slow
enough that reacting to them in flight matters.

**Visibility lets you attempt the shot; it doesn't guarantee a clear path.**
Projectile rules are independent of sight and movement rules. Distinguish three
effects rather than using one universal cover modifier:

| Tile or obstacle effect | What it does to the shot |
| --- | --- |
| **Projectile blocker** | Stops a round that intersects it, even if the tile is see-through and the actor beyond is visible. |
| **Damage attenuation** | Reduces the round's remaining damage when it passes through, potentially to zero. A round reduced to zero does no damage beyond that obstacle. |
| **Half-cover** | Makes the protected actor harder to hit. It does not reduce the damage of a hit that lands. |

Trace the actual round after applying spread. Don't also treat half-cover as a
damage penalty, or apply attenuation merely because a target is near an
obstacle: the round has to pass through the attenuating tile.

Other actors can intercept the round; a miss can hit another actor farther along
its path. **There are no friendly-fire safeguards**: no veto, confirmation, or
automatic burst cutoff because an ally enters the lane. Allies use the same
contact rules as anyone else. Draw the shooter's actual round trace as a straight
line from the shooter to the resolved endpoint. Don't clip the line to current
visibility, fog of war, or map memory. This is a rendering rule; the line
doesn't update map memory. The pre-shot preview is separate, advisory, and
uses only the shooter's available information.

When a round hits an actor, use the same weighted body-region selection
mechanism, anatomy, armor coverage, and injury handling as melee. Ranged combat
supplies the eligible regions and weights; melee reach restrictions and
defensive reactions do not automatically apply. Cartridge load supplies the
base damage and armor penetration. Two guns firing the same load should do the
same damage on an equivalent hit through equivalent protection. A gun-specific
impact modifier isn't needed at first; if a particular pairing eventually needs
one, model that exception rather than giving all larger guns flat damage bonuses.

## Fire modes and timing

Quick fire and the aiming panel share the same selected fire mode. The mode is
visible on the HUD and switching it doesn't require opening the panel or reset
steadiness by itself.

| Mode | One press of Fire | Tradeoff |
| --- | --- | --- |
| Single | One round. | Spend less ammo, disturb aim less, and act again sooner. |
| Burst | A short, authored count, such as three rounds. Releasing the key doesn't cancel it. | Several rounds with one input and a predictable commitment. |
| Full auto | A longer but bounded string in the first pass. | More rounds before a target can escape, but more ammo, more spread, and a longer commitment. |

Rounds in a string fire on separate simulation ticks. The first uses the same
starting steadiness as a comparable single shot. Each subsequent round uses the
then-current stability and world state: recoil accumulates, settling can happen
between rounds, and enemies or allies can move. The server schedules the string
from one trigger command, not one network command per round. Firing commits the
shooter's time, not a teammate's.

Only rounds actually fired spend ammunition. A string stops when it runs out
of fireable ammo or the shooter can no longer fire. It never retargets another
actor.
If a committed string loses sight of its target, the remaining rounds aim
toward the target's **last known tile**, not its hidden position. A string
explicitly aimed at a tile keeps firing there even if an actor leaves it.
An ally crossing the path is not a stop condition. Hold-to-continue or
release-to-stop for full auto can come later if bounded strings aren't
satisfying.

## Guns, loads, and magazines

Keep the source of each property clear:

| Owner | First-pass properties |
| --- | --- |
| Gun | Grip requirements and handling under movement or limb impairment; settled spread; recoil per round and settling rate; allowed fire modes, string size, cadence, and post-fire recovery; chamber layout and built-in ammo capacity, compatible ammo and magazines, cycling and base reload costs; weight and bulk. |
| Ammo load | Cartridge compatibility, damage, and armor penetration. Distinct loads can fit the same gun while behaving differently on impact. |
| Detachable magazine | Gun/mount compatibility, supported ammo, capacity, current load, and remaining rounds. Capacity does not belong to the gun when the magazine is detachable. |
| Shot noise | One authored loudness for the first pass; split contributions between gun and load only when content needs it. |

Handling, settling rate, and post-fire action recovery are different: one
determines how well the gun responds to movement and impairment, one how quickly
accuracy recovers, and one how soon the character can take another action. Avoid
making gun rarity or size increase all three, or damage, together.

A compact handgun and shoulder-fired carbine using the same load can land
equally damaging hits. The handgun can be easier to carry and use immediately
after moving; the carbine can be steadier once settled and on follow-up shots.
Compare these differences in the aiming loop rather than relying on a single
weapon score. Condition, malfunctions, formal suppression, and detailed
material penetration are deferred.

## Attachments

Customize guns only with removable attachments. There are no permanent
workbench modifications, caliber or fire-mode conversions, or internal
upgrade trees. Each gun defines which mounts it has and which items fit.
Reuse the [item-mount model](../doc/items_api.md#ownership-volume-and-mass):
an installed attachment is still an item, and each mount holds one.

A **detachable magazine is an attachment** in the gun's magazine mount.
The magazine item still owns its capacity, ammo type, and round count.
**Reload** is the shortcut for swapping it; the player doesn't have to open
an attachment menu. Built-in magazines and chambers are part of the gun,
not attachments.

Detachable magazines are the first attachment type. A red dot and a magnified
optic are candidates for a later shared optic slot: one helps snap shots, the
other helps settled shots at distance. Neither changes cartridge damage.
Consider supports only once bracing matters and suppressors once AI hearing
makes shot noise a gameplay cost.

## Loading and reloads

A detachable magazine can be partially loaded. It holds one ammo load at a time:
top it up with the same load, or empty it before changing loads.

| Loading type | Routine reload | Faster option |
| --- | --- | --- |
| Detachable magazine | Choose a spare in the quick reload picker, swap it in, and stow the old one intact. | **Drop-mag reload** drops the old magazine at the shooter's tile with its remaining rounds. |
| Built-in magazine (such as a tube) | Top up from accessible loose rounds; no detachable item is swapped. | **Load one** when there isn't time to fill it. |
| Direct chamber loading (single-shot, break-action, or cylinder) | Load loose rounds into available chambers. | Load only one now on a gun that can take more. |

For a detachable reload, time comes from the gun's base swap cost, access to
the spare, and where the old magazine is stowed. A pouch can be faster than a
pack. Apply relevant hand impairment or encumbrance once. Dropping skips the
stow cost, not retrieval or insertion. Ordinary Reload never silently drops
gear; if there is no room to stow the old magazine, explain why and offer the
explicit drop option.

**Quick Reload** for detachable magazines opens a small side modal listing all
available compatible spares and the reload time for each. Show the ammo load,
round count, and where each spare is carried so the player can compare a fast
partial mag with a slower full one. The displayed time reflects whether the
player is stowing or dropping the old mag. Browsing doesn't spend simulation
time or extend input grace; confirming a choice submits the reload. Bind that
specific spare when the action begins. If it becomes unavailable before
completion, don't silently substitute a different one.

**First-pass reload proposal:** treat each request as a single timed action
that commits at completion, with no intermediate equipment states. If it can
be interrupted, an interrupted swap neither inserts nor drops a magazine.
Direct loading can choose **Load one** or **Top up**, with time based on round
count and access to loose ammo, without requiring one keypress per round.

### Chamber state

Read the ammo type and remaining rounds directly from the inserted magazine
item; the gun doesn't keep a copy. The gun records what's in its chambers
and any built-in magazine. Swapping a detachable magazine leaves an already
chambered round in place.

**Fire** and **Reload** handle routine chambering and cycling as part of their
normal timing, without a separate keypress. Cycling uses only ammo already
loaded in the gun. Show chamber state when it matters, such as
`1 chambered · mag empty` or `Next round: previous load`.

## Sound and feedback

Gunfire should use shared world/AI hearing, not a special firearms-only alert
system. [Entity AI Design](EntityAIDesign.md#staged-implementation-and-verification)
lists sight as implemented and hearing as future work; there is no existing
hearing field to plug into in the current baseline. Emit one noise event per
round actually fired when that consumer exists, and let the shared hearing
system handle propagation and aggregation. The first ranged slice need not
implement an acoustic map just to make a gun fire.

**Feedback proposal:** Show the shooter the actual round traces. Keep the quick
HUD to essentials such as `BURST · 7 loaded`; the panel can show
`Up to 3 rounds · first shot Good, later shots Fair` with no promise about future
movement or impacts. Add short outcome text when the trace alone doesn't
explain what happened: `Miss`, `Stopped by cover`, `Hit through cover —
reduced damage`, `Hit ally — left arm`, `Armor stopped it`, or
`Burst ended — empty`. Group repetitive burst results instead of making the
player read a line per round. The exact presentation still needs playtesting.

## Starting arsenal and later additions

Use real firearm names. The [setting](../doc/city_identity_design.md#society-wide-centralized-computing)
supports both civilian leftovers in older towns and concentrated institutional
stock. These are loot-placement proposals, not a statement about the setting's
country or firearm laws. The
[police loot pool](../data/worldgen/loot/urban.json5#L69) already contains a
generic pump shotgun and shells; the Remington 870 takes that role rather than
adding a duplicate.

| First playable roster | Ammo | Role and proposed source |
| --- | --- | --- |
| **Glock 17** | 9×19 mm | Baseline snap-shot handgun and detachable reloads. Police duty lockers; occasional civilian finds if the setting supports them. |
| **HK MP5** (burst-equipped variant) | 9×19 mm | Shoulder-fired counterpart to the Glock with different magazines. Tests single, burst, and full auto. Rare police-response or specialist security stock, not routine guard loot. |
| **Remington 870** | 12 gauge | Pump shotgun with a fixed tube. Tests loading one shell versus topping up. Keep it in police-station loot. |
| **Stoeger Uplander** | 12 gauge | Side-by-side break-action hunting shotgun. Tests loading one or both chambers directly without a magazine. Occasional farmhouse or older sporting-goods stockroom find. |

The first four cover two ammo families. The Glock and MP5 test how handling
and fire modes distinguish guns using the same load. The 870 and Uplander test
how a shared shell feels different when the loading method and follow-up timing
change.

| Later addition | Ammo | Role and proposed source |
| --- | --- | --- |
| **Ruger PC Carbine** | 9×19 mm | Civilian semiautomatic carbine for settled single shots. Sporting-goods stockrooms and occasional preparedness caches. |
| **CZ 457** | .22 LR | Civilian bolt-action for deliberate shots against lighter threats. Cycling affects follow-up timing without a separate input. Sporting-goods shops and rural homes. |
| **Colt M4A1** | 5.56×45 mm | Military select-fire carbine with its own ammo supply. Reserve for military or emergency-response stock once suitable sites exist. |
| **KelTec KSG** | 12 gauge | Exotic shotgun with two selectable fixed tubes, each holding one load. A rare specialty retailer or security procurement find; add it after there are two shell loads worth switching between. |

## Playtest scenarios

- A teammate keeps moving while the shooter picks the second of three enemies
  in one sector. The selection doesn't jump, the shooter steadies during an
  automatic wait, and the teammate isn't held by the targeting UI.
- Compare a visible enemy behind a see-through projectile blocker, an
  attenuating tile, and half-cover. The first stops an intersecting round, the
  second weakens it, and the third changes hit chance only.
- An ally crosses the lane during a burst and the target moves out of sight.
  The ally can take a hit. The remaining rounds go toward the target's last
  known tile, not its hidden position; the actual trace is drawn even through
  fog of war.
- A handgun and a carbine share a load. Compare them immediately after moving,
  after settling, during follow-up shots, and with an impaired support arm.
  An equivalent hit isn't stronger just because it came from the carbine.
- Compare a partial-magazine swap with a spare in a pouch or a pack, then with
  the old mag dropped. Check that the quick reload picker shows the time for
  each choice. Compare those choices with loading one loose round or topping up
  a directly loaded gun.
- Swap to a different ammo load with one round chambered. The first shot
  uses the retained round, the next uses the new magazine, and neither requires
  a separate chambering input.
- A good first shot doesn't make full auto strictly better than single or
  burst: ammo, recoil, and time until the next action all matter.

## Decisions still open

1. **Cover tuning:** How strong is half-cover, and which terrain or furniture
   attenuates damage rather than stopping rounds outright?
2. **Skill tuning:** How should work be split between Aiming and category
   skills, and which handling delays should category skill shorten?
