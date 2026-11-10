#include "sdl/atlas_builder.h"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace cataclysmx::sdl {
namespace {
constexpr std::uint64_t imageBudget = 64 * 1024 * 1024;
constexpr std::uint64_t pngBudget = 16 * 1024 * 1024;
struct Surface {
    SDL_Surface* value = nullptr;
    ~Surface() { SDL_DestroySurface(value); }
};
std::uint32_t Big(const unsigned char* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
}

eastl::expected<bool, AssetError> BuildSourceAtlases(const String& directory,
    const TilesetDefinition& d, Vector<SDL_Surface*>& pages, std::stop_token stop, Diagnostics* diagnostics) {
    String current = directory;
    auto fail = [&](AssetError error, const char* message) -> eastl::expected<bool, AssetError> {
        if (diagnostics) { diagnostics->Add(DiagnosticFrame{String(message), DiagnosticLocation{current, {}, {}, {}}}); }
        return eastl::unexpected(error);
    };
    std::uint64_t pageBytes = 0;
    for (const auto& page : d.atlases) {
        pageBytes += std::uint64_t(page.width) * page.height * 4;
        if (pageBytes > imageBudget) { return fail(AssetError::Bounds, "atlas pages exceed CPU image budget"); }
    }
    for (const auto& page : d.atlases) {
        if (stop.stop_requested()) { return fail(AssetError::Cancelled, "atlas assembly cancelled"); }
        auto* surface = SDL_CreateSurface(static_cast<int>(page.width), static_cast<int>(page.height), SDL_PIXELFORMAT_RGBA32);
        if (!surface) { return fail(AssetError::Decode, SDL_GetError()); }
        pages.push_back(surface);
        if (!SDL_ClearSurface(surface, 0, 0, 0, 0)) { return fail(AssetError::Decode, SDL_GetError()); }
    }
    namespace fs = std::filesystem;
    std::error_code error;
    const auto root = fs::canonical(fs::u8path(directory.c_str()), error);
    if (error) { return fail(AssetError::File, "cannot resolve source pack"); }
    for (const auto& source : d.sourceImages) {
        if (stop.stop_requested()) { return fail(AssetError::Cancelled, "atlas assembly cancelled"); }
        current = source.path;
        const auto path = fs::canonical(root / fs::u8path(source.path.c_str()), error);
        if (error) { return fail(AssetError::File, "PNG disappeared before decoding"); }
        auto c = path.begin();
        for (auto r = root.begin(); r != root.end(); ++r, ++c) {
            if (c == path.end() || *r != *c) { return fail(AssetError::File, "PNG escapes source pack"); }
        }
        const auto utf8 = path.u8string();
        auto* file = SDL_IOFromFile(reinterpret_cast<const char*>(utf8.c_str()), "rb");
        if (!file) { return fail(AssetError::File, SDL_GetError()); }
        const auto length = SDL_GetIOSize(file);
        // Account for the encoded PNG, decoder storage and RGBA conversion together.
        const auto decodeBytes = std::uint64_t(source.width) * source.height * 8;
        if (length < 33 || length > static_cast<Sint64>(pngBudget)
            || decodeBytes + static_cast<std::uint64_t>(length) > imageBudget - pageBytes) {
            SDL_CloseIO(file); return fail(AssetError::Bounds, "PNG decode scratch and atlas pages exceed CPU budget");
        }
        Vector<unsigned char> encoded(static_cast<std::size_t>(length));
        const auto read = SDL_ReadIO(file, encoded.data(), encoded.size()); SDL_CloseIO(file);
        constexpr unsigned char signature[] = {137,80,78,71,13,10,26,10};
        if (read != encoded.size()) { return fail(AssetError::File, "could not read PNG"); }
        if (std::memcmp(encoded.data(), signature, 8) || Big(encoded.data() + 8) != 13 || std::memcmp(encoded.data() + 12, "IHDR", 4)
            || Big(encoded.data() + 16) != source.width || Big(encoded.data() + 20) != source.height) {
            return fail(AssetError::Decode, "PNG header changed since source discovery");
        }
        auto* stream = SDL_IOFromConstMem(encoded.data(), encoded.size());
        Surface decoded{stream ? IMG_LoadPNG_IO(stream) : nullptr};
        if (stream) { SDL_CloseIO(stream); }
        if (!decoded.value) { return fail(AssetError::Decode, SDL_GetError()); }
        if (decoded.value->w != static_cast<int>(source.width) || decoded.value->h != static_cast<int>(source.height)
            || decoded.value->pitch < 0 || std::uint64_t(decoded.value->pitch) * source.height > decodeBytes / 2) {
            return fail(AssetError::Bounds, "decoded PNG does not match validated dimensions");
        }
        Surface rgba{SDL_ConvertSurface(decoded.value, SDL_PIXELFORMAT_RGBA32)};
        if (!rgba.value) { return fail(AssetError::Decode, SDL_GetError()); }
        const auto columns = source.width / source.frameWidth;
        for (std::uint32_t frame = 0; frame < source.frameCount; ++frame) {
            if (stop.stop_requested()) { return fail(AssetError::Cancelled, "atlas assembly cancelled"); }
            const auto& sprite = d.sprites[source.firstSprite + frame];
            auto* page = pages[sprite.atlasIndex];
            const int sx = static_cast<int>((frame % columns) * source.frameWidth);
            const int sy = static_cast<int>((frame / columns) * source.frameHeight);
            const int w = static_cast<int>(sprite.width), h = static_cast<int>(sprite.height);
            // Raw copy preserves even fully transparent RGB; duplicate edge texels
            // into the padding so sampling cannot bleed into a neighboring sprite.
            for (int y = -1; y <= h; ++y) {
                const auto* row = static_cast<const unsigned char*>(rgba.value->pixels)
                    + (sy + std::clamp(y, 0, h - 1)) * rgba.value->pitch;
                auto* destination = static_cast<unsigned char*>(page->pixels)
                    + (static_cast<int>(sprite.y) + y) * page->pitch + (sprite.x - 1) * 4;
                std::memcpy(destination, row + sx * 4, 4);
                std::memcpy(destination + 4, row + sx * 4, static_cast<std::size_t>(w) * 4);
                std::memcpy(destination + (w + 1) * 4, row + (sx + w - 1) * 4, 4);
            }
        }
    }
    return true;
}
}
