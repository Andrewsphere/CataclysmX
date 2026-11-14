#pragma once

#include "client/localization.h"
#include "client/message_history.h"
#include "client/crafting_menu.h"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <atomic>
#include <memory>

namespace cataclysmx {

struct TerminalMapCache;

// At most one timer wakeup is outstanding; input events are never coalesced.
class TerminalTimerWakeup {
public:
    bool TryPost() { return !pending_.exchange(true); }
    void Consumed() { pending_.store(false); }
private:
    std::atomic<bool> pending_ = false;
};

struct GameplayPresentation {
    String session;
    String message;
    const MessageHistory* history = nullptr;
    String pending;
    bool interacting = false;
    bool enhancedKeyboard = false;
    // Display preference only; never limits authoritative perception or LAN snapshots.
    int viewRadius = 64;
    // Optional LAN content, already converted at the application boundary.
    ftxui::Element lan;
};

// Frontend state only. Views are snapshots; selection retains IDs, never entities.
// Rendered elements must be drawn before the next call or destruction of this UI.
class GameplayUi {
public:
    bool Open(InputAction action);
    bool IsOpen() const;
    void Close();
    bool PausesLocal(eastl::optional<InputAction> action) const;
    // Temporary snapshots transfer their cells into the node; lvalues are copied.
    ftxui::Element Render(WorldView view, const ContentCatalog& content,
        const Localization& localization, const Keymap& keys, const GameplayPresentation& presentation);
    // First preparation / source changes / changed previously cached visible cells.
    // Newly exposed cells on resize are not counted as a changed map.
    std::uint64_t MapPreparations() const;
    std::size_t MapRasterizedCells() const;
    // Appearance resolutions in the last draw; always bounded by rasterized cells.
    std::size_t MapResolvedCells() const;
    // Consumes every event while open. Submit returns true only when accepted;
    // failures keep Inventory open. The application owns routing and timing.
    // Quick actions open UI-owned windows directly; other choices close the
    // modal and call Activate once. Omitting Activate leaves those choices open.
    bool HandleEvent(const ftxui::Event& event, const WorldView& view, const Localization& localization,
        const Keymap& keys, const std::function<bool(Command)>& submit,
        const std::function<void(InputAction)>& activate = {}, const ContentCatalog* content = nullptr);

private:
    std::shared_ptr<TerminalMapCache> mapCache_;
    enum class Modal { None, Inventory, Players, Controls, QuickActions, Crafting };
    Vector<CraftingMenuRow> craftingRows_;
    eastl::optional<Command> craftingSelection_;
    std::size_t craftingIndex_ = 0;
    int craftingScroll_ = 0;
    enum class ItemStage { Browse, Actions, Item, Container, Direction };
    ItemStage itemStage_ = ItemStage::Browse;
    bool useSelection_ = false;
    bool managingContents_ = false;
    PersistentId sourceItem_ = 0;
    Vector<ItemActionInfo> itemActions_;
    Vector<PersistentId> itemTargets_;
    std::size_t itemChoice_ = 0;
    Command itemCommand_;
    void ReconcileInventory(const WorldView& view);
    Modal modal_ = Modal::None;
    PersistentId selectionPlayer_ = 0;
    PersistentId selectedItem_ = 0;
    std::size_t selectedIndex_ = 0;
    int inventoryScroll_ = 0;
    int playersScroll_ = 0;
    int controlsScroll_ = 0;
    int quickActionsScroll_ = 0;
    std::size_t quickActionIndex_ = 0;
    String feedback_;
};

String GameplayWelcome(const Localization& localization, const Keymap& keys);

}
