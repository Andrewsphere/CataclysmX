#pragma once

#include "client/localization.h"
#include <EASTL/array.h>

namespace cataclysmx {

struct ItemUiRow {
    ItemTypeId type{};
    ItemStorage storage; // Exact immediate source, including in aggregate panes.
    String name;
    Vector<PersistentId> items; // Stable ascending identities captured by grouping.
    std::uint64_t massGrams = 0;
    std::uint64_t volumeMl = 0;
    unsigned depth = 0;
    bool wielded = false;
    bool worn = false;
    HandMask heldHands = HandMask::None;
};
enum class ItemUiPaneMode { Storage, AllNearby };
struct ItemUiPane {
    ItemUiPaneMode mode = ItemUiPaneMode::Storage;
    // Only meaningful in Storage mode; aggregates never become engine storages.
    ItemStorage storage;
    String title;
    String capacity;
    Vector<ItemUiRow> rows;
    std::size_t selected = 0;
    int scroll = 0;
    bool selectionValid = false;
};
struct ItemExamination {
    ItemTypeId type{};
    String name;
    String description;
    // Borrows only immutable catalog data, whose lifetime contains the frontend.
    const TextArt* art = nullptr;
    Vector<String> stats;
    int scroll = 0;
};
enum class ItemUiScreen { Closed, Inventory, Advanced, Examine, Amount, Actions, Target, Direction, Destination };
// Direct frontend button/key actions; legacy InputAction::Wield remains supported.
enum class ItemUiAction { HoldLeft, HoldRight, TwoHanded, Regrip, Wear, Remove, Release };
const char* ItemUiActionLabel(ItemUiAction action);
struct ItemUiFrame {
    ItemUiScreen screen = ItemUiScreen::Closed;
    eastl::array<ItemUiPane, 2> panes;
    unsigned activePane = 0;
    ItemExamination examination;
    Vector<String> choices;
    std::size_t choice = 0;
    String title;
    String prompt;
    String amount;
    String feedback;
    String footer;
    bool busy = false;
};

// Shared keyboard workflow for both frontends. Owns only snapshot values and
// persistent identities. Rendering may update the frame's scroll offsets only.
class ItemUi {
public:
    void Open(bool advanced = false, bool useSelection = false);
    void Close();
    bool IsOpen() const;
    bool IsAdvanced() const;
    // Includes committed work and an interruptible batch. While true local
    // frontends allow ordinary simulation time, even with the window visible.
    bool NeedsTime() const;
    void Refresh(const WorldView& view, const ContentCatalog& content,
        const Localization& localization, const Keymap& keys);
    void Handle(const KeyChord& key, const Keymap& keys, const WorldView& view,
        const ContentCatalog& content, const Localization& localization);
    void HandleAction(ItemUiAction action, const WorldView& view);
    // Call after input and fresh snapshots. At most one request is outstanding.
    // A batch's next concrete item is issued only at a new ready opportunity.
    eastl::optional<Command> TakeCommand(const WorldView& view);
    // Every admitted request needs its authoritative final result (not queued).
    void CommandFinished(const CommandResult& result);
    void CommandRejected();
    ItemUiFrame& Frame();
    const ItemUiFrame& Frame() const;

private:
    ItemUiFrame frame_;
    bool advanced_ = false;
    bool useSelection_ = false;
    bool outstanding_ = false;
    bool waitingForReady_ = false;
    PersistentId player_ = 0;
    eastl::array<PersistentId, 2> selected_{};
    eastl::array<bool, 2> selectionLost_{};
    ItemUiScreen returnScreen_ = ItemUiScreen::Inventory;
    PersistentId examined_ = 0;
    Vector<Command> pending_;
    std::size_t nextCommand_ = 0;
    std::size_t moved_ = 0;
    std::size_t requested_ = 0;
    ItemActionList actions_;
    Vector<ItemUiAction> equipmentActions_;
    Vector<PersistentId> targets_;
    Command actionCommand_;
    PersistentId actionSource_ = 0;
    bool managing_ = false;
    Vector<PersistentId> amountItems_;
    ItemStorage amountSource_;
    ItemStorage amountDestination_;
    Vector<ItemStorage> nearbyStorages_; // Reused aggregate refresh scratch.
    Vector<ItemStorage> destinations_;
    Vector<ItemStorage> sourceAncestry_;
    Vector<ItemStorage> destinationAncestry_;
    // Zero requests a quantity prompt after choosing a concrete destination.
    std::size_t transferCount_ = 0;
    bool initialized_ = false;
    bool opening_ = false;
    std::uint64_t issuedRevision_ = 0;
    std::uint64_t issuedTick_ = 0;
    std::uint64_t finishedTick_ = 0;
    ItemUiScreen actionReturn_ = ItemUiScreen::Inventory;
    std::size_t actionChoice_ = 0;
    void SelectStorage(ItemStorage storage);
    void SelectAllNearby();
    void ConfirmTransferDestination(const WorldView& view);
    void QueueCommand(const Command& command);
    void QueueTransfer(eastl::span<const PersistentId> items, std::size_t count);
    void Examine(PersistentId id, const WorldView& view, const ContentCatalog& content,
        const Localization& localization);
    void BeginActions(PersistentId id, bool manage, const WorldView& view,
        const ContentCatalog& content, const Localization& localization);
    void ChooseAction(const WorldView& view, const ContentCatalog& content,
        const Localization& localization);
};

String ItemMassText(std::uint64_t grams);
String ItemVolumeText(std::uint64_t milliliters);
const InventoryEntry* FindInventoryEntry(const WorldView& view, PersistentId id);
void DescribeInventoryItem(const InventoryEntry& item, const ContentCatalog& content,
    const Localization& localization, ItemExamination& output);

}
