#pragma once

#include "client/perception_style.h"
#include "core/containers.h"
#include <SDL3/SDL_render.h>

namespace cataclysmx::sdl {

// Main-thread resource. The borrowed renderer/device must outlive this object.
// Applies perception to encoded RGB, for the default SDL_COLORSPACE_SRGB output.
class SpriteShader {
public:
    explicit SpriteShader(SDL_Renderer* renderer);
    ~SpriteShader();
    SpriteShader(const SpriteShader&) = delete;
    SpriteShader& operator=(const SpriteShader&) = delete;

    bool Ready() const;
    bool Apply(const PerceptionTransform& transform);
    bool Clear();
    static bool Supported(SDL_Renderer* renderer);

private:
    struct CachedState {
        PerceptionTransform transform;
        SDL_GPURenderState* state = nullptr;
    };

    SDL_Renderer* renderer_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShader* shader_ = nullptr;
    Vector<CachedState> states_;
};

}
