#pragma once

#include "core/server.h"

namespace cataclysmx {
// Uses the filtered projection, including independent terrain/overlay depths.
// Appends owned text; never loads chunks, teaches memories, or reveals unseen entities.
void DescribeTile(const World& world, const WorldView& view, TileCoord cursor, Vector<String>& output);
}
