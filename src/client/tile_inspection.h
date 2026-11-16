#pragma once

#include "core/presentation.h"

namespace cataclysmx {
// Borrowed catalog name, "Player"/"Creature" for untyped visible actors, or
// empty when no live actor is visible. Works with remote views; no world lookup.
eastl::string_view VisibleActorName(const ContentCatalog& content, const ViewCell& cell);

// Uses the filtered projection, including independent terrain/overlay depths.
// Appends owned text; never loads chunks, teaches memories, or reveals unseen entities.
// Empty lines separate terrain, actor, object (including contents), and ground items.
void DescribeTile(const World& world, const WorldView& view, TileCoord cursor, Vector<String>& output);
void DescribeTile(const ContentCatalog& content, const WorldView& view, TileCoord cursor,
    const TileInspection& details, Vector<String>& output);
}
