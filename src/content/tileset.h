#pragma once

#include "content/content.h"
#include <EASTL/optional.h>
#include <cstdint>

namespace cataclysmx {

inline constexpr std::size_t MaxTilesetMetadataBytes = 4 * 1024 * 1024;
inline constexpr std::size_t MaxTilesetAtlases = 64;
inline constexpr std::size_t MaxTilesetEntries = 4096;

enum class TilesetMappingKind { Missing, Sprite, Animation, Visual };
enum class SpriteLayer { Ground, Upright };
struct SpriteTransform {
    std::uint8_t quarterTurns = 0;
    bool flipX = false, flipY = false;
    bool operator==(const SpriteTransform&) const = default;
};
enum class AnimationPlayback { Loop, Once };
enum class SpriteActivation { ToggleLoop, PlayOnce };
struct SpriteActivationBinding {
    std::uint32_t animationIndex = 0;
    SpriteActivation kind = SpriteActivation::PlayOnce;
    bool operator==(const SpriteActivationBinding&) const = default;
};
struct TilesetMapping {
    TilesetMappingKind kind = TilesetMappingKind::Missing;
    std::uint32_t index = 0;
    eastl::optional<SpriteActivationBinding> activation = {};
    SpriteTransform transform;
    bool operator==(const TilesetMapping&) const = default;
};
enum class GenericSprite { Player, OtherPlayer, Npc, GroundItems, ObjectEnabled, ObjectDisabled, Count };
struct TilesetAtlas { String id; std::uint32_t width = 0, height = 0; };
struct TilesetSprite {
    String id;
    std::uint32_t atlasIndex = 0;
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    // Half-pixel anchors preserve the exact center of odd-sized images.
    float anchorX = 0, anchorY = 0;
    SpriteLayer layer = SpriteLayer::Ground;
    float cellAnchorX = -1, cellAnchorY = -1; // Negative selects cell bottom center.
};
struct TilesetSourceImage {
    String path;
    std::uint32_t width = 0, height = 0, frameWidth = 0, frameHeight = 0;
    std::uint32_t firstSprite = 0, frameCount = 0;
};
struct VisualAlternative { TilesetMapping mapping; std::uint32_t weight = 1; };
enum class VisualGroupKind { Variants, Connections };
struct VisualGroup {
    String id;
    VisualGroupKind kind = VisualGroupKind::Variants;
    Vector<VisualAlternative> alternatives; // Variants, or exactly 16 connection masks.
    Vector<TerrainId> connectsTo;
    std::uint64_t totalWeight = 0;
};
struct AnimationFrame { std::uint32_t spriteIndex = 0; std::uint64_t endMilliseconds = 0; };
struct AnimationClip { String id; Vector<AnimationFrame> frames; std::uint64_t totalMilliseconds = 0; };
struct AnimationSample {
    std::uint32_t spriteIndex = 0;
    eastl::optional<std::uint64_t> delayMilliseconds;
    bool completed = false;
};
struct TilesetDefinition {
    String id, name;
    std::uint32_t cellWidth = 0, cellHeight = 0;
    Vector<TilesetAtlas> atlases;
    Vector<TilesetSprite> sprites;
    Vector<AnimationClip> animations;
    Vector<VisualGroup> visuals;
    Vector<TilesetSourceImage> sourceImages;
    Vector<TilesetMapping> terrain, items;
    Vector<TilesetMapping> terrainGround; // Optional static ground beneath upright terrain.
    TilesetMapping generic[static_cast<unsigned>(GenericSprite::Count)]{};
};
ContentResult<TilesetDefinition> LoadTilesetFile(const char* path, const ContentCatalog& content, Diagnostics* diagnostics = nullptr);
AnimationSample SampleAnimation(const AnimationClip& clip, std::uint64_t elapsedMilliseconds,
    AnimationPlayback playback = AnimationPlayback::Loop);
SpriteTransform ComposeSpriteTransforms(SpriteTransform inner, SpriteTransform outer);
TilesetMapping ResolveVisual(const TilesetDefinition& definition, TilesetMapping mapping,
    int worldX, int worldZ, int worldY, std::uint64_t entity = 0, std::uint8_t connections = 0);
}
