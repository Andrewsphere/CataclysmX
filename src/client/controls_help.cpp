#include "client/controls_help.h"

namespace cataclysmx {

Vector<ControlHelpRow> CollectControlsHelp(const Keymap& keys, const Localization& loc, bool sdl) {
    Vector<ControlHelpRow> rows;
    const auto binding = [&](InputContext context, InputAction action) {
        auto hint = BindingHint(keys, context, action);
        return hint == "—" ? loc.Text("game.ui.unbound") : hint;
    };
    const auto append = [&](InputContext context, InputAction action) {
        String key = "action.";
        const auto name = ActionName(action);
        key.append(name.data(), name.size());
        rows.push_back({loc.Text({key.data(), key.size()}, name), binding(context, action)});
    };
    const auto heading = [&](const char* label) { rows.push_back({loc.Text(label), {}, true}); };
    const auto group = [&](const char* label, std::initializer_list<InputAction> actions) {
        heading(label);
        for (const auto action : actions) { append(InputContext::Gameplay, action); }
    };
    const auto nested = [&](const char* label, InputAction action) {
        rows.push_back({loc.Text(label), binding(InputContext::Gameplay, action) + " → p"});
    };
    group("game.ui.windows", {InputAction::QuickActions, InputAction::Inventory, InputAction::AdvancedInventory,
        InputAction::Crafting, InputAction::CombatMenu, InputAction::CombatInfo, InputAction::Players,
        InputAction::Settings, InputAction::Help});
    nested("game.ui.power_workshop", InputAction::Crafting);
    rows.push_back({loc.Text(sdl ? "sdl.ui.multiplayer" : "game.ui.top_lan", "Multiplayer"), "F9"});
    group("game.ui.actions", {InputAction::Use, InputAction::PickUp, InputAction::Drop, InputAction::Interact,
        InputAction::Examine, InputAction::Grab, InputAction::Wait, InputAction::Attack, InputAction::StyleBalanced,
        InputAction::StyleAggressive, InputAction::StyleDefensive, InputAction::FocusAbility, InputAction::LeapAbility,
        InputAction::SweepAbility});
    group("game.ui.movement", {InputAction::West, InputAction::North, InputAction::South, InputAction::East,
        InputAction::Northwest, InputAction::Northeast, InputAction::Southwest, InputAction::Southeast,
        InputAction::MoveDown, InputAction::MoveUp});
    if (!sdl) {
        group("game.ui.pacing", {InputAction::RateDown, InputAction::RateUp, InputAction::ToggleRate, InputAction::ToggleIdle});
    }
    group("game.ui.system", {InputAction::Save, InputAction::Debug, InputAction::Quit, InputAction::Cancel});
    nested("game.ui.power_diagnostics", InputAction::Debug);
    heading("context.interaction");
    for (const auto& entry : keys) {
        if (entry.context == InputContext::Interaction && (!IsCardinalDirection(entry.action)
            || binding(entry.context, entry.action) != binding(InputContext::Gameplay, entry.action))) {
            append(entry.context, entry.action);
        }
    }
    heading("context.items");
    for (const auto& entry : keys) {
        if (entry.context == InputContext::Items) { append(entry.context, entry.action); }
    }
    if (sdl) {
        heading("sdl.ui.map_controls");
        rows.push_back({loc.Text("sdl.ui.zoom_in"), "z"});
        rows.push_back({loc.Text("sdl.ui.zoom_out"), "Z"});
        rows.push_back({loc.Text("sdl.ui.select_current_tile"), "."});
        rows.push_back({loc.Text("sdl.ui.scroll_examine"), "PgUp/PgDn · Ctrl+Up/Down"});
    } else {
        rows.push_back({loc.Text("game.ui.lan_controls"), {}});
    }
    rows.push_back({loc.Text(sdl ? "sdl.ui.fixed_controls" : "game.ui.fixed_controls"), {}});
    return rows;
}

}
