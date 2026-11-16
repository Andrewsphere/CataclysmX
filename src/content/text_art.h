#pragma once

#include "content/content_result.h"
#include "core/containers.h"
#include "core/diagnostics.h"
#include <EASTL/span.h>

namespace cataclysmx {

struct ArtColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    bool useDefault = true;
    static constexpr ArtColor Rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        return {red, green, blue, false};
    }
    bool operator==(const ArtColor&) const = default;
};
struct ArtRun {
    String text;
    ArtColor color;
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
// line; named colors resolve to RGB, colors reset between lines, << escapes <,
// and <reset> restores the context default.
// Limits: 128 lines, 256 cells/line, 4096 source bytes/line, 131072 bytes total.
// Errors locate the decoded art line and byte-column (both one-based).
// See doc/item_presentation.md for the exact supported Unicode ranges.
ContentResult<TextArt> ParseTextArt(eastl::span<const String> lines,
    Diagnostics* diagnostics = nullptr);

// Versioned UTF-8 asset: @text-art 1, local @color name #RRGGBB declarations,
// optional @default name, then --- and the glyph rows. Diagnostics use physical
// file line/byte-column coordinates. LF and CRLF have identical meaning.
ContentResult<TextArt> ParseTextArtFile(eastl::string_view text,
    Diagnostics* diagnostics = nullptr);

}
