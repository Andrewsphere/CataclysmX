#pragma once

#include "client/item_ui.h"
#include "core/debug_items.h"

namespace cataclysmx {

enum class ItemBrowserScreen { Closed, List, Search, Examine, Amount };
struct ItemBrowserFrame {
    ItemBrowserScreen screen = ItemBrowserScreen::Closed;
    Vector<ItemTypeId> matches; // Stable catalog-ID order; internal wrappers omitted.
    std::size_t selected = 0;
    int scroll = 0;
    String query; // Committed search; input is the editable draft while searching.
    String input;
    String feedback;
    bool busy = false;
    ItemUiFrame examination; // Reuses the ordinary item sheet and immutable art.
};

// Catalog-only debug workflow. Frontends route committed text separately from
// key chords, then dispatch TakeGiveRequest on their authoritative owner thread.
class ItemBrowser {
public:
    void Open(const ContentCatalog& content, const Localization& localization);
    void Close();
    bool IsOpen() const;
    bool EditingText() const;
    void Handle(const KeyChord& key, const Keymap& keys, const ContentCatalog& content,
        const Localization& localization);
    void Text(eastl::string_view text);
    eastl::optional<DebugItemGrant> TakeGiveRequest();
    void GiveFinished(DebugGiveResult result);
    void GiveRejected(eastl::string_view reason);
    const String& Label(ItemTypeId type) const;
    ItemBrowserFrame& Frame();
    const ItemBrowserFrame& Frame() const;

private:
    ItemBrowserFrame frame_;
    Vector<String> labels_; // One localized name + stable ID per catalog entry.
    Vector<ItemTypeId> types_;
    eastl::optional<DebugItemGrant> pending_;
    bool outstanding_ = false;
    void Filter(eastl::string_view query);
    void QueueGive(std::uint32_t count);
    void Examine(const ContentCatalog& content, const Localization& localization);
};

}
