#include "client/power_view.h"
#include "core/ratio.h"
#include "power/power_system.h"
#include <EASTL/algorithm.h>

namespace cataclysmx {

    const char* StationAvailabilityText(const StationView& station, const ObjectDefinition& definition) {
        if (station.broken) return "broken";
        if (!station.enabled) return "disabled";
        if (station.worker) return "in use";
        return definition.craftingEnergyPerWork ? "idle" : "available";
    }

    String StationPowerText(const WorldView& view, const StationView& station, const ObjectDefinition& definition) {
        if (!definition.craftingEnergyPerWork) return {};
        String text = "Power: " + UnsignedText(definition.craftingEnergyPerWork) + "/" + UnsignedText(ratioScale)
            + " energy/base work | grid at station";
        if (!view.currentPower) return text + " | station grid: meter required";
        const auto x = std::int64_t{station.position.x} - view.origin.x;
        const auto z = std::int64_t{station.position.z} - view.origin.z;
        if (station.position.y != view.origin.y || x < 0 || z < 0 || x >= view.width || z >= view.height) {
            return text + " | station grid: outside view";
        }
        const auto index = static_cast<std::size_t>(z * view.width + x);
        if (index >= view.cells.size()) return text + " | station grid: outside view";
        const auto grid = view.cells[index].powerGridId;
        return text + " | station grid: " + (grid ? UnsignedText(grid) : String("unwired"));
    }

    void PowerWorkshop::Open(const WorldView& view) {
        cursor_ = view.player_position; player_ = view.playerId;
        width_ = view.powerSettings.maxWiringWidth; depth_ = view.powerSettings.maxWiringDepth;
        kit_ = 0; equipment_ = 0; fuel_ = 0; placing_ = false;
    }
    eastl::optional<Command> PowerWorkshop::Handle(const KeyChord& key, const WorldView& view, const ContentCatalog& content) {
        if (player_ != view.playerId) { Open(view); return {}; }
        Refresh(view, content);
        if (key.key == "Left") --cursor_.x;
        else if (key.key == "Right") ++cursor_.x;
        else if (key.key == "Up") --cursor_.z;
        else if (key.key == "Down") ++cursor_.z;
        else if (key.key == "w") width_ = width_ % view.powerSettings.maxWiringWidth + 1;
        else if (key.key == "W") width_ = (width_ + view.powerSettings.maxWiringWidth - 2) % view.powerSettings.maxWiringWidth + 1;
        else if (key.key == "d") depth_ = depth_ % view.powerSettings.maxWiringDepth + 1;
        else if (key.key == "D") depth_ = (depth_ + view.powerSettings.maxWiringDepth - 2) % view.powerSettings.maxWiringDepth + 1;
        else if (key.key == "Tab") Cycle(kits_, kit_);
        else if (key.key == "g") Cycle(equipmentItems_, equipment_);
        else if (key.key == "f") Cycle(fuelItems_, fuel_);
        else if (key.key == "l" || key.key == "i" || key.key == "x" || key.key == "c") {
            for (const auto& station : view.stations) {
                const auto* definition = content.TryGetObject(station.type);
                if (station.position != cursor_ || !definition || definition->behavior != ObjectBehaviorKind::Forge) continue;
                Command command; command.target = station.id;
                command.kind = key.key == "l" ? CommandKind::load_forge : key.key == "i" ? CommandKind::ignite_forge
                    : key.key == "x" ? CommandKind::extinguish_forge : CommandKind::discard_forge_fuel;
                if (command.kind == CommandKind::load_forge) {
                    if (!fuel_ || eastl::find(fuelItems_.begin(), fuelItems_.end(), fuel_) == fuelItems_.end()) return {};
                    command.item = fuel_;
                }
                return command;
            }
        }
        else if (key.key == "p") placing_ = !placing_;
        else if (key.key == "Home") cursor_ = view.player_position;
        else if (key.key == "r") {
            for (const auto& station : view.stations) {
                if (station.position != cursor_ || station.packing != StationActionOutcome::Packed) continue;
                Command command; command.kind = CommandKind::pack_station; command.target = station.id;
                return command;
            }
        }
        else if (key.key == "Enter") {
            Command command;
            if (placing_) {
                if (!equipment_ || eastl::find(equipmentItems_.begin(), equipmentItems_.end(), equipment_) == equipmentItems_.end()) return {};
                command.kind = CommandKind::place_power_object; command.item = equipment_; command.otherItem = kit_;
                command.position = cursor_;
                for (const auto& item : view.inventory) {
                    if (item.id != equipment_) continue;
                    const auto* station = content.TryGetObject(content.GetItem(item.type).placedObject);
                    if (station && station->packingItem.value) {
                        command.kind = CommandKind::place_station; command.otherItem = 0;
                    }
                    break;
                }
            } else {
                if (!kit_ || eastl::find(kits_.begin(), kits_.end(), kit_) == kits_.end()) return {};
                command.kind = CommandKind::wire_grid; command.item = kit_;
                command.powerBounds = {cursor_, {cursor_.x + width_ - 1, cursor_.z + depth_ - 1, cursor_.y}};
            }
            return command;
        }
        // The authority checks reach and current settings again at execution.
        ClampCursor(view);
        return {};
    }
    void PowerWorkshop::Describe(const WorldView& view, const ContentCatalog& content, Vector<String>& lines) {
        Refresh(view, content); lines.clear();
        lines.push_back("Power workshop | Enter: install | p: wire/place | r: pack station at cursor | Escape: back");
        lines.push_back("Arrows: region origin / destination | Home: player | w/W: width +/- | d/D: depth +/-");
        lines.push_back("Tab: reusable wiring kit | g: carried equipment/station | PgUp/PgDn: scroll");
        lines.push_back("Forge at cursor: f: cycle carried fuel | l: load whole selected item | i: ignite | x: extinguish | c: discard cold fuel");
        lines.push_back(String(placing_ ? "Place at " : "Wire inclusive rectangle from ") + NumberText(cursor_.x) + "," + NumberText(cursor_.z)
            + " Y=" + NumberText(view.player_position.y) + " | " + NumberText(width_) + " x " + NumberText(depth_));
        lines.push_back("Kit: " + Name(view, content, kit_) + " | Equipment: " + Name(view, content, equipment_));
        lines.push_back("Selected forge fuel: " + Name(view, content, fuel_));
        for (const auto& item : view.inventory) {
            if (item.id != kit_) continue;
            const auto& kit = content.GetItem(item.type);
            if (!kit.wiringMaterial.value) break;
            lines.push_back("Material: " + content.GetItem(kit.wiringMaterial).name + " | " + UnsignedText(kit.wiringTilesPerMaterial)
                + " tiles per item; consume ceil(new tiles / tiles per item). Kit is reusable.");
            lines.push_back("Battery installation: 4 tile equivalents = " + UnsignedText((4u + kit.wiringTilesPerMaterial - 1) / kit.wiringTilesPerMaterial)
                + " material item(s). Materials must be accessible ordinary leaf items.");
            break;
        }
        for (const auto& station : view.stations) {
            const auto* definition = content.TryGetObject(station.type);
            if (!definition) continue;
            String line = definition->name + " #" + UnsignedText(station.id) + " at "
                + NumberText(station.position.x) + "," + NumberText(station.position.z);
            line += String(" [") + StationAvailabilityText(station, *definition) + "]";
            if (station.worker) line += " worker #" + UnsignedText(station.worker);
            if (definition->craftingEnergyPerWork) line += " | " + StationPowerText(view, station, *definition);
            if (definition->behavior == ObjectBehaviorKind::Forge) {
                line += String(" | ") + (station.forge.ready ? "Ready" : "Cold") + " | fuel "
                    + UnsignedText(station.forge.fuel) + "/" + UnsignedText(definition->forge.fuelCapacity)
                    + " | ignition " + UnsignedText(definition->forge.ignitionCost);
                if (station.position == cursor_) {
                    for (const auto& fuel : definition->forge.fuels) {
                        lines.push_back("Accepted fuel: " + content.GetItem(fuel.item).name + " | whole-item heat " + UnsignedText(fuel.heat));
                    }
                }
            }
            for (const auto& trait : definition->toolTraits) {
                line += " | " + content.GetToolTrait(trait.trait).name + " " + UnsignedText(trait.tier);
            }
            line += station.packing == StationActionOutcome::Packed ? " | can pack" : String(" | ") + StationActionMessage(station.packing);
            lines.push_back(eastl::move(line));
        }
        if (!view.currentPower) { lines.push_back("Carry a grid multimeter to inspect power and coverage."); return; }
        const auto& p = *view.currentPower;
        lines.push_back("Worker's current grid: " + UnsignedText(p.grid ? p.grid->value : 0) + " | Mains: " + String(p.mainsAvailable ? "available" : "unavailable"));
        lines.push_back("Battery energy " + UnsignedText(p.batteryEnergy) + "/" + UnsignedText(p.batteryCapacity)
            + " | Generator output " + UnsignedText(p.generatorOutputAvailable));
        lines.push_back("Batteries " + NumberText(p.batteries) + " | Running generators " + NumberText(p.runningGenerators)
            + " | Machines running/waiting " + NumberText(p.runningMachines) + "/" + NumberText(p.waitingMachines)
            + " | Unloaded devices " + NumberText(p.unloadedDevices));
        lines.push_back("Coverage on view layer: @ player, . unwired, 0-9/A-Z grid ID modulo 36; exact IDs below");
        Vector<std::uint64_t> ids;
        constexpr char glyphs[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (int z = 0; z < view.height; ++z) {
            String row;
            for (int x = 0; x < view.width; ++x) {
                const auto id = view.cells[static_cast<std::size_t>(z * view.width + x)].powerGridId;
                row += view.origin.x + x == view.player_position.x && view.origin.z + z == view.player_position.z ? '@' : id ? glyphs[id % 36] : '.';
                if (id && eastl::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            }
            lines.push_back(eastl::move(row));
        }
        for (auto id : ids) lines.push_back(String(1, glyphs[id % 36]) + " = grid " + UnsignedText(id));
    }
    void PowerWorkshop::Cycle(const Vector<PersistentId>& items, PersistentId& selected) {
        const auto found = eastl::find(items.begin(), items.end(), selected);
        selected = items.empty() ? 0 : found == items.end() || found + 1 == items.end() ? items.front() : *(found + 1);
    }
    void PowerWorkshop::Refresh(const WorldView& view, const ContentCatalog& content) {
        kits_.clear(); equipmentItems_.clear(); fuelItems_.clear();
        for (const auto& item : view.inventory) {
            if (item.installation.value) continue;
            const auto& definition = content.GetItem(item.type);
            if (definition.wiringMaterial.value) kits_.push_back(item.id);
            if (definition.placedObject.value) equipmentItems_.push_back(item.id);
            if (item.contained_items) continue;
            bool installedAncestor = false;
            PersistentId parent = item.parent;
            while (parent) {
                const auto ancestor = eastl::find_if(view.inventory.begin(), view.inventory.end(),
                    [parent](const auto& entry) { return entry.id == parent; });
                if (ancestor == view.inventory.end()) break;
                if (ancestor->installation.value) { installedAncestor = true; break; }
                parent = ancestor->parent;
            }
            if (installedAncestor) continue;
            for (const auto& station : view.stations) {
                const auto* forge = content.TryGetObject(station.type);
                if (station.position != cursor_ || !forge || forge->behavior != ObjectBehaviorKind::Forge) continue;
                for (const auto& fuel : forge->forge.fuels) {
                    if (fuel.item == item.type) { fuelItems_.push_back(item.id); break; }
                }
                break;
            }
        }
        // Preserve a vanished selection until the user explicitly cycles it;
        // refreshing a live LAN view must not retarget an Enter press.
        if (!kit_ && !kits_.empty()) kit_ = kits_.front();
        if (!equipment_ && !equipmentItems_.empty()) equipment_ = equipmentItems_.front();
        if (!fuel_ && !fuelItems_.empty()) fuel_ = fuelItems_.front();
        width_ = eastl::clamp(width_, 1, view.powerSettings.maxWiringWidth);
        depth_ = eastl::clamp(depth_, 1, view.powerSettings.maxWiringDepth);
        ClampCursor(view);
    }
    void PowerWorkshop::ClampCursor(const WorldView& view) {
        const int reachX = placing_ ? 1 : view.powerSettings.maxWiringWidth;
        const int reachZ = placing_ ? 1 : view.powerSettings.maxWiringDepth;
        const int width = placing_ ? 1 : width_, depth = placing_ ? 1 : depth_;
        cursor_.x = eastl::clamp(cursor_.x, eastl::max(-world_coordinate_limit, view.player_position.x - reachX),
            eastl::min(world_coordinate_limit, view.player_position.x + reachX) - width + 1);
        cursor_.z = eastl::clamp(cursor_.z, eastl::max(-world_coordinate_limit, view.player_position.z - reachZ),
            eastl::min(world_coordinate_limit, view.player_position.z + reachZ) - depth + 1);
        cursor_.y = view.player_position.y;
    }
    String PowerWorkshop::Name(const WorldView& view, const ContentCatalog& content, PersistentId id) {
        for (const auto& item : view.inventory) if (item.id == id) return content.GetItem(item.type).name + " #" + UnsignedText(id);
        return "none";
    }
void CapturePowerDebug(const World& world, PowerDebugSnapshot& output) {
    world.Power().CollectDebugGrids(output.grids);
    world.CollectChunkResidencyDebug(output.columns, output.residency);
    output.workload = world.Power().Counters();
}

void DescribePowerDebug(const PowerDebugSnapshot& snapshot, TileCoord center, Vector<String>& lines) {
    lines.clear();
    lines.push_back("DEBUG power grids and chunk residency | p: refresh | Escape: close");
    const auto& c = snapshot.residency;
    lines.push_back("loadedColumns=" + NumberText(c.loadedColumns) + " activeColumns=" + NumberText(c.activeColumns)
        + " playerColumns=" + NumberText(c.playerColumns) + " machineColumns=" + NumberText(c.machineColumns));
    lines.push_back("machineOnlyColumns=" + NumberText(c.machineOnlyColumns) + " sharedColumns=" + NumberText(c.sharedColumns)
        + " loadedChunks=" + NumberText(c.loadedChunks) + " archivedChunks=" + NumberText(c.archivedChunks));
    const auto& w = snapshot.workload;
    lines.push_back("eventPasses=" + NumberText(w.eventPasses) + " sourceScans=" + NumberText(w.sourceScans)
        + " paymentAttempts=" + NumberText(w.paymentAttempts));
    lines.push_back("fundedMachineSlices=" + NumberText(w.fundedMachineSlices) + " failedMachineSlices=" + NumberText(w.failedMachineSlices)
        + " chargedEnergy=" + NumberText(w.chargedEnergy));
    lines.push_back("Chunk map (17x17 centered on player): P player, M machine, B both, O other; uppercase active / lowercase resident; . unloaded");
    const auto column = ChunkOf(center);
    for (int z = -8; z <= 8; ++z) {
        String row;
        for (int x = -8; x <= 8; ++x) {
            char glyph = '.';
            for (const auto& entry : snapshot.columns) {
                if (entry.column.x != column.x + x || entry.column.z != column.z + z) continue;
                const bool player = entry.playerActiveRequests || entry.playerResidentRequests;
                const bool machine = entry.machineOwners || entry.machineDependencies;
                glyph = player && machine ? 'B' : player ? 'P' : machine ? 'M' : 'O';
                if (!entry.active) glyph += 'a' - 'A';
                break;
            }
            row += glyph;
        }
        lines.push_back(eastl::move(row));
    }
    lines.push_back("Machine-only loaded columns (including outside the map):");
    bool machineOnly = false;
    for (const auto& entry : snapshot.columns) {
        if (entry.playerActiveRequests || entry.playerResidentRequests || (!entry.machineOwners && !entry.machineDependencies)) continue;
        machineOnly = true;
        lines.push_back("Column " + NumberText(entry.column.x) + "," + NumberText(entry.column.z)
            + (entry.active ? " active" : " resident") + " | machineOwners=" + UnsignedText(entry.machineOwners)
            + " machineDependencies=" + UnsignedText(entry.machineDependencies)
            + " otherActiveRequests=" + UnsignedText(entry.otherActiveRequests)
            + " otherResidentRequests=" + UnsignedText(entry.otherResidentRequests));
    }
    if (!machineOnly) lines.push_back("None.");
    lines.push_back("Power coverage (33x17 tiles on player layer): . unwired; 0-9/A-Z ID modulo 36");
    constexpr char glyphs[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int z = -8; z <= 8; ++z) {
        String row;
        for (int x = -16; x <= 16; ++x) {
            char glyph = '.';
            const TileCoord tile{center.x + x, center.z + z, center.y};
            for (const auto& grid : snapshot.grids) {
                for (const auto& region : grid.regions) if (PowerContains(region, tile)) { glyph = glyphs[grid.id.value % 36]; break; }
                if (glyph != '.') break;
            }
            row += glyph;
        }
        lines.push_back(eastl::move(row));
    }
    for (const auto& grid : snapshot.grids) {
        lines.push_back(String(1, glyphs[grid.id.value % 36]) + " = grid " + UnsignedText(grid.id.value)
            + " regions=" + NumberText(grid.regions.size()) + " devices=" + NumberText(grid.registeredDevices)
            + " holds=" + NumberText(grid.workshopHolds) + " cutoff(DEBUG)=" + NumberText(grid.mainsCutoffTick));
    }
}
}
