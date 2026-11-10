#pragma once

#include "content/tileset.h"
#include <EASTL/span.h>

namespace cataclysmx {

// Integration supplies PNG header dimensions; text is its optional sidecar or a
// standalone .meta definition. Paths are relative to the pack, with '/' separators.
struct SpriteSourceFile {
    String path, text;
    std::uint32_t width = 0, height = 0;
};

ContentResult<TilesetDefinition> ParseSourceTileset(eastl::string_view manifest,
    eastl::span<const SpriteSourceFile> files, const ContentCatalog& content,
    Diagnostics* diagnostics = nullptr);

}
