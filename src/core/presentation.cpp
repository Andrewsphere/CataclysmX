#include "core/presentation.h"
#include "combat/combat_system.h"
#include <cstdlib>

namespace cataclysmx {
namespace {
bool CaptureItem(const World& world, ItemHandle handle, unsigned depth, bool stored, Vector<InspectionItem>& items) {
    const auto* item = world.GetItem(handle);
    if (!item) return true;
    if (items.size() >= maxInspectionItems || depth > maxInspectionDepth) return false;
    items.push_back({item->id, item->type, depth, stored});
    for (auto child : item->contents)
        if (!CaptureItem(world, child, depth + 1, stored, items)) return false;
    return true;
}
}
Vector<MapItemVisual> CaptureMapItems(const World& world, const WorldView& view) {
    Vector<MapItemVisual> result;
    CaptureMapItems(world, view, result);
    return result;
}
void CaptureMapItems(const World& world, const WorldView& view, Vector<MapItemVisual>& result) {
    if (view.width < 0 || view.height < 0 ||
        static_cast<std::size_t>(view.width) * view.height != view.cells.size()) std::abort();
    result.clear(); result.resize(view.cells.size());
    const auto observer = world.FindActor(view.playerId);
    for (std::size_t i = 0; i < view.cells.size(); ++i) {
        const auto& cell = view.cells[i];
        if (cell.overlayVisibility != Visibility::Visible || cell.actor || !cell.ground_items) continue;
        const TileCoord position{view.origin.x + static_cast<int>(i % view.width),
            view.origin.z + static_cast<int>(i / view.width), view.origin.y - static_cast<int>(cell.overlayDepth)};
        if (!world.CanPlayerSee(observer, position)) continue;
        for (auto handle : world.ItemsAt(position)) {
            const auto* item = world.GetItem(handle);
            if (item && (!result[i].item || item->id < result[i].item)) result[i] = {item->id, item->type};
        }
    }
}
bool CapturePlayerPresentation(const World& world, const WorldView& view, PlayerPresentation& output) {
    output.interactions.clear();
    CaptureMapItems(world, view, output.mapItems);
    const auto observer = world.FindActor(view.playerId);
    const auto p = view.player_position;
    constexpr TileCoord offsets[] = {{0,0}, {-1,0}, {1,0}, {0,-1}, {0,1}, {-1,-1}, {1,-1}, {-1,1}, {1,1}};
    for (const auto offset : offsets) {
        QueryInteractions(world, observer, {p.x + offset.x, p.z + offset.z, p.y}, output.interactions);
        if (output.interactions.size() > maxPresentationInteractions) {
            output.mapItems.clear(); output.interactions.clear(); return false;
        }
    }
    return true;
}
bool CaptureTileInspection(const World& world, const WorldView& view, TileCoord cursor, TileInspection& output) {
    output = {};
    const auto x = static_cast<std::int64_t>(cursor.x) - view.origin.x;
    const auto z = static_cast<std::int64_t>(cursor.z) - view.origin.z;
    if (cursor.y != view.origin.y || x < 0 || z < 0 || x >= view.width || z >= view.height) return true;
    const auto& cell = view.cells[static_cast<std::size_t>(z * view.width + x)];
    if (cell.overlayVisibility != Visibility::Visible) return true;
    const TileCoord position{cursor.x, cursor.z, view.origin.y - static_cast<int>(cell.overlayDepth)};
    const auto observer = world.FindActor(view.playerId);
    if (!world.CanPlayerSee(observer, position)) return true;
    const auto actorHandle = world.FindActor(cell.actor);
    const auto* actor = world.GetActor(actorHandle);
    if (actor && actor->position == position) {
        output.actor = actor->id; output.speed = actor->speed;
        output.combat = world.Combat().Describe(actorHandle);
        // Inspection uses status only; do not disclose another actor's combat target/history.
        output.combat.feedback = {};
    }
    const auto objectHandle = world.ObjectAt(position);
    const auto* object = world.GetObject(objectHandle);
    if (object && cell.object == object->id) {
        output.object = object->id; output.objectType = object->type;
        output.objectKind = object->kind;
        output.enabled = object->enabled; output.broken = object->broken;
        if (const auto state = world.DescribeObjectState(objectHandle); state && state->behavior != ObjectBehaviorKind::None) {
            output.machine = true; output.energy = state->energy; output.energyCapacity = state->energyCapacity;
            output.hasJob = state->hasJob; output.running = state->running; output.remainingWork = state->remainingWork;
            output.gridPowered = state->gridPowered; output.runRequested = state->runRequested;
            output.fundedTicks = state->fundedTicks; output.generatorOutputRemaining = state->generatorOutputRemaining;
            output.generatorIntervalStart = state->generatorIntervalStart;
            output.generatorIntervalInitialized = state->generatorIntervalInitialized;
        }
        if (world.GetActor(observer) && world.CanUseObject(observer, objectHandle))
            for (auto item : object->contents)
                if (!CaptureItem(world, item, 1, true, output.items)) { output = {}; return false; }
    }
    for (auto item : world.ItemsAt(position))
        if (!CaptureItem(world, item, 0, false, output.items)) { output = {}; return false; }
    return true;
}
bool CapturePlayerInspection(const Server& server, PersistentId player, std::uint64_t requestId,
    TileCoord cursor, int radius, PlayerInspection& output) {
    output = {}; output.requestId = requestId; output.cursor = cursor;
    if (!requestId || !ValidPosition(cursor) || radius < 0 || radius > maxPresentationRadius) {
        output.status = InspectionStatus::Unavailable; return false;
    }
    output.view = server.ViewForPlayerAtLayer(player, radius, cursor.y);
    output.view.enemyHealth.clear();
    const auto x = static_cast<std::int64_t>(cursor.x) - output.view.origin.x;
    const auto z = static_cast<std::int64_t>(cursor.z) - output.view.origin.z;
    if (x < 0 || z < 0 || x >= output.view.width || z >= output.view.height) {
        output.view = {}; output.status = InspectionStatus::Unavailable; return false;
    }
    output.mapItems = CaptureMapItems(server.GetWorld(), output.view);
    if (!CaptureTileInspection(server.GetWorld(), output.view, cursor, output.details)) {
        output.view = {}; output.mapItems.clear(); output.status = InspectionStatus::TooLarge; return false;
    }
    return true;
}
}
