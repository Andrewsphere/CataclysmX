#include "client/input.h"

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
    "northwest", "northeast", "southwest", "southeast"
};
constexpr eastl::string_view contextNames[] = {"gameplay", "interaction", "menu", "debug", "items"};
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
    for (auto context : {InputContext::Gameplay, InputContext::Interaction, InputContext::Menu, InputContext::Debug}) {
        add(context, InputAction::West, {"Left"});
        add(context, InputAction::East, {"Right"});
        add(context, InputAction::North, {"Up"});
        add(context, InputAction::South, {"Down"});
        add(context, InputAction::Cancel, {"Escape"});
    }
    const auto game = InputContext::Gameplay;
    add(game, InputAction::Examine, {"x"});
    map.push_back({game, InputAction::Northwest, {{"Left", false, false, true}}});
    map.push_back({game, InputAction::Northeast, {{"Right", false, false, true}}});
    map.push_back({game, InputAction::Southwest, {{"Left", true}}});
    map.push_back({game, InputAction::Southeast, {{"Right", true}}});
    add(game, InputAction::Attack, {"Tab"});
    add(game, InputAction::StyleBalanced, {"1"});
    add(game, InputAction::StyleAggressive, {"2"});
    add(game, InputAction::StyleDefensive, {"3"});
    add(game, InputAction::Wait, {"."}); add(game, InputAction::PickUp, {"g"});
    add(game, InputAction::Drop, {"d"}); add(game, InputAction::Interact, {"e", "o"});
    add(game, InputAction::Save, {"S"}); add(game, InputAction::Quit, {"q"});
    add(game, InputAction::Settings, {"F2"}); add(game, InputAction::Debug, {"F11"});
    add(game, InputAction::RateDown, {"["}); add(game, InputAction::RateUp, {"]"});
    add(game, InputAction::ToggleRate, {"r"}); add(game, InputAction::ToggleIdle, {"p"});
    for (auto context : {InputContext::Gameplay, InputContext::Interaction}) {
        add(context, InputAction::MoveDown, {"<"});
        add(context, InputAction::MoveUp, {">"});
        map[map.size() - 2].keys.push_back({"Down", false, true});
        map.back().keys.push_back({"Up", false, true});
        add(context, InputAction::Inventory, {"i"});
        add(context, InputAction::Use, {"u"});
        add(context, InputAction::Players, {"P"});
        add(context, InputAction::Help, {"F1"});
        add(context, InputAction::QuickActions, {"a"});
    }
    add(InputContext::Interaction, InputAction::Quit, {"q"});
    add(InputContext::Interaction, InputAction::Settings, {"F2"});
    add(InputContext::Interaction, InputAction::Debug, {"F11"});
    add(InputContext::Menu, InputAction::Accept, {"Enter"});
    add(InputContext::Menu, InputAction::Next, {"Tab"});
    add(InputContext::Menu, InputAction::Previous, {"BackTab"});
    add(InputContext::Menu, InputAction::Apply, {"a"});
    add(InputContext::Menu, InputAction::SaveSettings, {"s"});
    add(InputContext::Menu, InputAction::Reload, {"R"});
    add(InputContext::Menu, InputAction::Inspect, {"i"});
    add(InputContext::Menu, InputAction::AddBinding, {"+"});
    add(InputContext::Menu, InputAction::Unbind, {"u"});
    add(InputContext::Items, InputAction::Use, {"u"});
    add(InputContext::Items, InputAction::Apply, {"a"});
    add(InputContext::Items, InputAction::MoveDown, {"<"});
    add(InputContext::Items, InputAction::MoveUp, {">"});
    add(InputContext::Menu, InputAction::Reset, {"Backspace"});
    const auto debug = InputContext::Debug;
    add(debug, InputAction::Inspect, {"a"});
    add(debug, InputAction::MoveUp, {">"});
    map.back().keys.push_back({"Up", false, true});
    add(debug, InputAction::MoveDown, {"<"});
    map.back().keys.push_back({"Down", false, true});
    add(debug, InputAction::Debug, {"F11", "q"});
    map.push_back({debug, InputAction::JumpWest, {{"Left", false, false, true}}});
    map.push_back({debug, InputAction::JumpEast, {{"Right", false, false, true}}});
    map.push_back({debug, InputAction::JumpNorth, {{"Up", false, false, true}}});
    map.push_back({debug, InputAction::JumpSouth, {{"Down", false, false, true}}});
    add(debug, InputAction::Recenter, {"Home", "c"});
    add(debug, InputAction::Layer1, {"1"}); add(debug, InputAction::Layer2, {"2"});
    add(debug, InputAction::Layer3, {"3"}); add(debug, InputAction::Layer4, {"4"});
    add(debug, InputAction::Layer5, {"5"}); add(debug, InputAction::ZoomIn, {"+"});
    add(debug, InputAction::ZoomOut, {"-"}); add(debug, InputAction::Next, {"Tab"});
    add(debug, InputAction::Previous, {"BackTab"});
    add(debug, InputAction::DetailsUp, {"PageUp"}); add(debug, InputAction::DetailsDown, {"PageDown"});
    return map;
}
void SetBinding(Keymap& overrides, InputBinding binding) {
    for (auto& current : overrides) {
        if (current.context == binding.context && current.action == binding.action) {
            current = eastl::move(binding); return;
        }
    }
    overrides.push_back(eastl::move(binding));
}
void ResetBinding(Keymap& overrides, InputContext context, InputAction action) {
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
            const bool introduced = binding.action == InputAction::Examine || binding.action == InputAction::Northwest
                || binding.action == InputAction::Northeast || binding.action == InputAction::Southwest || binding.action == InputAction::Southeast
                || (binding.action == InputAction::Interact && it->key == "e")
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
    for (const auto& binding : global) { SetBinding(resolved, binding); }
    for (const auto& binding : world) { SetBinding(resolved, binding); }
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
        bool known = false;
        for (const auto& candidate : defaults) {
            known |= candidate.context == binding.context && candidate.action == binding.action;
        }
        if (!known) { return fail(InputError::InvalidAction, "action is not available in this context"); }
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
            for (std::size_t j = 0; j <= i; ++j) {
                if (map[j].context != binding.context) { continue; }
                const std::size_t end = j == i ? k : map[j].keys.size();
                for (std::size_t other = 0; other < end; ++other) {
                    if (map[j].keys[other] == key) { return fail(InputError::Conflict, "key is already bound in this context"); }
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
