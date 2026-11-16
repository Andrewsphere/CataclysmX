#include "content/content_assets.h"

#include <EASTL/utility.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cataclysmx {
namespace {

ContentResult<String> Fail(ContentError error, eastl::string_view path,
    const char* message, Diagnostics* diagnostics) {
    if (diagnostics) {
        DiagnosticFrame frame;
        frame.message = message;
        frame.location = DiagnosticLocation{String(path.data(), path.size()), {}, {}, {}};
        diagnostics->Add(eastl::move(frame));
    }
    return eastl::unexpected(error);
}

bool Contained(const std::filesystem::path& root, const std::filesystem::path& file) {
    auto part = file.begin();
    for (const auto& component : root) {
        if (part == file.end() || component != *part) { return false; }
        ++part;
    }
    return part != file.end();
}

struct File {
    std::FILE* value = nullptr;
    ~File() { if (value) { std::fclose(value); } }
};

bool ValidUtf8(eastl::string_view text) {
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset++]);
        if (first < 0x80) { continue; }
        std::uint32_t scalar;
        std::uint32_t minimum;
        std::size_t remaining;
        if (first >= 0xc2 && first <= 0xdf) {
            scalar = first & 0x1f; minimum = 0x80; remaining = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            scalar = first & 0x0f; minimum = 0x800; remaining = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            scalar = first & 7; minimum = 0x10000; remaining = 3;
        } else { return false; }
        if (remaining > text.size() - offset) { return false; }
        for (std::size_t i = 0; i < remaining; ++i) {
            const auto byte = static_cast<unsigned char>(text[offset++]);
            if ((byte & 0xc0) != 0x80) { return false; }
            scalar = (scalar << 6) | (byte & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
bool NativeWidePath(eastl::string_view text, unsigned codePage, std::wstring& output) {
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int size = static_cast<int>(text.size());
    const int length = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (!length) { return false; }
    output.resize(static_cast<std::size_t>(length));
    return MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text.data(), size, output.data(), length) == length;
}
#endif

}

bool ValidContentAssetPath(eastl::string_view path) {
    if (path.size() < 2 || path[0] != '/' || path.back() == '/') { return false; }
    if (!ValidUtf8(path)) { return false; }
    for (const unsigned char ch : path) {
        if (ch < 0x20 || ch == 0x7f || ch == '\\' || ch == ':' || ch == '?' || ch == '*'
            || ch == '"' || ch == '<' || ch == '>' || ch == '|') { return false; }
    }
    std::size_t start = 1;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == eastl::string_view::npos) { end = path.size(); }
        const auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
            return false;
        }
        start = end + 1;
    }
    return true;
}

ContentResult<String> ReadContentAssetFile(eastl::string_view dataRoot,
    eastl::string_view path, Diagnostics* diagnostics) {
    if (!ValidContentAssetPath(path)) {
        return Fail(ContentError::InvalidData, path, "invalid data-root asset path", diagnostics);
    }
    if (dataRoot.empty() || std::memchr(dataRoot.data(), 0, dataRoot.size())) {
        return Fail(ContentError::Io, path, "asset loading requires an explicit data root", diagnostics);
    }
    namespace fs = std::filesystem;
    std::error_code error;
#ifdef _WIN32
    std::wstring hostRoot;
    std::wstring relative;
    if (!NativeWidePath(dataRoot, CP_ACP, hostRoot)) {
        return Fail(ContentError::Io, path, "invalid native-encoded asset data root", diagnostics);
    }
    if (!NativeWidePath(path.substr(1), CP_UTF8, relative)) {
        return Fail(ContentError::InvalidData, path, "invalid UTF-8 asset path", diagnostics);
    }
#else
    const std::string hostRoot(dataRoot.begin(), dataRoot.end());
    const std::string relative(path.begin() + 1, path.end());
#endif
    // Construct from the host's native character type; encoding failures were
    // handled above, outside filesystem's throwing conversion constructors.
    const auto root = fs::canonical(fs::path(hostRoot), error);
    if (error) { return Fail(ContentError::Io, path, "could not resolve asset data root", diagnostics); }
    const auto resolved = fs::canonical(root / fs::path(relative), error);
    if (error) { return Fail(ContentError::Io, path, "could not resolve asset file", diagnostics); }
    if (!Contained(root, resolved)) {
        return Fail(ContentError::InvalidData, path, "asset file escapes the data root", diagnostics);
    }
    if (!fs::is_regular_file(resolved, error) || error) {
        return Fail(ContentError::Io, path, "asset must be a regular file", diagnostics);
    }
    File file;
#ifdef _MSC_VER
    _wfopen_s(&file.value, resolved.c_str(), L"rb");
#else
    file.value = std::fopen(resolved.c_str(), "rb");
#endif
    if (!file.value) { return Fail(ContentError::Io, path, "could not open asset file", diagnostics); }
    String text;
    char buffer[8192];
    constexpr std::size_t maxFileBytes = 147456;
    for (;;) {
        const auto count = std::fread(buffer, 1, sizeof(buffer), file.value);
        if (count > maxFileBytes - text.size()) {
            return Fail(ContentError::InvalidData, path, "text art file exceeds 147456 bytes", diagnostics);
        }
        text.append(buffer, count);
        if (std::ferror(file.value) || (!count && !std::feof(file.value))) {
            return Fail(ContentError::Io, path, "could not read asset file", diagnostics);
        }
        if (std::feof(file.value)) { break; }
    }
    const int closeResult = std::fclose(file.value);
    file.value = nullptr;
    if (closeResult) { return Fail(ContentError::Io, path, "could not close asset file", diagnostics); }
    return text;
}

}
