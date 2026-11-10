#include "sdl/animation.h"

#include <cstdlib>

namespace cataclysmx::sdl {
namespace {
const AnimationClip& Clip(const TilesetDefinition& tileset, std::uint32_t index) {
    if (index >= tileset.animations.size()) { std::abort(); }
    return tileset.animations[index];
}
SpriteSample BaseSample(const TilesetMapping& mapping, const TilesetDefinition& tileset,
    std::uint64_t elapsedMilliseconds) {
    if (mapping.kind != TilesetMappingKind::Animation) { return {mapping, {}}; }
    const auto sample = SampleAnimation(Clip(tileset, mapping.index), elapsedMilliseconds);
    auto resolved = mapping;
    resolved.kind = TilesetMappingKind::Sprite; resolved.index = sample.spriteIndex;
    resolved.activation.reset();
    return {resolved, sample.delayMilliseconds, mapping.index};
}
}

void SpritePlaybackController::SetGeneration(std::uint64_t session, std::uint64_t assets) {
    if (session_ == session && assets_ == assets) { return; }
    playing_.clear();
    session_ = session;
    assets_ = assets;
}

SpriteActivationResult SpritePlaybackController::Activate(PersistentId item, const TilesetMapping& mapping,
    const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds) {
    if (!item || !mapping.activation || mapping.kind == TilesetMappingKind::Missing) {
        return SpriteActivationResult::Unavailable;
    }
    RetireCompleted(tileset, elapsedMilliseconds);
    const auto binding = *mapping.activation;
    (void)Clip(tileset, binding.animationIndex);
    for (auto& playing : playing_) {
        if (playing.item != item) { continue; }
        if (playing.binding == binding && binding.kind == SpriteActivation::ToggleLoop) {
            Stop(item);
            return SpriteActivationResult::Stopped;
        }
        playing.binding = binding;
        playing.startMilliseconds = elapsedMilliseconds;
        return binding.kind == SpriteActivation::ToggleLoop
            ? SpriteActivationResult::LoopStarted : SpriteActivationResult::PlayedOnce;
    }
    playing_.push_back({item, binding, elapsedMilliseconds});
    return binding.kind == SpriteActivation::ToggleLoop
        ? SpriteActivationResult::LoopStarted : SpriteActivationResult::PlayedOnce;
}

void SpritePlaybackController::Stop(PersistentId item) {
    for (auto iterator = playing_.begin(); iterator != playing_.end(); ++iterator) {
        if (iterator->item == item) { playing_.erase(iterator); return; }
    }
}

void SpritePlaybackController::RetireCompleted(const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds) {
    for (auto iterator = playing_.begin(); iterator != playing_.end();) {
        if (iterator->binding.kind == SpriteActivation::PlayOnce
            && elapsedMilliseconds >= iterator->startMilliseconds
            && elapsedMilliseconds - iterator->startMilliseconds >= Clip(tileset, iterator->binding.animationIndex).totalMilliseconds) {
            iterator = playing_.erase(iterator);
        } else { ++iterator; }
    }
}

SpriteSample SpritePlaybackController::Sample(PersistentId item, const TilesetMapping& mapping,
    const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds) const {
    if (item && mapping.activation) {
        for (const auto& playing : playing_) {
            if (playing.item != item || playing.binding != *mapping.activation) { continue; }
            const bool once = playing.binding.kind == SpriteActivation::PlayOnce;
            // Explicit test clocks may be rewound; never wrap an unsigned subtraction.
            const auto time = once ? (elapsedMilliseconds >= playing.startMilliseconds
                ? elapsedMilliseconds - playing.startMilliseconds : 0) : elapsedMilliseconds;
            const auto sample = SampleAnimation(Clip(tileset, playing.binding.animationIndex), time,
                once ? AnimationPlayback::Once : AnimationPlayback::Loop);
            if (!sample.completed) {
                auto resolved = mapping;
                resolved.kind = TilesetMappingKind::Sprite; resolved.index = sample.spriteIndex;
                resolved.activation.reset();
                return {resolved, sample.delayMilliseconds, playing.binding.animationIndex};
            }
            break;
        }
    }
    return BaseSample(mapping, tileset, elapsedMilliseconds);
}
}
