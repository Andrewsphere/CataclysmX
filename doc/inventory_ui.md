# Shared item UI

`client/item_ui.h` is a frontend-independent controller and value presentation.
`ItemUi` owns selection, prompts and captured commands; the frontend owns routing,
simulation timing and drawing. It stores persistent identities and snapshots,
never runtime entity pointers. `ItemExamination::art` borrows immutable catalog
data, so the catalog must outlive the UI and its rendered frame.

## Controls

Default gameplay **i** opens ordinary inventory; **u** opens its use-selection
variant. **/** opens advanced inventory. Ordinary inventory shows carried trees
followed by current-tile ground trees. Rows include names, counts, mass and volume;
the header reports total carried mass and carry capacity. Only snapshot entries
marked stackable, with the same type and exact immediate storage, are grouped.
Wielded items stay distinct. Group identities are ascending persistent IDs.
Mass uses g/kg and volume uses mL/L, preserving exact gram/milliliter precision
without floating-point rounding. Ordinary ground rows are explicitly labelled;
advanced tile titles include the keypad number and direction.

Menu up/down navigate. **Enter/e examines** in ordinary inventory; **d drops**,
**w wields/unwields**, **u uses**, and **a manages contents**. In use-selection mode,
Enter invokes use. One operational action executes or prompts directly; multiple
actions open a choice list. Contents actions are followed by Store and Extract,
which retain the existing authoritative `store_item` behavior. Escape backs out
of a prompt or examination, retaining the selected instance, then closes.
Selection also survives reopening the same workflow for the same player.

Examination resolves `item.<definition-id>.description`, falling back to authored
description and omitting absent prose. It displays fixed-column colored text art,
physical measurements, damage and penetration in HP (hundredths converted for
display), unmodified normal-player damage, attack ticks/hands, storage, tool
traits, capabilities, energy, activities and available actions. Up/down scroll;
the same use, contents, drop and wield controls work from examination. Art clips
at narrow widths and never wraps into a different picture.

## Advanced inventory

Initially the panes show **Inventory | All nearby**. Reopening retains their modes,
selections and scroll. **Tab** changes the active pane; **I** selects direct inventory.
Configurable uppercase **A** selects All nearby (lowercase **a** still manages contents).
Only one pane may show All nearby; selecting it on the other side swaps complete panes.
Fixed **1–9** use keypad directions (`7 8 9` north,
`4 5 6` west/current/east, `1 2 3` south), on the player's layer. Accessible object
storage at that tile takes priority over floor storage. **f** explicitly toggles
between object and floor. **o** opens portable container contents; **Backspace**
selects its immediate parent storage. Choosing the other pane's storage swaps
both panes, including their selected identities and scroll offsets.

All nearby shows only direct children of accessible floors and object storages in
the same-layer 3×3 neighborhood, never carried items or recursive container contents.
It excludes the opposite pane's entire world tile (both floor and objects), resolving
nested container parents to their root tile; carried containers exclude no tile.
Rows group only within an exact source and include labels such as `[7 floor]` and
`[6 cupboard]` in both frontends. Source order is north-to-south, west-to-east,
floor before objects, then object identity. Totals sum eligible source metadata
once, without invented aggregate capacity limits or double-counted contents.
Opening a container from All nearby enters that exact container; Backspace returns
to its real parent. Floor/object toggle uses the selected row's concrete source.

`ItemUiPane::mode` distinguishes `Storage` from `AllNearby`; `pane.storage` is
meaningful only in `Storage` mode. Every `ItemUiRow::storage` remains an exact
immediate source. This keeps aggregation in presentation rather than adding an
ambiguous engine destination. `ItemUiScreen::Destination` exposes its prompt and
captured choices through the existing frame fields. Nearby-source collection
reuses controller-owned scratch storage across refreshes.

**Enter** transfers one selected instance; **m** asks for a positive quantity
bounded by the selected stack; **M** transfers the entire stack. Escape cancels
quantity entry. Transfers into All nearby first show a Destination list of concrete,
accessible floor/object storages, including empty ones, excluding the source's
whole root tile. Choosing one does not replace the aggregate pane. For **m**,
destination selection precedes quantity entry; Escape from either prompt cancels.
Captured identities, accessibility and container ancestry are checked against fresh
snapshots; stale sources/destinations are refused rather than redirected.
**e** examines and returns to the same pane. Destinations are
exact: inventory means direct inventory, without autostashing. A container and
its tree form one command. Installed/reserved items and self ancestry cannot be
moved; authoritative access/capacity checks remain with the server. Totals and
limits come from `itemStorages`; unlimited sources do not acquire invented caps.
Quantity selection starts with the exact selected instance, then uses the
remaining ascending identities. A full-stack request uses ascending identities.

All action hints resolve the effective Items/Menu keymaps. The separate Items
context isolates inventory actions from gameplay and Settings. Numeric tile
selection and physical Escape are fixed controls.

## Command and time protocol

1. `Open` selects a workflow; `Refresh` reconciles authoritative snapshots and
   prepares `Frame`. Renderers may change only frame scroll offsets.
2. `Handle` captures exact item/source/destination/action identities. It never
   mutates world state. A vanished selection stays invalid until explicit
   navigation, including when other members of its old stack remain.
3. After input **and each authoritative refresh**, call `TakeCommand`. It returns
   at most one command, only at readiness (`remainingTicks == 0`, no queued
   command), and admits no second request while one is outstanding.
4. On admission failure call `CommandRejected`. Otherwise forward the exact final
   authoritative `CommandResult` to `CommandFinished`. A `queued` result is only
   a receipt and does not finish the request. This also permits synchronous local
   results during the submission callback.
5. A successful result advances a captured batch; refusal stops it with moved X/Y
   and a reason. Escape stops future requests without undoing an outstanding one.
   Ordinary inventory closes on final success; advanced inventory stays open.
6. While `NeedsTime` is true, allow normal local simulation time. This includes
   the final command's paid action time after the batch has no further commands.
   Only a fresh ready snapshot clears that waiting state. Browsing pauses again
   once committed work has drained. Discard wall-clock browsing intervals so
   opening or closing cannot trigger catch-up updates.

Snapshot revision/tick changes distinguish fresh readiness from a pre-command
ready frame. LAN clients use authoritative receipts and snapshots without local
prediction; hosts continue service while browsing. Player changes invalidate
prompts. Frontends must reset the controller on endpoint/world changes even if
persistent player/item IDs happen to be reused.

The value-frame API keeps terminal and SDL navigation identical while allowing
native rendering, clipping and scroll layout. Explicit captured command batches
keep partial progress and cancellation straightforward without introducing an
engine-side bulk transaction or a second gameplay timing model.
