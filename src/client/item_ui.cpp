#include "client/item_ui.h"
#include "client/text.h"
#include "client/combat_text.h"
#include "client/combat_menu.h"
#include <algorithm>
#include <charconv>

namespace cataclysmx {
namespace {
const Vector<InventoryEntry>& Nearby(const WorldView& v) { return v.nearbyItems.empty() ? v.groundItems : v.nearbyItems; }
template<class F> void Each(const WorldView& v, F f) {
    for (const auto& i : v.inventory) { f(i); }
    for (const auto& i : Nearby(v)) { f(i); }
}
const ItemStorageView* Storage(const WorldView& v, ItemStorage s) {
    for (const auto& entry : v.itemStorages) { if (entry.storage == s) { return &entry; } }
    return nullptr;
}
eastl::optional<ItemStorage> RootStorage(const WorldView& view, ItemStorage storage) {
    for (std::size_t depth = 0; storage.kind == ItemStorageKind::Container; ++depth) {
        if (depth > view.inventory.size() + Nearby(view).size()) { return {}; }
        const auto* item = FindInventoryEntry(view, storage.id);
        if (!item) { return {}; }
        storage = item->storage;
    }
    return storage;
}
eastl::optional<TileCoord> StoragePosition(const WorldView& view, ItemStorage storage, bool anchorCarried = false) {
    const auto root = RootStorage(view, storage);
    if (!root) { return {}; }
    if (root->kind == ItemStorageKind::Ground) { return root->position; }
    if (root->kind == ItemStorageKind::Object) {
        if (const auto* source = Storage(view, *root)) { return source->objectPosition; }
    }
    if (anchorCarried && root->kind == ItemStorageKind::Inventory) { return view.player_position; }
    return {};
}
bool NearbyStorage(const WorldView& view, ItemStorage storage, eastl::optional<TileCoord> excluded) {
    if (storage.kind != ItemStorageKind::Ground && storage.kind != ItemStorageKind::Object) { return false; }
    const auto* meta = Storage(view, storage);
    const auto pos = StoragePosition(view, storage);
    return meta && meta->accessible && pos && pos != excluded && pos->y == view.player_position.y
        && pos->x >= view.player_position.x - 1 && pos->x <= view.player_position.x + 1
        && pos->z >= view.player_position.z - 1 && pos->z <= view.player_position.z + 1;
}
void CollectNearbyStorages(const WorldView& view, ItemStorage opposite, Vector<ItemStorage>& result) {
    result.clear();
    const auto excluded = StoragePosition(view, opposite);
    for (const auto& meta : view.itemStorages) {
        if (NearbyStorage(view, meta.storage, excluded)) { result.push_back(meta.storage); }
    }
    std::sort(result.begin(), result.end(), [&](ItemStorage a, ItemStorage b) {
        const auto pa = *StoragePosition(view, a), pb = *StoragePosition(view, b);
        if (pa.z != pb.z) { return pa.z < pb.z; }
        if (pa.x != pb.x) { return pa.x < pb.x; }
        if (a.kind != b.kind) { return a.kind < b.kind; }
        return a.id < b.id;
    });
}
String SourceName(const WorldView& view, ItemStorage storage) {
    if (storage.kind == ItemStorageKind::Ground) { return "floor"; }
    const auto* meta = Storage(view, storage);
    return meta && !meta->name.empty() ? meta->name : String("object");
}
String SourceLabel(const WorldView& view, ItemStorage storage) {
    const auto pos = StoragePosition(view, storage);
    if (!pos) { return {}; }
    const int index = (1 - (pos->z - view.player_position.z)) * 3 + pos->x - view.player_position.x + 2;
    return " [" + NumberText(index) + " " + SourceName(view, storage) + "]";
}
void CaptureAncestry(const WorldView& view, ItemStorage storage, Vector<ItemStorage>& output) {
    output.clear();
    for (std::size_t depth = 0; depth <= view.inventory.size() + Nearby(view).size(); ++depth) {
        output.push_back(storage);
        if (storage.kind != ItemStorageKind::Container) { return; }
        const auto* item = FindInventoryEntry(view, storage.id);
        if (!item) { return; }
        storage = item->storage;
    }
}
bool SameAncestry(const WorldView& view, ItemStorage storage, const Vector<ItemStorage>& captured) {
    for (std::size_t depth = 0; depth < captured.size(); ++depth) {
        if (storage != captured[depth]) { return false; }
        if (storage.kind != ItemStorageKind::Container) { return depth + 1 == captured.size(); }
        const auto* item = FindInventoryEntry(view, storage.id);
        if (!item) { return false; }
        storage = item->storage;
    }
    return false;
}
bool TransferValid(const WorldView& view, PersistentId id, ItemStorage source, ItemStorage destination) {
    const auto* item = FindInventoryEntry(view, id);
    if (!item || item->storage != source || item->installation.value) { return false; }
    for (const auto storage : {source, destination}) {
        auto current = storage;
        for (std::size_t depth = 0;; ++depth) {
            if (depth > view.inventory.size() + Nearby(view).size()) { return false; }
            if (storage == source && (current.kind == ItemStorageKind::Ground || current.kind == ItemStorageKind::Object)
                && !Storage(view, current)) { return false; }
            if (const auto* meta = Storage(view, current); meta && !meta->accessible) { return false; }
            if (current.kind != ItemStorageKind::Container) { break; }
            if (storage == destination && current.id == id) { return false; }
            const auto* ancestor = FindInventoryEntry(view, current.id);
            if (!ancestor) { return false; }
            current = ancestor->storage;
        }
    }
    return true;
}
ItemStorage DefaultTileStorage(const WorldView& view, TileCoord position) {
    for (const auto& storage : view.itemStorages) {
        if (storage.storage.kind == ItemStorageKind::Object && storage.accessible
            && StoragePosition(view, storage.storage) == position) { return storage.storage; }
    }
    return {ItemStorageKind::Ground, position};
}
String LocationTitle(const WorldView& view, ItemStorage storage, String name) {
    const auto position = StoragePosition(view, storage);
    if (!position) { return name; }
    const int dx = position->x - view.player_position.x;
    const int dz = position->z - view.player_position.z;
    if (position->y == view.player_position.y && dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1) {
        constexpr const char* directions[] = {"Southwest", "South", "Southeast", "West", "Here", "East", "Northwest", "North", "Northeast"};
        const auto index = (1 - dz) * 3 + dx + 1;
        return "[" + NumberText(index + 1) + "] " + directions[index] + " — " + name;
    }
    return name + " (" + NumberText(position->x) + ", " + NumberText(position->z) + ")";
}
String ScaledMeasurement(std::uint64_t value, const char* smallUnit, const char* largeUnit) {
    if (value < 1000) { return UnsignedText(value) + ' ' + smallUnit; }
    String result = UnsignedText(value / 1000);
    if (const auto remainder = value % 1000) {
        String fraction = UnsignedText(remainder);
        fraction.insert(String::size_type{0}, 3 - fraction.size(), '0');
        while (fraction.back() == '0') { fraction.pop_back(); }
        result += '.'; result += fraction;
    }
    result += ' '; result += largeUnit;
    return result;
}
String Hundredths(std::uint32_t n) {
    String s = UnsignedText(n / 100); s += '.';
    if (n % 100 < 10) { s += '0'; } s += UnsignedText(n % 100); return s;
}
void AddRow(ItemUiPane& pane, std::size_t& rowCount, const InventoryEntry& item, unsigned depth,
    const ContentCatalog& content, const Localization& loc) {
    if (rowCount == pane.rows.size()) { pane.rows.emplace_back(); }
    auto& row = pane.rows[rowCount++]; row.items.clear();
    row.type = item.type; row.storage = item.storage; row.name = loc.ItemName(content, item.type);
    row.items.push_back(item.id); row.massGrams = item.massGrams; row.volumeMl = item.volumeMl;
    row.depth = depth; row.wielded = item.wielded; row.worn = item.worn; row.heldHands = item.heldHands;
    if (item.heldHands == HandMask::Both) { row.name += " [2H]"; }
    else if (item.heldHands == HandMask::Left) { row.name += " [L]"; }
    else if (item.heldHands == HandMask::Right) { row.name += " [R]"; }
    else if (item.wielded) { row.name += " [wielded]"; }
    if (item.worn) { row.name += " [worn]"; }
}
}
const char* ItemUiActionLabel(ItemUiAction action) {
    switch (action) {
    case ItemUiAction::HoldLeft: return "Hold in left hand";
    case ItemUiAction::HoldRight: return "Hold in right hand";
    case ItemUiAction::TwoHanded: return "Hold two-handed";
    case ItemUiAction::Regrip: return "Regrip (toggle one/two hands)";
    case ItemUiAction::Wear: return "Wear";
    case ItemUiAction::Remove: return "Remove worn item";
    case ItemUiAction::Release: return "Release held item";
    }
    return "Unknown equipment action";
}
String ItemMassText(std::uint64_t grams) { return ScaledMeasurement(grams, "g", "kg"); }
String ItemVolumeText(std::uint64_t milliliters) { return ScaledMeasurement(milliliters, "mL", "L"); }
const InventoryEntry* FindInventoryEntry(const WorldView& v, PersistentId id) {
    for (const auto& i : v.inventory) { if (i.id == id) { return &i; } }
    for (const auto& i : Nearby(v)) { if (i.id == id) { return &i; } }
    return nullptr;
}
void DescribeInventoryItem(const InventoryEntry& i, const ContentCatalog& content,
    const Localization& loc, ItemExamination& out) {
    out.type = i.type; out.name = loc.ItemName(content, i.type); out.stats.clear();
    const auto& def = content.GetItem(i.type);
    String key = "item."; key += def.id; key += ".description";
    out.description = loc.RenderItemText({key, def.description}); out.art = def.art.lines.empty() ? nullptr : &def.art;
    out.stats.push_back("Mass: " + ItemMassText(def.massGrams) + " | Volume: " + ItemVolumeText(def.volumeMl));
    if (i.contained_items) { out.stats.push_back("Mass with contents: " + ItemMassText(i.massGrams)); }
    if (i.heldHands != HandMask::None) {
        out.stats.push_back(i.heldHands == HandMask::Both ? "Held: 2H" : i.heldHands == HandMask::Left ? "Held: L" : "Held: R");
    } else if (i.wielded) { out.stats.push_back("Currently wielded"); }
    if (i.worn) { out.stats.push_back("Currently worn"); }
    const auto& melee = ItemMelee(def);
    constexpr const char* names[] = {"Blunt", "Slashing", "Piercing", "Ballistic", "Heat", "Chemical", "Choking"};
    for (const auto& d : melee.damage) {
        String s = names[static_cast<unsigned>(d.type)]; s += " damage: "; s += Hundredths(d.amount);
        s += " HP | Penetration: "; s += Hundredths(d.penetration);
        s += " HP (authored base; actual damage depends on move, grip, defense and armor)"; out.stats.push_back(eastl::move(s));
    }
    out.stats.push_back("Attack cost: " + UnsignedText(melee.attackCost) + " ticks | Hands: " + UnsignedText(melee.hands));
    const auto family = melee.handlingSkill.value ? melee.handlingSkill : content.GetCombatSkills().melee;
    const auto* handling = content.TryGetSkill(family);
    out.stats.push_back("Weapon family: " + (handling ? handling->name : String("improvised fallback")));
    for (const auto& grip : melee.grips) {
        out.stats.push_back(String(grip.kind == GripKind::TwoHanded ? "2H grip" : "1H grip")
            + " | damage ratio " + UnsignedText(grip.damageRatio) + "/" + UnsignedText(ratioScale)
            + " | time ratio " + UnsignedText(grip.costRatio) + "/" + UnsignedText(ratioScale)
            + " | stamina ratio " + UnsignedText(grip.staminaRatio) + "/" + UnsignedText(ratioScale));
    }
    if (def.armor) {
        const auto& armor = *def.armor;
        const char* layer = "normal";
        switch (armor.layer) {
        case ArmorLayer::Under: layer = "under"; break;
        case ArmorLayer::Normal: break;
        case ArmorLayer::Outer: layer = "outer"; break;
        case ArmorLayer::Strapped: layer = "strapped"; break;
        }
        out.stats.push_back(String("Armor layer: ") + layer + " | wear/remove base ticks "
            + UnsignedText(armor.wearCost) + "/" + UnsignedText(armor.removeCost));
        for (const auto& region : armor.regions) {
            String line = CombatRegionName(region.region);
            line += " | coverage " + Hundredths(region.coverage) + "% | encumbrance " + UnsignedText(region.encumbrance);
            if (region.overlapExempt) { line += " | overlap-penalty exempt"; }
            if (armor.rigidRegions & ContactBit(region.region)) { line += " | rigid"; }
            out.stats.push_back(eastl::move(line));
        }
        for (std::size_t n = 0; n < damage_type_count; ++n) {
            out.stats.push_back(String(names[n]) + " armor protection: " + Hundredths(armor.protection[n]) + " HP");
        }
        out.stats.push_back("Coverage is rolled per garment/contact; same-layer overlaps add regional encumbrance. Protection is not guaranteed damage prevention.");
    }
    if (i.capacityVolumeMl) { out.stats.push_back("Storage: " + ItemVolumeText(i.usedVolumeMl) + " / " + ItemVolumeText(i.capacityVolumeMl)); }
    for (const auto& cap : def.capabilities) { out.stats.push_back("Capability: " + cap.id + " (" + cap.type + ")"); }
    for (const auto& trait : def.toolTraits) { out.stats.push_back("Tool: " + content.GetToolTrait(trait.trait).name + " tier " + UnsignedText(trait.tier)); }
    for (const auto& e : i.energy) { out.stats.push_back("Energy #" + UnsignedText(e.capability.value) + ": " + UnsignedText(e.energy) + " / " + UnsignedText(e.capacity)); }
    for (const auto& a : i.activities) { out.stats.push_back(String(a.active ? "Active #" : "Inactive #") + UnsignedText(a.capability.value) + ": " + loc.RenderItemText(a.status) + " | " + UnsignedText(a.remainingTicks) + " ticks"); }
    for (const auto& a : i.actions) {
        String s = loc.ItemActionText(a) + " | Cost: " + UnsignedText(a.baseCost) + " ticks";
        if (a.availability != ItemUseOutcome::Succeeded) { s += " — "; s += loc.ItemRefusal(a.availability, a.reason); }
        out.stats.push_back(eastl::move(s));
    }
}
void ItemUi::Open(bool advanced, bool use) {
    if (frame_.advanced != advanced) { initialized_ = false; }
    if (!outstanding_ && pending_.empty()) { moved_ = 0; requested_ = 0; }
    frame_.advanced = advanced; useSelection_ = use; frame_.screen = advanced ? ItemUiScreen::Advanced : ItemUiScreen::Inventory;
    returnScreen_ = frame_.screen; frame_.feedback.clear(); opening_ = true;
}
void ItemUi::Close() {
    frame_.screen = ItemUiScreen::Closed;
    if (outstanding_) { pending_.resize(nextCommand_); } else { pending_.clear(); nextCommand_ = 0; }
    frame_.busy = NeedsTime();
}
bool ItemUi::IsOpen() const { return frame_.screen != ItemUiScreen::Closed; }
bool ItemUi::IsAdvanced() const { return frame_.advanced; }
bool ItemUi::NeedsTime() const { return outstanding_ || waitingForReady_ || nextCommand_ < pending_.size(); }
ItemUiFrame& ItemUi::Frame() { return frame_; }
const ItemUiFrame& ItemUi::Frame() const { return frame_; }
bool IsItemUiNumberKey(ItemUiScreen screen, const KeyChord& key) {
    if (screen != ItemUiScreen::Amount && screen != ItemUiScreen::Advanced) { return false; }
    return !key.ctrl && !key.alt && key.key.size() == 1
        && key.key[0] >= (screen == ItemUiScreen::Amount ? '0' : '1') && key.key[0] <= '9';
}
void ItemUi::SwapPanes() {
    eastl::swap(frame_.panes[0], frame_.panes[1]);
    eastl::swap(selected_[0], selected_[1]);
    eastl::swap(selectionLost_[0], selectionLost_[1]);
}
void ItemUi::SelectStorage(ItemStorage s) {
    const unsigned a = frame_.activePane, b = 1 - a;
    if (frame_.panes[b].mode == ItemUiPaneMode::Storage && frame_.panes[b].storage == s) {
        SwapPanes();
    } else if (frame_.panes[a].mode != ItemUiPaneMode::Storage || !(frame_.panes[a].storage == s)) {
        frame_.panes[a] = {}; frame_.panes[a].storage = s; selected_[a] = 0; selectionLost_[a] = false;
    }
}
void ItemUi::SelectAllNearby() {
    const unsigned a = frame_.activePane, b = 1 - a;
    if (frame_.panes[b].mode == ItemUiPaneMode::AllNearby) {
        SwapPanes();
    } else if (frame_.panes[a].mode != ItemUiPaneMode::AllNearby) {
        frame_.panes[a] = {}; frame_.panes[a].mode = ItemUiPaneMode::AllNearby;
        selected_[a] = 0; selectionLost_[a] = false;
    }
}
void ItemUi::Refresh(const WorldView& v, const ContentCatalog& content, const Localization& loc, const Keymap& keys) {
    const auto character = v.characterId ? v.characterId : v.playerId;
    if (initialized_ && player_ != character) {
        if (!opening_) { Close(); }
        outstanding_ = false; waitingForReady_ = false; pending_.clear(); nextCommand_ = 0;
        selected_ = {}; selectionLost_ = {}; initialized_ = false;
    }
    opening_ = false;
    player_ = character;
    if (waitingForReady_ && !outstanding_ && !v.remainingTicks && !v.commandQueued && v.tick >= finishedTick_
        && (v.revision != issuedRevision_ || v.tick != issuedTick_)) { waitingForReady_ = false; }
    if (!IsOpen()) { return; }
    if (!initialized_) {
        frame_.panes = {}; selected_ = {}; selectionLost_ = {}; frame_.activePane = 0;
        if (IsAdvanced()) { frame_.panes[1].mode = ItemUiPaneMode::AllNearby; }
        initialized_ = true;
    }
    for (unsigned p = 0; p < (IsAdvanced() ? 2u : 1u); ++p) {
        auto& pane = frame_.panes[p]; std::size_t rowCount = 0;
        const bool aggregate = pane.mode == ItemUiPaneMode::AllNearby;
        if (IsAdvanced()) {
            pane.anchor = aggregate ? eastl::optional<TileCoord>{v.player_position} : StoragePosition(v, pane.storage, true);
        }
        if (aggregate) { CollectNearbyStorages(v, frame_.panes[1 - p].storage, nearbyStorages_); }
        const auto* meta = aggregate ? nullptr : Storage(v, pane.storage);
        pane.title = pane.storage.kind == ItemStorageKind::Inventory ? "Inventory" : pane.storage.kind == ItemStorageKind::Ground ? "Floor" : "Storage";
        if (meta && !meta->name.empty()) { pane.title = meta->name; }
        if (pane.storage.kind == ItemStorageKind::Container) {
            if (const auto* item = FindInventoryEntry(v, pane.storage.id)) { pane.title = loc.ItemName(content, item->type); }
        }
        pane.title = LocationTitle(v, pane.storage, eastl::move(pane.title));
        if (aggregate) { pane.title = "All nearby"; }
        pane.capacity.clear();
        if (meta) {
            pane.capacity = ItemMassText(meta->massGrams);
            if (meta->massLimitGrams) { pane.capacity += " / "; pane.capacity += ItemMassText(*meta->massLimitGrams); }
            pane.capacity += " | "; pane.capacity += ItemVolumeText(meta->volumeMl);
            if (meta->volumeLimitMl) { pane.capacity += " / "; pane.capacity += ItemVolumeText(*meta->volumeLimitMl); }
            if (!meta->accessible) { pane.capacity += " — "; pane.capacity += InventoryOutcomeMessage(meta->reason); }
        }
        const auto add = [&](const InventoryEntry& i, unsigned depth) {
            if (i.stackable && !i.wielded && !i.worn && i.heldHands == HandMask::None) {
                for (std::size_t n = 0; n < rowCount; ++n) {
                    auto& row = pane.rows[n];
                    const auto* first = FindInventoryEntry(v, row.items.front());
                    if (first && first->stackable && !row.wielded && !row.worn && row.heldHands == HandMask::None
                        && row.type == i.type && first->storage == i.storage && row.depth == depth) {
                        row.items.push_back(i.id); std::sort(row.items.begin(), row.items.end()); row.massGrams += i.massGrams; row.volumeMl += i.volumeMl; return;
                    }
                }
            }
            AddRow(pane, rowCount, i, depth, content, loc);
        };
        if (IsAdvanced() && aggregate) {
            std::uint64_t mass = 0, volume = 0;
            for (const auto source : nearbyStorages_) {
                const auto* sourceMeta = Storage(v, source);
                mass += sourceMeta->massGrams; volume += sourceMeta->volumeMl;
                const auto begin = rowCount;
                Each(v, [&](const InventoryEntry& i) { if (i.storage == source) { add(i, 0); } });
                for (auto n = begin; n < rowCount; ++n) { pane.rows[n].name += SourceLabel(v, source); }
            }
            pane.capacity = ItemMassText(mass) + " | " + ItemVolumeText(volume);
        }
        else if (IsAdvanced()) { Each(v, [&](const InventoryEntry& i) { if (i.storage == pane.storage) { add(i, 0); } }); }
        else {
            // Emit parent before descendants, without assuming snapshot ID order is tree order.
            const auto tree = [&](auto&& self, const Vector<InventoryEntry>& list, PersistentId parent, unsigned depth) -> void {
                if (depth > list.size()) { return; }
                for (const auto& i : list) { if (i.parent == parent) { add(i, depth); self(self, list, i.id, depth + 1); } }
            };
            tree(tree, v.inventory, 0, 0);
            const auto carriedRows = rowCount;
            tree(tree, v.groundItems, 0, 0);
            for (auto index = carriedRows; index < rowCount; ++index) { pane.rows[index].name += " [ground]"; }
            pane.capacity = "Carried: " + ItemMassText(v.carriedMassGrams) + " / " + ItemMassText(v.carryCapacityGrams);
        }
        pane.rows.resize(rowCount); pane.selectionValid = false;
        if (!selected_[p] && !pane.rows.empty()) { selected_[p] = pane.rows.front().items.front(); }
        bool foundSelection = false;
        for (std::size_t n = 0; n < pane.rows.size(); ++n) {
            for (auto id : pane.rows[n].items) {
                if (id == selected_[p]) { pane.selected = n; foundSelection = true; pane.selectionValid = !selectionLost_[p]; }
            }
        }
        if (selected_[p] && !foundSelection) { selectionLost_[p] = true; }
    }
    frame_.title = IsAdvanced() ? "Advanced inventory" : "Inventory";
    if (frame_.screen == ItemUiScreen::Examine) {
        if (const auto* i = FindInventoryEntry(v, examined_)) { DescribeInventoryItem(*i, content, loc, frame_.examination); }
        else { frame_.feedback = "Selected item is no longer accessible."; }
    }
    frame_.footer.clear();
    const auto hint = [&](InputContext context, InputAction a, const char* label) {
        if (!frame_.footer.empty()) { frame_.footer += " | "; }
        const auto binding = BindingHint(keys, context, a);
        if (a == InputAction::Cancel && binding != "Escape") {
            frame_.footer += "Escape"; if (binding != "—") { frame_.footer += '/'; frame_.footer += binding; }
        } else { frame_.footer += binding; }
        frame_.footer += ' '; frame_.footer += label;
    };
    frame_.busy = NeedsTime();
    const auto* selectedItem = FindInventoryEntry(v, frame_.screen == ItemUiScreen::Examine ? examined_ : selected_[frame_.activePane]);
    const char* wieldLabel = selectedItem && selectedItem->wielded ? "unwield" : "wield";
    if (frame_.screen == ItemUiScreen::Amount || frame_.screen == ItemUiScreen::Actions || frame_.screen == ItemUiScreen::Target || frame_.screen == ItemUiScreen::Direction || frame_.screen == ItemUiScreen::Destination) {
        if (frame_.screen == ItemUiScreen::Amount) { frame_.footer = "0–9 quantity | Backspace erase"; }
        else {
            hint(InputContext::Menu, InputAction::North, frame_.screen == ItemUiScreen::Direction ? "north" : "previous");
            hint(InputContext::Menu, InputAction::South, frame_.screen == ItemUiScreen::Direction ? "south" : "next");
        }
        if (frame_.screen == ItemUiScreen::Direction) {
            hint(InputContext::Menu, InputAction::West, "west"); hint(InputContext::Menu, InputAction::East, "east");
            hint(InputContext::Items, InputAction::MoveDown, "down"); hint(InputContext::Items, InputAction::MoveUp, "up");
        } else { hint(InputContext::Menu, InputAction::Accept, "confirm"); }
        hint(InputContext::Menu, InputAction::Cancel, "back"); return;
    }
    if (frame_.screen == ItemUiScreen::Examine) {
        frame_.title = frame_.examination.name;
        hint(InputContext::Menu, InputAction::North, "scroll up"); hint(InputContext::Menu, InputAction::South, "scroll down");
        hint(InputContext::Items, InputAction::Drop, "drop"); hint(InputContext::Items, InputAction::Wield, wieldLabel);
        hint(InputContext::Items, InputAction::Use, "use"); hint(InputContext::Items, InputAction::Apply, "contents");
        hint(InputContext::Menu, InputAction::Cancel, "back"); return;
    }
    hint(InputContext::Menu, InputAction::North, "previous"); hint(InputContext::Menu, InputAction::South, "next");
    hint(InputContext::Menu, InputAction::Accept, IsAdvanced() ? "move one" : useSelection_ ? "use" : "examine");
    hint(InputContext::Items, InputAction::Examine, "examine");
    if (IsAdvanced()) {
        hint(InputContext::Items, InputAction::TransferAmount, "amount"); hint(InputContext::Items, InputAction::TransferStack, "stack");
        hint(InputContext::Menu, InputAction::West, "left pane"); hint(InputContext::Menu, InputAction::East, "right pane");
        hint(InputContext::Items, InputAction::Inventory, "inventory");
        hint(InputContext::Items, InputAction::AllNearby, "all nearby");
        frame_.footer += " | 1–9 tile"; hint(InputContext::Items, InputAction::ToggleStorage, "floor/object");
        hint(InputContext::Items, InputAction::OpenContainer, "open"); hint(InputContext::Items, InputAction::ParentStorage, "parent");
    } else { hint(InputContext::Items, InputAction::Drop, "drop"); hint(InputContext::Items, InputAction::Wield, wieldLabel); }
    hint(InputContext::Items, InputAction::Use, "use"); hint(InputContext::Items, InputAction::Apply, "contents");
    hint(InputContext::Menu, InputAction::Cancel, "back/close");
}
void ItemUi::Examine(PersistentId id, const WorldView& v, const ContentCatalog& c, const Localization& l) {
    const auto* i = FindInventoryEntry(v, id); if (!i) { frame_.feedback = "Selected item is no longer accessible."; return; }
    examined_ = id; frame_.examination.scroll = 0; DescribeInventoryItem(*i, c, l, frame_.examination); frame_.screen = ItemUiScreen::Examine;
}
void ItemUi::BeginActions(PersistentId id, bool manage, const WorldView& v, const ContentCatalog& c, const Localization& l) {
    const auto* i = FindInventoryEntry(v, id); if (!i) { frame_.feedback = "Selected item is no longer accessible."; return; }
    actionReturn_ = frame_.screen;
    actionSource_ = id; managing_ = manage; actions_.clear(); equipmentActions_.clear(); frame_.choices.clear(); frame_.choice = 0;
    for (const auto& a : i->actions) {
        if (a.category != (manage ? ItemActionCategory::ManageContents : ItemActionCategory::Operate)) { continue; }
        actions_.push_back(a);
        String label = l.ItemActionText(a) + " | " + UnsignedText(a.baseCost) + " ticks";
        if (a.availability != ItemUseOutcome::Succeeded) { label += " — "; label += l.ItemRefusal(a.availability, a.reason); }
        frame_.choices.push_back(eastl::move(label));
    }
    if (manage) { frame_.choices.push_back("Store in container"); frame_.choices.push_back("Extract to inventory"); }
    else if (actions_.empty()) {
        equipmentActions_ = {ItemUiAction::HoldLeft, ItemUiAction::HoldRight, ItemUiAction::TwoHanded,
            ItemUiAction::Regrip, ItemUiAction::Wear, ItemUiAction::Remove, ItemUiAction::Release};
        for (auto action : equipmentActions_) { frame_.choices.push_back(ItemUiActionLabel(action)); }
    }
    if (frame_.choices.empty()) { frame_.feedback = "No functional actions available."; return; }
    frame_.screen = ItemUiScreen::Actions;
    if (!manage && frame_.choices.size() == 1) { frame_.screen = actionReturn_; ChooseAction(v, c, l); }
}
void ItemUi::ChooseAction(const WorldView& v, const ContentCatalog& c, const Localization& l) {
    actionChoice_ = frame_.choice;
    const auto* source = FindInventoryEntry(v, actionSource_);
    if (!source) { frame_.feedback = "Selected item is no longer accessible."; return; }
    actionCommand_ = {}; actionCommand_.item = actionSource_;
    if (frame_.choice < actions_.size()) {
        const auto& captured = actions_[frame_.choice]; const ItemActionInfo* info = nullptr;
        for (const auto& a : source->actions) { if (a.capability == captured.capability && a.action == captured.action) { info = &a; } }
        if (!info) { frame_.feedback = "Selected action is no longer available."; return; }
        if (info->availability != ItemUseOutcome::Succeeded) { frame_.feedback = l.ItemRefusal(info->availability, info->reason); return; }
        actionCommand_.kind = CommandKind::use_item; actionCommand_.capability = info->capability; actionCommand_.itemAction = info->action;
        if (info->argument == ItemActionArgument::None) { QueueCommand(actionCommand_); return; }
        if (info->argument == ItemActionArgument::Direction) { frame_.screen = ItemUiScreen::Direction; return; }
    } else if (!managing_ && frame_.choice - actions_.size() < equipmentActions_.size()) {
        HandleAction(equipmentActions_[frame_.choice - actions_.size()], v); return;
    } else {
        actionCommand_.kind = CommandKind::store_item;
        if (frame_.choice == actions_.size() + 1) { QueueCommand(actionCommand_); return; }
    }
    targets_.clear(); frame_.choices.clear(); frame_.choice = 0;
    Each(v, [&](const InventoryEntry& i) {
        if (actionCommand_.kind == CommandKind::store_item && (i.id == actionSource_ || !i.capacityVolumeMl)) { return; }
        targets_.push_back(i.id); frame_.choices.push_back(l.ItemName(c, i.type));
    });
    frame_.screen = ItemUiScreen::Target;
}
void ItemUi::QueueCommand(const Command& command) {
    pending_.clear(); pending_.push_back(command); nextCommand_ = 0; requested_ = 0; moved_ = 0;
    frame_.feedback.clear(); frame_.busy = true;
}
void ItemUi::HandleAction(ItemUiAction action, const WorldView& view) {
    if (!IsOpen() || NeedsTime()) { return; }
    if (frame_.screen != ItemUiScreen::Inventory && frame_.screen != ItemUiScreen::Advanced
        && frame_.screen != ItemUiScreen::Examine && frame_.screen != ItemUiScreen::Actions) { return; }
    const auto& pane = frame_.panes[frame_.activePane];
    const bool actionMenu = frame_.screen == ItemUiScreen::Actions;
    const auto id = actionMenu ? actionSource_ : frame_.screen == ItemUiScreen::Examine
        ? examined_ : pane.selectionValid ? selected_[frame_.activePane] : 0;
    const auto* item = FindInventoryEntry(view, id);
    if (!item) { frame_.feedback = "Selected item is no longer accessible."; return; }
    if ((action == ItemUiAction::Regrip || action == ItemUiAction::Release) && item->heldHands == HandMask::None) {
        frame_.feedback = "Selected item is not held."; return;
    }
    actionSource_ = id;
    Command command; command.kind = CommandKind::equipment; command.equipment.item = id;
    command.equipment.hand = item->heldHands == HandMask::Right ? Hand::Right : Hand::Left;
    switch (action) {
    case ItemUiAction::HoldLeft: command.equipment.hand = Hand::Left; break;
    case ItemUiAction::HoldRight: command.equipment.hand = Hand::Right; break;
    case ItemUiAction::TwoHanded: command.equipment.grip = GripKind::TwoHanded; break;
    case ItemUiAction::Regrip:
        command.equipment.operation = EquipmentOperation::Regrip;
        command.equipment.item = 0;
        command.equipment.grip = item->heldHands == HandMask::Both ? GripKind::OneHanded : GripKind::TwoHanded;
        break;
    case ItemUiAction::Wear:
        command.equipment.operation = EquipmentOperation::Wear; command.equipment.hand = Hand::Left; break;
    case ItemUiAction::Remove:
        command.equipment.operation = EquipmentOperation::Remove; command.equipment.hand = Hand::Left; break;
    case ItemUiAction::Release:
        command.equipment.operation = EquipmentOperation::Release; command.equipment.item = 0; break;
    }
    QueueCommand(command);
}
void ItemUi::QueueTransfer(eastl::span<const PersistentId> items, std::size_t count) {
    pending_.clear(); nextCommand_ = 0; moved_ = 0; requested_ = std::min(count, items.size());
    for (std::size_t n = 0; n < requested_; ++n) {
        Command cmd; cmd.kind = CommandKind::transfer_item; cmd.item = items[n];
        cmd.source = amountSource_; cmd.destination = amountDestination_; pending_.push_back(eastl::move(cmd));
    }
    frame_.feedback.clear(); frame_.busy = true;
}
void ItemUi::ConfirmTransferDestination(const WorldView& view) {
    if (frame_.choice >= destinations_.size()) { return; }
    const auto destination = destinations_[frame_.choice];
    if (!NearbyStorage(view, destination, StoragePosition(view, amountSource_))) {
        frame_.feedback = "Selected destination is no longer accessible."; return;
    }
    if (!SameAncestry(view, amountSource_, sourceAncestry_)) {
        frame_.feedback = "Selected source ancestry has changed."; return;
    }
    for (auto id : amountItems_) {
        if (!TransferValid(view, id, amountSource_, destination)) {
            frame_.feedback = "Selected source is no longer accessible."; return;
        }
    }
    amountDestination_ = destination;
    CaptureAncestry(view, amountDestination_, destinationAncestry_);
    if (!transferCount_) {
        frame_.choices.clear();
        frame_.amount.clear(); frame_.prompt = "Quantity (1–" + UnsignedText(amountItems_.size()) + ")";
        frame_.screen = ItemUiScreen::Amount;
    } else {
        QueueTransfer(amountItems_, transferCount_); frame_.screen = returnScreen_;
    }
}
eastl::optional<Command> ItemUi::TakeCommand(const WorldView& v) {
    if (outstanding_ || waitingForReady_ || v.remainingTicks || v.commandQueued || nextCommand_ >= pending_.size()) { return {}; }
    outstanding_ = true; issuedRevision_ = v.revision; issuedTick_ = v.tick;
    const Command command = pending_[nextCommand_++];
    if (command.kind == CommandKind::equipment
        && (command.equipment.operation == EquipmentOperation::Release || command.equipment.operation == EquipmentOperation::Regrip)) {
        const auto* expected = FindInventoryEntry(v, actionSource_);
        const auto hand = command.equipment.hand == Hand::Left ? HandMask::Left : HandMask::Right;
        if (!expected || (static_cast<unsigned>(expected->heldHands) & static_cast<unsigned>(hand)) == 0) {
            CommandResult refused; refused.outcome = Outcome::combat_refused; refused.combatReason = CombatRefusal::MissingSource;
            CommandFinished(refused); return {};
        }
    }
    const auto* item = FindInventoryEntry(v, command.item);
    if (command.kind == CommandKind::transfer_item
        && (!TransferValid(v, command.item, command.source, command.destination)
            || !SameAncestry(v, command.source, sourceAncestry_)
            || !SameAncestry(v, command.destination, destinationAncestry_)
            || (!destinations_.empty() && !NearbyStorage(v, command.destination, StoragePosition(v, command.source))))) {
        CommandResult refused; refused.outcome = Outcome::inventory_refused;
        refused.inventoryOutcome = item ? InventoryOutcome::Inaccessible : InventoryOutcome::InvalidItem;
        CommandFinished(refused); return {};
    }
    return command;
}
void ItemUi::CommandFinished(const CommandResult& result) {
    if (!outstanding_ || result.outcome == Outcome::queued) { return; }
    outstanding_ = false;
    finishedTick_ = result.tick;
    const bool success = result.outcome == Outcome::used_item || result.outcome == Outcome::stored_item
        || result.outcome == Outcome::dropped || result.outcome == Outcome::transferred
        || result.outcome == Outcome::wielded || result.outcome == Outcome::unwielded
        || result.outcome == Outcome::equipment_changed;
    waitingForReady_ = (success || result.advanced) && result.cost != 0;
    if (success) { ++moved_; }
    else { pending_.resize(nextCommand_); }
    frame_.feedback = requested_ ? "Moved " + UnsignedText(moved_) + "/" + UnsignedText(requested_) : String{};
    if (!success) {
        if (!frame_.feedback.empty()) { frame_.feedback += ": "; }
        frame_.feedback += result.outcome == Outcome::combat_refused
            ? CombatRefusalText(result.combatReason) : CommandResultMessage(result);
    }
    if (success && !IsAdvanced()) { Close(); }
    if (nextCommand_ >= pending_.size()) { pending_.clear(); nextCommand_ = 0; }
    frame_.busy = NeedsTime();
}
void ItemUi::CommandRejected() {
    if (!outstanding_) { return; } outstanding_ = false; pending_.clear(); nextCommand_ = 0;
    frame_.feedback = requested_ ? "Moved " + UnsignedText(moved_) + "/" + UnsignedText(requested_) + ": " : String{};
    frame_.feedback += "Command could not be submitted."; frame_.busy = NeedsTime();
}
void ItemUi::Handle(const KeyChord& key, const Keymap& keys, const WorldView& v, const ContentCatalog& c, const Localization& l) {
    Refresh(v, c, l, keys); if (!IsOpen()) { return; }
    const bool numberKey = IsItemUiNumberKey(frame_.screen, key);
    auto a = ResolveAction(keys, InputContext::Menu, key);
    const auto itemAction = ResolveAction(keys, InputContext::Items, key);
    if (itemAction) { a = itemAction; }
    if (numberKey) { a.reset(); }
    if (key == KeyChord{"Escape"}) { a = InputAction::Cancel; }
    if (a == InputAction::Cancel) {
        if (NeedsTime()) {
            pending_.resize(nextCommand_);
            if (!outstanding_) { pending_.clear(); nextCommand_ = 0; }
            frame_.feedback = "Transfer interrupted.";
            if (requested_) { frame_.feedback += " Moved "; frame_.feedback += UnsignedText(moved_); frame_.feedback += '/'; frame_.feedback += UnsignedText(requested_); }
            frame_.busy = NeedsTime();
            return;
        }
        if (frame_.screen == ItemUiScreen::Target || frame_.screen == ItemUiScreen::Direction) {
            if (managing_ || actions_.size() > 1) {
                frame_.screen = ItemUiScreen::Actions; frame_.choices.clear(); frame_.choice = actionChoice_;
                for (const auto& info : actions_) { frame_.choices.push_back(l.ItemActionText(info)); }
                for (auto action : equipmentActions_) { frame_.choices.push_back(ItemUiActionLabel(action)); }
                if (managing_) { frame_.choices.push_back("Store in container"); frame_.choices.push_back("Extract to inventory"); }
            } else { frame_.screen = actionReturn_; }
        } else if (frame_.screen == ItemUiScreen::Actions) { frame_.screen = actionReturn_; }
        else if (frame_.screen != returnScreen_) { frame_.screen = returnScreen_; } else { Close(); } return;
    }
    if (frame_.screen == ItemUiScreen::Amount) {
        if (key.key == "Backspace") { if (!frame_.amount.empty()) { frame_.amount.pop_back(); } return; }
        if (numberKey) { if (frame_.amount.size() < 10) { frame_.amount += key.key; } return; }
        if (a == InputAction::Accept) {
            std::size_t n = 0; auto parsed = std::from_chars(frame_.amount.data(), frame_.amount.data() + frame_.amount.size(), n);
            if (parsed.ec != std::errc{} || !n || n > amountItems_.size()) { frame_.feedback = "Enter a quantity from 1 to " + UnsignedText(amountItems_.size()); return; }
            if (!SameAncestry(v, amountSource_, sourceAncestry_) || !SameAncestry(v, amountDestination_, destinationAncestry_)) {
                frame_.feedback = "Selected storage ancestry has changed."; return;
            }
            if (!destinations_.empty() && !NearbyStorage(v, amountDestination_, StoragePosition(v, amountSource_))) {
                frame_.feedback = "Selected destination is no longer accessible."; return;
            }
            for (std::size_t index = 0; index < n; ++index) {
                const auto id = amountItems_[index];
                if (!TransferValid(v, id, amountSource_, amountDestination_)
                    || c.GetItem(FindInventoryEntry(v, id)->type).unfinishedCraft) {
                    frame_.feedback = "Selected source is no longer accessible."; return;
                }
            }
            QueueTransfer(amountItems_, n); frame_.screen = returnScreen_;
        } return;
    }
    if (outstanding_ || nextCommand_ < pending_.size()) { return; }
    if (frame_.screen == ItemUiScreen::Actions || frame_.screen == ItemUiScreen::Target || frame_.screen == ItemUiScreen::Destination) {
        if (!frame_.choices.empty() && (a == InputAction::North || a == InputAction::South)) {
            frame_.choice = a == InputAction::North ? (frame_.choice + frame_.choices.size() - 1) % frame_.choices.size() : (frame_.choice + 1) % frame_.choices.size();
        } else if (a == InputAction::Accept && !frame_.choices.empty()) {
            if (frame_.screen == ItemUiScreen::Destination) {
                for (auto id : amountItems_) {
                    const auto* item = FindInventoryEntry(v, id);
                    if (!item || c.GetItem(item->type).unfinishedCraft) {
                        frame_.feedback = "Installed or reserved items cannot be transferred."; return;
                    }
                }
                ConfirmTransferDestination(v);
            }
            else if (frame_.screen == ItemUiScreen::Actions) { ChooseAction(v, c, l); }
            else if (FindInventoryEntry(v, actionSource_) && FindInventoryEntry(v, targets_[frame_.choice])) {
                actionCommand_.otherItem = targets_[frame_.choice]; QueueCommand(actionCommand_);
            } else { frame_.feedback = "Selected item is no longer accessible."; }
        } return;
    }
    if (frame_.screen == ItemUiScreen::Direction) {
        Command cmd = actionCommand_;
        if (a == InputAction::North) { cmd.dz = -1; } else if (a == InputAction::South) { cmd.dz = 1; }
        else if (a == InputAction::West) { cmd.dx = -1; } else if (a == InputAction::East) { cmd.dx = 1; }
        else if (a == InputAction::MoveUp) { cmd.dy = 1; } else if (a == InputAction::MoveDown) { cmd.dy = -1; } else { return; }
        if (FindInventoryEntry(v, cmd.item)) { QueueCommand(cmd); } return;
    }
    auto& pane = frame_.panes[frame_.activePane];
    if (frame_.screen == ItemUiScreen::Examine) {
        if (a == InputAction::North) { frame_.examination.scroll = std::max(0, frame_.examination.scroll - 1); return; }
        if (a == InputAction::South) { ++frame_.examination.scroll; return; }
        if (a != InputAction::Use && a != InputAction::Apply && a != InputAction::Wield && a != InputAction::Drop) { return; }
    } else {
        if (numberKey) {
            const int n = key.key[0] - '1'; auto pos = v.player_position; pos.x += n % 3 - 1; pos.z += 1 - n / 3;
            if (pane.mode == ItemUiPaneMode::Storage && StoragePosition(v, pane.storage) == pos) { SwapPanes(); }
            else { SelectStorage(DefaultTileStorage(v, pos)); }
            return;
        }
        if (IsAdvanced() && (a == InputAction::West || a == InputAction::East)) { frame_.activePane = a == InputAction::East ? 1 : 0; return; }
        if (IsAdvanced() && (a == InputAction::Next || a == InputAction::Previous)) { return; }
        if (IsAdvanced() && a == InputAction::Inventory) { SelectStorage({}); return; }
        if (IsAdvanced() && a == InputAction::AllNearby) { SelectAllNearby(); return; }
        if (IsAdvanced() && a == InputAction::ToggleStorage) {
            const auto source = pane.mode == ItemUiPaneMode::AllNearby && pane.selectionValid
                ? pane.rows[pane.selected].storage : pane.storage;
            if (source.kind != ItemStorageKind::Ground && source.kind != ItemStorageKind::Object) {
                frame_.feedback = "Select a tile before toggling floor/object storage."; return;
            }
            const auto position = StoragePosition(v, source);
            if (!position) { frame_.feedback = "Storage is no longer accessible."; return; }
            if (source.kind == ItemStorageKind::Object) {
                SelectStorage({ItemStorageKind::Ground, *position, 0});
            } else {
                const auto objectStorage = DefaultTileStorage(v, *position);
                if (objectStorage.kind == ItemStorageKind::Object) { SelectStorage(objectStorage); }
                else { frame_.feedback = "No accessible object storage on that tile."; }
            } return;
        }
        if (IsAdvanced() && a == InputAction::ParentStorage && pane.mode == ItemUiPaneMode::Storage && pane.storage.kind == ItemStorageKind::Container) {
            if (const auto* i = FindInventoryEntry(v, pane.storage.id)) { SelectStorage(i->storage); } else { frame_.feedback = "Storage is no longer accessible."; } return;
        }
        if ((a == InputAction::North || a == InputAction::South) && !pane.rows.empty()) {
            pane.selected = a == InputAction::North ? (std::min(pane.selected, pane.rows.size()) + pane.rows.size() - 1) % pane.rows.size() : (pane.selected + 1) % pane.rows.size();
            selected_[frame_.activePane] = pane.rows[pane.selected].items.front(); pane.selectionValid = true; selectionLost_[frame_.activePane] = false; return;
        }
    }
    const auto id = frame_.screen == ItemUiScreen::Examine ? examined_ : selected_[frame_.activePane];
    const auto* i = FindInventoryEntry(v, id);
    if (!i || (frame_.screen != ItemUiScreen::Examine && !pane.selectionValid)) { frame_.feedback = "Selected item is no longer accessible."; return; }
    if (a == InputAction::Use || (a == InputAction::Accept && useSelection_ && !IsAdvanced())) { BeginActions(id, false, v, c, l); return; }
    if (a == InputAction::Apply) { BeginActions(id, true, v, c, l); return; }
    if (a == InputAction::Examine || (a == InputAction::Accept && !IsAdvanced())) { Examine(id, v, c, l); return; }
    if (IsAdvanced() && a == InputAction::OpenContainer) {
        if (i->capacityVolumeMl) { SelectStorage({ItemStorageKind::Container, {}, id}); } else { frame_.feedback = "Item has no portable storage."; } return;
    }
    if (a == InputAction::Wield || a == InputAction::Drop) {
        if (a == InputAction::Drop) {
            bool carried = false; for (const auto& entry : v.inventory) { carried |= entry.id == id && entry.parent == 0; }
            if (!carried) { frame_.feedback = "Only directly carried items can be dropped."; return; }
        }
        Command cmd; cmd.kind = a == InputAction::Wield ? CommandKind::wield_item : CommandKind::drop; cmd.item = a == InputAction::Wield && i->wielded ? 0 : id;
        QueueCommand(cmd); return;
    }
    if (IsAdvanced() && (a == InputAction::Accept || a == InputAction::TransferAmount || a == InputAction::TransferStack)) {
        amountSource_ = i->storage; amountDestination_ = frame_.panes[1 - frame_.activePane].storage;
        destinations_.clear();
        CaptureAncestry(v, amountSource_, sourceAncestry_);
        CaptureAncestry(v, amountDestination_, destinationAncestry_);
        if (i->installation.value || c.GetItem(i->type).unfinishedCraft) { frame_.feedback = "Installed or reserved items cannot be transferred."; return; }
        auto dest = frame_.panes[1 - frame_.activePane].mode == ItemUiPaneMode::Storage ? amountDestination_ : ItemStorage{};
        for (std::size_t depth = 0; dest.kind == ItemStorageKind::Container && depth <= v.inventory.size() + Nearby(v).size(); ++depth) {
            if (dest.id == id) { frame_.feedback = "Cannot move an item into itself or its descendants."; return; }
            const auto* ancestor = FindInventoryEntry(v, dest.id); if (!ancestor) { break; } dest = ancestor->storage;
        }
        const auto& row = pane.rows[pane.selected];
        amountItems_.clear();
        transferCount_ = a == InputAction::Accept ? 1 : a == InputAction::TransferAmount ? 0 : row.items.size();
        if (a == InputAction::TransferStack) { amountItems_.assign(row.items.begin(), row.items.end()); }
        else {
            amountItems_.push_back(id);
            if (a == InputAction::TransferAmount) {
                for (auto member : row.items) { if (member != id) { amountItems_.push_back(member); } }
            }
        }
        if (frame_.panes[1 - frame_.activePane].mode == ItemUiPaneMode::AllNearby) {
            CollectNearbyStorages(v, amountSource_, destinations_);
            frame_.choices.clear(); frame_.choice = 0;
            for (const auto destination : destinations_) {
                frame_.choices.push_back(LocationTitle(v, destination, SourceName(v, destination)));
            }
            if (destinations_.empty()) { frame_.feedback = "No accessible destination storage nearby."; return; }
            frame_.prompt = "Choose destination"; frame_.screen = ItemUiScreen::Destination; return;
        }
        if (a == InputAction::TransferAmount) {
            frame_.choices.clear();
            frame_.amount.clear(); frame_.prompt = "Quantity (1–" + UnsignedText(row.items.size()) + ")"; frame_.screen = ItemUiScreen::Amount;
        }
        else { QueueTransfer(amountItems_, transferCount_); }
    }
}
}
