# Gameplay message history and top bar

Both frontends show Help (F1), Settings (F2), and Debug (F11) keyboard
shortcut labels in the top bar. The terminal also shows LAN (F9). Labels
follow the active bindings. All gameplay and menu interaction uses the keyboard.

Set **Message history size** in Settings → Interface, or add
`message_history_size: 10` to the `settings` object in `settings.json5`.
The default is 10 entries and the supported range is 1–100. Global and world
overrides use the existing settings inheritance and persistence rules.

History is frontend session state. Consecutive messages with identical rendered
text share an entry, shown with a count from the second occurrence onward, for
example `The way is blocked. ×3`. Empty and filtered messages leave the latest
group open. Another logged message starts a new group. The capacity counts
groups. Reducing it drops the oldest entries immediately, and increasing it
preserves the remaining entries. History has no effect on simulation timing or
world saves.

Both frontends record meaningful outcomes and actionable failures. Successful
movement, waiting, queue acknowledgements, and routine startup and menu
progress stay out of history. Detailed strike events report resolved attacks.
Windups and combat refusals can have their own entries. Pending actions,
connection progress, and direction prompts are shown with the relevant live UI.
For commands submitted by the frontend, the HUD waits 200 ms before showing
pending or queued feedback. The shared `pendingCommandStatusDelay` applies only
to presentation.

Messages appear oldest first, with the newest at the bottom. Small windows
clip older rows while retaining the newest. The terminal reserves at most
half the gameplay height for history and displays one row per entry, clipping
long messages horizontally. Interaction and grab direction prompts appear after
history while active. SDL wraps history in its status panel and clips overflow
from the top. The SDL grab prompt appears in live status while active.

`MessageHistory` in `client/message_history.h` owns a
`SmallVector<MessageHistoryEntry, 10>` so the default entry storage fits inline.
`Add` records an occurrence, `SetCapacity` applies a validated setting, and
`Entries` borrows an `eastl::span<const MessageHistoryEntry>` in chronological
order. Use the borrowed entries before the next mutation. Each entry stores text
and a count so comparisons use the original text. `AppendMessageHistoryEntry`
adds each formatted entry to a caller-owned buffer and gives both renderers the
same suffix. `ShouldRecordOutcome` filters typed
command outcomes before history insertion, so suppression is independent of
translation. Callers handle storage failures separately from the outcome.

The terminal borrows history through `GameplayPresentation::history`.
SDL borrows history, key bindings, and localization through `GameplayHud`,
an optional `DrawFrame` argument. Both top bars render labels and route keyboard
input through the ordinary action handlers.

SDL packs the labels into one compact line and omits lower-priority entries
when needed. Its HUD and contextual windows are described in [SDL UI](sdl_ui.md).
