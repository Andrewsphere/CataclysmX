#pragma once

#include "sdl/assets.h"
#include <stop_token>

namespace cataclysmx::sdl {

// Appends owned CPU pages; on failure the caller still owns and releases them.
eastl::expected<bool, AssetError> BuildSourceAtlases(const String& directory,
    const TilesetDefinition& definition, Vector<SDL_Surface*>& pages,
    std::stop_token stop, Diagnostics* diagnostics = nullptr);

}
