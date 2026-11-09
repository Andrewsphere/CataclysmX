#include "network/wire.h"
#include "items/item_behavior.h"

#include <EASTL/utility.h>

namespace cataclysmx::network::wire {
namespace {
// Semantic object kinds are fixed protocol constants, never content indices.
static_assert(static_cast<unsigned>(ObjectKind::None) == 0 && static_cast<unsigned>(ObjectKind::Generic) == 1 &&
    static_cast<unsigned>(ObjectKind::Chair) == 2 && static_cast<unsigned>(ObjectKind::Table) == 3 &&
    static_cast<unsigned>(ObjectKind::Lamp) == 4);
bool ValidViewBounds(const WorldView& view) {
    return view.width >= 1 && view.width <= maxViewDimension && view.height == view.width && (view.width & 1) != 0 &&
        ValidPosition(view.player_position) && view.origin.y == view.player_position.y &&
        static_cast<std::int64_t>(view.origin.x) + view.width / 2 == view.player_position.x &&
        static_cast<std::int64_t>(view.origin.z) + view.height / 2 == view.player_position.z &&
        static_cast<std::int64_t>(view.origin.x) + view.width - 1 <= INT32_MAX &&
        static_cast<std::int64_t>(view.origin.z) + view.height - 1 <= INT32_MAX;
}
bool ValidCell(const ViewCell& cell, const WorldView& view, std::size_t index) {
    if (cell.terrainVisibility > Visibility::Visible || cell.overlayVisibility > Visibility::Visible ||
        cell.terrainLight > LightBand::Bright || cell.overlayLight > LightBand::Bright ||
        !ValidObjectAppearance(cell.objectAppearance)) return false;
    const bool terrain = cell.terrainVisibility != Visibility::Unknown;
    const bool overlay = cell.overlayVisibility != Visibility::Unknown;
    const bool object = cell.objectAppearance.kind != ObjectKind::None;
    if (!terrain && (cell.tile != Tile{} || cell.terrainDepth || cell.terrainLight != LightBand::Dark)) return false;
    if (cell.terrainVisibility == Visibility::Remembered &&
        (cell.tile.damage || cell.terrainLight != LightBand::Dark)) return false;
    if (!overlay && (cell.actor || cell.player || cell.ground_items || cell.object ||
        cell.objectAppearance != ObjectAppearance{} || cell.overlayDepth || cell.overlayLight != LightBand::Dark)) return false;
    if (overlay && !(cell.actor || cell.ground_items || object)) return false;
    if (cell.overlayVisibility == Visibility::Remembered &&
        (cell.actor || cell.player || cell.ground_items || cell.object || !object || cell.overlayLight != LightBand::Dark)) return false;
    if (cell.overlayVisibility == Visibility::Visible && ((cell.object != 0) != object)) return false;
    if (!terrain && !overlay) return true;
    const auto maxDepth = static_cast<unsigned>(view.player_position.y - world_min_y);
    return (!terrain || cell.tile.terrain.value != 0) && cell.terrainDepth < world_layer_count &&
        cell.overlayDepth < world_layer_count && cell.terrainDepth <= maxDepth && cell.overlayDepth <= maxDepth &&
        (overlay || cell.overlayDepth == 0) && (!cell.player || cell.actor != 0) &&
        ValidPosition({view.origin.x + static_cast<int>(index % view.width),
            view.origin.z + static_cast<int>(index / view.width), view.origin.y});
}
bool ValidCapability(ItemCapabilityId id) {
    return id.value != 0 && id.value <= maxItemCapabilities;
}
bool WriteText(Writer& writer, const ItemText& text) {
    return writer.Text(text.key, maxItemText) && writer.Text(text.fallback, maxItemText);
}
bool ReadText(Reader& reader, ItemText& text) {
    return reader.Text(text.key, maxItemText) && reader.Text(text.fallback, maxItemText);
}
bool WriteReason(Writer& writer, const ItemUseReason& reason) {
    return writer.Text(reason.code, maxItemActionName) && WriteText(writer, reason.message);
}
bool ReadReason(Reader& reader, ItemUseReason& reason) {
    return reader.Text(reason.code, maxItemActionName) && ReadText(reader, reason.message);
}
bool ValidAction(const ItemActionInfo& action) {
    return ValidCapability(action.capability) && action.baseCost && ValidItemActionId(action.action) &&
        action.argument <= ItemActionArgument::Direction && action.category <= ItemActionCategory::ManageContents &&
        ValidItemText(action.label) && ValidItemUseReason(action.availability, action.reason);
}
const InventoryEntry* FindEntry(const Vector<InventoryEntry>& entries, PersistentId id) {
    std::size_t begin = 0, end = entries.size();
    while (begin < end) {
        const auto middle = begin + (end - begin) / 2;
        if (entries[middle].id < id) begin = middle + 1;
        else end = middle;
    }
    return begin < entries.size() && entries[begin].id == id ? &entries[begin] : nullptr;
}
bool ValidEntries(const Vector<InventoryEntry>& entries) {
    if (entries.size() > maxItemEntries) return false;
    PersistentId last = 0;
    for (const auto& entry : entries) {
        if (entry.id <= last || !entry.type.value || entry.contained_items > maxItemEntries ||
            entry.installation.value > maxItemCapabilities || (!entry.parent && entry.installation.value) ||
            entry.usedVolumeMl > entry.capacityVolumeMl || entry.energy.size() > maxItemCapabilities ||
            entry.activities.size() > maxItemCapabilities || entry.actions.size() > maxItemActions) return false;
        last = entry.id;
        std::uint16_t lastCapability = 0;
        for (const auto& energy : entry.energy) {
            if (!ValidCapability(energy.capability) || energy.capability.value <= lastCapability ||
                energy.energy > energy.capacity) return false;
            lastCapability = energy.capability.value;
        }
        lastCapability = 0;
        for (const auto& activity : entry.activities) {
            if (!ValidCapability(activity.capability) || activity.capability.value <= lastCapability ||
                !ValidItemText(activity.status)) return false;
            lastCapability = activity.capability.value;
        }
        for (std::size_t i = 0; i < entry.actions.size(); ++i) {
            if (!ValidAction(entry.actions[i])) return false;
            if (i && (entry.actions[i].capability.value < entry.actions[i - 1].capability.value ||
                (entry.actions[i].capability == entry.actions[i - 1].capability &&
                 entry.actions[i].action <= entry.actions[i - 1].action))) return false;
        }
    }
    for (const auto& entry : entries) {
        const InventoryEntry* current = &entry;
        unsigned depth = 0;
        while (current->parent) {
            if (++depth > 64) return false;
            current = FindEntry(entries, current->parent);
            if (!current || current == &entry) return false;
        }
    }
    return true;
}
bool ValidLists(const WorldView& view) {
    if (!ValidEntries(view.inventory) || !ValidEntries(view.groundItems)) return false;
    for (const auto& entry : view.inventory)
        if (FindEntry(view.groundItems, entry.id)) return false;
    return true;
}
bool WriteEntries(Writer& writer, const Vector<InventoryEntry>& entries) {
    writer.U16(static_cast<std::uint16_t>(entries.size()));
    for (const auto& entry : entries) {
        writer.U64(entry.id); writer.U32(entry.type.value); writer.U32(entry.contained_items);
        writer.U64(entry.parent); writer.U16(entry.installation.value); writer.U64(entry.massGrams);
        writer.U32(entry.volumeMl); writer.U32(entry.capacityVolumeMl); writer.U64(entry.usedVolumeMl);
        writer.U8(static_cast<std::uint8_t>(entry.energy.size()));
        for (const auto& energy : entry.energy) {
            writer.U16(energy.capability.value); writer.U32(energy.energy); writer.U32(energy.capacity);
        }
        writer.U8(static_cast<std::uint8_t>(entry.activities.size()));
        for (const auto& activity : entry.activities) {
            writer.U16(activity.capability.value); writer.U8(activity.active ? 1 : 0); writer.U64(activity.remainingTicks);
            if (!WriteText(writer, activity.status)) return false;
        }
        writer.U8(static_cast<std::uint8_t>(entry.actions.size()));
        for (const auto& action : entry.actions) {
            writer.U16(action.capability.value);
            if (!writer.Text(action.action, maxItemActionName)) return false;
            writer.U8(static_cast<std::uint8_t>(action.argument));
            writer.U8(static_cast<std::uint8_t>(action.availability)); writer.U32(action.baseCost);
            writer.U8(static_cast<std::uint8_t>(action.category));
            if (!WriteText(writer, action.label) || !WriteReason(writer, action.reason)) return false;
        }
        if (writer.bytes.size() > maxPacket) return false;
    }
    return true;
}
bool ReadEntries(Reader& reader, Vector<InventoryEntry>& entries) {
    std::uint16_t count;
    if (!reader.U16(count) || count > maxItemEntries) return false;
    entries.clear();
    for (unsigned i = 0; i < count; ++i) {
        InventoryEntry entry;
        if (!reader.U64(entry.id) || !reader.U32(entry.type.value) || !reader.U32(entry.contained_items) ||
            !reader.U64(entry.parent) || !reader.U16(entry.installation.value) || !reader.U64(entry.massGrams) ||
            !reader.U32(entry.volumeMl) || !reader.U32(entry.capacityVolumeMl) || !reader.U64(entry.usedVolumeMl)) return false;
        std::uint8_t n;
        if (!reader.U8(n) || n > maxItemCapabilities) return false;
        for (unsigned j = 0; j < n; ++j) {
            ItemEnergyView energy;
            if (!reader.U16(energy.capability.value) || !reader.U32(energy.energy) || !reader.U32(energy.capacity)) return false;
            entry.energy.push_back(energy);
        }
        if (!reader.U8(n) || n > maxItemCapabilities) return false;
        for (unsigned j = 0; j < n; ++j) {
            ItemBehaviorView activity; std::uint8_t active;
            if (!reader.U16(activity.capability.value) || !reader.U8(active) || active > 1 ||
                !reader.U64(activity.remainingTicks) || !ReadText(reader, activity.status)) return false;
            activity.active = active != 0; entry.activities.push_back(activity);
        }
        if (!reader.U8(n) || n > maxItemActions) return false;
        for (unsigned j = 0; j < n; ++j) {
            ItemActionInfo action; std::uint8_t argument, availability, category;
            if (!reader.U16(action.capability.value) || !reader.Text(action.action, maxItemActionName) || !reader.U8(argument) ||
                !reader.U8(availability) || !reader.U32(action.baseCost) || !reader.U8(category) ||
                !ReadText(reader, action.label) || !ReadReason(reader, action.reason)) return false;
            action.argument = static_cast<ItemActionArgument>(argument);
            action.availability = static_cast<ItemUseOutcome>(availability);
            action.category = static_cast<ItemActionCategory>(category);
            entry.actions.push_back(action);
        }
        entries.push_back(eastl::move(entry));
    }
    return true;
}
}

bool ValidTerrainDefinitions(const WorldView& view, const ContentCatalog& content) {
    for (const auto& cell : view.cells) {
        if (cell.terrainVisibility != Visibility::Unknown && !content.TryGetTerrain(cell.tile.terrain)) return false;
    }
    return true;
}

bool ValidItemDefinitions(const WorldView& view, const ContentCatalog& content) {
    if (!ValidLists(view)) return false;
    for (const auto* entries : {&view.inventory, &view.groundItems}) {
        for (const auto& entry : *entries) {
            const auto* definition = content.TryGetItem(entry.type);
            if (!definition || entry.volumeMl != definition->volumeMl) return false;
            const auto* container = ItemContainer(*definition);
            if (entry.capacityVolumeMl != (container ? container->capacityVolumeMl : 0)) return false;
            std::uint64_t mass = definition->massGrams, usedVolume = 0;
            std::size_t children = 0;
            for (const auto& child : *entries) {
                if (child.parent != entry.id) continue;
                ++children;
                if (child.massGrams > UINT64_MAX - mass) return false;
                mass += child.massGrams;
                if (!child.installation.value) {
                    if (!container || child.volumeMl > UINT64_MAX - usedVolume) return false;
                    usedVolume += child.volumeMl;
                } else {
                    for (const auto& sibling : *entries)
                        if (sibling.id != child.id && sibling.parent == entry.id &&
                            sibling.installation == child.installation) return false;
                }
            }
            if (children != entry.contained_items || mass != entry.massGrams || usedVolume != entry.usedVolumeMl) return false;
            if (entry.installation.value) {
                const auto* parent = FindEntry(*entries, entry.parent);
                const auto* parentDefinition = parent ? content.TryGetItem(parent->type) : nullptr;
                const auto* behavior = parentDefinition ? ItemBehaviorConfig(*parentDefinition, entry.installation) : nullptr;
                if (!behavior || !behavior->AcceptsInstallation(*definition)) return false;
            }
            if (entry.energy.size() != definition->energyStores.size() ||
                entry.activities.size() != definition->behaviors.size()) return false;
            for (const auto& energy : entry.energy) {
                const auto* config = ItemEnergyStore(*definition, energy.capability);
                if (!config || energy.capacity != config->capacityEnergy) return false;
            }
            for (const auto& activity : entry.activities) {
                const auto* behavior = ItemBehaviorConfig(*definition, activity.capability);
                if (!behavior) return false;
                const auto& schema = behavior->Schema();
                if ((!schema.hasActivity && activity.active) || activity.remainingTicks > schema.maximumDelay ||
                    ((!schema.countdown || !activity.active) && activity.remainingTicks)) return false;
            }
            for (const auto& action : entry.actions) {
                if (!ValidAction(action)) return false;
                const auto* behavior = ItemBehaviorConfig(*definition, action.capability);
                const auto* declared = behavior ? behavior->FindAction(action.action) : nullptr;
                if (!declared || action.baseCost != declared->baseCost || action.argument != declared->argument ||
                    action.category != declared->category || action.label != declared->label) return false;
            }
        }
    }
    return true;
}

void Writer::U8(std::uint8_t v) { bytes.push_back(v); }
void Writer::U16(std::uint16_t v) { U8(static_cast<std::uint8_t>(v >> 8)); U8(static_cast<std::uint8_t>(v)); }
void Writer::U32(std::uint32_t v) { U16(static_cast<std::uint16_t>(v >> 16)); U16(static_cast<std::uint16_t>(v)); }
void Writer::U64(std::uint64_t v) { U32(static_cast<std::uint32_t>(v >> 32)); U32(static_cast<std::uint32_t>(v)); }
void Writer::I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }
bool Writer::Text(eastl::string_view v, std::size_t limit) {
    if (v.size() > limit || v.size() > 255) return false;
    for (unsigned char c : v) if (c < 32 || c == 127) return false;
    U8(static_cast<std::uint8_t>(v.size()));
    for (char c : v) U8(static_cast<std::uint8_t>(c));
    return true;
}
bool Writer::CommandValue(const Command& v) {
    if (!ValidCommandShape(v) || v.capability.value > maxItemCapabilities ||
        (!v.itemAction.empty() && !ValidItemActionId(v.itemAction))) return false;
    U8(static_cast<std::uint8_t>(v.kind)); I32(v.dx); I32(v.dz); U64(v.item); U32(v.waitCost);
    U16(v.capability.value);
    if (!Text(v.itemAction, maxItemActionName)) return false;
    U64(v.otherItem); I32(v.dy);
    return bytes.size() <= maxPacket;
}
bool Writer::Result(const PlayerCommandResult& v) {
    if (!v.player || !v.sequence || !v.opportunity || v.result.outcome < Outcome::waited ||
        v.result.outcome > Outcome::invalid_container ||
        (v.result.storageError && (*v.result.storageError < SaveError::Io || *v.result.storageError > SaveError::NotOpen)) ||
        !ValidItemUseReason(v.result.itemOutcome, v.result.itemReason)) return false;
    U64(v.player); U64(v.sequence); U64(v.opportunity);
    U8(static_cast<std::uint8_t>(v.result.outcome)); U8(v.result.advanced ? 1 : 0);
    U64(v.result.tick); U64(v.result.cost);
    U8(v.result.storageError ? 1 : 0);
    if (v.result.storageError) U8(static_cast<std::uint8_t>(*v.result.storageError));
    U8(static_cast<std::uint8_t>(v.result.itemOutcome));
    return WriteReason(*this, v.result.itemReason) && bytes.size() <= maxPacket;
}
bool Writer::View(const WorldView& v) {
    if (!ValidViewBounds(v) ||
        v.cells.size() != static_cast<std::size_t>(v.width * v.height) ||
        !ValidLists(v) || v.players.size() > 32 ||
        v.playerId == 0 || v.opportunity == 0 || v.speed == 0 || v.pacing.ticksPerSecond == 0 ||
        v.playerLight > LightBand::Bright) return false;
    for (std::size_t i = 0; i < v.cells.size(); ++i)
        if (!ValidCell(v.cells[i], v, i)) return false;
    I32(v.origin.x); I32(v.origin.z); I32(v.origin.y);
    U8(static_cast<std::uint8_t>(v.width)); U8(static_cast<std::uint8_t>(v.height));
    I32(v.player_position.x); I32(v.player_position.z); I32(v.player_position.y); U64(v.tick);
    U64(v.playerId); U64(v.revision); U64(v.opportunity); U64(v.lastInputSequence);
    U64(v.remainingTicks); U32(v.speed);
    U32(v.pacing.ticksPerSecond); U8(v.pacing.rateLimited ? 1 : 0);
    U8(v.pacing.pauseWhenIdle ? 1 : 0); U8(v.commandQueued ? 1 : 0);
    U16(static_cast<std::uint16_t>(v.cells.size()));
    for (std::size_t i = 0; i < v.cells.size();) {
        const ViewCell& c = v.cells[i];
        std::size_t run = 1;
        while (i + run < v.cells.size() && v.cells[i + run] == c) { ++run; }
        U16(static_cast<std::uint16_t>(run));
        U8(static_cast<std::uint8_t>(c.terrainVisibility));
        U32(c.tile.terrain.value); U8(c.tile.damage); U64(c.actor); U8(c.player ? 1 : 0);
        U32(c.ground_items); U64(c.object);
        U8(static_cast<std::uint8_t>(c.objectAppearance.kind));
        U8(static_cast<std::uint8_t>(c.objectAppearance.facing));
        U8(c.objectAppearance.active ? 1 : 0); U8(c.objectAppearance.broken ? 1 : 0);
        U8(static_cast<std::uint8_t>(c.terrainDepth)); U8(static_cast<std::uint8_t>(c.overlayDepth));
        U8(static_cast<std::uint8_t>(c.overlayVisibility));
        U8(static_cast<std::uint8_t>(c.terrainLight)); U8(static_cast<std::uint8_t>(c.overlayLight));
        i += run;
    }
    if (!WriteEntries(*this, v.inventory) || !WriteEntries(*this, v.groundItems)) return false;
    U64(v.carriedMassGrams); U64(v.carryCapacityGrams);
    U8(static_cast<std::uint8_t>(v.players.size()));
    for (const PlayerStatus& p : v.players) {
        U64(p.id); if (!Text({p.name.data(), p.name.size()}, 32)) return false;
        U8(static_cast<std::uint8_t>(p.state)); U8(p.ready ? 1 : 0);
        U8(p.commandQueued ? 1 : 0); U64(p.opportunity); U64(p.graceMicroseconds);
    }
    U8(static_cast<std::uint8_t>(v.playerLight));
    return bytes.size() <= maxPacket;
}
bool Reader::U8(std::uint8_t& v) { if (offset >= bytes.size()) return false; v = bytes[offset++]; return true; }
bool Reader::U16(std::uint16_t& v) { std::uint8_t a, b; if (!U8(a) || !U8(b)) return false; v = (std::uint16_t(a) << 8) | b; return true; }
bool Reader::U32(std::uint32_t& v) { std::uint16_t a, b; if (!U16(a) || !U16(b)) return false; v = (std::uint32_t(a) << 16) | b; return true; }
bool Reader::U64(std::uint64_t& v) { std::uint32_t a, b; if (!U32(a) || !U32(b)) return false; v = (std::uint64_t(a) << 32) | b; return true; }
bool Reader::I32(std::int32_t& v) { std::uint32_t raw; if (!U32(raw)) return false; v = static_cast<std::int32_t>(raw); return true; }
bool Reader::Text(String& v, std::size_t limit) {
    std::uint8_t n; if (!U8(n) || n > limit || n > bytes.size() - offset) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (bytes[offset + i] < 32 || bytes[offset + i] == 127) return false;
    v.clear(); for (std::uint8_t i = 0; i < n; ++i) v.push_back(static_cast<char>(bytes[offset++]));
    return true;
}
bool Reader::CommandValue(Command& v) {
    std::uint8_t kind;
    if (bytes.size() > maxPacket || !U8(kind) || kind > static_cast<std::uint8_t>(CommandKind::store_item) ||
        !I32(v.dx) || !I32(v.dz) || !U64(v.item) || !U32(v.waitCost) ||
        !U16(v.capability.value) || v.capability.value > maxItemCapabilities ||
        !Text(v.itemAction, maxItemActionName) || !U64(v.otherItem) || !I32(v.dy)) return false;
    v.kind = static_cast<CommandKind>(kind);
    return (v.itemAction.empty() || ValidItemActionId(v.itemAction)) && ValidCommandShape(v);
}
bool Reader::Result(PlayerCommandResult& v) {
    std::uint8_t outcome, advanced;
    if (bytes.size() > maxPacket || !U64(v.player) || !v.player || !U64(v.sequence) || !v.sequence ||
        !U64(v.opportunity) || !v.opportunity || !U8(outcome) ||
        outcome > static_cast<std::uint8_t>(Outcome::invalid_container) || !U8(advanced) || advanced > 1 ||
        !U64(v.result.tick) || !U64(v.result.cost)) return false;
    std::uint8_t hasError;
    if (!U8(hasError) || hasError > 1) return false;
    v.result.storageError.reset();
    if (hasError) {
        std::uint8_t error;
        if (!U8(error) || error > static_cast<std::uint8_t>(SaveError::NotOpen)) return false;
        v.result.storageError = static_cast<SaveError>(error);
    }
    std::uint8_t itemOutcome;
    if (!U8(itemOutcome) || itemOutcome > static_cast<std::uint8_t>(ItemUseOutcome::Refused) ||
        !ReadReason(*this, v.result.itemReason)) return false;
    v.result.itemOutcome = static_cast<ItemUseOutcome>(itemOutcome);
    if (!ValidItemUseReason(v.result.itemOutcome, v.result.itemReason)) return false;
    v.result.outcome = static_cast<Outcome>(outcome); v.result.advanced = advanced != 0; return true;
}
bool Reader::View(WorldView& v) {
    if (bytes.size() > maxPacket) return false;
    std::uint8_t w, h, rate, pause, queued; std::uint16_t count;
    if (!I32(v.origin.x) || !I32(v.origin.z) || !I32(v.origin.y) || !U8(w) || !U8(h) || w == 0 ||
        w > maxViewDimension || h != w || (w & 1) == 0 ||
        !I32(v.player_position.x) || !I32(v.player_position.z) || !I32(v.player_position.y) || !U64(v.tick) ||
        !U64(v.playerId) || !U64(v.revision) || !U64(v.opportunity) || !U64(v.lastInputSequence) ||
        !U64(v.remainingTicks) || !U32(v.speed) || !U32(v.pacing.ticksPerSecond) ||
        !v.playerId || !v.opportunity || !v.speed || !v.pacing.ticksPerSecond ||
        !U8(rate) || rate > 1 || !U8(pause) || pause > 1 ||
        !U8(queued) || queued > 1 || !U16(count) || count != w * h) return false;
    v.width = w; v.height = h; v.pacing.rateLimited = rate; v.pacing.pauseWhenIdle = pause;
    if (!ValidViewBounds(v)) return false;
    v.commandQueued = queued;
    v.cells.clear();
    v.cells.reserve(count);
    while (v.cells.size() < count) {
        std::uint16_t run;
        if (!U16(run) || run == 0 || run > count - v.cells.size()) return false;
        ViewCell c;
        std::uint8_t terrainVisibility, overlayVisibility, terrainLight, overlayLight, kind, facing, active, broken;
        if (!U8(terrainVisibility)) return false;
        c.terrainVisibility = static_cast<Visibility>(terrainVisibility);
        std::uint8_t player, terrainDepth, overlayDepth;
        if (!U32(c.tile.terrain.value) || !U8(c.tile.damage) || !U64(c.actor) ||
            !U8(player) || player > 1 || !U32(c.ground_items) || !U64(c.object) ||
            !U8(kind) || !U8(facing) || !U8(active) || active > 1 || !U8(broken) || broken > 1 ||
            !U8(terrainDepth) || !U8(overlayDepth) || !U8(overlayVisibility) ||
            !U8(terrainLight) || !U8(overlayLight)) return false;
        c.player = player;
        c.objectAppearance = {static_cast<ObjectKind>(kind), static_cast<ObjectFacing>(facing), active != 0, broken != 0};
        c.terrainDepth = terrainDepth; c.overlayDepth = overlayDepth;
        c.overlayVisibility = static_cast<Visibility>(overlayVisibility);
        c.terrainLight = static_cast<LightBand>(terrainLight); c.overlayLight = static_cast<LightBand>(overlayLight);
        for (unsigned i = 0; i < run; ++i) {
            // A run may cross row/world boundaries. Validate each expanded
            // position, not just the first coordinate carrying this payload.
            if (!ValidCell(c, v, v.cells.size())) return false;
            v.cells.push_back(c);
        }
    }
    if (!ReadEntries(*this, v.inventory) || !ReadEntries(*this, v.groundItems) || !ValidLists(v) ||
        !U64(v.carriedMassGrams) || !U64(v.carryCapacityGrams)) return false;
    std::uint8_t n; if (!U8(n) || n > 32) return false;
    v.players.clear();
    PersistentId lastPlayer = 0;
    for (unsigned i = 0; i < n; ++i) {
        PlayerStatus p; std::uint8_t state, ready, pending;
        if (!U64(p.id) || p.id <= lastPlayer || !Text(p.name, 32) || p.name.empty() || !U8(state) ||
            state > static_cast<std::uint8_t>(PlayerConnectionState::Active) ||
            !U8(ready) || ready > 1 || !U8(pending) || pending > 1 ||
            !U64(p.opportunity) || !U64(p.graceMicroseconds)) return false;
        p.state = static_cast<PlayerConnectionState>(state); p.ready = ready; p.commandQueued = pending;
        lastPlayer = p.id;
        v.players.push_back(eastl::move(p));
    }
    std::uint8_t playerLight;
    if (!U8(playerLight) || playerLight > static_cast<std::uint8_t>(LightBand::Bright)) return false;
    v.playerLight = static_cast<LightBand>(playerLight);
    return true;
}

}
