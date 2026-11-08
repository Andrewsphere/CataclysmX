#include "terminal/debug_menu.h"

#include "client/worldgen_debug.h"
#include "terminal/render.h"

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
    WorldgenDebugMenu(const World& world, TileCoord playerPosition, std::function<void()> onClose)
        : world_(world), initialPosition_(ClampPosition(playerPosition)), cursor_(initialPosition_),
          onClose_(std::move(onClose)) {
        if (Available()) { RebuildPlan(); }
    }

    ftxui::Element OnRender() override {
        ftxui::Elements lines{
            ftxui::text("Debug menu | World generation") | ftxui::bold,
            ftxui::text("Seed: " + std::to_string(world_.GetSeed()) + " | Generation version: "
                + std::to_string(world_.GetGenerationVersion())),
            ftxui::text("Generated preview: excludes saved edits and entities"),
        };
        if (!Available()) {
            lines.push_back(ftxui::text("Generated preview unavailable for generation "
                + std::to_string(world_.GetGenerationVersion()) + "."));
            lines.push_back(ftxui::paragraph("This save uses a different generation algorithm; "
                "the current regional preview would not represent its terrain."));
            lines.push_back(ftxui::filler());
        } else {
            ftxui::Elements layers;
            for (int i = 0; i < 5; ++i) {
                const auto layer = static_cast<WorldgenDebugLayer>(i);
                auto label = ftxui::text(std::to_string(i + 1) + " " + WorldgenDebugLayerName(layer) + "  ");
                if (layer == layer_) { label = label | ftxui::bold | ftxui::inverted; }
                layers.push_back(std::move(label));
            }
            lines.push_back(ftxui::hbox(std::move(layers)));
            lines.push_back(ftxui::text(std::string("Layer: ") + WorldgenDebugLayerName(layer_)
                + " | Scale: " + std::to_string(scale_) + " | Cursor: " + Coordinates(cursor_)));
            lines.push_back(ftxui::text("Region: " + Coordinates({plan_.region.x, plan_.region.z})));
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
            lines.push_back(ftxui::paragraph(WorldgenDebugLegend(layer_)));
            lines.push_back(ftxui::text("Arrows/hjkl move; HJKL jump 256; Home/c recenter"));
            lines.push_back(ftxui::text("1-5/Tab layers; +/- zoom; PgUp/PgDn details"));
        }
        lines.push_back(ftxui::text("F11 / Escape / q: close"));
        return ftxui::vbox(std::move(lines)) | ftxui::border;
    }

    bool Focusable() const override { return true; }

    bool OnEvent(ftxui::Event event) override {
        if (event == ftxui::Event::F11 || event == ftxui::Event::Escape || event == ftxui::Event::Character('q')) {
            if (onClose_) { onClose_(); }
            return true;
        }
        if (!Available()) { return true; }
        const auto oldLayer = layer_;
        const int oldScale = scale_;
        const auto oldCursor = cursor_;
        if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('h')) { cursor_.x -= scale_; }
        else if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('l')) { cursor_.x += scale_; }
        else if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) { cursor_.z -= scale_; }
        else if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) { cursor_.z += scale_; }
        else if (event == ftxui::Event::Character('H')) { cursor_.x -= region_size; }
        else if (event == ftxui::Event::Character('L')) { cursor_.x += region_size; }
        else if (event == ftxui::Event::Character('K')) { cursor_.z -= region_size; }
        else if (event == ftxui::Event::Character('J')) { cursor_.z += region_size; }
        else if (event == ftxui::Event::Home || event == ftxui::Event::Character('c')) { cursor_ = initialPosition_; }
        else if (event == ftxui::Event::Character('+')) { scale_ = std::max(1, scale_ / 2); }
        else if (event == ftxui::Event::Character('-')) { scale_ = std::min(16, scale_ * 2); }
        else if (event == ftxui::Event::Tab) { layer_ = static_cast<WorldgenDebugLayer>((static_cast<int>(layer_) + 1) % 5); }
        else if (event == ftxui::Event::TabReverse) { layer_ = static_cast<WorldgenDebugLayer>((static_cast<int>(layer_) + 4) % 5); }
        else if (event == ftxui::Event::PageDown) { detailLine_ = std::min(detailLine_ + 1, details_.size() - 1); }
        else if (event == ftxui::Event::PageUp) { if (detailLine_ > 0) { --detailLine_; } }
        else {
            for (int i = 0; i < 5; ++i) {
                if (event == ftxui::Event::Character(static_cast<char>('1' + i))) {
                    layer_ = static_cast<WorldgenDebugLayer>(i);
                    break;
                }
            }
        }
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
    bool Available() const { return world_.GetGenerationVersion() == world_generation_version; }

    void RebuildImage() { image_ = BuildWorldgenDebugView(world_.GetContent(), plan_, layer_, scale_); }
    void RebuildDetails() {
        details_ = WorldgenDebugDetails(world_.GetContent(), plan_, cursor_);
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
    std::function<void()> onClose_;
    WorldgenDebugLayer layer_ = WorldgenDebugLayer::Roads;
    int scale_ = 8;
    RegionPlan plan_;
    WorldgenDebugView image_;
    Vector<String> details_;
    std::size_t detailLine_ = 0;
};

}

ftxui::Component CreateWorldgenDebugMenu(const World& world, TileCoord playerPosition,
    std::function<void()> onClose) {
    return ftxui::Make<WorldgenDebugMenu>(world, playerPosition, std::move(onClose));
}

}
