#include "terminal/gameplay_screen.h"

#include "client/text.h"
#include "client/combat_text.h"
#include "client/tile_inspection.h"
#include "terminal/keyboard.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace cataclysmx {

struct TerminalMapCache {
    struct Cell {
        ViewCell source;
        std::string glyph;
        ftxui::Color foreground;
        ftxui::Color background;
    };
    Vector<Cell> cells;
    const ContentCatalog* content = nullptr;
    int mapWidth = 0;
    int mapHeight = 0;
    int sourceX = 0;
    int sourceY = 0;
    int width = 0;
    int height = 0;
    std::uint64_t preparations = 0;
    std::size_t rasterizedCells = 0;
    std::size_t resolvedCells = 0;
};

std::uint64_t GameplayUi::MapPreparations() const { return mapCache_ ? mapCache_->preparations : 0; }
std::size_t GameplayUi::MapRasterizedCells() const { return mapCache_ ? mapCache_->rasterizedCells : 0; }
std::size_t GameplayUi::MapResolvedCells() const { return mapCache_ ? mapCache_->resolvedCells : 0; }

namespace {

constexpr InputAction quickActions[] = {
    InputAction::Inventory, InputAction::Use, InputAction::PickUp, InputAction::Interact, InputAction::Wait,
    InputAction::Players, InputAction::Settings, InputAction::Help, InputAction::Save,
    InputAction::ToggleIdle, InputAction::ToggleRate, InputAction::RateDown, InputAction::RateUp,
    InputAction::Debug, InputAction::Crafting
};

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
std::string Frontend(const String& text) { return {text.data(), text.size()}; }
ftxui::Element Text(const String& text) { return ftxui::text(Frontend(text)); }
ftxui::Element Paragraph(const String& text) { return ftxui::paragraph(Frontend(text)); }
ftxui::Element ParagraphLines(const String& text) {
    ftxui::Elements lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        lines.push_back(Paragraph(text.substr(start, end == String::npos ? end : end - start)));
        if (end == String::npos) { break; }
        start = end + 1;
    }
    return ftxui::vbox(std::move(lines));
}
ftxui::Element Padded(ftxui::Element content) {
    return ftxui::hbox({ftxui::text(" "), std::move(content) | ftxui::flex, ftxui::text(" ")});
}
ftxui::Element Panel(const String& title, ftxui::Element content) {
    return Padded(ftxui::vbox({Text(title) | ftxui::bold, ftxui::separatorLight(),
        std::move(content) | ftxui::flex}));
}
void Flexible(ftxui::Requirement& requirement) {
    requirement.min_x = 0; requirement.min_y = 0;
    requirement.flex_grow_x = 1; requirement.flex_grow_y = 1;
    requirement.flex_shrink_x = 1; requirement.flex_shrink_y = 1;
}

// Keep the newest rows visible when a small terminal cannot fit the history.
class RecentMessages final : public ftxui::Node {
public:
    RecentMessages(ftxui::Element heading, ftxui::Elements rows)
        : Node({std::move(heading)}) {
        for (auto& row : rows) { children_.push_back(std::move(row)); }
    }
    void ComputeRequirement() override {
        Node::ComputeRequirement(); Flexible(requirement_);
    }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        auto row = box; row.y_max = row.y_min;
        children_[0]->SetBox(row);
        const int available = std::max(0, box.y_max - box.y_min);
        first_ = std::max(1, static_cast<int>(children_.size()) - available);
        for (int i = first_; i < static_cast<int>(children_.size()); ++i) {
            row.y_min = box.y_max - static_cast<int>(children_.size()) + i + 1;
            row.y_max = row.y_min;
            children_[i]->SetBox(row);
        }
    }
    void Render(ftxui::Screen& screen) override {
        const auto previous = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(previous, box_);
        if (!screen.stencil.IsEmpty()) {
            children_[0]->Render(screen);
            for (int i = first_; i < static_cast<int>(children_.size()); ++i) { children_[i]->Render(screen); }
        }
        screen.stencil = previous;
    }
private:
    int first_ = 1;
};

class GameplayFrame final : public ftxui::Node {
public:
    GameplayFrame(ftxui::Element header, ftxui::Element body, ftxui::Element history, int rows)
        : Node({std::move(header), ftxui::separatorLight(), std::move(body),
            ftxui::separatorLight(), std::move(history)}), rows_(rows) {}
    void ComputeRequirement() override { Node::ComputeRequirement(); Flexible(requirement_); }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        const int height = std::max(0, box.y_max - box.y_min + 1);
        // Leave six body rows for compact status, the map heading, and a glyph.
        const int historyHeight = std::min(rows_, std::max(1, std::min(height / 2, height - 9)));
        auto part = box; part.y_max = part.y_min;
        children_[0]->SetBox(part);
        ++part.y_min; part.y_max = part.y_min; children_[1]->SetBox(part);
        ++part.y_min; part.y_max = std::max(part.y_min - 1, box.y_max - historyHeight - 1);
        children_[2]->SetBox(part);
        part.y_min = part.y_max + 1; part.y_max = part.y_min; children_[3]->SetBox(part);
        ++part.y_min; part.y_max = box.y_max; children_[4]->SetBox(part);
    }
private:
    int rows_;
};

// Allocation-based layout keeps the first frame and synthetic test screens
// consistent with the actual terminal; no physical screen query is needed.
class MapViewport final : public ftxui::Node {
public:
    MapViewport(const WorldView& view, Vector<ViewCell> cells, const ContentCatalog& content,
        std::shared_ptr<TerminalMapCache> cache, int radius, const String& cropped)
        : Node(ftxui::Elements{}), width_(view.width), height_(view.height),
          playerX_(static_cast<int>(std::clamp<std::int64_t>(
              static_cast<std::int64_t>(view.player_position.x) - view.origin.x, 0, std::max(0, view.width - 1)))),
          playerY_(static_cast<int>(std::clamp<std::int64_t>(
              static_cast<std::int64_t>(view.player_position.z) - view.origin.z, 0, std::max(0, view.height - 1)))),
          cells_(std::move(cells)), content_(content), cache_(std::move(cache)) {
        const int boundedRadius = std::clamp(radius, 0, 64);
        left_ = std::max(0, playerX_ - boundedRadius);
        top_ = std::max(0, playerY_ - boundedRadius);
        right_ = std::min(width_, playerX_ + boundedRadius + 1);
        bottom_ = std::min(height_, playerY_ + boundedRadius + 1);
        children_ = {Text(cropped)};
    }
    void ComputeRequirement() override { Node::ComputeRequirement(); Flexible(requirement_); }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        const int width = std::max(0, box.x_max - box.x_min + 1);
        const int height = std::max(0, box.y_max - box.y_min + 1);
        cropped_ = right_ - left_ > width || bottom_ - top_ > height;
        mapBox_ = box;
        if (cropped_ && height > 1) { --mapBox_.y_max; }
        visibleWidth_ = std::min(right_ - left_, width);
        const int mapHeight = std::max(0, mapBox_.y_max - mapBox_.y_min + 1);
        visibleHeight_ = std::min(bottom_ - top_, mapHeight);
        startX_ = std::clamp(playerX_ - visibleWidth_ / 2, left_, right_ - visibleWidth_);
        startY_ = std::clamp(playerY_ - visibleHeight_ / 2, top_, bottom_ - visibleHeight_);
        drawX_ = mapBox_.x_min + (width - visibleWidth_) / 2;
        drawY_ = mapBox_.y_min + (mapHeight - visibleHeight_) / 2;
        auto indicator = box; indicator.y_min = box.y_max;
        children_[0]->SetBox(indicator);
    }
    void Render(ftxui::Screen& screen) override {
        cache_->rasterizedCells = 0;
        cache_->resolvedCells = 0;
        const auto previous = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(previous, mapBox_);
        // Iterate only the intersection with the real screen and parent clip.
        // There is no per-cell DOM, layout traversal, or offscreen raster work.
        const auto clip = ftxui::Box::Intersection(screen.stencil,
            ftxui::Box{0, screen.dimx() - 1, 0, screen.dimy() - 1});
        const int xMin = std::max(drawX_, clip.x_min);
        const int yMin = std::max(drawY_, clip.y_min);
        const int xMax = std::min(drawX_ + visibleWidth_ - 1, clip.x_max);
        const int yMax = std::min(drawY_ + visibleHeight_ - 1, clip.y_max);
        if (xMin <= xMax && yMin <= yMax) { PrepareVisible(xMin, yMin, xMax, yMax); }
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                const auto index = static_cast<std::size_t>(startY_ + y - drawY_) * width_
                    + static_cast<std::size_t>(startX_ + x - drawX_);
                if (index >= cells_.size()) { continue; }
                const auto& appearance = cache_->cells[static_cast<std::size_t>(y - yMin) * cache_->width + x - xMin];
                ++cache_->rasterizedCells;
                auto& pixel = screen.PixelAt(x, y);
                pixel.character = appearance.glyph;
                pixel.foreground_color = appearance.foreground;
                pixel.background_color = appearance.background;
            }
        }
        if (cropped_ && box_.y_max > box_.y_min) {
            screen.stencil = ftxui::Box::Intersection(previous, box_);
            if (!screen.stencil.IsEmpty()) { children_[0]->Render(screen); }
        }
        screen.stencil = previous;
    }
private:
    void PrepareVisible(int xMin, int yMin, int xMax, int yMax) {
        auto& cache = *cache_;
        const int sourceX = startX_ + xMin - drawX_;
        const int sourceY = startY_ + yMin - drawY_;
        const int width = xMax - xMin + 1;
        const int height = yMax - yMin + 1;
        const bool sourceChanged = cache.content != &content_ || cache.mapWidth != width_ || cache.mapHeight != height_;
        const bool layoutChanged = sourceX != cache.sourceX || sourceY != cache.sourceY
            || width != cache.width || height != cache.height;
        Vector<TerminalMapCache::Cell> previous;
        if (sourceChanged || layoutChanged) {
            previous = std::move(cache.cells);
            cache.cells.resize(static_cast<std::size_t>(width) * height);
        }
        bool changed = sourceChanged;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int cellX = sourceX + x;
                const int cellY = sourceY + y;
                const auto sourceIndex = static_cast<std::size_t>(cellY) * width_ + cellX;
                if (sourceIndex >= cells_.size()) { continue; }
                auto& cached = cache.cells[static_cast<std::size_t>(y) * width + x];
                const bool reused = !sourceChanged && cellX >= cache.sourceX && cellX < cache.sourceX + cache.width
                    && cellY >= cache.sourceY && cellY < cache.sourceY + cache.height;
                if (reused && layoutChanged) {
                    cached = std::move(previous[static_cast<std::size_t>(cellY - cache.sourceY) * cache.width
                        + cellX - cache.sourceX]);
                }
                const auto& source = cells_[sourceIndex];
                if (reused && cached.source == source) { continue; }
                if (reused) { changed = true; }
                cached.source = source;
                const auto appearance = CellAppearance(source, content_);
                const std::string_view symbol(appearance.symbol.data(), appearance.symbol.size());
                cached.glyph = ftxui::string_width(symbol) == 1 ? std::string(symbol) : "?";
                cached.foreground = ftxui::Color::RGB(appearance.foreground.red, appearance.foreground.green, appearance.foreground.blue);
                cached.background = ftxui::Color::RGB(appearance.background.red, appearance.background.green, appearance.background.blue);
                ++cache.resolvedCells;
            }
        }
        if (changed) { ++cache.preparations; }
        cache.content = &content_; cache.mapWidth = width_; cache.mapHeight = height_;
        cache.sourceX = sourceX; cache.sourceY = sourceY; cache.width = width; cache.height = height;
    }

    int width_;
    int height_;
    int playerX_;
    int playerY_;
    Vector<ViewCell> cells_;
    const ContentCatalog& content_;
    std::shared_ptr<TerminalMapCache> cache_;
    int left_ = 0;
    int top_ = 0;
    int right_ = 0;
    int bottom_ = 0;
    int startX_ = 0;
    int startY_ = 0;
    int drawX_ = 0;
    int drawY_ = 0;
    int visibleWidth_ = 0;
    int visibleHeight_ = 0;
    bool cropped_ = false;
    ftxui::Box mapBox_;
};

class GameplayBody final : public ftxui::Node {
public:
    GameplayBody(ftxui::Element world, ftxui::Element status, ftxui::Element compact)
        : Node({std::move(world), std::move(status), std::move(compact), ftxui::separatorLight()}) {}
    void ComputeRequirement() override { Node::ComputeRequirement(); Flexible(requirement_); }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        wide_ = box.x_max - box.x_min + 1 >= 98; // Outer border brings this to 100.
        auto world = box; auto status = box; auto divider = box;
        if (wide_) {
            status.x_min = box.x_max - 31;
            divider.x_min = status.x_min - 1; divider.x_max = divider.x_min;
            world.x_max = divider.x_min - 1;
        } else {
            const int height = std::max(0, box.y_max - box.y_min + 1);
            const int statusHeight = std::min(std::max(1, children_[2]->requirement().min_y), std::max(1, height / 3));
            status.y_max = box.y_min + statusHeight - 1;
            divider.y_min = status.y_max + 1; divider.y_max = divider.y_min;
            world.y_min = divider.y_min + 1;
        }
        children_[0]->SetBox(world);
        children_[wide_ ? 1 : 2]->SetBox(status);
        statusBox_ = status;
        children_[3]->SetBox(divider);
    }
    void Render(ftxui::Screen& screen) override {
        const auto previous = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(previous, box_);
        if (!screen.stencil.IsEmpty()) {
            children_[0]->Render(screen);
            screen.stencil = ftxui::Box::Intersection(screen.stencil, statusBox_);
            if (!screen.stencil.IsEmpty()) { children_[wide_ ? 1 : 2]->Render(screen); }
            screen.stencil = ftxui::Box::Intersection(previous, box_);
            children_[3]->Render(screen);
        }
        screen.stencil = previous;
    }
private:
    bool wide_ = false;
    ftxui::Box statusBox_;
};

class ScrollPane final : public ftxui::Node {
public:
    ScrollPane(ftxui::Element content, int& offset, int selected = -1)
        : Node({std::move(content)}), offset_(offset), selected_(selected) {}
    void ComputeRequirement() override {
        Node::ComputeRequirement(); Flexible(requirement_);
        requirement_.min_y = children_[0]->requirement().min_y;
    }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        const int height = std::max(0, box.y_max - box.y_min + 1);
        contentHeight_ = children_[0]->requirement().min_y;
        const int contentHeight = std::max(height, contentHeight_);
        int wanted = offset_;
        if (selected_ >= 0 && height > 0) {
            if (selected_ < wanted) { wanted = selected_; }
            if (selected_ >= wanted + height) { wanted = selected_ - height + 1; }
        }
        actualOffset_ = std::clamp(wanted, 0, std::max(0, contentHeight - height));
        --box.x_max; // Leave a separate gutter for overflow markers.
        box.y_min -= actualOffset_; box.y_max = box.y_min + contentHeight - 1;
        children_[0]->SetBox(box);
    }
    void Render(ftxui::Screen& screen) override {
        offset_ = actualOffset_;
        const auto previous = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(previous, box_);
        if (!screen.stencil.IsEmpty()) { Node::Render(screen); }
        if (!screen.stencil.IsEmpty()) {
            const int height = box_.y_max - box_.y_min + 1;
            if (actualOffset_ > 0) { screen.PixelAt(box_.x_max, box_.y_min).character = "↑"; }
            if (actualOffset_ + height < contentHeight_) { screen.PixelAt(box_.x_max, box_.y_max).character = "↓"; }
        }
        screen.stencil = previous;
    }
private:
    int& offset_;
    int selected_;
    int actualOffset_ = 0;
    int contentHeight_ = 0;
};

class ModalOverlay final : public ftxui::Node {
public:
    ModalOverlay(ftxui::Element background, ftxui::Element window, int preferredWidth)
        : Node({std::move(background) | ftxui::dim, ftxui::filler() | ftxui::clear_under,
              std::move(window) | ftxui::clear_under}), preferredWidth_(preferredWidth) {}
    void ComputeRequirement() override { Node::ComputeRequirement(); Flexible(requirement_); }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box); children_[0]->SetBox(box);
        const int width = std::max(1, box.x_max - box.x_min + 1);
        const int height = std::max(1, box.y_max - box.y_min + 1);
        const int windowWidth = std::min(preferredWidth_, std::max(1, width - 4));
        const int windowHeight = std::min(std::max(1, height - 4), std::max(8, children_[2]->requirement().min_y));
        auto window = box;
        window.x_min += (width - windowWidth) / 2; window.x_max = window.x_min + windowWidth - 1;
        window.y_min += (height - windowHeight) / 2; window.y_max = window.y_min + windowHeight - 1;
        children_[2]->SetBox(window);
        auto gutter = window;
        --gutter.x_min; ++gutter.x_max;
        children_[1]->SetBox(ftxui::Box::Intersection(gutter, box));
    }
private:
    int preferredWidth_;
};

// Reserve room for the selected row and a fixed close control even when the
// shared footer is longer than a narrow terminal can fit.
class ItemWindow final : public ftxui::Node {
public:
    ItemWindow(ftxui::Element title, ftxui::Element body, ftxui::Element feedback,
        bool hasFeedback, ftxui::Element hints, ftxui::Element close)
        : Node({std::move(title), ftxui::separatorLight(), std::move(body),
            std::move(feedback), ftxui::separatorLight(), std::move(hints), std::move(close)}), hasFeedback_(hasFeedback) {}
    void ComputeRequirement() override {
        Node::ComputeRequirement(); Flexible(requirement_);
        requirement_.min_y = 4 + children_[2]->requirement().min_y
            + (hasFeedback_ ? 1 : 0) + std::min(2, children_[5]->requirement().min_y);
    }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        const int height = std::max(0, box.y_max - box.y_min + 1);
        const int hints = std::min(box.x_max - box.x_min + 1 >= 80 ? 3 : 2, std::max(1, height / 5));
        const int feedback = hasFeedback_ && height >= 10 ? 1 : 0;
        int y = box.y_min;
        const auto place = [&](unsigned n, int rows) {
            auto part = box; part.y_min = y; part.y_max = y + rows - 1;
            boxes_[n] = ftxui::Box::Intersection(part, box); children_[n]->SetBox(part); y += rows;
        };
        place(0, 1); place(1, 1);
        place(2, std::max(0, height - 4 - hints - feedback));
        place(3, feedback); place(4, 1); place(5, hints); place(6, 1);
    }
    void Render(ftxui::Screen& screen) override {
        const auto previous = screen.stencil;
        for (unsigned n = 0; n < children_.size(); ++n) {
            screen.stencil = ftxui::Box::Intersection(previous, boxes_[n]);
            if (!screen.stencil.IsEmpty()) { children_[n]->Render(screen); }
        }
        screen.stencil = previous;
    }
private:
    bool hasFeedback_;
    eastl::array<ftxui::Box, 7> boxes_{};
};

ftxui::Element Hint(const String& key, const String& label) {
    return ftxui::hbox({Text(key) | ftxui::bold | ftxui::xflex_shrink,
        ftxui::text(" "), Text(label) | ftxui::xflex_shrink}) | ftxui::xflex_shrink;
}
String ActionLabel(const Localization& localization, InputAction action) {
    String key = "action."; const auto name = ActionName(action); key.append(name.data(), name.size());
    const auto fallback = action == InputAction::Attack ? eastl::string_view("Attack")
        : action == InputAction::StyleBalanced ? eastl::string_view("Balanced style")
        : action == InputAction::StyleAggressive ? eastl::string_view("Aggressive style")
        : action == InputAction::StyleDefensive ? eastl::string_view("Defensive style") : name;
    return localization.Text(View(key), fallback);
}
String BindingLabel(const Keymap& keys, const Localization& localization, InputContext context, InputAction action) {
    auto hint = BindingHint(keys, context, action);
    return hint == "—" ? localization.Text("game.ui.unbound") : hint;
}
ftxui::Element StatusField(const String& label, const String& value) {
    return ftxui::hbox({Text(label) | ftxui::xflex_shrink, ftxui::text(" "),
        ftxui::filler(), Text(value) | ftxui::bold});
}
ftxui::Element BindingRow(const String& label, const String& binding) {
    return ftxui::hbox({Paragraph(label) | ftxui::flex, ftxui::text("  "),
        Text(binding) | ftxui::bold | ftxui::xflex_shrink});
}
String PlayerText(const PlayerStatus& player, const Localization& localization) {
    const auto state = player.state == PlayerConnectionState::Parked ? "game.ui.parked"
        : player.state == PlayerConnectionState::Joining ? "game.ui.joining" : "game.ui.active";
    String result = localization.Format("game.ui.player_status", {
        {"state", localization.Text(state)}, {"ready", localization.Text(player.ready ? "game.yes" : "game.no")},
        {"queued", localization.Text(player.commandQueued ? "game.yes" : "game.no")}});
    if (player.graceMicroseconds != 0) {
        String seconds = UnsignedText(player.graceMicroseconds / 1000000);
        seconds += '.'; seconds += UnsignedText((player.graceMicroseconds / 100000) % 10);
        result += ' '; result += localization.Format("game.ui.grace", {{"seconds", seconds}});
    }
    return result;
}


}

String GameplayWelcome(const Localization& localization, const Keymap& keys) {
    String result = localization.Text("game.welcome"); result += ' ';
    result += localization.Format("game.ui.discover", {
        {"inventory", BindingLabel(keys, localization, InputContext::Gameplay, InputAction::Inventory)},
        {"players", BindingLabel(keys, localization, InputContext::Gameplay, InputAction::Players)},
        {"help", BindingLabel(keys, localization, InputContext::Gameplay, InputAction::Help)},
        {"quick_actions", BindingLabel(keys, localization, InputContext::Gameplay, InputAction::QuickActions)}});
    return result;
}

bool GameplayUi::Open(InputAction action) {
    switch (action) {
    case InputAction::Crafting: modal_ = Modal::Crafting; craftingSelection_.reset(); craftingIndex_ = 0; break;
    case InputAction::Use:
    case InputAction::Inventory:
    case InputAction::AdvancedInventory:
        items_.Open(action == InputAction::AdvancedInventory, action == InputAction::Use);
        modal_ = Modal::Inventory; break;
    case InputAction::Players: modal_ = Modal::Players; break;
    case InputAction::Help: modal_ = Modal::Controls; break;
    case InputAction::QuickActions: modal_ = Modal::QuickActions; break;
    default: return false;
    }
    feedback_.clear(); return true;
}
bool GameplayUi::IsOpen() const { return modal_ == Modal::Inventory ? items_.IsOpen() : modal_ != Modal::None; }

void GameplayUi::Close() { items_.Close(); modal_ = Modal::None; feedback_.clear(); }
bool GameplayUi::PausesLocal(eastl::optional<InputAction> action) const {
    if (items_.NeedsTime()) { return false; }
    if (modal_ == Modal::Crafting || action == InputAction::Crafting) { return false; }
    return IsOpen() || action == InputAction::Inventory || action == InputAction::Players
        || action == InputAction::Help || action == InputAction::QuickActions || action == InputAction::Use
        || action == InputAction::AdvancedInventory;
}
void GameplayUi::RefreshItems(const WorldView& view, const ContentCatalog& content,
    const Localization& localization, const Keymap& keys, const std::function<bool(Command)>& submit) {
    items_.Refresh(view, content, localization, keys);
    if (auto command = items_.TakeCommand(view)) { if (!submit(*command)) { items_.CommandRejected(); } }
}
void GameplayUi::ItemCommandFinished(const CommandResult& result) { items_.CommandFinished(result); }
void GameplayUi::ResetItems() { items_ = {}; if (modal_ == Modal::Inventory) { modal_ = Modal::None; } }

ftxui::Element GameplayUi::Render(WorldView view, const ContentCatalog& content,
    const Localization& localization, const Keymap& keys, const GameplayPresentation& presentation) {
    itemContent_ = &content;
    // Reconcile against the complete snapshot before its map cells move into
    // MapViewport. Item storage presentation can resolve locations from cells.
    items_.Refresh(view, content, localization, keys);
    const auto t = [&](eastl::string_view key) { return localization.Text(key); };
    const auto on = [&](bool value) { return t(value ? "game.on" : "game.off"); };
    const String state = t(!presentation.pending.empty() ? "game.ui.awaiting"
        : view.commandQueued ? "game.ui.queued" : view.remainingTicks != 0 ? "game.ui.busy" : "game.ui.ready");
    auto stateField = StatusField(t("game.ui.action_state"), state);
    if (!presentation.pending.empty() || view.commandQueued || view.remainingTicks != 0) {
        stateField = stateField | ftxui::color(ftxui::Color::Yellow);
    }
    String position = localization.Format("game.ui.coordinates", {
        {"x", NumberText(view.player_position.x)}, {"z", NumberText(view.player_position.z)}});
    position += ", y="; position += NumberText(view.player_position.y);
    const char* lightKey = "game.light.dark";
    switch (view.playerLight) {
    case LightBand::Dark: break;
    case LightBand::Dim: lightKey = "game.light.dim"; break;
    case LightBand::Ordinary: lightKey = "game.light.ordinary"; break;
    case LightBand::Bright: lightKey = "game.light.bright"; break;
    }
    const auto light = t(lightKey);
    ftxui::Elements status{stateField,
        StatusField(t("game.ui.cooldown"), localization.Format("game.ui.ticks", {{"ticks", UnsignedText(view.remainingTicks)}})),
        StatusField(t("game.ui.speed"), UnsignedText(view.speed)), ftxui::separatorLight(),
        Text(t("game.ui.location")) | ftxui::bold, Paragraph(position),
        StatusField(t("game.ui.light"), light),
        StatusField(t("game.ui.tick"), UnsignedText(view.tick)), ftxui::separatorLight(),
        Text(t("game.ui.pacing")) | ftxui::bold,
        StatusField(t("game.ui.tick_rate"), localization.Format("game.ui.per_second", {{"rate", UnsignedText(view.pacing.ticksPerSecond)}})),
        StatusField(t("game.ui.rate_limit"), on(view.pacing.rateLimited)),
        StatusField(t("game.ui.idle_pause"), on(view.pacing.pauseWhenIdle))};
    const auto combatStatus = CombatStatusText(view.combat);
    const auto combatFeedback = CombatFeedbackText(view.combat);
    if (!combatStatus.empty()) { status.insert(status.begin() + 1, ParagraphLines(combatStatus)); }
    if (!combatFeedback.empty()) { status.insert(status.begin() + 1, Paragraph(combatFeedback)); }
    String compact = localization.Format("game.ui.compact_state", {{"position", position},
        {"tick", UnsignedText(view.tick)}, {"state", state}});
    compact += '\n';
    const String compactPacing = localization.Format("game.ui.compact_pacing", {{"rate", UnsignedText(view.pacing.ticksPerSecond)},
        {"limit", on(view.pacing.rateLimited)}, {"idle", on(view.pacing.pauseWhenIdle)},
        {"speed", UnsignedText(view.speed)}, {"delay", UnsignedText(view.remainingTicks)}});
    if (!presentation.pending.empty()) {
        const auto pending = localization.Format("game.ui.pending", {{"value", presentation.pending}});
        status.insert(status.begin() + 1, Paragraph(pending));
        compact += pending; compact += " | ";
    }
    compact += compactPacing;
    compact += " | "; compact += t("game.ui.light"); compact += ' '; compact += light;
    if (!combatStatus.empty()) { compact += '\n'; compact += combatStatus; }
    if (!combatFeedback.empty()) { compact += '\n'; compact += combatFeedback; }
    String visibleActors;
    for (const auto& cell : view.cells) {
        if (cell.player) { continue; }
        const auto typeName = VisibleActorName(content, cell);
        if (typeName.empty()) { continue; }
        if (!visibleActors.empty()) { visibleActors += ", "; }
        visibleActors.append(typeName.data(), typeName.size());
        visibleActors += " #"; visibleActors += UnsignedText(cell.actor);
    }
    if (!visibleActors.empty()) {
        visibleActors.insert(0, "Visible: ");
        status.insert(status.begin() + 1, Paragraph(visibleActors));
        compact += '\n'; compact += visibleActors;
    }
    if (!presentation.lan && !mapCache_) { mapCache_ = std::make_shared<TerminalMapCache>(); }
    auto world = presentation.lan ? presentation.lan
        : std::make_shared<MapViewport>(view, std::move(view.cells), content, mapCache_,
            presentation.viewRadius, t("game.ui.cropped"));
    auto body = std::make_shared<GameplayBody>(Panel(t(presentation.lan ? "game.ui.lan" : "game.ui.world"), world),
        Panel(t("game.ui.status"), ftxui::vbox(std::move(status))), Padded(ftxui::paragraph(Frontend(compact))));
    ftxui::Elements topBar{Text(t("game.title")) | ftxui::bold, ftxui::text(" · "),
        Text(presentation.session) | ftxui::xflex_shrink, ftxui::filler()};
    constexpr InputAction topActions[] = {InputAction::Help, InputAction::Settings, InputAction::Help, InputAction::Debug};
    constexpr const char* topLabels[] = {"game.ui.top_help", "game.ui.top_settings", "game.ui.top_lan", "game.ui.top_debug"};
    const auto topContext = presentation.interacting ? InputContext::Interaction : InputContext::Gameplay;
    for (unsigned i = 0; i < 4; ++i) {
        String key = "?";
        if (i == 2) { key = "F9"; }
        else {
            for (const auto& binding : keys) {
                if (binding.context == topContext && binding.action == topActions[i] && !binding.keys.empty()) {
                    key = ChordName(binding.keys.front()); break;
                }
            }
        }
        String label = "[";
        label += key;
        label += ' '; label += t(topLabels[i]); label += ']';
        topBar.push_back(Text(label) | ftxui::bold);
        if (i != 3) { topBar.push_back(ftxui::text(" ")); }
    }
    ftxui::Elements messages;
    if (presentation.history) {
        for (const auto& entry : presentation.history->Entries()) { messages.push_back(Text(entry)); }
    } else if (!presentation.message.empty()) { messages.push_back(Text(presentation.message)); }
    if (presentation.interacting) { messages.push_back(Text(t("game.interact_direction")) | ftxui::bold); }
    const int messageRows = static_cast<int>(messages.size()) + 1;
    auto history = std::make_shared<RecentMessages>(Text(t("game.ui.message_history")) | ftxui::bold, std::move(messages));
    auto background = std::make_shared<GameplayFrame>(Padded(ftxui::hbox(std::move(topBar))), body,
        Padded(history), messageRows) | ftxui::borderLight;
    if (!IsOpen()) { return background; }
    if (modal_ == Modal::Inventory) {
        auto& frame = items_.Frame();
        ftxui::Element itemBody;
        if (frame.screen == ItemUiScreen::Examine) {
            ftxui::Elements detail{Text(frame.examination.name) | ftxui::bold};
            if (!frame.examination.description.empty()) { detail.push_back(Paragraph(frame.examination.description)); }
            if (frame.examination.art) {
                const ftxui::Color colors[] = {ftxui::Color::Default, ftxui::Color::Black, ftxui::Color::Red,
                    ftxui::Color::Green, ftxui::Color::Yellow, ftxui::Color::Blue, ftxui::Color::Magenta,
                    ftxui::Color::Cyan, ftxui::Color::White, ftxui::Color::GrayLight, ftxui::Color::GrayDark};
                for (const auto& line : frame.examination.art->lines) {
                    ftxui::Elements runs;
                    for (const auto& run : line) { runs.push_back(Text(run.text) | ftxui::color(colors[static_cast<unsigned>(run.color)])); }
                    if (runs.empty()) { runs.push_back(ftxui::text("")); }
                    detail.push_back(ftxui::hbox(std::move(runs)));
                }
            }
            for (const auto& stat : frame.examination.stats) { detail.push_back(Paragraph(stat)); }
            itemBody = std::make_shared<ScrollPane>(ftxui::vbox(std::move(detail)), frame.examination.scroll);
        } else if (frame.screen == ItemUiScreen::Actions || frame.screen == ItemUiScreen::Target || frame.screen == ItemUiScreen::Direction || frame.screen == ItemUiScreen::Amount) {
            ftxui::Elements choices;
            if (frame.screen == ItemUiScreen::Amount) { choices.push_back(Text(frame.prompt)); choices.push_back(Text(frame.amount + "_")); }
            else if (frame.screen == ItemUiScreen::Direction) { choices.push_back(ftxui::text("Choose a direction; Escape returns.")); }
            else { for (std::size_t n = 0; n < frame.choices.size(); ++n) {
                auto row = Text(frame.choices[n]); if (n == frame.choice) { row = row | ftxui::inverted; } choices.push_back(row);
            } }
            itemBody = std::make_shared<ScrollPane>(ftxui::vbox(std::move(choices)), inventoryScroll_, static_cast<int>(frame.choice));
        } else {
            ftxui::Elements panes;
            for (unsigned p = 0; p < (items_.IsAdvanced() ? 2u : 1u); ++p) {
                auto& pane = frame.panes[p]; ftxui::Elements rows;
                // Shared widths align the header and every row, including a
                // mixture of compact units and different stack counts.
                eastl::array<int, 3> widths{5, 6, 4};
                for (const auto& item : pane.rows) {
                    widths[0] = std::max(widths[0], static_cast<int>(UnsignedText(item.items.size()).size()));
                    widths[1] = std::max(widths[1], static_cast<int>(ItemVolumeText(item.volumeMl).size()));
                    widths[2] = std::max(widths[2], static_cast<int>(ItemMassText(item.massGrams).size()));
                }
                const auto numeric = [&](const String& value, int width) {
                    return Text(value) | ftxui::align_right | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
                };
                const auto tableRow = [&](const String& name, const String& count, const String& volume, const String& mass) {
                    return ftxui::hbox({Text(name) | ftxui::flex, ftxui::text(" "), numeric(count, widths[0]),
                        ftxui::text(" "), numeric(volume, widths[1]), ftxui::text(" "), numeric(mass, widths[2])});
                };
                for (std::size_t n = 0; n < pane.rows.size(); ++n) {
                    const auto& item = pane.rows[n]; String label(item.depth * 2, ' ');
                    label += pane.selectionValid && n == pane.selected ? "> " : "  ";
                    label += UnsignedText(item.items.front()); label += ':'; label += item.name;
                    const auto* source = FindInventoryEntry(view, item.items.front());
                    if (source && source->contained_items) { label += " ("; label += UnsignedText(source->contained_items); label += " inside)"; }
                    if (source && source->parent) { label += " [in #"; label += UnsignedText(source->parent); label += ']'; }
                    if (item.wielded) { label += " [wielded]"; }
                    if (!items_.IsAdvanced()) {
                        bool ground = false; for (const auto& entry : view.groundItems) { ground |= source && entry.id == source->id; }
                        // The shared row name supplies the ground marker for
                        // both frontends; retain the legacy carried marker.
                        if (!ground) { label += " [carried]"; }
                    }
                    auto row = tableRow(label, UnsignedText(item.items.size()), ItemVolumeText(item.volumeMl), ItemMassText(item.massGrams));
                    if (pane.selectionValid && n == pane.selected) { row = row | ftxui::bold; if (p == frame.activePane) { row = row | ftxui::inverted; } }
                    rows.push_back(row);
                }
                if (rows.empty()) { rows.push_back(Paragraph(localization.Text("game.ui.inventory_empty"))); }
                auto list = std::make_shared<ScrollPane>(ftxui::vbox(std::move(rows)), pane.scroll,
                    pane.selectionValid ? static_cast<int>(pane.selected) : -1);
                const String count = UnsignedText(pane.selectionValid ? pane.selected + 1 : 0) + " / " + UnsignedText(pane.rows.size());
                // ScrollPane reserves its final column for overflow markers.
                auto heading = ftxui::hbox({tableRow("Name", "Count", "Volume", "Mass") | ftxui::flex, ftxui::text(" ")}) | ftxui::bold;
                auto panel = ftxui::vbox({Text(pane.title + "  " + count) | ftxui::bold, Text(pane.capacity), heading, list | ftxui::flex}) | ftxui::flex;
                if (p) { panes.push_back(ftxui::separatorLight()); } panes.push_back(panel);
            }
            itemBody = ftxui::hbox(std::move(panes));
        }
        String feedback = frame.feedback;
        if (frame.busy) { if (!feedback.empty()) { feedback += " | "; } feedback += "Working… Escape stops future transfers"; }
        String close = "Escape";
        const auto cancel = BindingHint(keys, InputContext::Menu, InputAction::Cancel);
        if (cancel != "Escape" && cancel != "—") { close += '/'; close += cancel; }
        close += " back/close";
        auto window = std::make_shared<ItemWindow>(Text(frame.title) | ftxui::bold, itemBody,
            Paragraph(feedback) | ftxui::color(ftxui::Color::Yellow), !feedback.empty(), Paragraph(frame.footer), Text(close));
        return std::make_shared<ModalOverlay>(background, window | ftxui::borderLight, items_.IsAdvanced() ? 116 : 90);
    }

    ftxui::Elements rows;
    String title;
    String subtitle;
    String count;
    int preferredWidth = 84;
    int* scroll = nullptr;
    int selected = -1;
    if (modal_ == Modal::Crafting) {
        if (selectionPlayer_ != view.playerId) {
            selectionPlayer_ = view.playerId; craftingSelection_.reset(); craftingIndex_ = 0; craftingScroll_ = 0;
        }
        title = "Crafting and recycling"; subtitle = "Availability is checked by the server. Work continues while this menu is open.";
        scroll = &craftingScroll_;
        CollectCraftingMenu(view, content, localization, craftingRows_);
        if (!craftingSelection_ && !craftingRows_.empty()) { craftingSelection_ = craftingRows_.front().command; }
        for (std::size_t i = 0; i < craftingRows_.size(); ++i) {
            const bool chosen = craftingSelection_ && SameCraftingChoice(*craftingSelection_, craftingRows_[i].command);
            auto row = Text(craftingRows_[i].label);
            if (chosen) { craftingIndex_ = i; selected = static_cast<int>(i); row = row | ftxui::bold | ftxui::inverted; }
            rows.push_back(row);
        }
        if (craftingRows_.empty()) { rows.push_back(ftxui::text("No recipes, reachable jobs, or recyclable items.")); }
        if (!presentation.message.empty()) { subtitle += "\n"; subtitle += presentation.message; }
    } else if (modal_ == Modal::Players) {
        title = t("game.ui.players"); subtitle = t("game.ui.players_hint");
        scroll = &playersScroll_; preferredWidth = 80;
        count = localization.Format("game.ui.player_count", {{"count", UnsignedText(view.players.size())}}, view.players.size());
        if (view.players.empty()) { rows.push_back(Paragraph(t("game.ui.players_empty"))); }
        for (const auto& player : view.players) {
            if (!rows.empty()) { rows.push_back(ftxui::text("")); }
            rows.push_back(StatusField(player.name, String("#") + UnsignedText(player.id)) | ftxui::bold);
            rows.push_back(Paragraph(PlayerText(player, localization)));
        }
    } else if (modal_ == Modal::QuickActions) {
        title = t("game.ui.quick_actions"); subtitle = t("game.ui.quick_actions_hint");
        scroll = &quickActionsScroll_; preferredWidth = 64;
        selected = static_cast<int>(quickActionIndex_);
        count = localization.Format("game.ui.selection_count", {{"selected", UnsignedText(quickActionIndex_ + 1)},
            {"total", UnsignedText(std::size(quickActions))}});
        for (std::size_t i = 0; i < std::size(quickActions); ++i) {
            const auto action = quickActions[i];
            auto row = ftxui::hbox({ftxui::text(i == quickActionIndex_ ? "> " : "  "),
                Text(ActionLabel(localization, action)) | ftxui::flex, ftxui::text("  "),
                Text(BindingLabel(keys, localization, InputContext::Gameplay, action)) | ftxui::bold | ftxui::xflex_shrink});
            if (i == quickActionIndex_) { row = row | ftxui::bold | ftxui::inverted; }
            rows.push_back(row);
        }
    } else {
        title = t("game.ui.controls"); subtitle = t("game.ui.controls_hint"); scroll = &controlsScroll_;
        const auto group = [&](eastl::string_view heading, std::initializer_list<InputAction> actions) {
            if (!rows.empty()) { rows.push_back(ftxui::text("")); }
            rows.push_back(Text(t(heading)) | ftxui::bold);
            for (auto action : actions) {
                rows.push_back(BindingRow(ActionLabel(localization, action),
                    BindingLabel(keys, localization, InputContext::Gameplay, action)));
            }
        };
        group("game.ui.windows", {InputAction::QuickActions, InputAction::Inventory, InputAction::AdvancedInventory, InputAction::Crafting, InputAction::Players,
            InputAction::Settings, InputAction::Help});
        group("game.ui.actions", {InputAction::Use, InputAction::PickUp, InputAction::Drop, InputAction::Interact, InputAction::Grab, InputAction::Wait,
            InputAction::Attack, InputAction::StyleBalanced, InputAction::StyleAggressive, InputAction::StyleDefensive});
        group("game.ui.movement", {InputAction::West, InputAction::North, InputAction::South, InputAction::East, InputAction::MoveDown, InputAction::MoveUp});
        group("game.ui.pacing", {InputAction::RateDown, InputAction::RateUp, InputAction::ToggleRate, InputAction::ToggleIdle});
        group("game.ui.system", {InputAction::Save, InputAction::Debug, InputAction::Quit, InputAction::Cancel});
        rows.push_back(ftxui::text("")); rows.push_back(Text(t("context.interaction")) | ftxui::bold);
        for (const auto& binding : keys) {
            if (binding.context != InputContext::Interaction) { continue; }
            rows.push_back(BindingRow(ActionLabel(localization, binding.action),
                BindingLabel(keys, localization, binding.context, binding.action)));
        }
        rows.push_back(Text(t("context.items")) | ftxui::bold);
        for (const auto& binding : keys) {
            if (binding.context == InputContext::Items) {
                rows.push_back(BindingRow(ActionLabel(localization, binding.action), BindingLabel(keys, localization, binding.context, binding.action)));
            }
        }
        rows.push_back(ftxui::text("")); rows.push_back(Paragraph(t("game.ui.lan_controls")));
        rows.push_back(Paragraph(t("game.ui.fixed_controls")));
        bool unavailable = false;
        for (const auto& binding : keys) {
            for (const auto& key : binding.keys) { unavailable |= !LegacyKeyAvailable(key); }
        }
        if (unavailable && !presentation.enhancedKeyboard) {
            rows.push_back(Paragraph(t("input.legacy")) | ftxui::color(ftxui::Color::Yellow));
        }
    }
    auto contentPane = std::make_shared<ScrollPane>(ftxui::vbox(std::move(rows)), *scroll, selected);
    String directions = BindingLabel(keys, localization, InputContext::Menu, InputAction::North);
    directions += '/'; directions += BindingLabel(keys, localization, InputContext::Menu, InputAction::South);
    const bool selectable = modal_ == Modal::QuickActions || modal_ == Modal::Crafting;
    ftxui::Elements hints{Hint(directions, t(selectable ? "game.ui.select" : "game.ui.scroll"))};
    if (modal_ == Modal::QuickActions || modal_ == Modal::Crafting) {
        hints.push_back(ftxui::text("  |  "));
        hints.push_back(Hint(BindingLabel(keys, localization, InputContext::Menu, InputAction::Accept),
            t("game.ui.activate")));
    }
    hints.push_back(ftxui::filler());
    String cancel = "Escape";
    const auto configuredCancel = BindingHint(keys, InputContext::Menu, InputAction::Cancel);
    if (configuredCancel != "Escape" && configuredCancel != "?") { cancel += '/'; cancel += configuredCancel; }
    auto header = ftxui::hbox({Text(title) | ftxui::bold | ftxui::xflex_shrink,
        ftxui::text("  "), ftxui::filler(), Text(count) | ftxui::bold});
    ftxui::Elements window{Padded(header), Padded(Paragraph(subtitle)), ftxui::separatorLight(), Padded(contentPane) | ftxui::flex};
    if (!feedback_.empty()) {
        window.push_back(Padded(Paragraph(feedback_) | ftxui::color(ftxui::Color::Red)));
        if (modal_ == Modal::Inventory && !presentation.message.empty()) {
            window.push_back(Padded(Paragraph(presentation.message)));
        }
    }
    window.push_back(ftxui::separatorLight()); window.push_back(Padded(ftxui::hbox(std::move(hints))));
    window.push_back(Padded(Hint(cancel, t("settings.ui.close"))));
    return std::make_shared<ModalOverlay>(background, ftxui::vbox(std::move(window)) | ftxui::borderLight, preferredWidth);
}

bool GameplayUi::HandleEvent(const ftxui::Event& event, const WorldView& view, const Localization& localization,
    const Keymap& keys, const std::function<bool(Command)>& submit, const std::function<void(InputAction)>& activate, const ContentCatalog* content) {
    if (!IsOpen()) { return false; }
    if (modal_ == Modal::Inventory) {
        if (content) { itemContent_ = content; }
        if (!itemContent_) { return true; }
        const auto& catalog = *itemContent_;
        const auto chord = DecodeTerminalInput({event.input().data(), event.input().size()});
        if (chord) { items_.Handle(*chord, keys, view, catalog, localization); }
        RefreshItems(view, catalog, localization, keys, submit); return true;
    }
    // A player change after the last frame invalidates the entire prompt. Do
    // not reinterpret an activation against the new player's first item.
    if (modal_ == Modal::Crafting && selectionPlayer_ != view.playerId) {
        Close(); return true;
    }
    const auto chord = DecodeTerminalInput({event.input().data(), event.input().size()});
    auto action = chord ? ResolveAction(keys, InputContext::Menu, *chord) : eastl::optional<InputAction>{};
    if (action == InputAction::Cancel) {
        Close();
        return true;
    }
    if (modal_ == Modal::Crafting) {
        if (!craftingRows_.empty() && (action == InputAction::North || action == InputAction::South)) {
            craftingIndex_ = action == InputAction::North ? (craftingIndex_ + craftingRows_.size() - 1) % craftingRows_.size()
                : (craftingIndex_ + 1) % craftingRows_.size();
            craftingSelection_ = craftingRows_[craftingIndex_].command; feedback_.clear();
        } else if (action == InputAction::Accept && craftingSelection_) {
            if (!submit(*craftingSelection_)) { feedback_ = localization.Text("item.ui.submit_failed"); }
            else { feedback_.clear(); }
        }
    } else if (modal_ == Modal::QuickActions) {
        if (action == InputAction::North) {
            quickActionIndex_ = (quickActionIndex_ + std::size(quickActions) - 1) % std::size(quickActions);
            feedback_.clear();
        } else if (action == InputAction::South) {
            quickActionIndex_ = (quickActionIndex_ + 1) % std::size(quickActions);
            feedback_.clear();
        } else if (action == InputAction::Accept) {
            const auto selected = quickActions[quickActionIndex_];
            if (Open(selected)) { return true; }
            if (activate) { Close(); activate(selected); }
            else { feedback_ = localization.Text("game.ui.action_unavailable"); }
        }
    } else {
        int& scroll = modal_ == Modal::Players ? playersScroll_ : controlsScroll_;
        if (action == InputAction::North) { scroll = std::max(0, scroll - 1); }
        else if (action == InputAction::South) { ++scroll; }
    }
    return true;
}

}
