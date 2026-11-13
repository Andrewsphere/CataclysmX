#include "sdl/renderer.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <thread>

namespace {
using namespace cataclysmx;

TEST_CASE("SDL map hides cells and orders overlays", "[sdl][render]") {
    ViewCell cell;
    cell.tile.terrain = TestTerrain("air");
    cell.actor = 42;
    cell.player = true;
    cell.ground_items = 1;
    cell.object = 1;
    cell.objectAppearance.kind = ObjectKind::Generic;
    CHECK(sdl::MapAppearance(cell, 42, TestContent()).symbol == " ");
    cell.terrainVisibility = cell.overlayVisibility = Visibility::Visible;
    CHECK(sdl::MapAppearance(cell, 42, TestContent()).symbol == "@");
    CHECK(sdl::MapAppearance(cell, 43, TestContent()).symbol == "P");
    cell.player = false;
    CHECK(sdl::MapAppearance(cell, 42, TestContent()).symbol == "@");
    CHECK(sdl::MapAppearance(cell, 43, TestContent()).symbol == "z");
    cell.actor = 0;
    CHECK(sdl::MapAppearance(cell, 42, TestContent()).symbol == "!");
    cell.ground_items = 0;
    CHECK(sdl::MapAppearance(cell, 42, TestContent()).symbol == "&");
}

TEST_CASE("SDL map layout maps output pixels to cells", "[sdl][render]") {
    const auto layout = sdl::MakeMapLayout(800, 600, 17, 17);
    int column = -1, row = -1;
    REQUIRE(sdl::MapCellAt(layout, layout.x + 3 * layout.cellSize, layout.y + 7 * layout.cellSize, column, row));
    CHECK(column == 3);
    CHECK(row == 7);
    CHECK_FALSE(sdl::MapCellAt(layout, layout.x + layout.width, layout.y, column, row));
    CHECK_FALSE(sdl::MapCellAt(layout, layout.x, layout.y - 1, column, row));
    const auto compact = sdl::MakeMapLayout(480, 360, 17, 17);
    CHECK(compact.cellSize == layout.cellSize);
    CHECK(compact.cellHeight == layout.cellHeight);
    const auto rectangular = sdl::MakeMapLayout(800, 600, 17, 17, 32, 64);
    REQUIRE(sdl::MapCellAt(rectangular, rectangular.x + 3 * rectangular.cellSize,
        rectangular.y + 7 * rectangular.cellHeight, column, row));
    CHECK(column == 3); CHECK(row == 7);
    CHECK(rectangular.cellHeight >= rectangular.cellSize * 2);
    const auto scaledUi = sdl::MakeMapLayout(1600, 1200, 17, 17, 32, 64, 0, 2);
    CHECK(rectangular.cellSize == 32);
    CHECK(rectangular.cellHeight == 64);
    CHECK(scaledUi.cellSize == rectangular.cellSize);
    CHECK(scaledUi.cellHeight == rectangular.cellHeight);
    const auto doubled = sdl::MakeMapLayout(800,600,17,17,32,64,4);
    CHECK(doubled.cellSize == 64); CHECK(doubled.cellHeight == 128);
}

TEST_CASE("SDL clipped map animations do not schedule redraws", "[sdl][render]") {
    TilesetDefinition tileset;
    tileset.cellWidth = tileset.cellHeight = 32;
    tileset.sprites = {{"a", 0, 0, 0, 32, 32, 16, 32}, {"b", 0, 32, 0, 32, 32, 16, 32}};
    tileset.animations.push_back({"loop", {{0, 10}, {1, 30}}, 30});
    sdl::MapPacket packet;
    packet.width = packet.height = 17;
    packet.cells.resize(17 * 17);
    auto& cell = packet.cells[8 * 17 + 12];
    cell.visible = true;
    cell.terrainSprite = {TilesetMappingKind::Animation, 0};
    REQUIRE(sdl::VisibleAnimationDelay(packet, tileset, 0, 480, 360));
    CHECK_FALSE(sdl::VisibleAnimationDelay(packet, tileset, 0, 480, 360, 16));
    cell.visible = false;
    CHECK_FALSE(sdl::VisibleAnimationDelay(packet, tileset, 0, 480, 360));
}

TEST_CASE("SDL zoom keeps the character centered instead of the map corner", "[sdl][render]") {
    for (const auto dimensions : {SDL_Point{800, 600}, SDL_Point{480, 360}, SDL_Point{1600, 1200}}) {
        for (const auto cell : {SDL_Point{32, 32}, SDL_Point{32, 64}}) {
            const float uiScale = dimensions.x == 1600 ? 2.0f : 1.0f;
            const auto base = sdl::MakeMapLayout(dimensions.x, dimensions.y, 17, 17,
                cell.x, cell.y, 0, uiScale);
            const int centerX = base.x + base.width / 2;
            const int centerY = base.y + base.height / 2;
            for (const int zoom : {-8, -1, 1, 8, 16}) {
                const auto layout = sdl::MakeMapLayout(dimensions.x, dimensions.y, 17, 17,
                    cell.x, cell.y, zoom, uiScale);
                CHECK(std::abs(layout.x + layout.width / 2 - centerX) <= 1);
                CHECK(std::abs(layout.y + layout.height / 2 - centerY) <= 1);
                int column = -1, row = -1;
                REQUIRE(sdl::MapCellAt(layout, centerX, centerY, column, row));
                CHECK(column == 8);
                CHECK(row == 8);
            }
        }
    }
}

TEST_CASE("SDL software renderer draws visibility and missing glyph fallback", "[sdl][render]") {
    const ContentSource source{"render.json5", R"({format_version:1,definitions:[
        {type:'terrain',id:'known',name:'known',movement:{passable:true},blocks_sight:false,
         appearance:{symbol:'.',foreground:'white',background:'black'}},
        {type:'terrain',id:'missing',name:'missing',movement:{passable:true},blocks_sight:false,
         appearance:{symbol:'X',foreground:'white',background:'black'}}]})"};
    auto content = LoadContent({&source, 1});
    REQUIRE(content.has_value());
    REQUIRE(SDL_Init(0));
    SDL_Surface* surface = SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface != nullptr);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer != nullptr);
    {
        sdl::SdlAssets assets;
        sdl::SdlText text(renderer);
        REQUIRE(text.Ready());
        for (int i = 0; i < 1000000 && assets.Poll(renderer) == sdl::AssetState::Loading; ++i) { std::this_thread::yield(); }
        REQUIRE(assets.State() == sdl::AssetState::Ready);
        CHECK(assets.Definition().cellWidth == 32);
        CHECK(assets.Definition().cellHeight == 32);
        float atlasWidth = 0, atlasHeight = 0;
        REQUIRE(SDL_GetTextureSize(assets.Atlas(), &atlasWidth, &atlasHeight));
        CHECK(atlasWidth == 512);
        CHECK(atlasHeight == 32);
        const auto fallback = sdl::SdlAssets::GlyphRect("X");
        const auto question = sdl::SdlAssets::GlyphRect("?");
        CHECK(fallback.x == question.x);
        CHECK(fallback.w == 32);
        CHECK(fallback.h == 32);
        CHECK(fallback.x + fallback.w <= atlasWidth);
        WorldView view;
        view.width = 3; view.height = 1; view.playerId = 1;
        view.cells.resize(3);
        view.cells[0].tile.terrain = content->FindTerrain("known");
        view.cells[1].terrainVisibility = Visibility::Visible;
        view.cells[1].terrainLight = LightBand::Ordinary;
        view.cells[1].tile.terrain = content->FindTerrain("known");
        view.cells[2].terrainVisibility = Visibility::Visible;
        view.cells[2].terrainLight = LightBand::Ordinary;
        view.cells[2].tile.terrain = content->FindTerrain("missing");
        REQUIRE(sdl::DrawFrame(renderer, text, assets, &view, *content, "Ready", "Move"));
        const auto layout = sdl::MakeMapLayout(800, 600, 3, 1);
        int lit[3]{};
        bool pixelsRead = true;
        Uint8 r, g, b, a;
        for (int cell = 0; cell < 3; ++cell) {
            for (int y = 0; y < layout.cellSize; ++y) {
                for (int x = 0; x < layout.cellSize; ++x) {
                    if (!SDL_ReadSurfacePixel(surface, layout.x + cell * layout.cellSize + x, layout.y + y, &r, &g, &b, &a)) {
                        pixelsRead = false; continue;
                    }
                    if (r > 200 && g > 200 && b > 200) { ++lit[cell]; }
                }
            }
        }
        REQUIRE(pixelsRead);
        CHECK(lit[0] == 0);
        CHECK(lit[1] > 0);
        CHECK(lit[2] > lit[1]);
        REQUIRE(SDL_ReadSurfacePixel(surface, layout.x, layout.y, &r, &g, &b, &a));
        CHECK(r == 12); CHECK(g == 16); CHECK(b == 20);
        REQUIRE(SDL_ReadSurfacePixel(surface, layout.x + layout.cellSize, layout.y, &r, &g, &b, &a));
        CHECK(r == 0); CHECK(g == 0); CHECK(b == 0);
        if (const char* path = std::getenv("CATACLYSMX_SDL_PREVIEW")) {
            Server server(TestContent());
            const auto preview = server.View();
            REQUIRE(sdl::DrawFrame(renderer, text, assets, &preview, TestContent(), "Local session ready.",
                "Left/h west\nRight/l east\nUp/k north\nDown/j south\n< down\n> up\n. wait\ng pick up\nd drop\no interact\nF1 help\nq quit"));
            REQUIRE(SDL_SavePNG(surface, path));
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    SDL_Quit();
}

}
