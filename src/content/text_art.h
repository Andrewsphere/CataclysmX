#pragma once

#include "content/content_result.h"
#include "core/containers.h"
#include "core/diagnostics.h"
#include <EASTL/span.h>

namespace cataclysmx {

enum class ArtColor : std::uint8_t {
    Default, Black, Red, Green, Yellow, Blue, Magenta, Cyan, White, Gray, DarkGray
};
struct ArtRun {
    String text;
    ArtColor color = ArtColor::Default;
    std::uint32_t cells = 0;
    bool operator==(const ArtRun&) const = default;
};
using ArtLine = Vector<ArtRun>;
struct TextArt {
    Vector<ArtLine> lines;
    std::uint32_t width = 0;
    bool operator==(const TextArt&) const = default;
};

// Parses definition-owned, single-cell UTF-8 art. Each source element is one
// line; colors reset between lines, << escapes <, and <reset> restores default.
// Limits: 128 lines, 256 cells/line, 4096 source bytes/line, 131072 bytes total.
// Errors locate the decoded art line and byte-column (both one-based).
// See doc/item_presentation.md for the exact supported Unicode ranges.
ContentResult<TextArt> ParseTextArt(eastl::span<const String> lines,
    Diagnostics* diagnostics = nullptr);

}
