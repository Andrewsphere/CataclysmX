#include "client/text.h"

#include <charconv>
#include <cstdlib>
#include <algorithm>

namespace cataclysmx {
namespace {

template<class T>
void append_number(String& text, T value) {
    // Enough for every signed/unsigned 64-bit decimal value, including its sign.
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) { std::abort(); }
    text.append(buffer, static_cast<String::size_type>(result.ptr - buffer));
}

RgbColor DepthTint(RgbColor color, unsigned int depth, bool background, bool memory = false) {
    const unsigned int percent = depth == 0 ? 0 : std::min(70U, 15U + std::min(depth, 20U) * 10U);
    const unsigned int strength = (background ? percent / 2 : percent) / (memory ? 4 : 1);
    const auto blend = [strength](unsigned int value, unsigned int target) {
        return static_cast<unsigned char>((value * (100U - strength) + target * strength) / 100U);
    };
    return {blend(color.red, 48), blend(color.green, 96), blend(color.blue, 192)};
}

RgbColor PerceptionColor(RgbColor color, Visibility visibility, LightBand light, unsigned depth, bool background) {
    const bool memory = visibility == Visibility::Remembered;
    if (memory) {
        const unsigned value = (unsigned(color.red) * 30 + unsigned(color.green) * 59 + unsigned(color.blue) * 11) / 100;
        color = {static_cast<unsigned char>(40 + value * 55 / 100),
            static_cast<unsigned char>(30 + value * 43 / 100), static_cast<unsigned char>(20 + value * 28 / 100)};
    } else {
        const auto channel = [light, background](unsigned value) {
            switch (light) {
            case LightBand::Dark: return background ? value / 4 : 24 + value / 4;
            case LightBand::Dim: return value * 55 / 100;
            case LightBand::Ordinary: return value;
            case LightBand::Bright: return value + (255 - value) / 3;
            }
            return value;
        };
        color = {static_cast<unsigned char>(channel(color.red)), static_cast<unsigned char>(channel(color.green)),
            static_cast<unsigned char>(channel(color.blue))};
    }
    return DepthTint(color, depth, background, memory);
}

}

TerminalAppearance CellAppearance(const ViewCell& cell, const ContentCatalog& content) {
    TerminalAppearance appearance{" ", {}, {}};
    if (cell.terrainVisibility != Visibility::Unknown) {
        appearance = content.GetTerrain(cell.tile.terrain).appearance;
        appearance.background = PerceptionColor(appearance.background, cell.terrainVisibility,
            cell.terrainLight, cell.terrainDepth, true);
    }
    const bool live = cell.overlayVisibility == Visibility::Visible;
    const bool overlay = (live && (cell.actor || cell.ground_items)) ||
        (cell.overlayVisibility != Visibility::Unknown && cell.objectAppearance.kind != ObjectKind::None);
    if (live && cell.actor != 0) {
        appearance.symbol = cell.player ? "@" : "z";
        appearance.foreground = cell.player ? RgbColor{255, 255, 255} : RgbColor{0, 255, 0};
    } else if (live && cell.ground_items != 0) {
        appearance.symbol = "!";
        appearance.foreground = {255, 255, 0};
    } else if (overlay) {
        switch (cell.objectAppearance.kind) {
        case ObjectKind::Chair: appearance.symbol = "h"; break;
        case ObjectKind::Table: appearance.symbol = "T"; break;
        case ObjectKind::Lamp: appearance.symbol = cell.objectAppearance.active ? "*" : "i"; break;
        case ObjectKind::Generic: appearance.symbol = cell.objectAppearance.active ? "*" : "&"; break;
        case ObjectKind::None: break;
        }
        appearance.foreground = cell.objectAppearance.broken ? RgbColor{160, 96, 64}
            : cell.objectAppearance.active ? RgbColor{0, 255, 255} : RgbColor{0, 128, 128};
    }
    if (overlay || cell.terrainVisibility != Visibility::Unknown) {
        appearance.foreground = PerceptionColor(appearance.foreground,
            overlay ? cell.overlayVisibility : cell.terrainVisibility,
            overlay ? cell.overlayLight : cell.terrainLight, overlay ? cell.overlayDepth : cell.terrainDepth, false);
    }
    return appearance;
}

String render_text(const WorldView& view, const ContentCatalog& content) {
    String result = "tick=";
    append_number(result, view.tick);
    result += " position=";
    append_number(result, view.player_position.x);
    result += ',';
    append_number(result, view.player_position.z);
    result += ',';
    append_number(result, view.player_position.y);
    result += '\n';
    for (int row = 0; row < view.height; ++row) {
        for (int column = 0; column < view.width; ++column) {
            result += CellAppearance(view.cells[static_cast<std::size_t>(row * view.width + column)], content).symbol;
        }
        result += '\n';
    }
    result += "Inventory:";
    bool roots = false;
    for (const InventoryEntry& item : view.inventory) {
        if (item.parent != 0) { continue; }
        roots = true;
        result += ' ';
        append_number(result, item.id);
        result += ':';
        result += content.GetItem(item.type).name;
        if (item.contained_items != 0) {
            result += '(';
            append_number(result, item.contained_items);
            result += " inside)";
        }
    }
    if (!roots) { result += " empty"; }
    result += '\n';
    bool stateful = false;
    for (const auto& item : view.inventory) { stateful |= !item.energy.empty() || !item.activities.empty(); }
    for (const auto& item : view.groundItems) { stateful |= !item.energy.empty() || !item.activities.empty(); }
    if (stateful) { result += ItemDetails(view, content); }
    return result;
}

String ItemDetails(const WorldView& view, const ContentCatalog& content) {
    String text = "carried-mass="; append_number(text, view.carriedMassGrams);
    text += '/'; append_number(text, view.carryCapacityGrams); text += "g\n";
    const auto list = [&](const Vector<InventoryEntry>& entries, const char* location) {
        for (const auto& item : entries) {
            text += location; text += " item="; append_number(text, item.id);
            text += " name="; text += content.GetItem(item.type).name;
            text += " parent="; append_number(text, item.parent);
            text += " installation="; append_number(text, item.installation.value);
            text += " mass-g="; append_number(text, item.massGrams);
            text += " volume-ml="; append_number(text, item.volumeMl);
            text += " storage-ml="; append_number(text, item.usedVolumeMl);
            text += '/'; append_number(text, item.capacityVolumeMl); text += '\n';
            for (const auto& energy : item.energy) {
                text += "  capability="; append_number(text, energy.capability.value);
                text += " energy="; append_number(text, energy.energy); text += '/'; append_number(text, energy.capacity); text += '\n';
            }
            for (const auto& activity : item.activities) {
                text += "  capability="; append_number(text, activity.capability.value);
                text += activity.active ? " active=yes remaining=" : " active=no remaining=";
                append_number(text, activity.remainingTicks);
                if (!activity.status.fallback.empty()) { text += " status="; text += activity.status.fallback; }
                text += '\n';
            }
            for (const auto& action : item.actions) {
                text += "  capability="; append_number(text, action.capability.value);
                text += " action="; text += action.action;
                text += " label="; text += action.label.fallback;
                text += " availability="; text += ItemUseMessage(action.availability);
                if (!action.reason.code.empty()) { text += " reason="; text += action.reason.code; }
                if (!action.reason.message.fallback.empty()) { text += " message="; text += action.reason.message.fallback; }
                text += " cost="; append_number(text, action.baseCost); text += '\n';
            }
        }
    };
    list(view.inventory, "carried"); list(view.groundItems, "ground");
    return text;
}

bool ParseItemCommand(eastl::string_view text, Command& command) {
    Vector<eastl::string_view> words;
    while (!text.empty()) {
        const auto begin = text.find_first_not_of(" \t\r");
        if (begin == eastl::string_view::npos) { break; }
        text.remove_prefix(begin);
        const auto end = text.find_first_of(" \t\r");
        words.push_back(text.substr(0, end));
        if (words.size() > 5) { return false; }
        if (end == eastl::string_view::npos) { break; }
        text.remove_prefix(end);
    }
    const auto number = [](eastl::string_view word, auto& value) {
        const auto parsed = std::from_chars(word.data(), word.data() + word.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == word.data() + word.size();
    };
    Command candidate;
    if (words.size() == 3 && words[0] == "store") {
        candidate.kind = CommandKind::store_item;
        if (!number(words[1], candidate.item) || !number(words[2], candidate.otherItem)) { return false; }
    } else if ((words.size() == 4 || words.size() == 5) && words[0] == "use") {
        candidate.kind = CommandKind::use_item;
        if (!number(words[1], candidate.item) || !number(words[2], candidate.capability.value)) { return false; }
        if (!ValidItemActionId(words[3])) { return false; }
        candidate.itemAction.assign(words[3].data(), words[3].size());
        if (words.size() == 5) {
            if (words[4] == "n") { candidate.dz = -1; }
            else if (words[4] == "s") { candidate.dz = 1; }
            else if (words[4] == "w") { candidate.dx = -1; }
            else if (words[4] == "e") { candidate.dx = 1; }
            else if (words[4] == "down" || words[4] == "<") { candidate.dy = -1; }
            else if (words[4] == "up" || words[4] == ">") { candidate.dy = 1; }
            else if (!number(words[4], candidate.otherItem) || candidate.otherItem == 0) { return false; }
        }
    } else { return false; }
    if (!ValidCommandShape(candidate)) { return false; }
    command = candidate; return true;
}

}
