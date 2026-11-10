#include "sdl/item_visual.h"

#include <cstdlib>

namespace cataclysmx::sdl {
Vector<MapItemVisual> CaptureMapItems(const World& world, const WorldView& view) {
    if (view.width < 0 || view.height < 0
        || static_cast<std::size_t>(view.width) * static_cast<std::size_t>(view.height) != view.cells.size()) { std::abort(); }
    Vector<MapItemVisual> result(view.cells.size());
    for (int row = 0; row < view.height; ++row) {
        for (int column = 0; column < view.width; ++column) {
            const auto index = static_cast<std::size_t>(row) * view.width + column;
            const auto& cell = view.cells[index];
            if (cell.overlayVisibility != Visibility::Visible || cell.actor || !cell.ground_items) { continue; }
            const TileCoord position{view.origin.x + column, view.origin.z + row,
                view.origin.y - static_cast<int>(cell.overlayDepth)};
            for (const auto handle : world.ItemsAt(position)) {
                const auto* item = world.GetItem(handle);
                if (!item) { std::abort(); }
                if (!result[index].item || item->id < result[index].item) { result[index] = {item->id, item->type}; }
            }
        }
    }
    return result;
}
}
