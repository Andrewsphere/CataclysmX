#pragma once

#include "client/localization.h"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>

namespace cataclysmx {

struct GameplayPresentation {
    String session;
    String message;
    String pending;
    bool interacting = false;
    bool enhancedKeyboard = false;
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
    ftxui::Element Render(const WorldView& view, const ContentCatalog& content,
        const Localization& localization, const Keymap& keys, const GameplayPresentation& presentation);
    // Consumes every event while open. Submit returns true only when accepted;
    // failures keep Inventory open. The application owns routing and timing.
    // Quick actions open UI-owned windows directly; other choices close the
    // modal and call Activate once. Omitting Activate leaves those choices open.
    bool HandleEvent(const ftxui::Event& event, const WorldView& view, const Localization& localization,
        const Keymap& keys, const std::function<bool(Command)>& submit,
        const std::function<void(InputAction)>& activate = {}, const ContentCatalog* content = nullptr);

private:
    enum class Modal { None, Inventory, Players, Controls, QuickActions };
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
