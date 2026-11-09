#pragma once

#include "core/diagnostics.h"

#include <EASTL/expected.h>
#include <EASTL/optional.h>
#include <EASTL/string_view.h>

namespace cataclysmx {

// A key is one UTF-8 scalar or an explicit name (Left, Enter, F2, ...).
// Character case is significant even with Ctrl. Shift and Alt are not bindings.
struct KeyChord {
    String key;
    bool ctrl = false;
    bool operator==(const KeyChord&) const = default;
};

enum class InputContext { Gameplay, Interaction, Menu, Debug, Items };
enum class InputAction {
    West, East, North, South, Wait, PickUp, Drop, Interact, Save, Quit,
    Settings, Debug, RateDown, RateUp, ToggleRate, ToggleIdle, Cancel,
    Accept, Next, Previous, JumpWest, JumpEast, JumpNorth, JumpSouth,
    Recenter, Layer1, Layer2, Layer3, Layer4, Layer5, ZoomIn, ZoomOut,
    DetailsUp, DetailsDown, Apply, SaveSettings, Reload, Inspect, AddBinding, Unbind, Reset,
    Inventory, Players, Help, QuickActions, Use, MoveDown, MoveUp
};
struct InputBinding {
    InputContext context = InputContext::Gameplay;
    InputAction action = InputAction::Wait;
    Vector<KeyChord> keys;
    bool operator==(const InputBinding&) const = default;
};
using Keymap = Vector<InputBinding>;

enum class InputError { InvalidKey, InvalidAction, Conflict, ReservedKey };
eastl::string_view ContextName(InputContext context);
eastl::optional<InputContext> ParseContext(eastl::string_view name);
eastl::string_view ActionName(InputAction action);
eastl::optional<InputAction> ParseAction(eastl::string_view name);
bool ValidKey(const KeyChord& chord);
bool LegacyKeyAvailable(const KeyChord& chord);
String ChordName(const KeyChord& chord);
Keymap DefaultKeymap();
Keymap ResolveKeymap(const Keymap& global, const Keymap& world);
eastl::expected<void, InputError> ValidateKeymap(const Keymap& map,
    Diagnostics* diagnostics = nullptr);
eastl::optional<InputAction> ResolveAction(const Keymap& map, InputContext context,
    const KeyChord& chord);
String BindingHint(const Keymap& map, InputContext context, InputAction action);
void SetBinding(Keymap& overrides, InputBinding binding);
void ResetBinding(Keymap& overrides, InputContext context, InputAction action);

}
