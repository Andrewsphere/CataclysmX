#include "terminal/debug_menu.h"

#include "client/ai_debug.h"
#include "client/worldgen_debug.h"
#include "terminal/render.h"
#include "terminal/keyboard.h"

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace cataclysmx {
namespace {

TileCoord ClampPosition(TileCoord position) {
    return {std::clamp(position.x, -world_coordinate_limit, world_coordinate_limit),
        std::clamp(position.z, -world_coordinate_limit, world_coordinate_limit)};
}

std::string Coordinates(TileCoord position) {
    return std::to_string(position.x) + "," + std::to_string(position.z);
}

// FTXUI's ComponentBase is the frontend's component interface. All inspection
// state belongs here; the shared Component lifetime is managed by FTXUI itself.
class WorldgenDebugMenu final : public ftxui::ComponentBase {
public:
    WorldgenDebugMenu(const World& world, TileCoord playerPosition, std::function<void()> onClose,
        const Localization* localization, const Keymap* keymap)
        : world_(world), initialPosition_(ClampPosition(playerPosition)), cursor_(initialPosition_),
          aiInitialPosition_{initialPosition_.x, initialPosition_.z, std::clamp(playerPosition.y, world_min_y, world_max_y)},
          aiCursor_(aiInitialPosition_),
          onClose_(std::move(onClose)), localization_(localization), keymap_(keymap) {
        if (Available()) { RebuildPlan(); }
    }

    ftxui::Element OnRender() override {
        if (aiMode_) { return RenderAi(); }
        ftxui::Elements lines{
            ftxui::text(T("debug.title", "Debug menu | World generation")) | ftxui::bold,
            ftxui::text(F("debug.seed", "Seed: " + std::to_string(world_.GetSeed()) + " | Generation version: "
                + std::to_string(world_.GetGenerationVersion()), {{"seed", UnsignedText(world_.GetSeed())},
                    {"version", UnsignedText(world_.GetGenerationVersion())}})),
            ftxui::text(T("debug.baseline", "Surface (Y=0) generated preview: excludes saved edits and entities")),
        };
        if (!Available()) {
            lines.push_back(ftxui::text(F("debug.unavailable", "Generated preview unavailable for generation "
                + std::to_string(world_.GetGenerationVersion()) + ".", {{"version", UnsignedText(world_.GetGenerationVersion())}})));
            lines.push_back(ftxui::paragraph(T("debug.legacy", "This save uses a different generation algorithm; "
                "the current regional preview would not represent its terrain.")));
            lines.push_back(ftxui::filler());
        } else {
            ftxui::Elements layers;
            for (int i = 0; i < 5; ++i) {
                const auto layer = static_cast<WorldgenDebugLayer>(i);
                const auto action = static_cast<InputAction>(static_cast<unsigned>(InputAction::Layer1) + i);
                const auto hint = keymap_ ? BindingHint(*keymap_, InputContext::Debug, action) : NumberText(i + 1);
                auto label = ftxui::text(std::string(hint.data(), hint.size()) + " " + LayerName(layer) + "  ");
                if (layer == layer_) { label = label | ftxui::bold | ftxui::inverted; }
                layers.push_back(std::move(label));
            }
            lines.push_back(ftxui::hbox(std::move(layers)));
            lines.push_back(ftxui::text(F("debug.layer", std::string("Layer: ") + WorldgenDebugLayerName(layer_)
                + " | Scale: " + std::to_string(scale_) + " | Cursor: " + Coordinates(cursor_),
                {{"layer", String(LayerName(layer_).c_str())}, {"scale", NumberText(scale_)},
                    {"x", NumberText(cursor_.x)}, {"z", NumberText(cursor_.z)}})));
            lines.push_back(ftxui::text(F("debug.region", "Region: " + Coordinates({plan_.region.x, plan_.region.z}),
                {{"x", NumberText(plan_.region.x)}, {"z", NumberText(plan_.region.z)}})));
            lines.push_back(ftxui::separator());
            ftxui::Elements rows;
            const int selectedX = (cursor_.x - image_.bounds.min.x) / scale_;
            const int selectedZ = (cursor_.z - image_.bounds.min.z) / scale_;
            for (int z = 0; z < image_.height; ++z) {
                ftxui::Elements cells;
                for (int x = 0; x < image_.width; ++x) {
                    auto cell = RenderTerrainCell(image_.cells[static_cast<std::size_t>(z * image_.width + x)]);
                    if (x == selectedX && z == selectedZ) { cell = cell | ftxui::inverted | ftxui::focus; }
                    cells.push_back(std::move(cell));
                }
                rows.push_back(ftxui::hbox(std::move(cells)));
            }
            ftxui::Elements details;
            for (std::size_t i = 0; i < details_.size(); ++i) {
                // EASTL strings cross into std::string only at the FTXUI boundary.
                auto line = ftxui::paragraph(std::string(details_[i].data(), details_[i].size()));
                if (i == detailLine_) { line = line | ftxui::focus; }
                details.push_back(std::move(line));
            }
            lines.push_back(ftxui::hbox({
                ftxui::vbox(std::move(rows)) | ftxui::frame | ftxui::flex,
                ftxui::separator(),
                ftxui::vbox(std::move(details)) | ftxui::yframe | ftxui::flex,
            }) | ftxui::flex);
            constexpr eastl::string_view legends[] = {"debug.legend.roads", "debug.legend.parcels", "debug.legend.ownership", "debug.legend.biomes", "debug.legend.terrain"};
            lines.push_back(ftxui::paragraph(T(legends[static_cast<unsigned>(layer_)], WorldgenDebugLegend(layer_))));
            if (!keymap_) {
                lines.push_back(ftxui::text("Arrows/hjkl move; HJKL jump 256; Home/c recenter"));
                lines.push_back(ftxui::text("1-5/Tab layers; +/- zoom; PgUp/PgDn details"));
            } else { lines.push_back(ftxui::paragraph(Controls())); }
        }
        lines.push_back(ftxui::text(ToggleHint()));
        lines.push_back(ftxui::text(keymap_ ? Hint(InputAction::Debug) + " / Escape" : "F11 / Escape / q: close"));
        return ftxui::vbox(std::move(lines)) | ftxui::border;
    }

    bool Focusable() const override { return true; }

    bool OnEvent(ftxui::Event event) override {
        const auto chord = DecodeTerminalInput({event.input().data(), event.input().size()});
        const auto action = chord ? ResolveAction(keymap_ ? *keymap_ : defaultKeys_, InputContext::Debug, *chord)
            : eastl::optional<InputAction>{};
        if (action == InputAction::Debug || action == InputAction::Cancel) {
            if (onClose_) { onClose_(); }
            return true;
        }
        if (action == InputAction::Inspect) {
            aiMode_ = !aiMode_;
            if (aiMode_) { CaptureAi(); }
            return true;
        }
        if (aiMode_) { return OnAiEvent(action); }
        if (!Available()) { return true; }
        const auto oldLayer = layer_;
        const int oldScale = scale_;
        const auto oldCursor = cursor_;
        if (action) { switch (*action) {
        case InputAction::West: cursor_.x -= scale_; break;
        case InputAction::East: cursor_.x += scale_; break;
        case InputAction::North: cursor_.z -= scale_; break;
        case InputAction::South: cursor_.z += scale_; break;
        case InputAction::JumpWest: cursor_.x -= region_size; break;
        case InputAction::JumpEast: cursor_.x += region_size; break;
        case InputAction::JumpNorth: cursor_.z -= region_size; break;
        case InputAction::JumpSouth: cursor_.z += region_size; break;
        case InputAction::Recenter: cursor_ = initialPosition_; break;
        case InputAction::ZoomIn: scale_ = std::max(1, scale_ / 2); break;
        case InputAction::ZoomOut: scale_ = std::min(16, scale_ * 2); break;
        case InputAction::Next: layer_ = static_cast<WorldgenDebugLayer>((static_cast<int>(layer_) + 1) % 5); break;
        case InputAction::Previous: layer_ = static_cast<WorldgenDebugLayer>((static_cast<int>(layer_) + 4) % 5); break;
        case InputAction::DetailsDown: detailLine_ = std::min(detailLine_ + 1, details_.size() - 1); break;
        case InputAction::DetailsUp: if (detailLine_ > 0) { --detailLine_; } break;
        case InputAction::Layer1: case InputAction::Layer2: case InputAction::Layer3: case InputAction::Layer4: case InputAction::Layer5:
            layer_ = static_cast<WorldgenDebugLayer>(static_cast<unsigned>(*action) - static_cast<unsigned>(InputAction::Layer1)); break;
        default: break;
        } }
        // Clamp before RegionOf: its precondition excludes out-of-world tiles.
        cursor_ = ClampPosition(cursor_);
        if (RegionOf(cursor_) != plan_.region) { RebuildPlan(); }
        else {
            if (scale_ != oldScale || layer_ != oldLayer) { RebuildImage(); }
            if (cursor_ != oldCursor) { RebuildDetails(); }
        }
        // This is a modal, including unknown keys and gameplay commands.
        return true;
    }

private:
    std::string ToggleHint() const {
        return (keymap_ ? Hint(InputAction::Inspect) : "a") + ": AI / world generation";
    }
    ftxui::Element RenderAi() const {
        ftxui::Elements lines{
            ftxui::text("Debug menu | AI inspector") | ftxui::bold,
            ftxui::text("Live resident map | Last-decision overlay"),
            ftxui::text("Cursor: " + std::to_string(aiCursor_.x) + "," + std::to_string(aiCursor_.z)
                + "," + std::to_string(aiCursor_.y) + " (X,Z,Y) | Tick: " + std::to_string(aiView_.tick)),
            ftxui::separator(),
        };
        ftxui::Elements rows;
        const auto& image = aiView_.image;
        const int selectedX = aiCursor_.x - image.bounds.min.x;
        const int selectedZ = aiCursor_.z - image.bounds.min.z;
        for (int z = 0; z < image.height; ++z) {
            ftxui::Elements cells;
            for (int x = 0; x < image.width; ++x) {
                auto cell = RenderTerrainCell(image.cells[static_cast<std::size_t>(z * image.width + x)]);
                if (x == selectedX && z == selectedZ) { cell = cell | ftxui::inverted | ftxui::focus; }
                cells.push_back(std::move(cell));
            }
            rows.push_back(ftxui::hbox(std::move(cells)));
        }
        ftxui::Elements details;
        for (std::size_t i = 0; i < aiView_.details.size(); ++i) {
            const auto& text = aiView_.details[i];
            auto line = ftxui::paragraph(std::string(text.data(), text.size()));
            if (i == aiDetailLine_) { line = line | ftxui::focus; }
            details.push_back(std::move(line));
        }
        lines.push_back(ftxui::hbox({ftxui::vbox(std::move(rows)) | ftxui::frame | ftxui::flex,
            ftxui::separator(), ftxui::vbox(std::move(details)) | ftxui::yframe | ftxui::flex}) | ftxui::flex);
        lines.push_back(ftxui::paragraph("Green eligible; red blocked; yellow chosen; blue decision origin; purple goal; orange backtracking"));
        lines.push_back(ftxui::paragraph(keymap_ ? AiControls() :
            "Arrows/hjkl move; Alt+Up/Down or >/< change Y; Home/c recenter; PgUp/PgDn details"));
        lines.push_back(ftxui::text(ToggleHint()));
        lines.push_back(ftxui::text(keymap_ ? Hint(InputAction::Debug) + " / Escape" : "F11 / Escape / q: close"));
        return ftxui::vbox(std::move(lines)) | ftxui::border;
    }
    std::string AiControls() const {
        std::string text;
        for (const auto action : {InputAction::West, InputAction::East, InputAction::North, InputAction::South,
            InputAction::MoveUp, InputAction::MoveDown, InputAction::Recenter, InputAction::DetailsUp, InputAction::DetailsDown}) {
            if (!text.empty()) { text += " | "; }
            String key = "action."; const auto name = ActionName(action); key.append(name.data(), name.size());
            text += Hint(action) + " " + T({key.data(), key.size()}, name);
        }
        return text;
    }
    void CaptureAi() {
        CaptureAiDebugView(world_, aiCursor_, aiView_);
        aiDetailLine_ = 0;
    }
    bool OnAiEvent(eastl::optional<InputAction> action) {
        const auto oldCursor = aiCursor_;
        if (action) { switch (*action) {
        case InputAction::West: --aiCursor_.x; break;
        case InputAction::East: ++aiCursor_.x; break;
        case InputAction::North: --aiCursor_.z; break;
        case InputAction::South: ++aiCursor_.z; break;
        case InputAction::MoveUp: aiCursor_.y = std::min(world_max_y, aiCursor_.y + 1); break;
        case InputAction::MoveDown: aiCursor_.y = std::max(world_min_y, aiCursor_.y - 1); break;
        case InputAction::Recenter: aiCursor_ = aiInitialPosition_; break;
        case InputAction::DetailsDown:
            if (aiDetailLine_ + 1 < aiView_.details.size()) { ++aiDetailLine_; } break;
        case InputAction::DetailsUp: if (aiDetailLine_ > 0) { --aiDetailLine_; } break;
        default: break;
        } }
        aiCursor_.x = std::clamp(aiCursor_.x, -world_coordinate_limit, world_coordinate_limit);
        aiCursor_.z = std::clamp(aiCursor_.z, -world_coordinate_limit, world_coordinate_limit);
        if (aiCursor_ != oldCursor) { CaptureAi(); }
        return true;
    }
    std::string T(eastl::string_view key, eastl::string_view fallback) const {
        if (!localization_) { return {fallback.data(), fallback.size()}; }
        const auto text = localization_->Text(key, fallback); return {text.data(), text.size()};
    }
    std::string F(eastl::string_view key, const std::string& fallback, std::initializer_list<TextArgument> arguments) const {
        if (!localization_) { return fallback; }
        const auto text = localization_->Format(key, arguments); return {text.data(), text.size()};
    }
    std::string LayerName(WorldgenDebugLayer layer) const {
        constexpr eastl::string_view names[] = {"debug.layer.roads", "debug.layer.parcels", "debug.layer.ownership", "debug.layer.biomes", "debug.layer.terrain"};
        return T(names[static_cast<unsigned>(layer)], WorldgenDebugLayerName(layer));
    }
    std::string Hint(InputAction action) const {
        const auto text = BindingHint(*keymap_, InputContext::Debug, action); return {text.data(), text.size()};
    }
    std::string Controls() const {
        std::string text;
        for (const auto action : {InputAction::West, InputAction::East, InputAction::North, InputAction::South,
            InputAction::JumpWest, InputAction::JumpEast, InputAction::Recenter, InputAction::Next,
            InputAction::ZoomIn, InputAction::ZoomOut, InputAction::DetailsUp, InputAction::DetailsDown}) {
            if (!text.empty()) { text += " | "; }
            String key = "action."; const auto name = ActionName(action); key.append(name.data(), name.size());
            text += Hint(action) + " " + T({key.data(), key.size()}, name);
        }
        return text;
    }
    bool Available() const { return world_.GetGenerationVersion() == world_generation_version; }

    void RebuildImage() { image_ = BuildWorldgenDebugView(world_.GetContent(), plan_, layer_, scale_); }
    void RebuildDetails() {
        details_ = WorldgenDebugDetails(world_.GetContent(), plan_, cursor_, localization_);
        detailLine_ = 0;
    }
    void RebuildPlan() {
        plan_ = GenerateRegionPlan(world_.GetContent(), world_.GetSeed(), RegionOf(cursor_));
        RebuildImage();
        RebuildDetails();
    }

    const World& world_;
    TileCoord initialPosition_;
    TileCoord cursor_;
    TileCoord aiInitialPosition_;
    TileCoord aiCursor_;
    AiDebugView aiView_;
    std::size_t aiDetailLine_ = 0;
    bool aiMode_ = false;
    std::function<void()> onClose_;
    WorldgenDebugLayer layer_ = WorldgenDebugLayer::Roads;
    int scale_ = 8;
    RegionPlan plan_;
    WorldgenDebugView image_;
    Vector<String> details_;
    std::size_t detailLine_ = 0;
    const Localization* localization_ = nullptr;
    const Keymap* keymap_ = nullptr;
    Keymap defaultKeys_ = DefaultKeymap();
};

}

ftxui::Component CreateWorldgenDebugMenu(const World& world, TileCoord playerPosition,
    std::function<void()> onClose, const Localization* localization, const Keymap* keymap) {
    return ftxui::Make<WorldgenDebugMenu>(world, playerPosition, std::move(onClose), localization, keymap);
}

}
