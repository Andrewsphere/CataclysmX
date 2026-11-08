#include "terminal/gameplay_screen.h"

#include "client/text.h"
#include "terminal/keyboard.h"
#include "terminal/render.h"

#include <ftxui/dom/node.hpp>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace cataclysmx {
namespace {

constexpr InputAction quickActions[] = {
    InputAction::Inventory, InputAction::PickUp, InputAction::Interact, InputAction::Wait,
    InputAction::Players, InputAction::Settings, InputAction::Help, InputAction::Save,
    InputAction::ToggleIdle, InputAction::ToggleRate, InputAction::RateDown, InputAction::RateUp,
    InputAction::Debug
};

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
std::string Frontend(const String& text) { return {text.data(), text.size()}; }
ftxui::Element Text(const String& text) { return ftxui::text(Frontend(text)); }
ftxui::Element Paragraph(const String& text) { return ftxui::paragraph(Frontend(text)); }
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
    MapViewport(const WorldView& view, const ContentCatalog& content, const String& cropped)
        : Node(ftxui::Elements{}), width_(view.width), height_(view.height),
          playerX_(static_cast<int>(std::clamp<std::int64_t>(
              static_cast<std::int64_t>(view.player_position.x) - view.origin.x, 0, std::max(0, view.width - 1)))),
          playerY_(static_cast<int>(std::clamp<std::int64_t>(
              static_cast<std::int64_t>(view.player_position.z) - view.origin.z, 0, std::max(0, view.height - 1)))) {
        ftxui::Elements rows;
        for (int y = 0; y < height_; ++y) {
            ftxui::Elements cells;
            for (int x = 0; x < width_; ++x) {
                cells.push_back(RenderTerrainCell(CellAppearance(
                    view.cells[static_cast<std::size_t>(y * width_ + x)], content)));
            }
            rows.push_back(ftxui::hbox(std::move(cells)));
        }
        children_ = {ftxui::vbox(std::move(rows)), Text(cropped)};
    }
    void ComputeRequirement() override { Node::ComputeRequirement(); Flexible(requirement_); }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        const int width = std::max(0, box.x_max - box.x_min + 1);
        const int height = std::max(0, box.y_max - box.y_min + 1);
        cropped_ = width_ > width || height_ > height;
        mapBox_ = box;
        if (cropped_ && height > 1) { --mapBox_.y_max; }
        const int visibleWidth = std::min(width_, width);
        const int mapHeight = std::max(0, mapBox_.y_max - mapBox_.y_min + 1);
        const int visibleHeight = std::min(height_, mapHeight);
        const int startX = std::clamp(playerX_ - visibleWidth / 2, 0, std::max(0, width_ - visibleWidth));
        const int startY = std::clamp(playerY_ - visibleHeight / 2, 0, std::max(0, height_ - visibleHeight));
        auto terrain = mapBox_;
        terrain.x_min += (width - visibleWidth) / 2 - startX;
        terrain.y_min += (mapHeight - visibleHeight) / 2 - startY;
        terrain.x_max = terrain.x_min + width_ - 1;
        terrain.y_max = terrain.y_min + height_ - 1;
        children_[0]->SetBox(terrain);
        auto indicator = box; indicator.y_min = box.y_max;
        children_[1]->SetBox(indicator);
    }
    void Render(ftxui::Screen& screen) override {
        const auto previous = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(previous, mapBox_);
        if (!screen.stencil.IsEmpty()) { children_[0]->Render(screen); }
        if (cropped_ && box_.y_max > box_.y_min) {
            screen.stencil = ftxui::Box::Intersection(previous, box_);
            if (!screen.stencil.IsEmpty()) { children_[1]->Render(screen); }
        }
        screen.stencil = previous;
    }
private:
    int width_;
    int height_;
    int playerX_;
    int playerY_;
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
    return localization.Text(View(key));
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
        || action == InputAction::Help || action == InputAction::QuickActions;
}
void GameplayUi::ReconcileInventory(const WorldView& view) {
    if (view.inventory.empty()) { selectedItem_ = 0; selectedIndex_ = 0; inventoryScroll_ = 0; return; }
    for (std::size_t i = 0; i < view.inventory.size(); ++i) {
        if (view.inventory[i].id == selectedItem_) { selectedIndex_ = i; return; }
    }
    selectedIndex_ = std::min(selectedIndex_, view.inventory.size() - 1);
    selectedItem_ = view.inventory[selectedIndex_].id;
}

ftxui::Element GameplayUi::Render(const WorldView& view, const ContentCatalog& content,
    const Localization& localization, const Keymap& keys, const GameplayPresentation& presentation) {
    const auto t = [&](eastl::string_view key) { return localization.Text(key); };
    const auto on = [&](bool value) { return t(value ? "game.on" : "game.off"); };
    const String state = t(!presentation.pending.empty() ? "game.ui.awaiting"
        : view.commandQueued ? "game.ui.queued" : view.remainingTicks != 0 ? "game.ui.busy" : "game.ui.ready");
    auto stateField = StatusField(t("game.ui.action_state"), state);
    if (!presentation.pending.empty() || view.commandQueued || view.remainingTicks != 0) {
        stateField = stateField | ftxui::color(ftxui::Color::Yellow);
    }
    const String position = localization.Format("game.ui.coordinates", {
        {"x", NumberText(view.player_position.x)}, {"z", NumberText(view.player_position.z)}});
    ftxui::Elements status{stateField,
        StatusField(t("game.ui.cooldown"), localization.Format("game.ui.ticks", {{"ticks", UnsignedText(view.remainingTicks)}})),
        StatusField(t("game.ui.speed"), UnsignedText(view.speed)), ftxui::separatorLight(),
        Text(t("game.ui.location")) | ftxui::bold, Paragraph(position),
        StatusField(t("game.ui.tick"), UnsignedText(view.tick)), ftxui::separatorLight(),
        Text(t("game.ui.pacing")) | ftxui::bold,
        StatusField(t("game.ui.tick_rate"), localization.Format("game.ui.per_second", {{"rate", UnsignedText(view.pacing.ticksPerSecond)}})),
        StatusField(t("game.ui.rate_limit"), on(view.pacing.rateLimited)),
        StatusField(t("game.ui.idle_pause"), on(view.pacing.pauseWhenIdle))};
    String compact = localization.Format("game.ui.compact_state", {{"position", position},
        {"tick", UnsignedText(view.tick)}, {"state", state}});
    compact += '\n';
    const String compactPacing = localization.Format("game.ui.compact_pacing", {{"rate", UnsignedText(view.pacing.ticksPerSecond)},
        {"limit", on(view.pacing.rateLimited)}, {"idle", on(view.pacing.pauseWhenIdle)},
        {"speed", UnsignedText(view.speed)}, {"delay", UnsignedText(view.remainingTicks)}});
    if (!presentation.pending.empty()) {
        const auto pending = localization.Format("game.ui.pending", {{"value", presentation.pending}});
        status.push_back(ftxui::separatorLight()); status.push_back(Paragraph(pending));
        compact += pending; compact += " | ";
    }
    compact += compactPacing;
    auto world = presentation.lan ? presentation.lan
        : std::make_shared<MapViewport>(view, content, t("game.ui.cropped"));
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
        title = t("game.ui.inventory"); subtitle = t("game.ui.inventory_hint");
        scroll = &inventoryScroll_; preferredWidth = 72;
        count = localization.Format("game.ui.selection_count", {
            {"selected", UnsignedText(view.inventory.empty() ? 0 : selectedIndex_ + 1)},
            {"total", UnsignedText(view.inventory.size())}});
        if (view.inventory.empty()) { rows.push_back(Paragraph(t("game.ui.inventory_empty"))); }
        else {
            selected = static_cast<int>(selectedIndex_);
            for (std::size_t i = 0; i < view.inventory.size(); ++i) {
                const auto& item = view.inventory[i];
                String label = i == selectedIndex_ ? "> " : "  ";
                label += localization.Format("inventory.entry", {{"id", UnsignedText(item.id)}, {"name", localization.ItemName(item.kind)}});
                if (item.contained_items != 0) {
                    label += " (";
                    label += localization.Format("inventory.inside", {{"count", UnsignedText(item.contained_items)}}, item.contained_items);
                    label += ')';
                }
                auto row = Text(label);
                if (i == selectedIndex_) { row = row | ftxui::bold | ftxui::inverted; }
                rows.push_back(row);
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
        group("game.ui.actions", {InputAction::PickUp, InputAction::Drop, InputAction::Interact, InputAction::Wait});
        group("game.ui.movement", {InputAction::West, InputAction::North, InputAction::South, InputAction::East});
        group("game.ui.pacing", {InputAction::RateDown, InputAction::RateUp, InputAction::ToggleRate, InputAction::ToggleIdle});
        group("game.ui.system", {InputAction::Save, InputAction::Debug, InputAction::Quit, InputAction::Cancel});
        rows.push_back(ftxui::text("")); rows.push_back(Text(t("context.interaction")) | ftxui::bold);
        for (const auto& binding : keys) {
            if (binding.context != InputContext::Interaction) { continue; }
            rows.push_back(BindingRow(ActionLabel(localization, binding.action),
                BindingLabel(keys, localization, binding.context, binding.action)));
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
    if ((modal_ == Modal::Inventory && !view.inventory.empty()) || modal_ == Modal::QuickActions) {
        hints.push_back(ftxui::text("  |  "));
        hints.push_back(Hint(BindingLabel(keys, localization, InputContext::Menu, InputAction::Accept),
            t(modal_ == Modal::Inventory ? "action.drop" : "game.ui.activate")));
    }
    hints.push_back(ftxui::filler());
    String cancel = "Escape";
    const auto configuredCancel = BindingHint(keys, InputContext::Menu, InputAction::Cancel);
    if (configuredCancel != "Escape" && configuredCancel != "?") { cancel += '/'; cancel += configuredCancel; }
    auto header = ftxui::hbox({Text(title) | ftxui::bold | ftxui::xflex_shrink,
        ftxui::text("  "), ftxui::filler(), Text(count) | ftxui::bold});
    ftxui::Elements window{Padded(header), Padded(Paragraph(subtitle)), ftxui::separatorLight(), Padded(contentPane) | ftxui::flex};
    if (!feedback_.empty()) { window.push_back(Padded(Paragraph(feedback_) | ftxui::color(ftxui::Color::Red))); }
    window.push_back(ftxui::separatorLight()); window.push_back(Padded(ftxui::hbox(std::move(hints))));
    window.push_back(Padded(Hint(cancel, t("settings.ui.close"))));
    return std::make_shared<ModalOverlay>(background, ftxui::vbox(std::move(window)) | ftxui::borderLight, preferredWidth);
}

bool GameplayUi::HandleEvent(const ftxui::Event& event, const WorldView& view, const Localization& localization,
    const Keymap& keys, const std::function<bool(Command)>& submit, const std::function<void(InputAction)>& activate) {
    if (!IsOpen()) { return false; }
    const auto chord = DecodeTerminalInput({event.input().data(), event.input().size()});
    const auto action = chord ? ResolveAction(keys, InputContext::Menu, *chord) : eastl::optional<InputAction>{};
    if (action == InputAction::Cancel) { Close(); return true; }
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
        // Validate the exact selection from the last frame before reconciliation.
        // A snapshot arriving between rendering and Enter must not drop a neighbor.
        if (action == InputAction::Accept && selectedItem_ != 0) {
            bool present = false;
            for (const auto& item : view.inventory) { present |= item.id == selectedItem_; }
            if (!present) {
                ReconcileInventory(view); feedback_ = localization.Text("game.ui.item_gone"); return true;
            }
            const Command command{CommandKind::drop, 0, 0, selectedItem_};
            if (submit(command)) { Close(); }
            else { feedback_ = localization.Text("game.ui.drop_failed"); }
            return true;
        }
        ReconcileInventory(view);
        if (!view.inventory.empty() && (action == InputAction::North || action == InputAction::South)) {
            selectedIndex_ = action == InputAction::North
                ? (selectedIndex_ + view.inventory.size() - 1) % view.inventory.size()
                : (selectedIndex_ + 1) % view.inventory.size();
            selectedItem_ = view.inventory[selectedIndex_].id; feedback_.clear();
        }
    } else {
        int& scroll = modal_ == Modal::Players ? playersScroll_ : controlsScroll_;
        if (action == InputAction::North) { scroll = std::max(0, scroll - 1); }
        else if (action == InputAction::South) { ++scroll; }
    }
    return true;
}

}
