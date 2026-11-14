# Gameplay message history and top bar

Both frontends show Help (F1), Settings (F2), and Debug (F11) keyboard
shortcut labels in the top bar. The terminal also shows LAN (F9). Labels
follow the active bindings. All gameplay and menu interaction uses the keyboard.

Set **Message history size** in Settings → Interface, or add
`message_history_size: 10` to the `settings` object in `settings.json5`.
The default is 10 messages and the supported range is 1–100. Global and world
overrides use the existing settings inheritance and persistence rules.

History is frontend session state. Each posted message is appended, including
identical consecutive outcomes and multiple replies in one update. Empty
messages are ignored. Reducing the capacity immediately drops the oldest
entries; increasing it preserves the remaining entries. History is not saved
with the world and does not affect simulation or gameplay timing.

Messages appear oldest first, with the newest at the bottom. Small windows
clip older rows while retaining the newest. The terminal reserves at most
half the gameplay height for history and displays one row per message;
long messages are clipped horizontally. Interaction direction prompts appear
after history without replacing it. SDL wraps text within its status panel
and clips overflow from the top.

`MessageHistory` in `client/message_history.h` owns `SmallVector<String, 10>`:
the default history fits inline, and larger configured histories spill.
`Add` records an event, `SetCapacity` applies a validated setting, and `Entries`
returns `eastl::span<const String>` in chronological order. Callers must not
retain the span or element references across mutations. The small bounded collection keeps
ownership explicit and avoids allocating a copied list during rendering.

The terminal borrows history through `GameplayPresentation::history`.
SDL borrows history, key bindings, and localization through `GameplayHud`,
the final optional `DrawFrame` argument. Both top bars only render labels;
keyboard input follows the normal action routing.

SDL packs the labels into one compact line and omits lower-priority entries
when needed. Its HUD and contextual windows are described in [SDL UI](sdl_ui.md).
