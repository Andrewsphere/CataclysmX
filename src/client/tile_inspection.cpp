#include "client/tile_inspection.h"
#include "client/localization.h"
#include "client/combat_text.h"

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
void ItemLine(const ContentCatalog& content, const InspectionItem& item, Vector<String>& output) {
    String line(item.depth * 2, ' ');
    line += content.GetItem(item.type).name;
    line += " #"; line += UnsignedText(item.item);
    output.push_back(eastl::move(line));
}
}
eastl::string_view VisibleActorName(const ContentCatalog& content, const ViewCell& cell) {
    if (cell.overlayVisibility != Visibility::Visible || cell.actor == 0) { return {}; }
    if (cell.player) { return "Player"; }
    if (const auto* definition = content.TryGetMonster(cell.monsterType)) {
        return {definition->name.data(), definition->name.size()};
    }
    return "Creature";
}
void DescribeTile(const World& world, const WorldView& view, TileCoord cursor, Vector<String>& output) {
    TileInspection details;
    if (!CaptureTileInspection(world, view, cursor, details)) { output.push_back("Inspection exceeds presentation limit"); return; }
    DescribeTile(world.GetContent(), view, cursor, details, output);
}
void DescribeTile(const ContentCatalog& content, const WorldView& view, TileCoord cursor,
    const TileInspection& details, Vector<String>& output, eastl::optional<ExaminedEnemy>* enemy) {
    if (enemy) { enemy->reset(); }
    const auto x = static_cast<std::int64_t>(cursor.x) - view.origin.x;
    const auto z = static_cast<std::int64_t>(cursor.z) - view.origin.z;
    if (cursor.y != view.origin.y || x < 0 || z < 0 || x >= view.width || z >= view.height) {
        output.push_back("Outside current view"); return;
    }
    const auto& cell = view.cells[static_cast<std::size_t>(z * view.width + x)];
    if (cell.terrainVisibility != Visibility::Unknown) {
        const auto& terrain = content.GetTerrain(cell.tile.terrain);
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
        if (cell.objectAppearance.kind != ObjectKind::None) {
            output.push_back({});
            output.push_back(String(ObjectName(cell.objectAppearance.kind)) + " [remembered]");
        }
        return;
    }
    if (cell.overlayVisibility != Visibility::Visible) { return; }
    if (details.actor && details.actor == cell.actor) {
        const auto typeName = VisibleActorName(content, cell);
        String name(typeName.data(), typeName.size());
        String status = "#" + UnsignedText(details.actor) + " | Speed " + UnsignedText(details.speed);
        const auto* monster = enemy && !cell.player ? content.TryGetMonster(cell.monsterType) : nullptr;
        // The demo's legacy enemy has no catalog identity in the filtered view.
        if (enemy && !cell.player && (!cell.monsterType.value || (monster && Hostile(CombatFaction::Survivors, monster->faction)))) {
            const auto combatStatus = CombatStatusText(details.combat, false);
            if (!combatStatus.empty()) { status += '\n'; status += combatStatus; }
            enemy->emplace(ExaminedEnemy{eastl::move(name), eastl::move(status), details.combat});
        } else {
            name += ' ';
            name += status;
            output.push_back({});
            output.push_back(eastl::move(name));
            output.push_back(CombatStatusText(details.combat));
        }
    }
    if (details.object && cell.object == details.object) {
        output.push_back({});
        output.push_back(details.objectType.value ? content.GetObject(details.objectType).name : String(ObjectName(details.objectKind)));
        output.push_back(details.enabled ? "Enabled" : "Disabled");
        if (details.broken) { output.push_back("Broken"); }
        if (details.machine) {
            String energy = "Energy "; energy += UnsignedText(details.energy); energy += "/"; energy += UnsignedText(details.energyCapacity);
            output.push_back(eastl::move(energy));
            String work = details.running ? "Running" : details.hasJob ? "Paused" : "Idle";
            if (details.hasJob) { work += " | Remaining "; work += UnsignedText(details.remainingWork); }
            output.push_back(eastl::move(work));
        }
        for (const auto& item : details.items) if (item.stored) ItemLine(content, item, output);
    }
    bool firstGroundItem = true;
    for (const auto& item : details.items) {
        if (item.stored) continue;
        if (firstGroundItem) { output.push_back({}); firstGroundItem = false; }
        ItemLine(content, item, output);
    }
}
}
