#include "client/text.h"

#include <charconv>
#include <cstdlib>

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

}

TerminalAppearance CellAppearance(const ViewCell& cell, const ContentCatalog& content) {
    if (!cell.visible) { return {" ", {}, {}}; }
    TerminalAppearance appearance = content.GetTerrain(cell.tile.terrain).appearance;
    if (cell.actor != 0) {
        appearance.symbol = cell.player ? "@" : "z";
        appearance.foreground = cell.player ? RgbColor{255, 255, 255} : RgbColor{0, 255, 0};
    } else if (cell.ground_items != 0) {
        appearance.symbol = "!";
        appearance.foreground = {255, 255, 0};
    } else if (cell.object != 0) {
        appearance.symbol = cell.object_enabled ? "*" : "&";
        appearance.foreground = cell.object_enabled ? RgbColor{0, 255, 255} : RgbColor{0, 128, 128};
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
    result += '\n';
    for (int row = 0; row < view.height; ++row) {
        for (int column = 0; column < view.width; ++column) {
            result += CellAppearance(view.cells[static_cast<std::size_t>(row * view.width + column)], content).symbol;
        }
        result += '\n';
    }
    result += "Inventory:";
    for (const InventoryEntry& item : view.inventory) {
        result += ' ';
        append_number(result, item.id);
        result += ':';
        result += item_name(item.kind);
        if (item.contained_items != 0) {
            result += '(';
            append_number(result, item.contained_items);
            result += " inside)";
        }
    }
    if (view.inventory.empty()) { result += " empty"; }
    result += '\n';
    return result;
}

}
