#include "client/input.h"

#include <EASTL/algorithm.h>
#include <EASTL/utility.h>

#include <cstdlib>
#include <initializer_list>
#include <iterator>

namespace cataclysmx {
namespace {

constexpr eastl::string_view actionNames[] = {
    "west", "east", "north", "south", "wait", "pick_up", "drop", "interact", "save", "quit",
    "settings", "debug", "rate_down", "rate_up", "toggle_rate", "toggle_idle", "cancel",
    "accept", "next", "previous", "jump_west", "jump_east", "jump_north", "jump_south",
    "recenter", "layer_1", "layer_2", "layer_3", "layer_4", "layer_5", "zoom_in", "zoom_out",
    "details_up", "details_down", "apply", "save_settings", "reload", "inspect", "add_binding", "unbind", "reset",
    "inventory", "players", "help", "quick_actions", "use", "move_down", "move_up", "attack",
    "style_balanced", "style_aggressive", "style_defensive", "examine",
    "northwest", "northeast", "southwest", "southeast", "crafting", "grab",
    "advanced_inventory", "wield", "transfer_amount", "transfer_stack", "open_container", "parent_storage", "toggle_storage",
    "combat_menu", "combat_info", "hold_left", "hold_right", "two_handed", "regrip", "wear", "remove_worn", "release_hand",
    "ability_focus", "ability_leap", "ability_sweep", "all_nearby",
    "switch_settings_scope", "previous_settings_section", "next_settings_section"
};
constexpr eastl::string_view contextNames[] = {"gameplay", "interaction", "menu", "debug", "items", "shared", "shared_layer"};
constexpr InputContext playContexts[] = {InputContext::Gameplay, InputContext::Interaction,
    InputContext::Menu, InputContext::Debug, InputContext::Items};
constexpr InputContext playInteract[] = {InputContext::Gameplay, InputContext::Interaction};
constexpr InputContext playDebug[] = {InputContext::Gameplay, InputContext::Interaction, InputContext::Debug};
constexpr InputContext playItems[] = {InputContext::Gameplay, InputContext::Interaction, InputContext::Items};
constexpr InputContext dropContexts[] = {InputContext::Gameplay, InputContext::Items};
constexpr InputContext layerContexts[] = {InputContext::Gameplay, InputContext::Interaction, InputContext::Debug, InputContext::Items};
constexpr InputContext menuDebug[] = {InputContext::Menu, InputContext::Debug};
struct SharedDefault {
    InputAction action;
    const char* key;
    eastl::span<const InputContext> contexts;
    InputContext context = InputContext::Shared;
};
constexpr SharedDefault sharedDefaults[] = {
    {InputAction::West, "Left", playContexts}, {InputAction::East, "Right", playContexts},
    {InputAction::North, "Up", playContexts}, {InputAction::South, "Down", playContexts},
    {InputAction::Cancel, "Escape", playContexts},
    {InputAction::Help, "F1", playInteract}, {InputAction::Settings, "F2", playInteract},
    {InputAction::Debug, "F11", playDebug}, {InputAction::Inventory, "i", playDebug},
    {InputAction::AdvancedInventory, "/", playInteract}, {InputAction::Crafting, "&", playInteract},
    {InputAction::CombatMenu, "C", playInteract}, {InputAction::CombatInfo, "@", playInteract},
    {InputAction::Players, "P", playInteract}, {InputAction::QuickActions, "a", playInteract},
    {InputAction::Quit, "q", playInteract}, {InputAction::Use, "u", playItems},
    {InputAction::Drop, "d", dropContexts},
    {InputAction::MoveDown, "<", layerContexts}, {InputAction::MoveUp, ">", layerContexts},
    {InputAction::MoveDown, "Down", playDebug, InputContext::SharedLayer},
    {InputAction::MoveUp, "Up", playDebug, InputContext::SharedLayer},
    {InputAction::Next, "Tab", menuDebug}, {InputAction::Previous, "BackTab", menuDebug},
    {InputAction::DetailsUp, "PageUp", menuDebug}, {InputAction::DetailsDown, "PageDown", menuDebug},
};
KeyChord SharedDefaultChord(const SharedDefault& entry) {
    return {entry.key, false, entry.context == InputContext::SharedLayer};
}
constexpr eastl::string_view namedKeys[] = {
    "Left", "Right", "Up", "Down", "Escape", "Enter", "Tab", "Backspace", "Delete",
    "Insert", "Home", "End", "PageUp", "PageDown", "BackTab", "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12"
};

bool OneScalar(eastl::string_view text) {
    if (text.empty()) { return false; }
    const auto first = static_cast<unsigned char>(text[0]);
    std::size_t size = 1;
    std::uint32_t scalar = first;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { size = 2; scalar &= 31; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { size = 3; scalar &= 15; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { size = 4; scalar &= 7; minimum = 0x10000; }
    else if (first >= 0x80) { return false; }
    if (text.size() != size) { return false; }
    for (std::size_t i = 1; i < size; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if ((byte & 0xc0) != 0x80) { return false; }
        scalar = (scalar << 6) | (byte & 63);
    }
    return scalar >= minimum && scalar >= 32 && !(scalar >= 127 && scalar <= 159)
        && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff);
}

}

eastl::string_view ContextName(InputContext context) {
    const auto index = static_cast<unsigned>(context);
    if (index >= std::size(contextNames)) { std::abort(); }
    return contextNames[index];
}
eastl::optional<InputContext> ParseContext(eastl::string_view name) {
    for (unsigned i = 0; i < std::size(contextNames); ++i) {
        if (contextNames[i] == name) { return static_cast<InputContext>(i); }
    }
    return {};
}
eastl::string_view ActionName(InputAction action) {
    const auto index = static_cast<unsigned>(action);
    if (index >= std::size(actionNames)) { std::abort(); }
    return actionNames[index];
}
eastl::optional<InputAction> ParseAction(eastl::string_view name) {
    for (unsigned i = 0; i < std::size(actionNames); ++i) {
        if (actionNames[i] == name) { return static_cast<InputAction>(i); }
    }
    return {};
}
bool IsCardinalDirection(InputAction action) {
    return action == InputAction::West || action == InputAction::East
        || action == InputAction::North || action == InputAction::South;
}
bool IsSharedInputContext(InputContext context) {
    return context == InputContext::Shared || context == InputContext::SharedLayer;
}
eastl::span<const InputContext> SharedInputContexts(InputContext context, InputAction action) {
    for (const auto& entry : sharedDefaults) {
        if (entry.context == context && entry.action == action) { return entry.contexts; }
    }
    return {};
}
bool HasSharedInput(InputContext context, InputAction action) {
    for (const auto& entry : sharedDefaults) {
        if (entry.action == action && eastl::find(entry.contexts.begin(), entry.contexts.end(), context) != entry.contexts.end()) {
            return true;
        }
    }
    return false;
}
bool IsSharedInputKey(const Keymap& shared, const InputBinding& local, const KeyChord& key) {
    for (const auto& binding : shared) {
        if (local.action != binding.action) { continue; }
        const auto targets = SharedInputContexts(binding.context, binding.action);
        if (eastl::find(targets.begin(), targets.end(), local.context) != targets.end()
            && eastl::find(binding.keys.begin(), binding.keys.end(), key) != binding.keys.end()) { return true; }
    }
    return false;
}
Keymap DefaultSharedKeymap() {
    Keymap keys;
    for (const auto& entry : sharedDefaults) {
        keys.push_back({entry.context, entry.action, {SharedDefaultChord(entry)}});
    }
    return keys;
}
bool ValidKey(const KeyChord& chord) {
    const eastl::string_view key(chord.key.data(), chord.key.size());
    if (OneScalar(key)) { return !chord.shift && ( !chord.ctrl || (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z')
        || (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z')); }
    for (const auto name : namedKeys) { if (key == name) { return true; } }
    return false;
}
bool LegacyKeyAvailable(const KeyChord& chord) {
    if (chord.alt || chord.shift) {
        for (const auto key : {"Left", "Right", "Up", "Down", "Home", "End", "Insert", "Delete", "PageUp", "PageDown"}) {
            if (chord.key == key) { return true; }
        }
        return false;
    }
    if (!chord.ctrl) { return true; }
    if (chord.key == "F3") { return false; } // Collides with cursor-position replies.
    for (const auto key : {"Left", "Right", "Up", "Down", "Home", "End", "Insert", "Delete", "PageUp", "PageDown"}) {
        if (chord.key == key) { return true; }
    }
    for (const auto key : {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"}) {
        if (chord.key == key) { return true; }
    }
    if (chord.key.size() != 1 || chord.key[0] < 'a' || chord.key[0] > 'z') { return false; }
    // FTXUI canonicalizes these bytes to Backspace, Tab, and Enter.
    return chord.key != "h" && chord.key != "i" && chord.key != "j" && chord.key != "m";
}
String ChordName(const KeyChord& chord) {
    String result = chord.ctrl ? "Ctrl+" : "";
    if (chord.alt) { result += "Alt+"; }
    if (chord.shift) { result += "Shift+"; }
    result += chord.key == " " ? String("Space") : chord.key;
    return result;
}
Keymap DefaultKeymap() {
    Keymap map;
    const auto add = [&](InputContext context, InputAction action, std::initializer_list<const char*> keys) {
        InputBinding binding{context, action, {}};
        for (const char* key : keys) { binding.keys.push_back({String(key), false}); }
        map.push_back(eastl::move(binding));
    };
    const auto addShared = [&](InputContext context, InputAction action, std::initializer_list<const char*> extra = {}) {
        InputBinding binding{context, action, {}};
        for (const auto& entry : sharedDefaults) {
            if (entry.action == action && eastl::find(entry.contexts.begin(), entry.contexts.end(), context) != entry.contexts.end()) {
                binding.keys.push_back(SharedDefaultChord(entry));
            }
        }
        for (const auto* key : extra) { binding.keys.push_back({key}); }
        map.push_back(eastl::move(binding));
    };
    for (auto context : {InputContext::Gameplay, InputContext::Interaction, InputContext::Menu, InputContext::Debug}) {
        addShared(context, InputAction::West);
        addShared(context, InputAction::East);
        addShared(context, InputAction::North);
        addShared(context, InputAction::South);
        addShared(context, InputAction::Cancel);
    }
    const auto game = InputContext::Gameplay;
    add(game, InputAction::Examine, {"x"});
    add(game, InputAction::Grab, {"G"});
    map.push_back({game, InputAction::Northwest, {{"Left", false, false, true}}});
    map.push_back({game, InputAction::Northeast, {{"Right", false, false, true}}});
    map.push_back({game, InputAction::Southwest, {{"Left", true}}});
    map.push_back({game, InputAction::Southeast, {{"Right", true}}});
    add(game, InputAction::Attack, {"Tab"});
    add(game, InputAction::StyleBalanced, {"1"});
    add(game, InputAction::StyleAggressive, {"2"});
    add(game, InputAction::StyleDefensive, {"3"});
    add(game, InputAction::FocusAbility, {"4"});
    add(game, InputAction::LeapAbility, {"5"});
    add(game, InputAction::SweepAbility, {"6"});
    add(game, InputAction::Wait, {"."}); add(game, InputAction::PickUp, {"g"});
    addShared(game, InputAction::Drop); add(game, InputAction::Interact, {"e", "o"});
    add(game, InputAction::Save, {"S"}); addShared(game, InputAction::Quit);
    addShared(game, InputAction::Settings); addShared(game, InputAction::Debug);
    add(game, InputAction::RateDown, {"["}); add(game, InputAction::RateUp, {"]"});
    add(game, InputAction::ToggleRate, {"r"}); add(game, InputAction::ToggleIdle, {"p"});
    for (auto context : {InputContext::Gameplay, InputContext::Interaction}) {
        addShared(context, InputAction::MoveDown);
        addShared(context, InputAction::MoveUp);
        addShared(context, InputAction::Inventory);
        addShared(context, InputAction::AdvancedInventory);
        addShared(context, InputAction::Crafting);
        addShared(context, InputAction::CombatMenu);
        addShared(context, InputAction::CombatInfo);
        addShared(context, InputAction::Use);
        addShared(context, InputAction::Players);
        addShared(context, InputAction::Help);
        addShared(context, InputAction::QuickActions);
    }
    addShared(InputContext::Interaction, InputAction::Quit);
    addShared(InputContext::Interaction, InputAction::Settings);
    addShared(InputContext::Interaction, InputAction::Debug);
    add(InputContext::Menu, InputAction::Accept, {"Enter"});
    addShared(InputContext::Menu, InputAction::Next);
    addShared(InputContext::Menu, InputAction::Previous);
    // Keep retired Menu actions valid in existing user keymaps.
    add(InputContext::Menu, InputAction::Apply, {});
    add(InputContext::Menu, InputAction::SaveSettings, {"s"});
    add(InputContext::Menu, InputAction::Reload, {});
    add(InputContext::Menu, InputAction::Inspect, {"i"});
    add(InputContext::Menu, InputAction::AddBinding, {"+"});
    add(InputContext::Menu, InputAction::Unbind, {"u"});
    add(InputContext::Menu, InputAction::SwitchSettingsScope, {"g"});
    map.push_back({InputContext::Menu, InputAction::PreviousSettingsSection, {{"Up", true}}});
    map.push_back({InputContext::Menu, InputAction::NextSettingsSection, {{"Down", true}}});
    addShared(InputContext::Menu, InputAction::DetailsUp);
    addShared(InputContext::Menu, InputAction::DetailsDown);
    addShared(InputContext::Items, InputAction::Use);
    add(InputContext::Items, InputAction::Apply, {"a"});
    add(InputContext::Items, InputAction::Examine, {"e"});
    add(InputContext::Items, InputAction::Wield, {"w"});
    addShared(InputContext::Items, InputAction::Drop);
    add(InputContext::Items, InputAction::Inventory, {"I"});
    add(InputContext::Items, InputAction::TransferAmount, {"m"});
    add(InputContext::Items, InputAction::TransferStack, {"M"});
    add(InputContext::Items, InputAction::OpenContainer, {"o"});
    add(InputContext::Items, InputAction::ParentStorage, {"Backspace"});
    add(InputContext::Items, InputAction::ToggleStorage, {"f"});
    add(InputContext::Items, InputAction::AllNearby, {"A"});
    add(InputContext::Items, InputAction::HoldLeft, {"L"});
    add(InputContext::Items, InputAction::HoldRight, {"R"});
    add(InputContext::Items, InputAction::TwoHanded, {"H"});
    add(InputContext::Items, InputAction::Regrip, {"G"});
    add(InputContext::Items, InputAction::Wear, {"W"});
    add(InputContext::Items, InputAction::RemoveWorn, {"T"});
    add(InputContext::Items, InputAction::ReleaseHand, {"U"});
    addShared(InputContext::Items, InputAction::MoveDown);
    addShared(InputContext::Items, InputAction::MoveUp);
    add(InputContext::Menu, InputAction::Reset, {"Backspace"});
    const auto debug = InputContext::Debug;
    addShared(debug, InputAction::Inventory);
    add(debug, InputAction::Inspect, {"a"});
    addShared(debug, InputAction::MoveUp);
    addShared(debug, InputAction::MoveDown);
    addShared(debug, InputAction::Debug, {"q"});
    map.push_back({debug, InputAction::JumpWest, {{"Left", false, false, true}}});
    map.push_back({debug, InputAction::JumpEast, {{"Right", false, false, true}}});
    map.push_back({debug, InputAction::JumpNorth, {{"Up", false, false, true}}});
    map.push_back({debug, InputAction::JumpSouth, {{"Down", false, false, true}}});
    add(debug, InputAction::Recenter, {"Home", "c"});
    add(debug, InputAction::Layer1, {"1"}); add(debug, InputAction::Layer2, {"2"});
    add(debug, InputAction::Layer3, {"3"}); add(debug, InputAction::Layer4, {"4"});
    add(debug, InputAction::Layer5, {"5"}); add(debug, InputAction::ZoomIn, {"+", "="});
    add(debug, InputAction::ZoomOut, {"-"}); addShared(debug, InputAction::Next);
    addShared(debug, InputAction::Previous);
    addShared(debug, InputAction::DetailsUp); addShared(debug, InputAction::DetailsDown);
    return map;
}
void SetBinding(Keymap& overrides, InputBinding binding) {
    if (binding.context == InputContext::Shared && IsCardinalDirection(binding.action)) {
        for (auto it = overrides.begin(); it != overrides.end();) {
            it = it->context != binding.context && it->action == binding.action ? overrides.erase(it) : it + 1;
        }
    }
    for (auto& current : overrides) {
        if (current.context == binding.context && current.action == binding.action) {
            current = eastl::move(binding); return;
        }
    }
    overrides.push_back(eastl::move(binding));
}
void ResetBinding(Keymap& overrides, InputContext context, InputAction action) {
    if (context == InputContext::Shared && IsCardinalDirection(action)) {
        for (auto it = overrides.begin(); it != overrides.end();) {
            it = it->action == action ? overrides.erase(it) : it + 1;
        }
        return;
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (it->context == context && it->action == action) { overrides.erase(it); return; }
    }
}
Keymap ResolveKeymap(const Keymap& global, const Keymap& world) {
    auto resolved = DefaultKeymap();
    // Newly introduced defaults must not invalidate existing explicit bindings.
    for (auto& binding : resolved) {
        bool explicitAction = false;
        for (const auto* layer : {&global, &world}) {
            for (const auto& entry : *layer) { explicitAction |= entry.context == binding.context && entry.action == binding.action; }
        }
        if (explicitAction) { continue; }
        for (auto it = binding.keys.begin(); it != binding.keys.end();) {
            const bool introduced = binding.action >= InputAction::AdvancedInventory
                || (binding.context == InputContext::Menu
                    && (binding.action == InputAction::DetailsUp || binding.action == InputAction::DetailsDown))
                || (binding.context == InputContext::Items && (binding.action == InputAction::Examine
                    || binding.action == InputAction::Drop || binding.action == InputAction::Inventory))
                || binding.action == InputAction::Grab || binding.action == InputAction::Examine || binding.action == InputAction::Northwest
                || binding.action == InputAction::Northeast || binding.action == InputAction::Southwest || binding.action == InputAction::Southeast
                || (binding.action == InputAction::Interact && it->key == "e")
                || (binding.context == InputContext::Debug && binding.action == InputAction::ZoomIn && it->key == "=")
                || ((binding.action == InputAction::MoveUp || binding.action == InputAction::MoveDown) && it->alt)
                || ((binding.action == InputAction::JumpWest || binding.action == InputAction::JumpEast
                    || binding.action == InputAction::JumpNorth || binding.action == InputAction::JumpSouth) && it->shift);
            bool claimed = false;
            if (introduced) {
                for (const auto* layer : {&global,&world}) {
                    for (const auto& entry : *layer) {
                        if (entry.context != binding.context || entry.action == binding.action) { continue; }
                        for (const auto& key : entry.keys) { claimed |= key == *it; }
                    }
                }
            }
            if (claimed) { it = binding.keys.erase(it); } else { ++it; }
        }
    }
    auto shared = DefaultSharedKeymap();
    for (const auto* layer : {&global, &world}) {
        for (const auto& binding : *layer) {
            if (IsSharedInputContext(binding.context)) { continue; }
            auto replacement = binding;
            if (binding.inheritShared) {
                replacement.keys.clear();
                for (const auto& previous : resolved) {
                    if (previous.context != binding.context || previous.action != binding.action) { continue; }
                    for (const auto& key : previous.keys) {
                        if (IsSharedInputKey(shared, previous, key)) { replacement.keys.push_back(key); }
                    }
                    break;
                }
                for (const auto& key : binding.keys) { replacement.keys.push_back(key); }
                replacement.inheritShared = false;
            }
            SetBinding(resolved, eastl::move(replacement));
        }
        // Remove inherited shared chords together before applying replacements.
        // Context-specific alternatives on the same action remain available.
        for (auto& inherited : shared) {
            for (const auto& binding : *layer) {
                if (binding.context != inherited.context || binding.action != inherited.action) { continue; }
                for (auto& local : resolved) {
                    if (local.action != binding.action) { continue; }
                    const auto contexts = SharedInputContexts(binding.context, binding.action);
                    if (eastl::find(contexts.begin(), contexts.end(), local.context) == contexts.end()) { continue; }
                    if (IsCardinalDirection(binding.action)) { local.keys.clear(); }
                    else {
                        for (const auto& key : inherited.keys) {
                            const auto match = eastl::find(local.keys.begin(), local.keys.end(), key);
                            if (match != local.keys.end()) { local.keys.erase(match); }
                        }
                    }
                }
            }
        }
        for (const auto& binding : *layer) {
            if (!IsSharedInputContext(binding.context)) { continue; }
            for (const auto context : SharedInputContexts(binding.context, binding.action)) {
                InputBinding replacement{context, binding.action, {}};
                for (const auto& local : resolved) {
                    if (local.context == context && local.action == binding.action) { replacement.keys = local.keys; break; }
                }
                const auto position = binding.context == InputContext::SharedLayer
                    ? replacement.keys.end() : replacement.keys.begin();
                replacement.keys.insert(position, binding.keys.begin(), binding.keys.end());
                SetBinding(resolved, eastl::move(replacement));
            }
            SetBinding(shared, binding);
        }
    }
    return resolved;
}
eastl::expected<void, InputError> ValidateKeymap(const Keymap& map, Diagnostics* diagnostics) {
    const auto fail = [&](InputError error, eastl::string_view message) -> eastl::expected<void, InputError> {
        if (diagnostics) { diagnostics->Add(message); }
        return eastl::unexpected(error);
    };
    const auto defaults = DefaultKeymap();
    for (std::size_t i = 0; i < map.size(); ++i) {
        const auto& binding = map[i];
        bool known = !SharedInputContexts(binding.context, binding.action).empty()
            || (binding.context == InputContext::Items && (IsCardinalDirection(binding.action) || binding.action == InputAction::Cancel));
        for (const auto& candidate : defaults) {
            known |= candidate.context == binding.context && candidate.action == binding.action;
        }
        if (!known || (binding.inheritShared && !HasSharedInput(binding.context, binding.action))) {
            return fail(InputError::InvalidAction, "action is not available in this context");
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (map[previous].context == binding.context && map[previous].action == binding.action) {
                return fail(InputError::Conflict, "duplicate context/action entry");
            }
        }
        for (std::size_t k = 0; k < binding.keys.size(); ++k) {
            const auto& key = binding.keys[k];
            if (!ValidKey(key)) { return fail(InputError::InvalidKey, "expected a character or named key; Ctrl supports ASCII letters and named keys"); }
            if ((key.ctrl && key.key == "c")
                || (key.key == "Escape" && (key.ctrl || key.alt || key.shift || binding.action != InputAction::Cancel))) {
                return fail(InputError::ReservedKey, "Escape and Ctrl+c are reserved for cancellation and exit");
            }
            if (IsSharedInputContext(binding.context) && key == KeyChord{"F9"}) {
                return fail(InputError::ReservedKey, "F9 is reserved for Multiplayer");
            }
            for (std::size_t j = 0; j <= i; ++j) {
                if (j != i) {
                    if (IsSharedInputContext(binding.context)) {
                        const auto targets = SharedInputContexts(binding.context, binding.action);
                        const auto otherTargets = SharedInputContexts(map[j].context, map[j].action);
                        if (eastl::none_of(targets.begin(), targets.end(), [&](InputContext target) {
                            return eastl::find(otherTargets.begin(), otherTargets.end(), target) != otherTargets.end();
                        })) { continue; }
                    } else if (map[j].context != binding.context) { continue; }
                }
                const std::size_t end = j == i ? k : map[j].keys.size();
                for (std::size_t other = 0; other < end; ++other) {
                    if (map[j].keys[other] == key) {
                        const auto context = ContextName(binding.context);
                        const auto first = ActionName(map[j].action);
                        const auto second = ActionName(binding.action);
                        String message = ChordName(key) + " is already bound in ";
                        message.append(context.data(), context.size());
                        message += " ("; message.append(first.data(), first.size());
                        message += " / "; message.append(second.data(), second.size()); message += ')';
                        return fail(InputError::Conflict, {message.data(), message.size()});
                    }
                }
            }
        }
    }
    return {};
}
eastl::optional<InputAction> ResolveAction(const Keymap& map, InputContext context, const KeyChord& chord) {
    if (chord.key == "Escape" && !chord.ctrl && !chord.alt && !chord.shift) { return InputAction::Cancel; }
    for (const auto& binding : map) {
        if (binding.context != context) { continue; }
        for (const auto& key : binding.keys) { if (key == chord) { return binding.action; } }
    }
    return {};
}
String BindingHint(const Keymap& map, InputContext context, InputAction action) {
    String result;
    for (const auto& binding : map) {
        if (binding.context != context || binding.action != action) { continue; }
        for (const auto& key : binding.keys) {
            if (!result.empty()) { result += '/'; }
            result += ChordName(key);
        }
    }
    return result.empty() ? String("—") : result;
}

}
