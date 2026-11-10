#include "sdl/map_packet.h"
#include <EASTL/utility.h>
#include <cstdlib>

namespace cataclysmx::sdl {
namespace {
TilesetMapping FreezeMemoryMapping(const TilesetDefinition& tileset, TilesetMapping mapping) {
    if (mapping.kind == TilesetMappingKind::Animation) {
        if (mapping.index >= tileset.animations.size() || tileset.animations[mapping.index].frames.empty()) { std::abort(); }
        mapping.kind = TilesetMappingKind::Sprite;
        mapping.index = tileset.animations[mapping.index].frames.front().spriteIndex;
    }
    mapping.activation.reset();
    return mapping;
}
}

MapPacket PrepareMapPacket(const WorldView& view, const ContentCatalog& content,
    const TilesetDefinition& tileset, MapIdentity identity, const Vector<MapItemVisual>& items) {
    MapPacket packet;
    packet.identity = identity;
    packet.width = view.width;
    packet.height = view.height;
    if (view.width < 0 || view.height < 0 || static_cast<std::size_t>(view.width) * static_cast<std::size_t>(view.height) != view.cells.size()) { std::abort(); }
    if (!items.empty() && items.size() != view.cells.size()) { std::abort(); }
    packet.cells.reserve(view.cells.size());
    for (const auto& cell : view.cells) {
        PreparedMapCell prepared;
        prepared.visible = cell.terrainVisibility != Visibility::Unknown || cell.overlayVisibility != Visibility::Unknown;
        if (!prepared.visible) { packet.cells.push_back(eastl::move(prepared)); continue; }
        prepared.terrainShading = MakePerceptionTransform(cell.terrainVisibility, cell.terrainLight, cell.terrainDepth);
        prepared.overlayShading = MakePerceptionTransform(cell.overlayVisibility, cell.overlayLight, cell.overlayDepth);
        ViewCell terrain = cell;
        terrain.actor = 0;
        terrain.ground_items = 0;
        terrain.object = 0;
        terrain.objectAppearance = {};
        terrain.overlayVisibility = Visibility::Unknown;
        prepared.terrain = CellAppearance(terrain, content);
        if (cell.terrainVisibility != Visibility::Unknown) {
            if (cell.tile.terrain.value < tileset.terrain.size()) { prepared.terrainSprite = tileset.terrain[cell.tile.terrain.value]; }
            if (cell.tile.terrain.value < tileset.terrainGround.size()) { prepared.groundSprite = tileset.terrainGround[cell.tile.terrain.value]; }
        }
        const bool liveOverlay = cell.overlayVisibility == Visibility::Visible;
        prepared.hasOverlay = (liveOverlay && (cell.actor != 0 || cell.ground_items != 0))
            || (cell.overlayVisibility != Visibility::Unknown && cell.objectAppearance.kind != ObjectKind::None);
        if (prepared.hasOverlay) {
            ViewCell display = cell;
            GenericSprite kind;
            if (liveOverlay && cell.actor != 0) {
                if (cell.actor == view.playerId) { display.player = true; kind = GenericSprite::Player; }
                else if (cell.player) { kind = GenericSprite::OtherPlayer; }
                else { kind = GenericSprite::Npc; }
            } else if (liveOverlay && cell.ground_items != 0) { kind = GenericSprite::GroundItems; }
            else { kind = cell.objectAppearance.active ? GenericSprite::ObjectEnabled : GenericSprite::ObjectDisabled; }
            prepared.overlay = CellAppearance(display, content);
            if (liveOverlay && cell.actor != 0 && cell.player && cell.actor != view.playerId) { prepared.overlay.symbol = "P"; }
            prepared.overlaySprite = tileset.generic[static_cast<unsigned>(kind)];
            if (liveOverlay && kind == GenericSprite::GroundItems && !items.empty()) {
                prepared.itemVisual = items[packet.cells.size()];
                const auto type = prepared.itemVisual.type.value;
                if (type < tileset.items.size() && tileset.items[type].kind != TilesetMappingKind::Missing) {
                    prepared.overlaySprite = tileset.items[type];
                }
            }
        }
        const int column = static_cast<int>(packet.cells.size() % view.width);
        const int row = static_cast<int>(packet.cells.size() / view.width);
        std::uint8_t connections = 0;
        if (prepared.terrainSprite.kind == TilesetMappingKind::Visual) {
            const auto& group = tileset.visuals[prepared.terrainSprite.index];
            if (group.kind == VisualGroupKind::Connections) {
                constexpr int dx[] = {0, 1, 0, -1}, dz[] = {-1, 0, 1, 0};
                for (unsigned direction = 0; direction < 4; ++direction) {
                    const int x = column + dx[direction], z = row + dz[direction];
                    if (x < 0 || z < 0 || x >= view.width || z >= view.height) { continue; }
                    const auto& neighbor = view.cells[static_cast<std::size_t>(z * view.width + x)];
                    if (neighbor.terrainVisibility == Visibility::Unknown || neighbor.terrainDepth != cell.terrainDepth) { continue; }
                    for (auto terrainId : group.connectsTo) {
                        if (neighbor.tile.terrain == terrainId) { connections |= static_cast<std::uint8_t>(1U << direction); break; }
                    }
                }
            }
        }
        prepared.terrainSprite = ResolveVisual(tileset, prepared.terrainSprite, view.origin.x + column,
            view.origin.z + row, view.origin.y - static_cast<int>(cell.terrainDepth), 0, connections);
        const auto entity = liveOverlay ? (cell.actor ? cell.actor : (prepared.itemVisual.item ? prepared.itemVisual.item : cell.object)) : 0;
        prepared.overlaySprite = ResolveVisual(tileset, prepared.overlaySprite, view.origin.x + column,
            view.origin.z + row, view.origin.y - static_cast<int>(cell.overlayDepth), entity);
        if (cell.terrainVisibility == Visibility::Remembered) {
            prepared.terrainSprite = FreezeMemoryMapping(tileset, prepared.terrainSprite);
            prepared.groundSprite = FreezeMemoryMapping(tileset, prepared.groundSprite);
        }
        if (cell.overlayVisibility == Visibility::Remembered) {
            prepared.overlaySprite = FreezeMemoryMapping(tileset, prepared.overlaySprite);
        }
        packet.cells.push_back(eastl::move(prepared));
    }
    return packet;
}
eastl::optional<std::uint64_t> NextAnimationDelay(const MapPacket& packet,
    const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds, const SpritePlaybackController* playback) {
    eastl::optional<std::uint64_t> result;
    const SpritePlaybackController ambient;
    const auto& controller = playback ? *playback : ambient;
    auto include = [&](PersistentId item, const TilesetMapping& mapping) {
        const auto delay = controller.Sample(item, mapping, tileset, elapsedMilliseconds).delayMilliseconds;
        if (delay && (!result || *delay < *result)) result = *delay;
    };
    for (const auto& cell : packet.cells) if (cell.visible) {
        include(0, cell.terrainSprite);
        if (cell.hasOverlay) { include(cell.itemVisual.item, cell.overlaySprite); }
    }
    return result;
}
MapPreparation::MapPreparation(const ContentCatalog& content, const TilesetDefinition& tileset,
    Wake wake, void* userdata) : content_(content), tileset_(tileset), wake_(wake), userdata_(userdata),
    worker_([this](std::stop_token stop) { Run(stop); }) {}
MapPreparation::~MapPreparation() {
    {
        std::lock_guard lock(mutex_);
        worker_.request_stop();
    }
    condition_.notify_all();
    worker_.join();
}
void MapPreparation::Submit(WorldView view, MapIdentity identity, Vector<MapItemVisual> items) {
    {
        std::lock_guard lock(mutex_);
        eastl::swap(view, pendingView_);
        eastl::swap(items, pendingItems_);
        pendingIdentity_ = identity;
        hasJob_ = true;
        ++generation_;
    }
    condition_.notify_one();
}
bool MapPreparation::Take(MapPacket& packet) {
    std::lock_guard lock(mutex_);
    if (!hasResult_) return false;
    eastl::swap(packet, pendingResult_);
    hasResult_ = false;
    return true;
}
void MapPreparation::Run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        WorldView view;
        Vector<MapItemVisual> items;
        MapIdentity identity;
        std::uint64_t generation;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return stop.stop_requested() || hasJob_; });
            if (stop.stop_requested()) return;
            eastl::swap(view, pendingView_);
            eastl::swap(items, pendingItems_);
            identity = pendingIdentity_;
            generation = generation_;
            hasJob_ = false;
        }
        MapPacket result = PrepareMapPacket(view, content_, tileset_, identity, items);
        bool published = false;
        {
            std::lock_guard lock(mutex_);
            if (!stop.stop_requested() && generation == generation_) {
                eastl::swap(result, pendingResult_);
                hasResult_ = true;
                published = true;
            }
        }
        if (published && wake_) wake_(userdata_);
    }
}
}
