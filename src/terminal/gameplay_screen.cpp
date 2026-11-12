#include "terminal/gameplay_screen.h"

#include "client/text.h"
#include "client/combat_text.h"
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
    InputAction::Debug
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

std::size_t ItemCount(const WorldView& view) { return view.inventory.size() + view.groundItems.size(); }
const InventoryEntry& ItemAt(const WorldView& view, std::size_t index) {
    return index < view.inventory.size() ? view.inventory[index] : view.groundItems[index - view.inventory.size()];
}
const InventoryEntry* FindItem(const WorldView& view, PersistentId id) {
    for (std::size_t i = 0; i < ItemCount(view); ++i) { if (ItemAt(view, i).id == id) { return &ItemAt(view, i); } }
    return nullptr;
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
    itemStage_ = ItemStage::Browse; useSelection_ = action == InputAction::Use;
    switch (action) {
    case InputAction::Use:
    case InputAction::Inventory: modal_ = Modal::Inventory; break;
    case InputAction::Players: modal_ = Modal::Players; break;
    case InputAction::Help: modal_ = Modal::Controls; break;
    case InputAction::QuickActions: modal_ = Modal::QuickActions; break;
    default: return false;
    }
    feedback_.clear(); return true;
}
bool GameplayUi::IsOpen() const { return modal_ != Modal::None; }
void GameplayUi::Close() { modal_ = Modal::None; feedback_.clear(); }
bool GameplayUi::PausesLocal(eastl::optional<InputAction> action) const {
    return IsOpen() || action == InputAction::Inventory || action == InputAction::Players
        || action == InputAction::Help || action == InputAction::QuickActions || action == InputAction::Use;
}
void GameplayUi::ReconcileInventory(const WorldView& view) {
    if (selectionPlayer_ != view.playerId) {
        selectionPlayer_ = view.playerId;
        selectedItem_ = 0; selectedIndex_ = 0; inventoryScroll_ = 0;
        sourceItem_ = 0; itemStage_ = ItemStage::Browse;
        itemActions_.clear(); itemTargets_.clear(); feedback_.clear();
    }
    if (ItemCount(view) == 0) { selectedIndex_ = 0; inventoryScroll_ = 0; return; }
    for (std::size_t i = 0; i < ItemCount(view); ++i) {
        if (ItemAt(view, i).id == selectedItem_) { selectedIndex_ = i; return; }
    }
    // A vanished ID remains selected until explicit navigation. Rendering a new
    // snapshot must not turn a later activation into an action on its neighbor.
    if (selectedItem_ != 0) { return; }
    selectedIndex_ = std::min(selectedIndex_, ItemCount(view) - 1);
    selectedItem_ = ItemAt(view, selectedIndex_).id;
}

ftxui::Element GameplayUi::Render(WorldView view, const ContentCatalog& content,
    const Localization& localization, const Keymap& keys, const GameplayPresentation& presentation) {
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
    if (!presentation.lan && !mapCache_) { mapCache_ = std::make_shared<TerminalMapCache>(); }
    auto world = presentation.lan ? presentation.lan
        : std::make_shared<MapViewport>(view, std::move(view.cells), content, mapCache_,
            presentation.viewRadius, t("game.ui.cropped"));
    auto body = std::make_shared<GameplayBody>(Panel(t(presentation.lan ? "game.ui.lan" : "game.ui.world"), world),
        Panel(t("game.ui.status"), ftxui::vbox(std::move(status))), Padded(ftxui::paragraph(Frontend(compact))));
    auto background = ftxui::vbox({Padded(ftxui::hbox({Text(t("game.title")) | ftxui::bold,
        ftxui::text("  ·  "), Text(presentation.session) | ftxui::xflex_shrink, ftxui::filler()})),
        ftxui::separatorLight(), body | ftxui::flex, ftxui::separatorLight(),
        Padded(Paragraph(presentation.interacting ? t("game.interact_direction") : presentation.message))
            | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 3)}) | ftxui::borderLight;
    if (!IsOpen()) { return background; }

    ftxui::Elements rows;
    String title;
    String subtitle;
    String count;
    int preferredWidth = 84;
    int* scroll = nullptr;
    int selected = -1;
    if (modal_ == Modal::Inventory) {
        ReconcileInventory(view);
        title = t("game.ui.inventory"); subtitle = t(useSelection_ ? "item.ui.choose_use" : "game.ui.inventory_hint");
        scroll = &inventoryScroll_; preferredWidth = 72;
        count = localization.Format("game.ui.selection_count", {
            {"selected", UnsignedText(ItemCount(view) == 0 ? 0 : selectedIndex_ + 1)},
            {"total", UnsignedText(ItemCount(view))}});
        if (ItemCount(view) == 0) { rows.push_back(Paragraph(t("game.ui.inventory_empty"))); }
        else {
            selected = static_cast<int>(selectedIndex_);
            for (std::size_t i = 0; i < ItemCount(view); ++i) {
                const auto& item = ItemAt(view, i);
                String label = item.id == selectedItem_ ? "> " : "  ";
                label += localization.Format("inventory.entry", {{"id", UnsignedText(item.id)}, {"name", localization.ItemName(content, item.type)}});
                label += ' '; label += t(i < view.inventory.size() ? "item.ui.carried" : "item.ui.ground");
                if (item.parent != 0) { label += localization.Format("item.ui.parent", {{"id", UnsignedText(item.parent)}, {"cap", UnsignedText(item.installation.value)}}); }
                if (item.contained_items != 0) {
                    label += " (";
                    label += localization.Format("inventory.inside", {{"count", UnsignedText(item.contained_items)}}, item.contained_items);
                    label += ')';
                }
                auto row = Text(label);
                if (item.id == selectedItem_) { row = row | ftxui::bold | ftxui::inverted; }
                rows.push_back(row);
            }
        }
        const auto* detail = FindItem(view, itemStage_ == ItemStage::Browse ? selectedItem_ : sourceItem_);
        if (detail) {
            subtitle += ' '; subtitle += localization.Format("item.ui.measure", {{"mass", UnsignedText(detail->massGrams)},
                {"volume", UnsignedText(detail->volumeMl)}, {"used", UnsignedText(detail->usedVolumeMl)}, {"capacity", UnsignedText(detail->capacityVolumeMl)}});
            for (const auto& energy : detail->energy) {
                subtitle += ' '; subtitle += localization.Format("item.ui.energy", {{"cap", UnsignedText(energy.capability.value)}, {"energy", UnsignedText(energy.energy)}, {"capacity", UnsignedText(energy.capacity)}});
            }
            for (const auto& activity : detail->activities) {
                subtitle += ' '; subtitle += localization.Format("item.ui.activity", {{"cap", UnsignedText(activity.capability.value)}, {"mode", on(activity.active)}, {"ticks", UnsignedText(activity.remainingTicks)}});
                const auto activityStatus = localization.RenderItemText(activity.status);
                if (!activityStatus.empty()) { subtitle += ' '; subtitle += activityStatus; }
            }
        }
        count += ' '; count += localization.Format("item.ui.mass", {{"mass", UnsignedText(view.carriedMassGrams)}, {"capacity", UnsignedText(view.carryCapacityGrams)}});
        if (itemStage_ != ItemStage::Browse) {
            rows.clear(); selected = static_cast<int>(itemChoice_); count.clear();
            title = t(itemStage_ == ItemStage::Actions ? "item.ui.actions" : itemStage_ == ItemStage::Item ? "item.ui.target" : itemStage_ == ItemStage::Container ? "item.ui.container" : "item.ui.direction");
            const auto addChoice = [&](std::size_t index, const String& label) {
                auto row = Text(String(index == itemChoice_ ? "> " : "  ") + label);
                if (index == itemChoice_) { row = row | ftxui::bold | ftxui::inverted; }
                rows.push_back(row);
            };
            if (itemStage_ == ItemStage::Actions) {
                for (std::size_t i = 0; i < itemActions_.size(); ++i) {
                    const auto& info = itemActions_[i];
                    String label = localization.ItemActionText(info);
                    label += localization.Format("item.ui.cap_cost", {{"cap", UnsignedText(info.capability.value)}, {"cost", UnsignedText(info.baseCost)}});
                    if (info.availability != ItemUseOutcome::Succeeded) { label += " — "; label += localization.ItemRefusal(info.availability, info.reason); }
                    addChoice(i, label);
                }
                if (managingContents_) {
                    addChoice(itemActions_.size(), t("item.ui.store"));
                    addChoice(itemActions_.size() + 1, t("item.ui.extract"));
                }
            } else if (itemStage_ == ItemStage::Direction) { rows.push_back(Paragraph(t("item.ui.direction_hint"))); }
            else {
                for (std::size_t i = 0; i < itemTargets_.size(); ++i) {
                    const auto* target = FindItem(view, itemTargets_[i]);
                    String label = UnsignedText(itemTargets_[i]); label += ": ";
                    label += target ? localization.ItemName(content, target->type) : t("game.ui.item_gone");
                    addChoice(i, label);
                }
                if (itemTargets_.empty()) { rows.push_back(Paragraph(t("item.ui.no_targets"))); }
            }
        }
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
        group("game.ui.windows", {InputAction::QuickActions, InputAction::Inventory, InputAction::Players,
            InputAction::Settings, InputAction::Help});
        group("game.ui.actions", {InputAction::Use, InputAction::PickUp, InputAction::Drop, InputAction::Interact, InputAction::Wait,
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
    const bool selectable = modal_ == Modal::Inventory || modal_ == Modal::QuickActions;
    ftxui::Elements hints{Hint(directions, t(selectable ? "game.ui.select" : "game.ui.scroll"))};
    if ((modal_ == Modal::Inventory && ItemCount(view) != 0) || modal_ == Modal::QuickActions) {
        hints.push_back(ftxui::text("  |  "));
        hints.push_back(Hint(BindingLabel(keys, localization, InputContext::Menu, InputAction::Accept),
            t(modal_ == Modal::Inventory && itemStage_ == ItemStage::Browse && !useSelection_ ? "action.drop" : "game.ui.activate")));
    }
    if (modal_ == Modal::Inventory && itemStage_ == ItemStage::Browse) {
        hints.push_back(ftxui::text("  |  "));
        hints.push_back(Hint(BindingLabel(keys, localization, InputContext::Items, InputAction::Use), t("action.use")));
        hints.push_back(ftxui::text("  |  "));
        hints.push_back(Hint(BindingLabel(keys, localization, InputContext::Items, InputAction::Apply), t("item.ui.manage")));
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
    const Keymap& keys, const std::function<bool(Command)>& submit, const std::function<void(InputAction)>& activate, const ContentCatalog*) {
    if (!IsOpen()) { return false; }
    // A player change after the last frame invalidates the entire prompt. Do
    // not reinterpret an activation against the new player's first item.
    if (modal_ == Modal::Inventory && selectionPlayer_ != view.playerId) {
        ReconcileInventory(view); Close(); return true;
    }
    const auto chord = DecodeTerminalInput({event.input().data(), event.input().size()});
    auto action = chord ? ResolveAction(keys, InputContext::Menu, *chord) : eastl::optional<InputAction>{};
    if (modal_ == Modal::Inventory && chord) {
        if (auto itemAction = ResolveAction(keys, InputContext::Items, *chord)) {
            if (itemStage_ == ItemStage::Browse || (itemStage_ == ItemStage::Direction
                && (*itemAction == InputAction::MoveDown || *itemAction == InputAction::MoveUp))) { action = itemAction; }
        }
    }
    if (action == InputAction::Cancel) {
        if (modal_ == Modal::Inventory && itemStage_ != ItemStage::Browse) { itemStage_ = ItemStage::Browse; feedback_.clear(); }
        else { Close(); }
        return true;
    }
    if (modal_ == Modal::QuickActions) {
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
    } else if (modal_ == Modal::Inventory) {
        const auto send = [&](Command command) {
            if (!FindItem(view, command.item) || (command.otherItem != 0 && !FindItem(view, command.otherItem))) {
                feedback_ = localization.Text("game.ui.item_gone"); return;
            }
            if (command.kind == CommandKind::use_item) {
                const auto* source = FindItem(view, command.item);
                bool found = false;
                for (const auto& info : source->actions) {
                    if (info.capability == command.capability && info.action == command.itemAction) {
                        found = true;
                        if (info.availability != ItemUseOutcome::Succeeded) { feedback_ = localization.ItemRefusal(info.availability, info.reason); return; }
                    }
                }
                if (!found) { feedback_ = localization.Text("item.ui.action_gone"); return; }
            }
            if (submit(command)) { Close(); }
            else { feedback_ = localization.Text("item.ui.submit_failed"); }
        };
        const auto prepare = [&](ItemActionInfo info) {
            itemCommand_ = {}; itemCommand_.kind = CommandKind::use_item;
            itemCommand_.item = sourceItem_; itemCommand_.capability = info.capability; itemCommand_.itemAction = info.action;
            if (info.availability != ItemUseOutcome::Succeeded) { feedback_ = localization.ItemRefusal(info.availability, info.reason); return; }
            if (info.argument == ItemActionArgument::Direction) { itemStage_ = ItemStage::Direction; return; }
            if (info.argument == ItemActionArgument::None) { send(itemCommand_); return; }
            itemStage_ = ItemStage::Item; itemChoice_ = 0; itemTargets_.clear();
            for (std::size_t i = 0; i < ItemCount(view); ++i) {
                const auto& target = ItemAt(view, i);
                itemTargets_.push_back(target.id);
            }
        };
        if (itemStage_ == ItemStage::Direction) {
            Command command = itemCommand_;
            if (action == InputAction::North) { command.dz = -1; }
            else if (action == InputAction::South) { command.dz = 1; }
            else if (action == InputAction::West) { command.dx = -1; }
            else if (action == InputAction::East) { command.dx = 1; }
            else if (action == InputAction::MoveDown) { command.dy = -1; }
            else if (action == InputAction::MoveUp) { command.dy = 1; }
            else { return true; }
            send(command); return true;
        }
        if (itemStage_ != ItemStage::Browse) {
            const auto size = itemStage_ == ItemStage::Actions ? itemActions_.size() + (managingContents_ ? 2 : 0) : itemTargets_.size();
            if (size != 0 && (action == InputAction::North || action == InputAction::South)) {
                itemChoice_ = action == InputAction::North ? (itemChoice_ + size - 1) % size : (itemChoice_ + 1) % size;
                feedback_.clear();
            } else if (action == InputAction::Accept && size != 0) {
                if (!FindItem(view, sourceItem_)) { feedback_ = localization.Text("game.ui.item_gone"); return true; }
                if (itemStage_ == ItemStage::Actions) {
                    if (itemChoice_ < itemActions_.size()) { prepare(itemActions_[itemChoice_]); }
                    else if (itemChoice_ == itemActions_.size() + 1) {
                        Command command; command.kind = CommandKind::store_item; command.item = sourceItem_; send(command);
                    } else {
                        itemCommand_ = {}; itemCommand_.kind = CommandKind::store_item; itemCommand_.item = sourceItem_;
                        itemTargets_.clear(); itemChoice_ = 0; itemStage_ = ItemStage::Container;
                        for (std::size_t i = 0; i < ItemCount(view); ++i) {
                            const auto& target = ItemAt(view, i);
                            if (target.id != sourceItem_ && target.capacityVolumeMl != 0) { itemTargets_.push_back(target.id); }
                        }
                    }
                } else { Command command = itemCommand_; command.otherItem = itemTargets_[itemChoice_]; send(command); }
            }
            return true;
        }
        if (action == InputAction::Accept || action == InputAction::Use || action == InputAction::Apply) {
            if (selectedItem_ == 0) { ReconcileInventory(view); return true; }
            const auto* source = FindItem(view, selectedItem_);
            if (!source) { feedback_ = localization.Text("game.ui.item_gone"); return true; }
            if (action == InputAction::Accept && !useSelection_) {
                bool carried = false;
                for (const auto& item : view.inventory) { carried |= item.id == selectedItem_ && item.parent == 0; }
                if (!carried) { feedback_ = localization.Text("item.ui.drop_root"); return true; }
                send({CommandKind::drop, 0, 0, selectedItem_}); return true;
            }
            sourceItem_ = source->id; itemActions_.clear(); itemChoice_ = 0; inventoryScroll_ = 0;
            managingContents_ = action == InputAction::Apply;
            const auto category = managingContents_ ? ItemActionCategory::ManageContents : ItemActionCategory::Operate;
            for (const auto& info : source->actions) { if (info.category == category) { itemActions_.push_back(info); } }
            if (action != InputAction::Apply && itemActions_.size() == 1) { prepare(itemActions_[0]); }
            else if (action != InputAction::Apply && itemActions_.empty()) { feedback_ = localization.Text("item.ui.no_actions"); }
            else { itemStage_ = ItemStage::Actions; }
            return true;
        }
        ReconcileInventory(view);
        if (ItemCount(view) != 0 && (action == InputAction::North || action == InputAction::South)) {
            selectedIndex_ = action == InputAction::North
                ? (selectedIndex_ + ItemCount(view) - 1) % ItemCount(view) : (selectedIndex_ + 1) % ItemCount(view);
            selectedItem_ = ItemAt(view, selectedIndex_).id; feedback_.clear();
        }
    } else {
        int& scroll = modal_ == Modal::Players ? playersScroll_ : controlsScroll_;
        if (action == InputAction::North) { scroll = std::max(0, scroll - 1); }
        else if (action == InputAction::South) { ++scroll; }
    }
    return true;
}

}
