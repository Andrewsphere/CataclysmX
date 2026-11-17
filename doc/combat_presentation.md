# Combat presentation

`CombatStatusText` and `CombatFeedbackText` format authoritative `CombatView`
values, returning empty text for unavailable data. `CombatDetailsText` and
`CollectCombatMenu` provide owned information and command choices, including
styles, ability sources, costs, cooldowns, refusals, equipment and art study.
`CombatEventText` distinguishes combat outcomes, injury, death and suppressed
training. Presentation does not change simulation or draw RNG. See
[frontend-neutral combat UI](combat_ui.md) for the value model and
[gameplay UI](gameplay_ui.md#combat-windows-and-targeting) for the integrated
frontend and event serial handling.

Default gameplay keys **C** and **@** open combat actions and character information.
**4/5/6** select the stable starter ability names Focus, Leap and Sweep, respectively,
using an available grant and its exact source. These keys are configurable; they
are not a general arbitrary-ability binding editor. Inventory **L/R/H/G/W/T/U**
hold left/right, hold two-handed, regrip, wear, remove and release. Menus display
unavailable actions and reasons. Targeting and cancellation spend no action cost;
the server validates a submitted command. After death, **C** offers a new character
in the same world. Art manuals can be found in loot and studied from the combat
menu; knowing an art does not automatically select its style.
