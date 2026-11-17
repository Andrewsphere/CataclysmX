#include "client/item_browser.h"

#include <EASTL/algorithm.h>
#include <EASTL/utility.h>
#include <cstdlib>

namespace cataclysmx {
namespace {

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
unsigned char Fold(unsigned char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }

bool Matches(eastl::string_view label, eastl::string_view query) {
    if (query.size() > label.size()) { return false; }
    for (std::size_t start = 0; start <= label.size() - query.size(); ++start) {
        std::size_t index = 0;
        while (index < query.size() && Fold(label[start + index]) == Fold(query[index])) { ++index; }
        if (index == query.size()) { return true; }
    }
    return false;
}

bool PrintableText(eastl::string_view text) {
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset]);
        const std::size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        if (bytes > text.size() - offset || !ValidKey({String(text.data() + offset, bytes)})) { return false; }
        offset += bytes;
    }
    return true;
}

void EraseLastScalar(String& text) {
    if (text.empty()) { return; }
    std::size_t start = text.size() - 1;
    while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) { --start; }
    text.resize(start);
}

eastl::optional<std::uint32_t> Count(eastl::string_view text) {
    if (text.empty()) { return {}; }
    std::uint32_t count = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') { return {}; }
        const auto digit = static_cast<std::uint32_t>(ch - '0');
        if (count > (maxDebugGiveItems - digit) / 10) { return {}; }
        count = count * 10 + digit;
    }
    return count ? eastl::optional<std::uint32_t>(count) : eastl::nullopt;
}

}

void ItemBrowser::Open(const ContentCatalog& content, const Localization& localization) {
    pending_.reset();
    frame_ = {};
    frame_.busy = outstanding_;
    frame_.screen = ItemBrowserScreen::List;
    labels_.clear(); labels_.resize(content.ItemCount());
    types_.clear(); types_.reserve(content.ItemCount());
    for (std::size_t i = 0; i < content.ItemCount(); ++i) {
        const ItemTypeId type{static_cast<std::uint32_t>(i + 1)};
        const auto& definition = content.GetItem(type);
        if (definition.unfinishedCraft || type == content.CorpseItem()) { continue; }
        labels_[i] = localization.ItemName(content, type) + " [" + definition.id + "]";
        types_.push_back(type);
    }
    Filter({});
}

void ItemBrowser::Close() {
    pending_.reset();
    frame_.screen = ItemBrowserScreen::Closed;
    frame_.busy = outstanding_;
}
bool ItemBrowser::IsOpen() const { return frame_.screen != ItemBrowserScreen::Closed; }
bool ItemBrowser::EditingText() const {
    return frame_.screen == ItemBrowserScreen::Search || frame_.screen == ItemBrowserScreen::Amount;
}
ItemBrowserFrame& ItemBrowser::Frame() { return frame_; }
const ItemBrowserFrame& ItemBrowser::Frame() const { return frame_; }
const String& ItemBrowser::Label(ItemTypeId type) const {
    if (!type.value || type.value > labels_.size()) { std::abort(); }
    return labels_[type.value - 1];
}

void ItemBrowser::Filter(eastl::string_view query) {
    const auto old = frame_.selected < frame_.matches.size() ? frame_.matches[frame_.selected] : ItemTypeId{};
    frame_.matches.clear();
    frame_.selected = 0; frame_.scroll = 0;
    for (const auto type : types_) {
        if (!Matches(View(Label(type)), query)) { continue; }
        if (type == old) { frame_.selected = frame_.matches.size(); }
        frame_.matches.push_back(type);
    }
}

void ItemBrowser::Text(eastl::string_view text) {
    if (!EditingText() || text.empty()) { return; }
    const std::size_t limit = frame_.screen == ItemBrowserScreen::Search ? 128 : 16;
    if (!PrintableText(text) || text.size() > limit - frame_.input.size()) {
        frame_.feedback = "Input is too long or contains unsupported control characters.";
        return;
    }
    frame_.input.append(text.data(), text.size());
    frame_.feedback.clear();
    if (frame_.screen == ItemBrowserScreen::Search) { Filter(View(frame_.input)); }
}

void ItemBrowser::QueueGive(std::uint32_t count) {
    if (frame_.busy) { return; }
    if (frame_.selected >= frame_.matches.size()) { frame_.feedback = "No matching item selected."; return; }
    pending_ = DebugItemGrant{frame_.matches[frame_.selected], count};
    frame_.busy = true;
    frame_.feedback = "Giving items...";
}

void ItemBrowser::Examine(const ContentCatalog& content, const Localization& localization) {
    if (frame_.selected >= frame_.matches.size()) { return; }
    const auto type = frame_.matches[frame_.selected];
    const auto& definition = content.GetItem(type);
    InventoryEntry initial;
    initial.type = type;
    if (const auto* container = ItemContainer(definition)) { initial.capacityVolumeMl = container->capacityVolumeMl; }
    for (const auto& store : definition.energyStores) {
        initial.energy.push_back({store.capability, store.config.initialEnergy, store.config.capacityEnergy});
    }
    auto& sheet = frame_.examination;
    sheet.screen = ItemUiScreen::Examine;
    sheet.title = "Item browser | Examine definition";
    sheet.footer = "Up/Down scroll | Escape back | F11 close";
    sheet.examination.scroll = 0;
    DescribeInventoryItem(initial, content, localization, sheet.examination);
    sheet.examination.stats.push_back("ID: " + definition.id);
    for (const auto id : definition.flags.Ids()) {
        const auto& flag = content.GetFlag(id);
        sheet.examination.stats.push_back("Flag: " + flag.id + " | " + flag.description);
    }
    frame_.screen = ItemBrowserScreen::Examine;
}

void ItemBrowser::Handle(const KeyChord& key, const Keymap& keys, const ContentCatalog& content,
    const Localization& localization) {
    if (!IsOpen()) { return; }
    const bool plain = !key.ctrl && !key.alt && !key.shift;
    const auto action = ResolveAction(keys, InputContext::Menu, key);
    const bool cancel = key == KeyChord{"Escape"} || action == InputAction::Cancel;
    const bool accept = key == KeyChord{"Enter"} || action == InputAction::Accept;
    if (cancel) {
        if (frame_.screen == ItemBrowserScreen::List) { Close(); }
        else {
            if (frame_.screen == ItemBrowserScreen::Search) { Filter(View(frame_.query)); }
            frame_.screen = ItemBrowserScreen::List;
            frame_.input.clear();
        }
        return;
    }
    if (EditingText()) {
        if (accept) {
            if (frame_.screen == ItemBrowserScreen::Search) {
                frame_.query = frame_.input;
                frame_.input.clear(); frame_.screen = ItemBrowserScreen::List;
            } else {
                const auto count = Count(View(frame_.input));
                if (!count) { frame_.feedback = "Enter a whole count from 1 to " + UnsignedText(maxDebugGiveItems) + "."; }
                else {
                    QueueGive(*count);
                    frame_.input.clear(); frame_.screen = ItemBrowserScreen::List;
                }
            }
        } else if (key == KeyChord{"Backspace"} || key == KeyChord{"u", true}) {
            if (key.ctrl) { frame_.input.clear(); } else { EraseLastScalar(frame_.input); }
            frame_.feedback.clear();
            if (frame_.screen == ItemBrowserScreen::Search) { Filter(View(frame_.input)); }
        }
        return; // Printable text arrives only through Text(), not twice via chords.
    }
    if (frame_.screen == ItemBrowserScreen::Examine) {
        auto& scroll = frame_.examination.examination.scroll;
        if (action == InputAction::North || key == KeyChord{"Up"}) { scroll = eastl::max(0, scroll - 1); }
        else if (action == InputAction::South || key == KeyChord{"Down"}) { scroll = eastl::min(100000, scroll + 1); }
        else if (key == KeyChord{"PageUp"}) { scroll = eastl::max(0, scroll - 10); }
        else if (key == KeyChord{"PageDown"}) { scroll = eastl::min(100000, scroll + 10); }
        else if (accept || (plain && key.key == "e")) { frame_.screen = ItemBrowserScreen::List; }
        return;
    }
    if (plain && key.key == "/") {
        frame_.input = frame_.query; frame_.screen = ItemBrowserScreen::Search; frame_.feedback.clear();
    } else if (plain && key.key == "e") { Examine(content, localization); }
    else if (plain && key.key == "m") {
        if (!frame_.busy && !frame_.matches.empty()) {
            frame_.input.clear(); frame_.feedback.clear(); frame_.screen = ItemBrowserScreen::Amount;
        }
    } else if (accept) { QueueGive(1); }
    else if (!frame_.matches.empty()) {
        const auto count = frame_.matches.size();
        if (action == InputAction::North || key == KeyChord{"Up"}) { frame_.selected = (frame_.selected + count - 1) % count; }
        else if (action == InputAction::South || key == KeyChord{"Down"}) { frame_.selected = (frame_.selected + 1) % count; }
        else if (key == KeyChord{"Home"}) { frame_.selected = 0; }
        else if (key == KeyChord{"End"}) { frame_.selected = count - 1; }
        else if (key == KeyChord{"PageUp"}) { frame_.selected -= eastl::min<std::size_t>(10, frame_.selected); }
        else if (key == KeyChord{"PageDown"}) { frame_.selected += eastl::min<std::size_t>(10, count - 1 - frame_.selected); }
    }
}

eastl::optional<DebugItemGrant> ItemBrowser::TakeGiveRequest() {
    if (!pending_) { return {}; }
    const auto result = pending_;
    pending_.reset(); outstanding_ = true;
    return result;
}

void ItemBrowser::GiveFinished(DebugGiveResult result) {
    if (!outstanding_) { return; }
    outstanding_ = false; frame_.busy = false;
    switch (result.outcome) {
    case DebugGiveOutcome::Given:
        frame_.feedback = "Gave " + UnsignedText(result.count) + (result.count == 1 ? " item." : " items."); break;
    case DebugGiveOutcome::CapacityExceeded: frame_.feedback = "Cannot give items: carry capacity exceeded."; break;
    case DebugGiveOutcome::InvalidCount: frame_.feedback = "Invalid count (1-" + UnsignedText(maxDebugGiveItems) + ")."; break;
    case DebugGiveOutcome::InvalidItem: frame_.feedback = "This item cannot be created."; break;
    case DebugGiveOutcome::Unavailable: frame_.feedback = "Item giving is unavailable."; break;
    }
}

void ItemBrowser::GiveRejected(eastl::string_view reason) {
    outstanding_ = false; pending_.reset(); frame_.busy = false;
    frame_.feedback.assign(reason.data(), reason.size());
}

}
