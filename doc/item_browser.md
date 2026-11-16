# Debug item browser

Open **F11**, then **i** (the Debug-context Inventory binding) in either SDL or
the terminal. The browser lists the current content pack's item definitions in
stable-ID order, excluding the internal unfinished-craft wrapper. Names are
localized; stable IDs appear alongside them.

| Key | Action |
| --- | --- |
| Up / Down | Select an item |
| Home / End / PageUp / PageDown | Navigate the list |
| `/` | Edit a live substring search over name and stable ID |
| `e` | Examine description, colored art, stats, capabilities, and flags |
| Enter | Give one selected item while in the list |
| `m` | Enter a count from 1 to 1000, then Enter to give that batch |
| Backspace / Ctrl+u | Delete one UTF-8 scalar / clear the edited input |
| Escape | Cancel input or leave examination; from the list, return to debug |
| F11 | Close the entire debug menu |

Enter in search commits the query; it does not give an item. Search folds ASCII
case; non-ASCII names match their literal UTF-8 text. Queries are bounded to 128
bytes. Empty queries show all items. Invalid quantities remain in the amount
prompt with feedback. Empty/no-match lists cannot give or examine an item.
Examination is a definition preview: containers are empty and energy stores show
their authored initial charge. No runtime entity is created for examination.

## Shared controller

`client/item_browser.h` provides `ItemBrowser` and its owning `ItemBrowserFrame`.
`Open` builds localized labels once; filtering reuses a vector of catalog IDs.
`Handle` receives control chords and `Text` receives committed text separately,
avoiding duplicated SDL key/text events. `Frame` exposes presentation state;
frontends may update list/examination scroll offsets. Artwork borrows immutable
catalog data, which must outlive the browser.

`TakeGiveRequest` emits one `DebugItemGrant` and marks it outstanding. Another
grant cannot be queued until `GiveFinished` or `GiveRejected`. Closing cancels
an unsent request; an already dispatched request remains outstanding across
reopening. Frontends reset the browser when the session generation changes and
correlate asynchronous replies by request ID.

The existing item sheet renderer is reused for SDL examination. Terminal rendering
uses the same definition/examination values and RGB runs. Ordinary inventory
selection and gameplay input remain independent of this catalog-only controller.

## Authoritative grant boundary

`core/debug_items.h` defines:

```cpp
DebugGiveResult DebugGiveItems(Server& server, PersistentId player, DebugItemGrant request);
```

This owner-thread administrative API accepts only the available local/host actor,
a normal catalog item, and a count in `[1, maxDebugGiveItems]` (1000). It preflights
the whole batch's mass before creating normal initialized inventory roots. Failure
returns `Unavailable`, `InvalidItem`, `InvalidCount`, or `CapacityExceeded` with
zero granted items; successful `Given` reports the requested count. Expected
refusals do not consume IDs, mutate inventory, or advance time. World revision and
save dirtiness follow ordinary `World::CreateItem` behavior.

The cap bounds work per debug request rather than imposing an item-storage limit.
Flags, capabilities, and runtime state use normal creation paths. Typed outcomes
allow frontends to display useful feedback without exceptions or partial batches.

SDL carries `DebugItemGrant` in a dedicated `DebugGiveItems` session request,
handled exclusively on the session owner. Local requests require an acknowledged
pause; host requests require menu ownership. Busy recovery, wrong player,
wrong generation, and guest requests are rejected. The owner publishes the new
snapshot before its `ItemsGiven` reply. Local simulation stays paused; LAN hosts
continue their existing live-world behavior. This API is not exposed as a network
gameplay command, and guests cannot invoke it through the protocol.

The terminal uses an optional synchronous grant callback on
`CreateWorldgenDebugMenu`. Production wiring calls the same administrative API;
components without the callback can browse/examine and report giving unavailable.

## Coverage

`item_browser_tests.cpp` covers search/editing, definition examination, quantities,
selection, and request lifecycle. `debug_items_tests.cpp` covers initialized
instances, complete-batch preflight, rejected requests, and the count boundary.
Terminal debug-menu tests exercise the actual key workflow; SDL session tests
cover owner-thread grants, pause/live-host behavior, and rejection boundaries.
