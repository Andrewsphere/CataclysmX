#include "content/text_art.h"

#include <EASTL/utility.h>
#include <cstdio>

namespace cataclysmx {
namespace {

bool Fail(Diagnostics* diagnostics, std::size_t line, std::size_t column,
          eastl::string_view message, bool file = false) {
    if (diagnostics) {
        char path[32];
        if (file) { path[0] = '\0'; }
        else { std::snprintf(path, sizeof(path), "[%zu]", line); }
        DiagnosticFrame frame;
        frame.message.assign(message.data(), message.size());
        frame.location = DiagnosticLocation{{}, path, line + 1, column + 1};
        diagnostics->Add(eastl::move(frame));
    }
    return false;
}

struct NamedColor { eastl::string_view name; ArtColor color; };

bool ReadArtColor(eastl::string_view name, ArtColor& color) {
    static constexpr NamedColor colors[] = {
        {"black", ArtColor::Rgb(0, 0, 0)}, {"k", ArtColor::Rgb(0, 0, 0)},
        {"red", ArtColor::Rgb(220, 83, 83)}, {"r", ArtColor::Rgb(220, 83, 83)},
        {"green", ArtColor::Rgb(88, 194, 132)}, {"g", ArtColor::Rgb(88, 194, 132)},
        {"yellow", ArtColor::Rgb(225, 204, 89)}, {"y", ArtColor::Rgb(225, 204, 89)},
        {"blue", ArtColor::Rgb(100, 140, 230)}, {"b", ArtColor::Rgb(100, 140, 230)},
        {"magenta", ArtColor::Rgb(200, 110, 210)}, {"m", ArtColor::Rgb(200, 110, 210)},
        {"cyan", ArtColor::Rgb(90, 200, 210)}, {"c", ArtColor::Rgb(90, 200, 210)},
        {"white", ArtColor::Rgb(245, 245, 245)}, {"w", ArtColor::Rgb(245, 245, 245)},
        {"gray", ArtColor::Rgb(170, 170, 170)}, {"grey", ArtColor::Rgb(170, 170, 170)},
        {"darkgray", ArtColor::Rgb(90, 90, 90)}, {"darkgrey", ArtColor::Rgb(90, 90, 90)},
        {"dark_gray", ArtColor::Rgb(90, 90, 90)},
    };
    for (const auto& entry : colors) {
        if (name == entry.name) { color = entry.color; return true; }
    }
    return false;
}

struct Palette {
    eastl::span<const NamedColor> colors;
    ArtColor defaultColor;
    bool file = false;
};

bool ResolveColor(eastl::string_view name, const Palette& palette, ArtColor& color) {
    if (name == "default" || name == "reset") { color = palette.defaultColor; return true; }
    if (!palette.file) { return ReadArtColor(name, color); }
    for (const auto& entry : palette.colors) {
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

bool ReadScalar(eastl::string_view source, std::size_t offset, std::size_t& bytes, std::uint32_t& scalar) {
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

bool ParseLine(eastl::string_view source, std::size_t lineIndex, ArtLine& line,
               std::uint32_t& cells, Diagnostics* diagnostics, const Palette& palette) {
    ArtColor color = palette.defaultColor;
    for (std::size_t offset = 0; offset < source.size();) {
        const std::size_t start = offset;
        std::size_t bytes = 1;
        if (source[offset] == '<') {
            if (offset + 1 < source.size() && source[offset + 1] == '<') {
                ++offset; // Append the second '<' as one ordinary cell.
            } else {
                const auto end = source.find('>', offset + 1);
                if (end == eastl::string_view::npos) { return Fail(diagnostics, lineIndex, start, "unterminated art color tag", palette.file); }
                const eastl::string_view name(source.data() + offset + 1, end - offset - 1);
                if (!ResolveColor(name, palette, color)) {
                    return Fail(diagnostics, lineIndex, start, "unknown or malformed art color tag; use <reset> or a named palette color", palette.file);
                }
                offset = end + 1;
                continue;
            }
        } else {
            std::uint32_t scalar = 0;
            if (!ReadScalar(source, offset, bytes, scalar)) {
                return Fail(diagnostics, lineIndex, start, "invalid UTF-8 in art", palette.file);
            }
            if (!SingleCell(scalar)) {
                return Fail(diagnostics, lineIndex, start, "art character is outside the supported single-cell ranges (controls, combining, wide and zero-width characters are not allowed)", palette.file);
            }
        }
        if (cells == 256) { return Fail(diagnostics, lineIndex, start, "art line exceeds 256 cells", palette.file); }
        if (line.empty() || line.back().color != color) { line.push_back(ArtRun{{}, color, 0}); }
        line.back().text.append(source.data() + offset, bytes);
        ++line.back().cells;
        ++cells;
        offset += bytes;
    }
    return true;
}

constexpr std::size_t maxTextArtFileBytes = 147456;
constexpr std::size_t maxTextArtHeaderBytes = 16384;

bool ReadPhysicalLine(eastl::string_view text, std::size_t& offset, std::size_t lineIndex,
                      eastl::string_view& line, Diagnostics* diagnostics) {
    const std::size_t start = offset;
    const auto newline = text.find('\n', start);
    std::size_t end = newline == eastl::string_view::npos ? text.size() : newline;
    offset = newline == eastl::string_view::npos ? text.size() : newline + 1;
    if (newline != eastl::string_view::npos && end > start && text[end - 1] == '\r') { --end; }
    line = text.substr(start, end - start);
    const auto carriageReturn = line.find('\r');
    if (carriageReturn != eastl::string_view::npos) {
        return Fail(diagnostics, lineIndex, carriageReturn, "bare CR is not allowed; use LF or CRLF", true);
    }
    if (line.size() > 4096) {
        return Fail(diagnostics, lineIndex, 4096, "art physical line exceeds 4096 bytes", true);
    }
    return true;
}

bool ValidColorName(eastl::string_view name) {
    if (name.empty() || name.size() > 64 || name == "default" || name == "reset"
        || name[0] < 'a' || name[0] > 'z') { return false; }
    for (char ch : name) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) { return false; }
    }
    return true;
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
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
        if (!ParseLine({lines[i].data(), lines[i].size()}, i, line, cells, diagnostics, Palette{})) {
            return eastl::unexpected(ContentError::InvalidData);
        }
        if (cells > art.width) { art.width = cells; }
        art.lines.push_back(eastl::move(line));
    }
    return art;
}

ContentResult<TextArt> ParseTextArtFile(eastl::string_view text, Diagnostics* diagnostics) {
    const auto invalid = [&](std::size_t line, std::size_t column, eastl::string_view message)
        -> ContentResult<TextArt> {
        Fail(diagnostics, line, column, message, true);
        return eastl::unexpected(ContentError::InvalidData);
    };
    if (text.size() > maxTextArtFileBytes) { return invalid(0, 0, "art file exceeds 147456 bytes"); }
    std::size_t offset = 0;
    eastl::string_view source;
    if (!ReadPhysicalLine(text, offset, 0, source, diagnostics)) {
        return eastl::unexpected(ContentError::InvalidData);
    }
    constexpr eastl::string_view prefix = "@text-art ";
    if (source.substr(0, prefix.size()) != prefix) { return invalid(0, 0, "expected @text-art 1 on the first line"); }
    const auto versionText = source.substr(prefix.size());
    if (versionText.empty() || versionText[0] < '1' || versionText[0] > '9') {
        return invalid(0, prefix.size(), "expected a canonical positive uint32 version");
    }
    std::uint32_t version = 0;
    for (std::size_t i = 0; i < versionText.size(); ++i) {
        const char ch = versionText[i];
        if (ch < '0' || ch > '9' || version > (0xffffffffu - static_cast<unsigned>(ch - '0')) / 10) {
            return invalid(0, prefix.size() + i, "expected a canonical positive uint32 version");
        }
        version = version * 10 + static_cast<unsigned>(ch - '0');
    }
    if (version != 1) {
        Fail(diagnostics, 0, prefix.size(), "unsupported text art version", true);
        return eastl::unexpected(ContentError::UnsupportedVersion);
    }

    NamedColor colors[64];
    std::size_t colorCount = 0;
    eastl::string_view defaultName;
    std::size_t defaultLine = 0;
    std::size_t lineIndex = 1;
    bool separator = false;
    for (; offset < text.size(); ++lineIndex) {
        if (!ReadPhysicalLine(text, offset, lineIndex, source, diagnostics)) {
            return eastl::unexpected(ContentError::InvalidData);
        }
        if (offset > maxTextArtHeaderBytes) { return invalid(lineIndex, 0, "art header exceeds 16384 bytes"); }
        if (source == "---") { separator = true; ++lineIndex; break; }
        if (source.substr(0, 7) == "@color ") {
            const auto space = source.find(' ', 7);
            if (space == eastl::string_view::npos || !ValidColorName(source.substr(7, space - 7))) {
                return invalid(lineIndex, 7, "invalid or reserved palette color name");
            }
            const auto name = source.substr(7, space - 7);
            for (std::size_t i = 0; i < colorCount; ++i) {
                if (colors[i].name == name) { return invalid(lineIndex, 7, "duplicate palette color name"); }
            }
            if (colorCount == 64) { return invalid(lineIndex, 7, "palette exceeds 64 colors"); }
            const auto hex = source.substr(space + 1);
            if (hex.size() != 7 || hex[0] != '#') {
                return invalid(lineIndex, space + 1, "expected #RRGGBB palette color");
            }
            std::uint8_t components[3]{};
            for (std::size_t i = 1; i < 7; ++i) {
                const int digit = HexDigit(hex[i]);
                if (digit < 0) { return invalid(lineIndex, space + 1 + i, "invalid hexadecimal color digit"); }
                components[(i - 1) / 2] = static_cast<std::uint8_t>(components[(i - 1) / 2] * 16 + digit);
            }
            colors[colorCount++] = {name, ArtColor::Rgb(components[0], components[1], components[2])};
        } else if (source.substr(0, 9) == "@default ") {
            if (!defaultName.empty()) { return invalid(lineIndex, 0, "duplicate @default directive"); }
            defaultName = source.substr(9);
            if (!ValidColorName(defaultName)) { return invalid(lineIndex, 9, "invalid default palette name"); }
            defaultLine = lineIndex;
        } else {
            return invalid(lineIndex, 0, "expected @color, @default, or --- separator");
        }
    }
    if (!separator) { return invalid(lineIndex, 0, "missing --- separator"); }
    Palette palette{{colors, colorCount}, {}, true};
    if (!defaultName.empty() && !ResolveColor(defaultName, palette, palette.defaultColor)) {
        return invalid(defaultLine, 9, "unknown default palette color");
    }
    TextArt art;
    std::size_t bodyBytes = 0;
    for (; offset < text.size(); ++lineIndex) {
        if (!ReadPhysicalLine(text, offset, lineIndex, source, diagnostics)) {
            return eastl::unexpected(ContentError::InvalidData);
        }
        if (art.lines.size() == 128) { return invalid(lineIndex, 0, "art exceeds 128 rows"); }
        if (source.size() > 131072 - bodyBytes) { return invalid(lineIndex, 0, "art body exceeds 131072 source bytes"); }
        bodyBytes += source.size();
        ArtLine line;
        std::uint32_t cells = 0;
        if (!ParseLine(source, lineIndex, line, cells, diagnostics, palette)) {
            return eastl::unexpected(ContentError::InvalidData);
        }
        if (cells > art.width) { art.width = cells; }
        art.lines.push_back(eastl::move(line));
    }
    if (art.lines.empty()) { return invalid(lineIndex, 0, "art file requires at least one body row"); }
    return art;
}

}
