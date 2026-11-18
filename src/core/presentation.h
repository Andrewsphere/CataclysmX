#pragma once

#include "core/server.h"

namespace cataclysmx {
inline constexpr int maxPresentationRadius = 64;
inline constexpr std::size_t maxPresentationInteractions = 256;
inline constexpr std::size_t maxInspectionItems = 2048;
inline constexpr unsigned maxInspectionDepth = 64;
struct MapItemVisual {
    PersistentId item = 0;
    ItemTypeId type{};
    bool operator==(const MapItemVisual&) const = default;
};
struct PlayerPresentation {
    Vector<MapItemVisual> mapItems; // Exactly one per view cell, empty when occluded.
    Vector<InteractionChoice> interactions;
};
struct InspectionItem {
    PersistentId item = 0;
    ItemTypeId type{};
    unsigned depth = 0;
    bool stored = false;
};
struct TileInspection {
    PersistentId actor = 0;
    std::uint32_t speed = 0;
    CombatView combat;
    PersistentId object = 0;
    ObjectTypeId objectType{};
    ObjectKind objectKind = ObjectKind::None;
    bool enabled = false;
    bool broken = false;
    bool machine = false;
    std::uint64_t energy = 0;
    std::uint64_t energyCapacity = 0;
    bool hasJob = false;
    bool running = false;
    Tick remainingWork = 0;
    bool gridPowered = false;
    bool runRequested = false;
    Tick fundedTicks = 0;
    std::uint64_t generatorOutputRemaining = 0;
    Tick generatorIntervalStart = 0;
    bool generatorIntervalInitialized = false;
    Vector<InspectionItem> items; // Preorder, stored roots at depth 1; ground roots at 0.
};
enum class InspectionStatus : std::uint8_t { Complete, TooLarge, Unavailable, Disconnected };
struct PlayerInspection {
    std::uint64_t requestId = 0;
    TileCoord cursor{};
    InspectionStatus status = InspectionStatus::Complete;
    WorldView view;
    Vector<MapItemVisual> mapItems;
    TileInspection details;
};
Vector<MapItemVisual> CaptureMapItems(const World& world, const WorldView& view);
// Reuses output capacity; overwrites every cell, including cells that became hidden.
void CaptureMapItems(const World& world, const WorldView& view, Vector<MapItemVisual>& output);
// False indicates a budget overflow; callers must not publish partial output.
bool CapturePlayerPresentation(const World& world, const WorldView& view, PlayerPresentation& output);
bool CaptureTileInspection(const World& world, const WorldView& view, TileCoord cursor, TileInspection& output);
bool CapturePlayerInspection(const Server& server, PersistentId player, std::uint64_t requestId,
    TileCoord cursor, int radius, PlayerInspection& output);
}
