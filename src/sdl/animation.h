#pragma once

#include "content/tileset.h"
#include "core/world_types.h"

namespace cataclysmx::sdl {

struct SpriteSample {
    TilesetMapping mapping;
    eastl::optional<std::uint64_t> delayMilliseconds;
    eastl::optional<std::uint32_t> animationIndex;
};
enum class SpriteActivationResult { Unavailable, LoopStarted, Stopped, PlayedOnce };

// Render-owner state. Item IDs are non-owning and scoped to the supplied generations.
class SpritePlaybackController {
public:
    void SetGeneration(std::uint64_t session, std::uint64_t assets);
    SpriteActivationResult Activate(PersistentId item, const TilesetMapping& mapping,
        const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds);
    void Stop(PersistentId item);
    void RetireCompleted(const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds);
    SpriteSample Sample(PersistentId item, const TilesetMapping& mapping,
        const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds) const;
private:
    struct Playing {
        PersistentId item = 0;
        SpriteActivationBinding binding;
        std::uint64_t startMilliseconds = 0;
    };
    std::uint64_t session_ = 0, assets_ = 0;
    Vector<Playing> playing_;
};

}
