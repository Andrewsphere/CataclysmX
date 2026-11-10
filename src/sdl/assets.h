#pragma once

#include "content/tileset.h"

#include <SDL3/SDL.h>
#include <EASTL/array.h>
#include <atomic>
#include <thread>

namespace cataclysmx::sdl {

enum class AssetState { Loading, Ready, Failed };
enum class AssetError { Metadata, File, Decode, Bounds, Upload, Cancelled };

class SdlAssets {
public:
    using Wake = void (*)(void*);
    // A null catalog selects built-in glyphs for isolated render tests.
    explicit SdlAssets(const ContentCatalog* content = nullptr, String directory = {},
        Wake wake = nullptr, void* userdata = nullptr);
    ~SdlAssets();
    SdlAssets(const SdlAssets&) = delete;
    SdlAssets& operator=(const SdlAssets&) = delete;

    // Uploads at most one atlas per call; activation waits for the complete pack.
    AssetState Poll(SDL_Renderer* renderer);
    AssetState State() const { return state_; }
    const TilesetDefinition& Definition() const { return definition_; }
    const Diagnostics& Report() const { return diagnostics_; }
    eastl::optional<AssetError> Error() const { return error_; }
    SDL_Texture* Atlas() const { return glyphAtlas_; }
    SDL_Texture* Texture(std::uint32_t atlasIndex) const;
    static SDL_FRect GlyphRect(eastl::string_view symbol);

private:
    void Prepare(const ContentCatalog* content, const String& directory, std::stop_token stop);
    void Fail(AssetError error, eastl::string_view message);
    void ClearTextures();

    static constexpr int glyphSize_ = 32;
    static constexpr int glyphCount_ = 16;
    eastl::array<unsigned char, glyphCount_ * glyphSize_ * glyphSize_ * 4> pixels_{};
    TilesetDefinition definition_;
    Vector<SDL_Surface*> surfaces_;
    Vector<SDL_Texture*> textures_;
    Diagnostics diagnostics_;
    eastl::optional<AssetError> error_;
    SDL_Texture* glyphAtlas_ = nullptr;
    std::size_t nextUpload_ = 0;
    AssetState state_ = AssetState::Loading;
    Wake wake_ = nullptr;
    void* userdata_ = nullptr;
    std::atomic<bool> prepared_{false};
    std::jthread worker_;
};

}
