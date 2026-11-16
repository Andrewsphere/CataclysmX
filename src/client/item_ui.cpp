#include "client/item_ui.h"
#include "client/text.h"
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
eastl::optional<TileCoord> StoragePosition(const WorldView& view, ItemStorage storage) {
    if (storage.kind == ItemStorageKind::Ground) { return storage.position; }
    if (storage.kind == ItemStorageKind::Object) {
        if (const auto* source = Storage(view, storage)) { return source->objectPosition; }
    }
    return {};
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
    row.type = item.type; row.name = loc.ItemName(content, item.type);
    row.items.push_back(item.id); row.massGrams = item.massGrams; row.volumeMl = item.volumeMl;
    row.depth = depth; row.wielded = item.wielded;
}
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
    if (i.wielded) { out.stats.push_back("Currently wielded"); }
    const auto& melee = ItemMelee(def);
    constexpr const char* names[] = {"Blunt", "Slashing", "Piercing", "Ballistic", "Heat", "Chemical", "Choking"};
    for (const auto& d : melee.damage) {
        String s = names[static_cast<unsigned>(d.type)]; s += " damage: "; s += Hundredths(d.amount);
        s += " HP | Penetration: "; s += Hundredths(d.penetration);
        s += " HP | Normal player damage: "; s += Hundredths(d.amount); s += " HP"; out.stats.push_back(eastl::move(s));
    }
    out.stats.push_back("Attack cost: " + UnsignedText(melee.attackCost) + " ticks | Hands: " + UnsignedText(melee.hands));
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
    if (advanced_ != advanced) { initialized_ = false; }
    if (!outstanding_ && pending_.empty()) { moved_ = 0; requested_ = 0; }
    advanced_ = advanced; useSelection_ = use; frame_.screen = advanced ? ItemUiScreen::Advanced : ItemUiScreen::Inventory;
    returnScreen_ = frame_.screen; frame_.feedback.clear(); opening_ = true;
}
void ItemUi::Close() {
    frame_.screen = ItemUiScreen::Closed;
    if (outstanding_) { pending_.resize(nextCommand_); } else { pending_.clear(); nextCommand_ = 0; }
    frame_.busy = NeedsTime();
}
bool ItemUi::IsOpen() const { return frame_.screen != ItemUiScreen::Closed; }
bool ItemUi::IsAdvanced() const { return advanced_; }
bool ItemUi::NeedsTime() const { return outstanding_ || waitingForReady_ || nextCommand_ < pending_.size(); }
ItemUiFrame& ItemUi::Frame() { return frame_; }
const ItemUiFrame& ItemUi::Frame() const { return frame_; }
void ItemUi::SelectStorage(ItemStorage s) {
    const unsigned a = frame_.activePane, b = 1 - a;
    if (frame_.panes[b].storage == s) {
        eastl::swap(frame_.panes[a], frame_.panes[b]); eastl::swap(selected_[a], selected_[b]); eastl::swap(selectionLost_[a], selectionLost_[b]);
    } else if (!(frame_.panes[a].storage == s)) {
        frame_.panes[a] = {}; frame_.panes[a].storage = s; selected_[a] = 0; selectionLost_[a] = false;
    }
}
void ItemUi::Refresh(const WorldView& v, const ContentCatalog& content, const Localization& loc, const Keymap& keys) {
    if (initialized_ && player_ != v.playerId) {
        if (!opening_) { Close(); }
        outstanding_ = false; waitingForReady_ = false; pending_.clear(); nextCommand_ = 0;
        selected_ = {}; selectionLost_ = {}; initialized_ = false;
    }
    opening_ = false;
    player_ = v.playerId;
    if (waitingForReady_ && !outstanding_ && !v.remainingTicks && !v.commandQueued && v.tick >= finishedTick_
        && (v.revision != issuedRevision_ || v.tick != issuedTick_)) { waitingForReady_ = false; }
    if (!IsOpen()) { return; }
    if (!initialized_) {
        frame_.panes = {}; selected_ = {}; selectionLost_ = {}; frame_.activePane = 0;
        frame_.panes[1].storage = DefaultTileStorage(v, v.player_position); initialized_ = true;
    }
    for (unsigned p = 0; p < (advanced_ ? 2u : 1u); ++p) {
        auto& pane = frame_.panes[p]; std::size_t rowCount = 0;
        const auto* meta = Storage(v, pane.storage);
        pane.title = pane.storage.kind == ItemStorageKind::Inventory ? "Inventory" : pane.storage.kind == ItemStorageKind::Ground ? "Floor" : "Storage";
        if (meta && !meta->name.empty()) { pane.title = meta->name; }
        if (pane.storage.kind == ItemStorageKind::Container) {
            if (const auto* item = FindInventoryEntry(v, pane.storage.id)) { pane.title = loc.ItemName(content, item->type); }
        }
        pane.title = LocationTitle(v, pane.storage, eastl::move(pane.title));
        pane.capacity.clear();
        if (meta) {
            pane.capacity = ItemMassText(meta->massGrams);
            if (meta->massLimitGrams) { pane.capacity += " / "; pane.capacity += ItemMassText(*meta->massLimitGrams); }
            pane.capacity += " | "; pane.capacity += ItemVolumeText(meta->volumeMl);
            if (meta->volumeLimitMl) { pane.capacity += " / "; pane.capacity += ItemVolumeText(*meta->volumeLimitMl); }
            if (!meta->accessible) { pane.capacity += " — "; pane.capacity += InventoryOutcomeMessage(meta->reason); }
        }
        const auto add = [&](const InventoryEntry& i, unsigned depth) {
            if (i.stackable && !i.wielded) {
                for (std::size_t n = 0; n < rowCount; ++n) {
                    auto& row = pane.rows[n];
                    const auto* first = FindInventoryEntry(v, row.items.front());
                    if (first && first->stackable && !row.wielded && row.type == i.type && first->storage == i.storage && row.depth == depth) {
                        row.items.push_back(i.id); std::sort(row.items.begin(), row.items.end()); row.massGrams += i.massGrams; row.volumeMl += i.volumeMl; return;
                    }
                }
            }
            AddRow(pane, rowCount, i, depth, content, loc);
        };
        if (advanced_) { Each(v, [&](const InventoryEntry& i) { if (i.storage == pane.storage) { add(i, 0); } }); }
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
    frame_.title = advanced_ ? "Advanced inventory" : "Inventory";
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
    if (frame_.screen == ItemUiScreen::Amount || frame_.screen == ItemUiScreen::Actions || frame_.screen == ItemUiScreen::Target || frame_.screen == ItemUiScreen::Direction) {
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
    hint(InputContext::Menu, InputAction::Accept, advanced_ ? "move one" : useSelection_ ? "use" : "examine");
    hint(InputContext::Items, InputAction::Examine, "examine");
    if (advanced_) {
        hint(InputContext::Items, InputAction::TransferAmount, "amount"); hint(InputContext::Items, InputAction::TransferStack, "stack");
        hint(InputContext::Menu, InputAction::Next, "pane"); hint(InputContext::Items, InputAction::Inventory, "inventory");
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
    actionSource_ = id; managing_ = manage; actions_.clear(); frame_.choices.clear(); frame_.choice = 0;
    for (const auto& a : i->actions) {
        if (a.category != (manage ? ItemActionCategory::ManageContents : ItemActionCategory::Operate)) { continue; }
        actions_.push_back(a);
        String label = l.ItemActionText(a) + " | " + UnsignedText(a.baseCost) + " ticks";
        if (a.availability != ItemUseOutcome::Succeeded) { label += " — "; label += l.ItemRefusal(a.availability, a.reason); }
        frame_.choices.push_back(eastl::move(label));
    }
    if (manage) { frame_.choices.push_back("Store in container"); frame_.choices.push_back("Extract to inventory"); }
    if (frame_.choices.empty()) { frame_.feedback = "No functional actions available."; return; }
    frame_.screen = ItemUiScreen::Actions;
    if (!manage && actions_.size() == 1) { frame_.screen = actionReturn_; ChooseAction(v, c, l); }
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
void ItemUi::QueueTransfer(const ItemUiRow& row, std::size_t count) {
    pending_.clear(); nextCommand_ = 0; moved_ = 0; requested_ = std::min(count, row.items.size());
    for (std::size_t n = 0; n < requested_; ++n) {
        Command cmd; cmd.kind = CommandKind::transfer_item; cmd.item = row.items[n];
        cmd.source = amountSource_; cmd.destination = amountDestination_; pending_.push_back(eastl::move(cmd));
    }
    frame_.feedback.clear(); frame_.busy = true;
}
eastl::optional<Command> ItemUi::TakeCommand(const WorldView& v) {
    if (outstanding_ || waitingForReady_ || v.remainingTicks || v.commandQueued || nextCommand_ >= pending_.size()) { return {}; }
    outstanding_ = true; issuedRevision_ = v.revision; issuedTick_ = v.tick;
    const Command command = pending_[nextCommand_++];
    const auto* item = FindInventoryEntry(v, command.item);
    if (command.kind == CommandKind::transfer_item && (!item || !(item->storage == command.source) || item->installation.value)) {
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
        || result.outcome == Outcome::wielded || result.outcome == Outcome::unwielded;
    waitingForReady_ = (success || result.advanced) && result.cost != 0;
    if (success) { ++moved_; }
    else { pending_.resize(nextCommand_); }
    frame_.feedback = requested_ ? "Moved " + UnsignedText(moved_) + "/" + UnsignedText(requested_) : String{};
    if (!success) { if (!frame_.feedback.empty()) { frame_.feedback += ": "; } frame_.feedback += CommandResultMessage(result); }
    if (success && !advanced_) { Close(); }
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
    auto a = ResolveAction(keys, InputContext::Menu, key);
    const auto itemAction = ResolveAction(keys, InputContext::Items, key);
    if (itemAction) { a = itemAction; }
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
                if (managing_) { frame_.choices.push_back("Store in container"); frame_.choices.push_back("Extract to inventory"); }
            } else { frame_.screen = actionReturn_; }
        } else if (frame_.screen == ItemUiScreen::Actions) { frame_.screen = actionReturn_; }
        else if (frame_.screen != returnScreen_) { frame_.screen = returnScreen_; } else { Close(); } return;
    }
    if (frame_.screen == ItemUiScreen::Amount) {
        if (key.key == "Backspace") { if (!frame_.amount.empty()) { frame_.amount.pop_back(); } return; }
        if (!key.ctrl && !key.alt && key.key.size() == 1 && key.key[0] >= '0' && key.key[0] <= '9') { if (frame_.amount.size() < 10) { frame_.amount += key.key; } return; }
        if (a == InputAction::Accept) {
            std::size_t n = 0; auto parsed = std::from_chars(frame_.amount.data(), frame_.amount.data() + frame_.amount.size(), n);
            if (parsed.ec != std::errc{} || !n || n > amountItems_.size()) { frame_.feedback = "Enter a quantity from 1 to " + UnsignedText(amountItems_.size()); return; }
            ItemUiRow row; row.items = amountItems_; QueueTransfer(row, n); frame_.screen = returnScreen_;
        } return;
    }
    if (outstanding_ || nextCommand_ < pending_.size()) { return; }
    if (frame_.screen == ItemUiScreen::Actions || frame_.screen == ItemUiScreen::Target) {
        if (!frame_.choices.empty() && (a == InputAction::North || a == InputAction::South)) {
            frame_.choice = a == InputAction::North ? (frame_.choice + frame_.choices.size() - 1) % frame_.choices.size() : (frame_.choice + 1) % frame_.choices.size();
        } else if (a == InputAction::Accept && !frame_.choices.empty()) {
            if (frame_.screen == ItemUiScreen::Actions) { ChooseAction(v, c, l); }
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
        if (advanced_ && (a == InputAction::Next || a == InputAction::Previous)) { frame_.activePane = 1 - frame_.activePane; return; }
        if (advanced_ && a == InputAction::Inventory) { SelectStorage({}); return; }
        if (advanced_ && key.key.size() == 1 && key.key[0] >= '1' && key.key[0] <= '9' && !key.ctrl && !key.alt) {
            const int n = key.key[0] - '1'; auto pos = v.player_position; pos.x += n % 3 - 1; pos.z += 1 - n / 3;
            SelectStorage(DefaultTileStorage(v, pos)); return;
        }
        if (advanced_ && a == InputAction::ToggleStorage) {
            if (pane.storage.kind != ItemStorageKind::Ground && pane.storage.kind != ItemStorageKind::Object) {
                frame_.feedback = "Select a tile before toggling floor/object storage."; return;
            }
            const auto position = StoragePosition(v, pane.storage);
            if (!position) { frame_.feedback = "Storage is no longer accessible."; return; }
            if (pane.storage.kind == ItemStorageKind::Object) {
                SelectStorage({ItemStorageKind::Ground, *position, 0});
            } else {
                const auto source = DefaultTileStorage(v, *position);
                if (source.kind == ItemStorageKind::Object) { SelectStorage(source); }
                else { frame_.feedback = "No accessible object storage on that tile."; }
            } return;
        }
        if (advanced_ && a == InputAction::ParentStorage && pane.storage.kind == ItemStorageKind::Container) {
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
    if (a == InputAction::Use || (a == InputAction::Accept && useSelection_ && !advanced_)) { BeginActions(id, false, v, c, l); return; }
    if (a == InputAction::Apply) { BeginActions(id, true, v, c, l); return; }
    if (a == InputAction::Examine || (a == InputAction::Accept && !advanced_)) { Examine(id, v, c, l); return; }
    if (advanced_ && a == InputAction::OpenContainer) {
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
    if (advanced_ && (a == InputAction::Accept || a == InputAction::TransferAmount || a == InputAction::TransferStack)) {
        amountSource_ = pane.storage; amountDestination_ = frame_.panes[1 - frame_.activePane].storage;
        if (i->installation.value || c.GetItem(i->type).unfinishedCraft) { frame_.feedback = "Installed or reserved items cannot be transferred."; return; }
        auto dest = amountDestination_;
        for (std::size_t depth = 0; dest.kind == ItemStorageKind::Container && depth <= v.inventory.size() + Nearby(v).size(); ++depth) {
            if (dest.id == id) { frame_.feedback = "Cannot move an item into itself or its descendants."; return; }
            const auto* ancestor = FindInventoryEntry(v, dest.id); if (!ancestor) { break; } dest = ancestor->storage;
        }
        const auto& row = pane.rows[pane.selected];
        if (a == InputAction::TransferAmount) {
            amountItems_.clear(); amountItems_.push_back(id);
            for (auto member : row.items) { if (member != id) { amountItems_.push_back(member); } }
            frame_.amount.clear(); frame_.prompt = "Quantity (1–" + UnsignedText(row.items.size()) + ")"; frame_.screen = ItemUiScreen::Amount;
        }
        else { ItemUiRow captured = row; if (a == InputAction::Accept) { captured.items = {id}; } QueueTransfer(captured, a == InputAction::Accept ? 1 : row.items.size()); }
    }
}
}
