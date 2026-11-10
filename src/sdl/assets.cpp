#include "sdl/assets.h"
#include "sdl/atlas_builder.h"

#include <cstdlib>
#include <filesystem>

namespace cataclysmx::sdl {
namespace {
// Resolve symlinks too: lexical '..' validation alone cannot contain pack files.
bool ContainedPath(const String& directory, const char* file, String& path) {
    std::error_code error;
    const auto root = std::filesystem::canonical(std::filesystem::u8path(directory.c_str()), error);
    if (error) { return false; }
    const auto candidate = std::filesystem::canonical(root / std::filesystem::u8path(file), error);
    if (error) { return false; }
    auto r = root.begin();
    auto c = candidate.begin();
    for (; r != root.end(); ++r, ++c) {
        if (c == candidate.end() || *r != *c) { return false; }
    }
    const auto utf8 = candidate.u8string();
    path.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    return true;
}
}

SdlAssets::SdlAssets(const ContentCatalog* content, String directory, Wake wake, void* userdata)
    : wake_(wake), userdata_(userdata), worker_([this, content, directory = eastl::move(directory)](std::stop_token stop) {
        Prepare(content, directory, stop);
        prepared_.store(true, std::memory_order_release);
        if (wake_) { wake_(userdata_); }
    }) {}

SdlAssets::~SdlAssets() {
    worker_.request_stop();
    worker_.join();
    if (!SDL_IsMainThread()) { std::abort(); }
    ClearTextures();
    for (auto* surface : surfaces_) { SDL_DestroySurface(surface); }
}

void SdlAssets::Fail(AssetError error, eastl::string_view message) {
    error_ = error;
    diagnostics_.Add(message);
}

void SdlAssets::Prepare(const ContentCatalog* content, const String& directory, std::stop_token stop) {
    constexpr unsigned char glyphs[16][8] = {
        {0,0,0,0,0,0,0,0}, {0,0,0,0,0,24,24,0},
        {20,20,127,20,127,20,20,0}, {0,8,8,62,8,8,0,0},
        {1,2,4,8,16,32,64,0}, {127,8,8,8,8,8,8,0},
        {0,24,24,0,24,24,0,0}, {32,16,8,4,8,16,32,0},
        {28,34,77,85,78,32,30,0}, {62,33,33,62,32,32,32,0},
        {0,0,62,4,8,16,62,0}, {8,8,8,8,8,0,8,0},
        {0,20,8,62,8,20,0,0}, {24,36,40,16,42,36,26,0},
        {62,65,1,6,8,0,8,0}, {127,127,127,127,127,127,127,127}
    };
    // Expand the compact bitmap masks into native 32x32 atlas cells.
    constexpr int maskScale = glyphSize_ / 8;
    for (int glyph = 0; glyph < glyphCount_; ++glyph) {
        for (int y = 0; y < glyphSize_; ++y) {
            for (int x = 0; x < glyphSize_; ++x) {
                const int index = (y * glyphCount_ * glyphSize_ + glyph * glyphSize_ + x) * 4;
                pixels_[index] = pixels_[index + 1] = pixels_[index + 2] = 255;
                pixels_[index + 3] = (glyphs[glyph][y / maskScale] & (1 << (7 - x / maskScale))) ? 255 : 0;
            }
        }
    }
    if (!content) {
        definition_.id = "glyphs"; definition_.name = "Simple glyphs";
        definition_.cellWidth = definition_.cellHeight = glyphSize_;
        return;
    }
    const String root = directory.empty() ? String(CATACLYSMX_DEFAULT_TILESET) : directory;
    String metadata;
    if (!ContainedPath(root, "tileset.json5", metadata)) {
        Fail(AssetError::File, "Tileset metadata is missing or outside the pack directory."); return;
    }
    auto parsed = LoadTilesetFile(metadata.c_str(), *content, &diagnostics_);
    if (!parsed) { error_ = AssetError::Metadata; return; }
    definition_ = eastl::move(*parsed);
    auto built = BuildSourceAtlases(root, definition_, surfaces_, stop, &diagnostics_);
    if (!built) { error_ = built.error(); }
}

void SdlAssets::ClearTextures() {
    for (auto* texture : textures_) { SDL_DestroyTexture(texture); }
    textures_.clear();
    SDL_DestroyTexture(glyphAtlas_); glyphAtlas_ = nullptr;
}

AssetState SdlAssets::Poll(SDL_Renderer* renderer) {
    if (!SDL_IsMainThread()) { std::abort(); }
    if (state_ != AssetState::Loading || !prepared_.load(std::memory_order_acquire)) { return state_; }
    if (error_) {
        for (auto*& surface : surfaces_) { SDL_DestroySurface(surface); surface = nullptr; }
        state_ = AssetState::Failed; return state_;
    }
    bool success = true;
    if (!glyphAtlas_) {
        glyphAtlas_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
            glyphCount_ * glyphSize_, glyphSize_);
        success = glyphAtlas_ && SDL_UpdateTexture(glyphAtlas_, nullptr, pixels_.data(), glyphCount_ * glyphSize_ * 4)
            && SDL_SetTextureScaleMode(glyphAtlas_, SDL_SCALEMODE_NEAREST)
            && SDL_SetTextureBlendMode(glyphAtlas_, SDL_BLENDMODE_BLEND);
    } else if (nextUpload_ < surfaces_.size()) {
        const auto maximum = SDL_GetNumberProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
        if (maximum > 0 && (surfaces_[nextUpload_]->w > maximum || surfaces_[nextUpload_]->h > maximum)) {
            Fail(AssetError::Bounds, "Atlas exceeds renderer texture dimensions.");
            ClearTextures();
            for (auto*& surface : surfaces_) { SDL_DestroySurface(surface); surface = nullptr; }
            state_ = AssetState::Failed; return state_;
        }
        auto* texture = SDL_CreateTextureFromSurface(renderer, surfaces_[nextUpload_]);
        success = texture && SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)
            && SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        if (success) {
            textures_.push_back(texture);
            SDL_DestroySurface(surfaces_[nextUpload_]); surfaces_[nextUpload_] = nullptr;
            ++nextUpload_;
        } else { SDL_DestroyTexture(texture); }
    }
    if (!success) {
        Fail(AssetError::Upload, SDL_GetError()); ClearTextures(); state_ = AssetState::Failed;
        for (auto*& surface : surfaces_) { SDL_DestroySurface(surface); surface = nullptr; }
    } else if (nextUpload_ == surfaces_.size()) { state_ = AssetState::Ready; }
    return state_;
}

SDL_Texture* SdlAssets::Texture(std::uint32_t atlasIndex) const {
    if (state_ != AssetState::Ready || atlasIndex >= textures_.size()) { std::abort(); }
    return textures_[atlasIndex];
}

SDL_FRect SdlAssets::GlyphRect(eastl::string_view symbol) {
    constexpr eastl::string_view symbols = " .#+/T:>@Pz!*&?";
    const auto index = symbol.size() == 1 ? symbols.find(symbol[0]) : eastl::string_view::npos;
    return {static_cast<float>((index == eastl::string_view::npos ? 14 : index) * glyphSize_),
        0, glyphSize_, glyphSize_};
}

}
