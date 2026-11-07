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

char cell_glyph(const ViewCell& cell) {
    if (!cell.visible) { return ' '; }
    if (cell.actor != 0) { return cell.player ? '@' : 'z'; }
    if (cell.ground_items != 0) { return '!'; }
    if (cell.object != 0) { return cell.object_enabled ? '*' : '&'; }
    switch (cell.tile.terrain) {
    case Terrain::floor: return '.';
    case Terrain::wall: return '#';
    case Terrain::closed_door: return '+';
    case Terrain::open_door: return '/';
    }
    return '?';
}

String render_text(const WorldView& view) {
    String result = "tick=";
    append_number(result, view.tick);
    result += " position=";
    append_number(result, view.player_position.x);
    result += ',';
    append_number(result, view.player_position.z);
    result += '\n';
    for (int row = 0; row < view.height; ++row) {
        for (int column = 0; column < view.width; ++column) {
            result += cell_glyph(view.cells[static_cast<std::size_t>(row * view.width + column)]);
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
