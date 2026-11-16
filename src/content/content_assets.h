#pragma once

#include "content/content_result.h"
#include "core/containers.h"
#include "core/diagnostics.h"

namespace cataclysmx {

// Synchronous load-time boundary. The callback receives a validated data-root
// path and returns owned bytes. Neither callback nor context survives loading.
struct ContentAssetReader {
    void* context = nullptr;
    ContentResult<String> (*read)(void* context, eastl::string_view path,
        Diagnostics* diagnostics) = nullptr;
};

// Canonical virtual absolute paths: /art/items/example/art.txt. No host root,
// backslashes, empty/dot/parent components, controls, or trailing dots/spaces.
bool ValidContentAssetPath(eastl::string_view path);

// File-backed adapter used by pack loading. Resolves within the explicit root,
// including symlink containment, and bounds reads to the text-art file budget.
ContentResult<String> ReadContentAssetFile(eastl::string_view dataRoot,
    eastl::string_view path, Diagnostics* diagnostics = nullptr);

}
