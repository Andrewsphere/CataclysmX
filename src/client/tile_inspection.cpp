#include "client/tile_inspection.h"
#include "client/localization.h"
#include "client/combat_text.h"
#include "combat/combat_system.h"

#include <cstdlib>

namespace cataclysmx {
namespace {
const char* ObjectName(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Chair: return "Chair";
    case ObjectKind::Table: return "Table";
    case ObjectKind::Lamp: return "Lamp";
    case ObjectKind::Generic: return "Object";
    case ObjectKind::None: return "";
    }
    return "Object";
}
void ItemLines(const World& world, ItemHandle handle, unsigned depth, Vector<String>& output) {
    const auto* item = world.GetItem(handle);
    if (!item) { return; }
    String line(depth * 2, ' ');
    line += world.GetContent().GetItem(item->type).name;
    line += " #"; line += UnsignedText(item->id);
    output.push_back(eastl::move(line));
    for (auto child : item->contents) { ItemLines(world, child, depth + 1, output); }
}
}
void DescribeTile(const World& world, const WorldView& view, TileCoord cursor, Vector<String>& output) {
    const int x = cursor.x - view.origin.x, z = cursor.z - view.origin.z;
    if (cursor.y != view.origin.y || x < 0 || z < 0 || x >= view.width || z >= view.height) {
        output.push_back("Outside current view"); return;
    }
    const auto& cell = view.cells[static_cast<std::size_t>(z * view.width + x)];
    if (cell.terrainVisibility != Visibility::Unknown) {
        const auto& terrain = world.GetContent().GetTerrain(cell.tile.terrain);
        String line = terrain.name;
        line += " (layer "; line += NumberText(view.origin.y - static_cast<int>(cell.terrainDepth)); line += ")";
        if (cell.terrainVisibility == Visibility::Remembered) { line += " [remembered]"; }
        output.push_back(eastl::move(line));
        if (cell.terrainVisibility == Visibility::Visible) {
            output.push_back(terrain.passable ? "Passable" : "Blocks movement");
            if (terrain.stairs) { output.push_back("Stairs"); }
            if (terrain.openSpace) { output.push_back("Open space"); }
            if (cell.tile.damage) { output.push_back("Terrain damage: " + UnsignedText(cell.tile.damage)); }
        }
    } else { output.push_back("Unknown terrain"); }
    if (cell.overlayVisibility == Visibility::Remembered) {
        if (cell.objectAppearance.kind != ObjectKind::None) { output.push_back(String(ObjectName(cell.objectAppearance.kind)) + " [remembered]"); }
        return;
    }
    if (cell.overlayVisibility != Visibility::Visible) { return; }
    const TileCoord position{cursor.x, cursor.z, view.origin.y - static_cast<int>(cell.overlayDepth)};
    const auto observer = world.FindActor(view.playerId);
    if (!world.CanPlayerSee(observer, position)) { return; }
    if (cell.actor) {
        const auto* actor = world.GetActor(world.FindActor(cell.actor));
        if (actor && actor->position == position) {
            String name = actor->player ? "Player #" : "Creature #";
            name += UnsignedText(actor->id); name += " | Speed "; name += UnsignedText(actor->speed);
            output.push_back(eastl::move(name));
            output.push_back(CombatStatusText(world.Combat().Describe(world.FindActor(cell.actor))));
        }
    }
    const auto objectHandle = world.ObjectAt(position);
    const auto* object = world.GetObject(objectHandle);
    if (object && cell.object == object->id) {
        output.push_back(object->type.value ? world.GetContent().GetObject(object->type).name : String(ObjectName(object->kind)));
        output.push_back(object->enabled ? "Enabled" : "Disabled");
        if (object->broken) { output.push_back("Broken"); }
        if (const auto state = world.DescribeObjectState(objectHandle); state && state->behavior != ObjectBehaviorKind::None) {
            String energy = "Energy "; energy += UnsignedText(state->energy); energy += "/"; energy += UnsignedText(state->energyCapacity);
            output.push_back(eastl::move(energy));
            String work = state->running ? "Running" : state->hasJob ? "Paused" : "Idle";
            if (state->hasJob) { work += " | Remaining "; work += UnsignedText(state->remainingWork); }
            output.push_back(eastl::move(work));
        }
        // Stored contents are exposed only within the existing access distance.
        const auto* player = world.GetActor(observer);
        if (player && world.CanUseObject(observer, objectHandle)) {
            for (auto item : object->contents) { ItemLines(world, item, 1, output); }
        }
    }
    for (auto item : world.ItemsAt(position)) { ItemLines(world, item, 0, output); }
}
}
