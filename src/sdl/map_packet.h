#pragma once

#include "client/text.h"
#include "content/tileset.h"
#include "sdl/item_visual.h"
#include "sdl/animation.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cataclysmx::sdl {
struct MapIdentity {
    std::uint64_t session = 0, viewRevision = 0, assetGeneration = 0, viewportGeneration = 0;
    bool operator==(const MapIdentity&) const = default;
};
struct PreparedMapCell {
    bool visible = false, hasOverlay = false;
    TerminalAppearance terrain, overlay;
    TilesetMapping terrainSprite, overlaySprite;
    TilesetMapping groundSprite;
    MapItemVisual itemVisual;
};
struct MapPacket {
    MapIdentity identity;
    int width = 0, height = 0;
    Vector<PreparedMapCell> cells;
};
MapPacket PrepareMapPacket(const WorldView& view, const ContentCatalog& content,
    const TilesetDefinition& tileset, MapIdentity identity, const Vector<MapItemVisual>& items = {});
eastl::optional<std::uint64_t> NextAnimationDelay(const MapPacket& packet,
    const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds,
    const SpritePlaybackController* playback = nullptr);

// Catalog and tileset must remain immutable and alive until this owner is destroyed.
class MapPreparation {
public:
    using Wake = void (*)(void*);
    MapPreparation(const ContentCatalog& content, const TilesetDefinition& tileset,
                   Wake wake = nullptr, void* userdata = nullptr);
    ~MapPreparation();
    MapPreparation(const MapPreparation&) = delete;
    MapPreparation& operator=(const MapPreparation&) = delete;
    void Submit(WorldView view, MapIdentity identity, Vector<MapItemVisual> items = {});
    bool Take(MapPacket& packet);
private:
    void Run(std::stop_token stop);
    const ContentCatalog& content_;
    const TilesetDefinition& tileset_;
    Wake wake_;
    void* userdata_;
    std::mutex mutex_;
    std::condition_variable condition_;
    WorldView pendingView_;
    Vector<MapItemVisual> pendingItems_;
    MapIdentity pendingIdentity_;
    bool hasJob_ = false;
    MapPacket pendingResult_;
    bool hasResult_ = false;
    std::uint64_t generation_ = 0;
    std::jthread worker_;
};
}
