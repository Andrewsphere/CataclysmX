#include "content/text_art.h"

#include <EASTL/utility.h>
#include <cstdio>

namespace cataclysmx {
namespace {

bool Fail(Diagnostics* diagnostics, std::size_t line, std::size_t column,
          eastl::string_view message) {
    if (diagnostics) {
        char path[32];
        std::snprintf(path, sizeof(path), "[%zu]", line);
        DiagnosticFrame frame;
        frame.message.assign(message.data(), message.size());
        frame.location = DiagnosticLocation{{}, path, line + 1, column + 1};
        diagnostics->Add(eastl::move(frame));
    }
    return false;
}

bool ReadArtColor(eastl::string_view name, ArtColor& color) {
    struct NamedColor { eastl::string_view name; ArtColor color; };
    static constexpr NamedColor colors[] = {
        {"default", ArtColor::Default}, {"reset", ArtColor::Default},
        {"black", ArtColor::Black}, {"k", ArtColor::Black},
        {"red", ArtColor::Red}, {"r", ArtColor::Red},
        {"green", ArtColor::Green}, {"g", ArtColor::Green},
        {"yellow", ArtColor::Yellow}, {"y", ArtColor::Yellow},
        {"blue", ArtColor::Blue}, {"b", ArtColor::Blue},
        {"magenta", ArtColor::Magenta}, {"m", ArtColor::Magenta},
        {"cyan", ArtColor::Cyan}, {"c", ArtColor::Cyan},
        {"white", ArtColor::White}, {"w", ArtColor::White},
        {"gray", ArtColor::Gray}, {"grey", ArtColor::Gray},
        {"darkgray", ArtColor::DarkGray}, {"darkgrey", ArtColor::DarkGray},
        {"dark_gray", ArtColor::DarkGray},
    };
    for (const auto& entry : colors) {
        if (name == entry.name) { color = entry.color; return true; }
    }
    return false;
}

bool SingleCell(std::uint32_t scalar) {
    // An explicit portable content policy, not a platform wcwidth query.
    return (scalar >= 0x20 && scalar <= 0x7e)
        || (scalar >= 0xa1 && scalar <= 0xac)
        || (scalar >= 0xae && scalar <= 0x24f)
        || (scalar >= 0x391 && scalar <= 0x3a1)
        || (scalar >= 0x3a3 && scalar <= 0x481)
        || (scalar >= 0x48a && scalar <= 0x52f)
        || (scalar >= 0x2500 && scalar <= 0x259f);
}

bool ReadScalar(const String& source, std::size_t offset, std::size_t& bytes, std::uint32_t& scalar) {
    const auto first = static_cast<unsigned char>(source[offset]);
    std::uint32_t minimum = 0;
    if (first < 0x80) { bytes = 1; scalar = first; }
    else if (first >= 0xc2 && first <= 0xdf) { bytes = 2; scalar = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { bytes = 3; scalar = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { bytes = 4; scalar = first & 7; minimum = 0x10000; }
    else { return false; }
    if (bytes > source.size() - offset) { return false; }
    for (std::size_t i = 1; i < bytes; ++i) {
        const auto byte = static_cast<unsigned char>(source[offset + i]);
        if ((byte & 0xc0) != 0x80) { return false; }
        scalar = (scalar << 6) | (byte & 0x3f);
    }
    return scalar >= minimum && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff);
}

bool ParseLine(const String& source, std::size_t lineIndex, ArtLine& line,
               std::uint32_t& cells, Diagnostics* diagnostics) {
    ArtColor color = ArtColor::Default;
    for (std::size_t offset = 0; offset < source.size();) {
        const std::size_t start = offset;
        std::size_t bytes = 1;
        if (source[offset] == '<') {
            if (offset + 1 < source.size() && source[offset + 1] == '<') {
                ++offset; // Append the second '<' as one ordinary cell.
            } else {
                const auto end = source.find('>', offset + 1);
                if (end == String::npos) { return Fail(diagnostics, lineIndex, start, "unterminated art color tag"); }
                const eastl::string_view name(source.data() + offset + 1, end - offset - 1);
                if (!ReadArtColor(name, color)) {
                    return Fail(diagnostics, lineIndex, start, "unknown or malformed art color tag; use <reset> or a named palette color");
                }
                offset = end + 1;
                continue;
            }
        } else {
            std::uint32_t scalar = 0;
            if (!ReadScalar(source, offset, bytes, scalar)) {
                return Fail(diagnostics, lineIndex, start, "invalid UTF-8 in art");
            }
            if (!SingleCell(scalar)) {
                return Fail(diagnostics, lineIndex, start, "art character is outside the supported single-cell ranges (controls, combining, wide and zero-width characters are not allowed)");
            }
        }
        if (cells == 256) { return Fail(diagnostics, lineIndex, start, "art line exceeds 256 cells"); }
        if (line.empty() || line.back().color != color) { line.push_back(ArtRun{{}, color, 0}); }
        line.back().text.append(source.data() + offset, bytes);
        ++line.back().cells;
        ++cells;
        offset += bytes;
    }
    return true;
}

}

ContentResult<TextArt> ParseTextArt(eastl::span<const String> lines, Diagnostics* diagnostics) {
    if (lines.size() > 128) {
        Fail(diagnostics, 128, 0, "art exceeds 128 lines");
        return eastl::unexpected(ContentError::InvalidData);
    }
    std::size_t totalBytes = 0;
    // Validate source limits before constructing output storage.
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].size() > 4096 || lines[i].size() > 131072 - totalBytes) {
            Fail(diagnostics, i, 0, "art source exceeds 4096 bytes per line or 131072 bytes total");
            return eastl::unexpected(ContentError::InvalidData);
        }
        totalBytes += lines[i].size();
    }
    TextArt art;
    art.lines.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        ArtLine line;
        std::uint32_t cells = 0;
        if (!ParseLine(lines[i], i, line, cells, diagnostics)) {
            return eastl::unexpected(ContentError::InvalidData);
        }
        if (cells > art.width) { art.width = cells; }
        art.lines.push_back(eastl::move(line));
    }
    return art;
}

}
