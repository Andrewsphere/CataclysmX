# Frontend-neutral combat UI

`client/combat_menu.h` exposes the approved value model:

```cpp
struct CombatMenuEntry {
    String label;
    String details;
    Command command;
    bool enabled = true;
    AbilityTargetKind targeting = AbilityTargetKind::Self;
};
void CollectCombatMenu(const ContentCatalog&, const WorldView&, Vector<CombatMenuEntry>&);
String CombatDetailsText(const ContentCatalog&, const WorldView&);
String CombatRefusalText(CombatRefusal);
String CombatEventText(const CombatEvent&, const ContentCatalog&);
```

The model owns values and persistent identities, borrows no world entities, draws
no RNG, and submits no commands. Output vectors belong to the caller. Catalog
order and server snapshot order determine row order. Rows keep unavailable choices
visible with reasons. Render `details` as multiline text, and disable confirmation
when `enabled` is false. The server remains authoritative when a snapshot becomes
stale or target-specific validation fails.

## Commands and choice identity

All basic styles (`art == 0`) and styles belonging to learned arts are listed.
Style details expose authored attack, defense, and counter weights/costs; current
move-query reasons/cooldowns are shown where provided. Stance weights select among
eligible defenses; they are not a flat defense-rating modifier.

Abilities use the exact `AbilityView.source` in `command.ability.source`. Two
identical ability IDs granted by different arts remain separate choices. Authored
costs are always visible; a ready query also shows its current cost (including
effective-skill stamina discount). Cooldown remaining and total cooldown use ticks.

For `Self`, send the captured command. For `Actor`, `Tile`, or
`CardinalDirection`, acquire a target, then replace only `command.ability.target`
with a fresh `AbilityTarget` of that kind. Tile coordinates are world coordinates;
directions are `North`, `East`, `South`, or `West`. Canceling acquisition submits
nothing. Preserve the ability ID and source throughout refresh and targeting.
`SameCraftingChoice` now also compares typed combat payloads when used by adapters
to preserve a captured choice; labels and changing details are never identity.

Art study commands carry both book-information ID and art ID. Manual rows display
eligibility/refusal, prerequisite learned skill, success probability per completed
attempt, duration, and remaining progress. Failed attempts may be repeated, and
paused attempts retain progress. The snapshot has no attempt counter or pause-
requested flag, so the UI does not invent either. An active attempt exposes a
`pause_art_study` row even while its paid slice is busy. Dead characters receive
the `new_character` row instead of combat actions.

`CollectLearningSummary` includes the shared player-combat details and manual
study summaries. Crafting-menu command collection retains its existing purpose;
frontends open combat actions through `CollectCombatMenu`.

## Inventory actions and frontend adapter handoff

`ItemUi::HandleAction(ItemUiAction, const WorldView&)` provides these direct actions:

| Exact enum member | Typed equipment payload | Suggested item-context binding |
| --- | --- | --- |
| `ItemUiAction::HoldLeft` | `Hold`, left, one-handed, selected item | `Shift+L` |
| `ItemUiAction::HoldRight` | `Hold`, right, one-handed, selected item | `Shift+R` |
| `ItemUiAction::TwoHanded` | `Hold`, two-handed, selected item | `Shift+H` |
| `ItemUiAction::Regrip` | `Regrip`, selected occupied hand, toggle 1H/2H | `Shift+G` |
| `ItemUiAction::Wear` | `Wear`, selected item | `Shift+W` |
| `ItemUiAction::Remove` | `Remove`, selected item | `Shift+T` |
| `ItemUiAction::Release` | `Release`, selected occupied hand | `Shift+U` |

These defaults are installed in the shared keymap. Printable uppercase letters
are represented directly (`L`, `R`, etc.). `ItemUiActionLabel` supplies labels. Call `Refresh` with the
fresh snapshot before a direct action, then use the existing `TakeCommand`,
`CommandFinished`, and `CommandRejected` lifecycle. Equipment completion recognizes
`Outcome::equipment_changed`. Regrip/release require the selected item to be held;
their typed requests identify a hand and have `item == 0`, as required by command
shape validation. Before issuing them, the UI checks that the captured selected
item still occupies that hand.

For ordinary items with no operational uses, the existing Use menu exposes all
seven equipment choices. Operational item-use workflows retain their existing
single-action shortcut. Direct actions work independently of operational menus.
The legacy Wield adapter continues to use `wield_item`.

Inventory names carry `[L]`, `[R]`, `[2H]`, and `[worn]`, and row values expose
`heldHands` and `worn`. Equipped items never form interchangeable stack groups,
even if an incoming snapshot incorrectly sets `stackable`. Character replacement
resets captured inventory state using `characterId` (legacy snapshots without it
fall back to `playerId`).

Gameplay defaults are `C` for combat actions and `@` for player information.
`4`, `5`, and `6` activate the stable starter identities `focus`, `leap`, and
`sweep` through the same targeting and command path as the menu. Their keys are
configurable through the existing keymap. There is no arbitrary ability-binding
editor. Menu navigation and the dead-character row use normal menu confirmation.

## Information and event presentation

`CombatDetailsText` explains the actual timing convention:
`ceil(base cost * speed / 100)`, with lower speed values meaning shorter actions.
It includes stamina regeneration/lock rates, attack/dodge rating formulas, defense
chance clamps, stamina/block efficiency, learned/effective combat skills, held
weapon families, art XP, and training difficulty. Runtime contact still depends on
the chosen move, source, target, armor, and injury; inspection does not claim a
predicted player-damage number.

Regional encumbrance lists head, eyes, mouth, ears, arms, hands, legs, and feet
separately (or simple-anatomy zones). Item inspection includes armor layer,
regional coverage, encumbrance, rigidity/overlap exemption, per-damage-type
protection, and wear/remove cost. Coverage remains probabilistic.

`CombatEventText` is the canonical event formatter. It distinguishes misses,
dodges, parries, block absorption, armor-stopped contact, actual HP/max-HP loss,
counters, death, invalidation, commitments, art attempts, and training-suppression
reasons. A suppression event describes affected awards: another skill's award can
still succeed. Existing `CombatFeedbackText` remains available for last-result
presentation. `Localization::OutcomeText` handles all expanded command outcomes;
`ResultText` includes the typed combat refusal reason with English fallback.

### Event identity

Frontend owners own message history and deduplication. Reformatting a snapshot
must not append the same server event again; identical text is not identity, and
two equal strikes in one tick must both survive. Tick plus vector index is not
stable when the bounded server history evicts its prefix.

`CombatEvent::serial` is monotonically increasing and nonzero within a published
server/session stream, and is preserved across snapshots and transport. The server
assigns serials after draining resolution-ordered events; raw experiment events
can retain zero. Reset frontend cursors only on a new server/session
stream, not on menu open or character replacement. On refresh, append events with
`serial > lastSeenSerial` in server order, formatting each through
`CombatEventText`, and advance the cursor. A rolling-history gap may lose already-
evicted events but must not duplicate retained ones. The pure formatter does not
own the deduplication cursor; each frontend tracks its own received stream.

## Integration and checks

The build registers `src/client/combat_menu.cpp` and
`tests/combat_ui_tests.cpp`. The focused test tag is `[combat-ui]`; tests cover
style knowledge/readiness, source identity and targeting, manual attempts/pause,
event distinctions, regional encumbrance, typed equipment command shapes,
equipped stack exclusion, armor inspection, and expanded outcome fallbacks.
See [verification](combat_verification.md) for the integrated frontend checks.
