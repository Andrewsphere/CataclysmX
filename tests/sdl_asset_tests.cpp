#include "sdl/renderer.h"
#include "sdl/input.h"
#include "content_fixture.h"
#include "core/save_database.h"
#include "network/wire.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::sdl;
namespace fs = std::filesystem;

struct RenderFixture {
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    RenderFixture() {
        REQUIRE(SDL_Init(0));
        surface = SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(surface);
        renderer = SDL_CreateSoftwareRenderer(surface);
        REQUIRE(renderer);
    }
    ~RenderFixture() { SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit(); }
};
struct WakeState {
    std::mutex mutex;
    std::condition_variable condition;
    bool ready = false;
    static void Wake(void* data) {
        auto& state = *static_cast<WakeState*>(data);
        { std::lock_guard lock(state.mutex); state.ready = true; }
        state.condition.notify_one();
    }
    void Wait() {
        std::unique_lock lock(mutex);
        REQUIRE(condition.wait_for(lock, std::chrono::seconds(10), [&] { return ready; }));
    }
};
struct Pack {
    fs::path root;
    Pack() {
        auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("catax-assets-" + std::to_string(seed));
        REQUIRE(fs::create_directory(root));
        REQUIRE(fs::create_directory(root / "tile"));
        REQUIRE(fs::create_directory(root / "sprite"));
    }
    ~Pack() { std::error_code error; fs::remove_all(root, error); }
    String Directory() const { auto value = root.string(); return String(value.data(), value.size()); }
    void Write(const char* name, const char* data) {
        std::ofstream file(root / name, std::ios::binary);
        REQUIRE(file.good()); file << data; REQUIRE(file.good());
    }
    void Metadata(const char* mappings = "") {
        std::ofstream file(root / "tileset.json5");
        file << "{format_version:2,id:'test',name:'Test',cell_width:32,cell_height:32,"
             << mappings << "}";
        REQUIRE(file.good());
    }
    void Image() {
        SDL_Surface* image = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(image);
        REQUIRE(SDL_FillSurfaceRect(image, nullptr, SDL_MapSurfaceRGBA(image, 255, 0, 0, 255)));
        REQUIRE(SDL_SavePNG(image, (root / "tile/red.png").string().c_str()));
        REQUIRE(SDL_FillSurfaceRect(image, nullptr, SDL_MapSurfaceRGBA(image, 0, 255, 0, 255)));
        REQUIRE(SDL_SavePNG(image, (root / "tile/green.png").string().c_str()));
        REQUIRE(SDL_FillSurfaceRect(image, nullptr, SDL_MapSurfaceRGBA(image, 0, 0, 255, 255)));
        const SDL_Rect transparent{0, 0, 16, 32};
        REQUIRE(SDL_FillSurfaceRect(image, &transparent, SDL_MapSurfaceRGBA(image, 0, 0, 0, 0)));
        REQUIRE(SDL_SavePNG(image, (root / "sprite/actor.png").string().c_str()));
        SDL_DestroySurface(image);
    }
};
AssetState Finish(SdlAssets& assets, WakeState& wake, SDL_Renderer* renderer) {
    wake.Wait();
    for (int i = 0; i < 70 && assets.State() == AssetState::Loading; ++i) { assets.Poll(renderer); }
    return assets.State();
}
}

TEST_CASE("SDL asset loading reports failures and uses bundled default", "[sdl][assets]") {
    RenderFixture render;
    SECTION("bundled default") {
        WakeState wake;
        SdlAssets assets(&TestContent(), {}, WakeState::Wake, &wake);
        REQUIRE(Finish(assets, wake, render.renderer) == AssetState::Ready);
        const auto& definition = assets.Definition();
        CHECK(definition.id == "gen1_32x32");
        CHECK(definition.cellWidth == 32); CHECK(definition.cellHeight == 32);
        REQUIRE(definition.atlases.size() == 1); REQUIRE(definition.sprites.size() == 20);
        CHECK(definition.sourceImages.size() == 20); CHECK(definition.animations.size() == 2);
        const auto road = definition.terrain[TestTerrain("road").value];
        REQUIRE(road.kind == TilesetMappingKind::Sprite);
        CHECK(definition.sprites[road.index].id == "tile.world.road.asphalt");
        for (std::uint8_t mask = 0; mask < 16; ++mask) {
            CHECK(ResolveVisual(definition, road, 0, 0, 0, 0, mask) == road);
        }
        const auto grass = definition.terrain[TestTerrain("grass").value];
        REQUIRE(grass.kind == TilesetMappingKind::Visual);
        const auto& variations = definition.visuals[grass.index];
        REQUIRE(variations.kind == VisualGroupKind::Variants);
        for (const auto& alternative : variations.alternatives) {
            CHECK(alternative.mapping.transform.quarterTurns == 0);
            CHECK_FALSE(alternative.mapping.transform.flipY);
        }
        for (const auto& visual : definition.visuals) { CHECK(visual.kind != VisualGroupKind::Connections); }
        CHECK(definition.items[TestItem("backpack").value].kind == TilesetMappingKind::Sprite);
        for (std::size_t i = 1; i < definition.terrain.size(); ++i) {
            CHECK((definition.terrain[i].kind == TilesetMappingKind::Sprite
                || definition.terrain[i].kind == TilesetMappingKind::Visual));
        }
        for (const auto& mapping : definition.generic) { CHECK(mapping.kind == TilesetMappingKind::Sprite); }
        float width = 0, height = 0;
        REQUIRE(SDL_GetTextureSize(assets.Texture(0), &width, &height));
        CHECK(width == definition.atlases[0].width); CHECK(height == definition.atlases[0].height);
        SDL_ScaleMode scaleMode = SDL_SCALEMODE_LINEAR;
        REQUIRE(SDL_GetTextureScaleMode(assets.Texture(0), &scaleMode)); CHECK(scaleMode == SDL_SCALEMODE_NEAREST);
    }
    SECTION("empty source pack retains glyph fallback") {
        Pack pack; pack.Metadata();
        WakeState wake; SdlAssets assets(&TestContent(), pack.Directory(), WakeState::Wake, &wake);
        REQUIRE(Finish(assets, wake, render.renderer) == AssetState::Ready);
        CHECK(assets.Definition().atlases.empty()); CHECK(assets.Definition().sprites.empty());
        CHECK(assets.Definition().terrain[TestTerrain("floor").value].kind == TilesetMappingKind::Missing);
        float width = 0, height = 0;
        REQUIRE(SDL_GetTextureSize(assets.Atlas(), &width, &height)); CHECK(width == 512); CHECK(height == 32);
    }
    SECTION("missing explicit pack") {
        Pack pack; WakeState wake; SdlAssets assets(&TestContent(),pack.Directory(),WakeState::Wake,&wake);
        CHECK(Finish(assets,wake,render.renderer) == AssetState::Failed);
        CHECK(assets.Error() == AssetError::File); CHECK_FALSE(assets.Report().Empty());
    }
    SECTION("version 1 fails instead of selecting another pack") {
        Pack pack; pack.Write("tileset.json5", "{format_version:1,atlases:{main:'missing.png'},sprites:{}}");
        WakeState wake; SdlAssets assets(&TestContent(), pack.Directory(), WakeState::Wake, &wake);
        CHECK(Finish(assets, wake, render.renderer) == AssetState::Failed);
        CHECK(assets.Error() == AssetError::Metadata); CHECK_FALSE(assets.Report().Empty());
    }
    SECTION("invalid PNG header") {
        Pack pack; pack.Metadata(); pack.Write("tile/red.png", "not a png and padded to more than thirty three bytes");
        WakeState wake; SdlAssets assets(&TestContent(),pack.Directory(),WakeState::Wake,&wake);
        CHECK(Finish(assets,wake,render.renderer) == AssetState::Failed); CHECK(assets.Error() == AssetError::Metadata);
    }
    SECTION("corrupt PNG body") {
        Pack pack; pack.Image(); pack.Metadata(); fs::resize_file(pack.root / "tile/red.png", 33);
        WakeState wake; SdlAssets assets(&TestContent(),pack.Directory(),WakeState::Wake,&wake);
        CHECK(Finish(assets,wake,render.renderer) == AssetState::Failed); CHECK(assets.Error() == AssetError::Decode);
    }
    SECTION("oversized PNG header") {
        Pack pack; pack.Metadata();
        const unsigned char header[33] = {137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',0,0,0x20,1,0,0,0,8};
        std::ofstream file(pack.root / "tile/red.png",std::ios::binary); file.write(reinterpret_cast<const char*>(header),sizeof(header)); file.close();
        WakeState wake; SdlAssets assets(&TestContent(),pack.Directory(),WakeState::Wake,&wake);
        CHECK(Finish(assets,wake,render.renderer) == AssetState::Failed); CHECK(assets.Error() == AssetError::Metadata);
    }
    SECTION("upload failure") {
        Pack pack; pack.Image(); pack.Metadata();
        WakeState wake; SdlAssets assets(&TestContent(),pack.Directory(),WakeState::Wake,&wake);
        CHECK(Finish(assets,wake,nullptr) == AssetState::Failed); CHECK(assets.Error() == AssetError::Upload); CHECK(assets.Atlas() == nullptr);
    }
    SECTION("immediate destruction joins worker") {
        Pack pack; pack.Image(); pack.Metadata(); SdlAssets assets(&TestContent(),pack.Directory());
    }
}

TEST_CASE("SDL source asset loading rejects PNG symlinks outside the pack", "[sdl][assets][source]") {
    RenderFixture render;
    Pack pack; Pack outside; outside.Image(); pack.Metadata();
    std::error_code error; fs::create_symlink(outside.root / "tile/red.png", pack.root / "tile/red.png", error);
    if (error) { SKIP("Symlinks unavailable without Windows developer privileges"); }
    WakeState wake; SdlAssets assets(&TestContent(), pack.Directory(), WakeState::Wake, &wake);
    CHECK(Finish(assets, wake, render.renderer) == AssetState::Failed);
    CHECK(assets.Error() == AssetError::Metadata);
}

TEST_CASE("SDL default artwork preserves terrain under transparent overlays", "[sdl][assets][render]") {
    RenderFixture render;
    WakeState wake;
    SdlAssets assets(&TestContent(), {}, WakeState::Wake, &wake);
    REQUIRE(Finish(assets, wake, render.renderer) == AssetState::Ready);
    const auto root = fs::path(CATACLYSMX_TERRAIN_FILE).parent_path().parent_path()
        / "assets" / "tiles" / "gen1_32x32";
    const char* paths[] = {"tile/world/floor.png", "tile/world/wall.png", "tile/world/door/closed.png",
        "tile/world/door/open.png", "tile/world/grass/meadow.png", "tile/world/tree/base.png",
        "tile/world/road/asphalt.png", "tile/world/rubble.png", "tile/world/stairs.png", "tile/world/air.png",
        "sprite/character/player.png", "sprite/character/other_player.png", "sprite/character/zombie/default.png",
        "sprite/item/ground_items.png", "sprite/object/enabled.png", "sprite/object/disabled.png"};
    for (int cell = 0; cell < 16; ++cell) {
        CAPTURE(paths[cell]);
        SDL_Surface* image = SDL_LoadPNG((root / paths[cell]).string().c_str()); REQUIRE(image);
        bool binaryAlpha = true, solidTerrain = true, clearCorners = true;
        int transparent = 0, opaque = 0;
        for (int y = 0; y < image->h; ++y) {
            for (int x = 0; x < image->w; ++x) {
                Uint8 red, green, blue, alpha;
                REQUIRE(SDL_ReadSurfacePixel(image, x, y, &red, &green, &blue, &alpha));
                binaryAlpha = binaryAlpha && (alpha == 0 || alpha == 255);
                solidTerrain = solidTerrain && alpha == 255;
                if (alpha == 0) { ++transparent; } else { ++opaque; }
                if ((x == 0 || x == image->w - 1) && (y == 0 || y == image->h - 1)) {
                    clearCorners = clearCorners && alpha == 0;
                }
            }
        }
        SDL_DestroySurface(image);
        CHECK(binaryAlpha);
        if (cell < 10) { CHECK(solidTerrain); }
        else { CHECK(clearCorners); CHECK(transparent > 0); CHECK(opaque > 0); }
    }

    SdlText text(render.renderer);
    REQUIRE(text.Ready());
    WorldView view;
    view.width = view.height = 1;
    view.playerId = 1;
    view.cells.resize(1);
    view.cells[0].terrainVisibility = view.cells[0].overlayVisibility = Visibility::Visible;
    view.cells[0].terrainLight = view.cells[0].overlayLight = LightBand::Ordinary;
    view.cells[0].tile.terrain = TestTerrain("floor");
    const auto layout = MakeMapLayout(800, 600, 1, 1);
    REQUIRE(DrawFrame(render.renderer, text, assets, &view, TestContent(), "Ready", ""));
    Uint8 floorRed = 0, floorGreen = 0, floorBlue = 0, alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(render.surface, layout.x + layout.cellSize / 2,
        layout.y + layout.cellHeight / 2, &floorRed, &floorGreen, &floorBlue, &alpha));
    Uint8 cornerRed = 0, cornerGreen = 0, cornerBlue = 0;
    REQUIRE(SDL_ReadSurfacePixel(render.surface, layout.x + 1, layout.y + 1,
        &cornerRed, &cornerGreen, &cornerBlue, &alpha));
    view.cells[0].actor = view.playerId;
    REQUIRE(DrawFrame(render.renderer, text, assets, &view, TestContent(), "Ready", ""));
    Uint8 red = 0, green = 0, blue = 0;
    REQUIRE(SDL_ReadSurfacePixel(render.surface, layout.x + layout.cellSize / 2,
        layout.y + layout.cellHeight / 2, &red, &green, &blue, &alpha));
    CHECK((red != floorRed || green != floorGreen || blue != floorBlue));
    REQUIRE(SDL_ReadSurfacePixel(render.surface, layout.x + 1, layout.y + 1, &red, &green, &blue, &alpha));
    CHECK(red == cornerRed);
    CHECK(green == cornerGreen);
    CHECK(blue == cornerBlue);
}

TEST_CASE("SDL bundled lightbulb artwork animates all three content items", "[sdl][assets][animation]") {
    RenderFixture render;
    WakeState wake;
    SdlAssets assets(&TestContent(), {}, WakeState::Wake, &wake);
    REQUIRE(Finish(assets, wake, render.renderer) == AssetState::Ready);
    const auto& definition = assets.Definition();
    const auto path = fs::path(CATACLYSMX_TERRAIN_FILE).parent_path().parent_path()
        / "assets" / "tiles" / "gen1_32x32" / "sprite" / "item" / "bulb";
    bool binaryAlpha = true, clearEdges = true;
    int opaque[2]{}, transparent[2]{}, brightness[2]{};
    Uint8 frameColors[2][3]{};
    const char* frames[] = {"off.png", "on.png"};
    for (int cell = 0; cell < 2; ++cell) {
        SDL_Surface* image = SDL_LoadPNG((path / frames[cell]).string().c_str()); REQUIRE(image);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                Uint8 red, green, blue, alpha;
                REQUIRE(SDL_ReadSurfacePixel(image, x, y, &red, &green, &blue, &alpha));
                binaryAlpha = binaryAlpha && (alpha == 0 || alpha == 255);
                if (alpha) { ++opaque[cell]; brightness[cell] += red + green; }
                else { ++transparent[cell]; }
                if (x == 0 || y == 0 || x == 31 || y == 31) { clearEdges = clearEdges && alpha == 0; }
            }
        }
        Uint8 alpha;
        REQUIRE(SDL_ReadSurfacePixel(image, 16, 16,
            &frameColors[cell][0], &frameColors[cell][1], &frameColors[cell][2], &alpha));
        CHECK(alpha == 255);
        CHECK(opaque[cell] > 0);
        CHECK(transparent[cell] > 0);
        SDL_DestroySurface(image);
    }
    CHECK(binaryAlpha);
    CHECK(clearEdges);
    CHECK(brightness[1] > brightness[0]); // Compare the glass body, including the dark filament.

    SdlText text(render.renderer);
    REQUIRE(text.Ready());
    WorldView view;
    view.width = view.height = 1;
    view.cells.resize(1);
    view.cells[0].terrainVisibility = view.cells[0].overlayVisibility = Visibility::Visible;
    view.cells[0].terrainLight = view.cells[0].overlayLight = LightBand::Ordinary;
    view.cells[0].ground_items = 1;
    view.cells[0].tile.terrain = TestTerrain("floor");
    const auto layout = MakeMapLayout(800, 600, 1, 1);
    const char* names[] = {"blinking_lightbulb", "toggle_lightbulb", "single_blink_lightbulb"};
    for (int item = 0; item < 3; ++item) {
        DYNAMIC_SECTION(names[item]) {
            const Vector<MapItemVisual> items{{77, TestItem(names[item])}};
            const auto packet = PrepareMapPacket(view, TestContent(), definition, {}, items);
            const auto& mapping = packet.cells[0].overlaySprite;
            SpritePlaybackController playback;
            playback.SetGeneration(1, 1);
            auto draw = [&](std::uint64_t time, int frame) {
                REQUIRE(DrawFrame(render.renderer, text, assets, &view, TestContent(), "Ready", "",
                    time, &packet, 0, 1, {}, &playback));
                Uint8 red, green, blue, alpha;
                REQUIRE(SDL_ReadSurfacePixel(render.surface, layout.x + layout.cellSize / 2,
                    layout.y + layout.cellHeight / 2, &red, &green, &blue, &alpha));
                CHECK(red == frameColors[frame][0]);
                CHECK(green == frameColors[frame][1]);
                CHECK(blue == frameColors[frame][2]);
            };
            if (item == 0) {
                CHECK(mapping.kind == TilesetMappingKind::Animation);
                CHECK_FALSE(mapping.activation);
                draw(0, 1);
                draw(500, 0);
                draw(1000, 1);
            } else {
                REQUIRE(mapping.activation);
                draw(0, 0);
                if (item == 1) {
                    REQUIRE(playback.Activate(77, mapping, definition, 0) == SpriteActivationResult::LoopStarted);
                    draw(0, 1);
                    draw(500, 0);
                    draw(1000, 1);
                    REQUIRE(playback.Activate(77, mapping, definition, 1000) == SpriteActivationResult::Stopped);
                    draw(1000, 0);
                } else {
                    REQUIRE(playback.Activate(77, mapping, definition, 0) == SpriteActivationResult::PlayedOnce);
                    draw(0, 1);
                    draw(120, 0);
                    draw(240, 0);
                    CHECK_FALSE(playback.Sample(77, mapping, definition, 240).delayMilliseconds);
                }
            }
        }
    }
}

TEST_CASE("SDL rendering samples atlas animation without rebuilding map", "[sdl][assets]") {
    RenderFixture render; Pack pack; pack.Image();
    pack.Write("tile/cycle.meta", "{kind:'animation',frames:[{sprite:'tile.red',duration_ms:10},{sprite:'tile.green',duration_ms:20}]}");
    pack.Metadata("terrain:{air:{visual:'tile.cycle'}},generic:{player:{visual:'sprite.actor'}}");
    WakeState wake; SdlAssets assets(&TestContent(),pack.Directory(),WakeState::Wake,&wake);
    REQUIRE(Finish(assets,wake,render.renderer) == AssetState::Ready);
    SdlText text(render.renderer); REQUIRE(text.Ready());
    WorldView view; view.width = 1; view.height = 1; view.playerId = 1; view.cells.resize(1);
    view.cells[0].terrainVisibility = view.cells[0].overlayVisibility = Visibility::Visible;
    view.cells[0].terrainLight = view.cells[0].overlayLight = LightBand::Ordinary;
    view.cells[0].tile.terrain = TestTerrain("air"); view.cells[0].actor = 1;
    const auto packet = PrepareMapPacket(view,TestContent(),assets.Definition(),{1,2,3,4});
    const auto* packetCells = packet.cells.data();
    const auto layout = MakeMapLayout(800,600,1,1,32,32);
    auto checkPixel = [&](std::uint64_t time, Uint8 expectedRed, Uint8 expectedGreen) {
        REQUIRE(DrawFrame(render.renderer,text,assets,&view,TestContent(),"Ready","Move",time,&packet));
        Uint8 red,green,blue,alpha;
        REQUIRE(SDL_ReadSurfacePixel(render.surface,layout.x + layout.cellSize / 4,layout.y + layout.cellHeight / 2,&red,&green,&blue,&alpha));
        CHECK(red == expectedRed); CHECK(green == expectedGreen); CHECK(blue == 0);
        REQUIRE(SDL_ReadSurfacePixel(render.surface,layout.x + 3 * layout.cellSize / 4,layout.y + layout.cellHeight / 2,&red,&green,&blue,&alpha));
        CHECK(red == 0); CHECK(green == 0); CHECK(blue == 255);
        CHECK(packet.cells.data() == packetCells);
    };
    checkPixel(0,255,0); checkPixel(10,0,255); checkPixel(30,255,0);
    view.cells[0].terrainLight = LightBand::Dim;
    const auto shaded = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    CHECK_FALSE(DrawFrame(render.renderer, text, assets, &view, TestContent(), "Ready", "Move", 0, &shaded));
}

TEST_CASE("SDL controlled sprites draw one blink and resume the base without new resources", "[sdl][assets][animation]") {
    RenderFixture render;
    Pack pack;
    pack.Image();
    pack.Write("tile/blink.meta", "{kind:'animation',frames:[{sprite:'tile.green',duration_ms:10},{sprite:'tile.red',duration_ms:20}]}");
    pack.Metadata("items:{toggle_lightbulb:{visual:'tile.red',activation:{animation:'tile.blink',mode:'toggle_loop'}},"
        "single_blink_lightbulb:{visual:'tile.red',activation:{animation:'tile.blink',mode:'once'}}}");
    WakeState wake;
    SdlAssets assets(&TestContent(), pack.Directory(), WakeState::Wake, &wake);
    REQUIRE(Finish(assets, wake, render.renderer) == AssetState::Ready);
    SdlText text(render.renderer);
    REQUIRE(text.Ready());
    WorldView view;
    view.width = view.height = 1;
    view.cells.resize(1);
    view.cells[0].terrainVisibility = view.cells[0].overlayVisibility = Visibility::Visible;
    view.cells[0].terrainLight = view.cells[0].overlayLight = LightBand::Ordinary;
    view.cells[0].tile.terrain = TestTerrain("floor");
    view.cells[0].ground_items = 1;
    const Vector<MapItemVisual> items{{77, TestItem("single_blink_lightbulb")}};
    const auto packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {1, 2, 3, 4}, items);
    const auto* cells = packet.cells.data();
    const auto* texture = assets.Texture(0);
    const auto layout = MakeMapLayout(800, 600, 1, 1);
    SpritePlaybackController playback;
    playback.SetGeneration(1, 3);
    const auto& mapping = packet.cells[0].overlaySprite;
    const auto draw = [&](std::uint64_t time, Uint8 expectedRed, Uint8 expectedGreen) {
        REQUIRE(DrawFrame(render.renderer, text, assets, &view, TestContent(), "Ready", "", time,
            &packet, 0, 1, {}, &playback));
        Uint8 red, green, blue, alpha;
        REQUIRE(SDL_ReadSurfacePixel(render.surface, layout.x + layout.cellSize / 2,
            layout.y + layout.cellHeight / 2, &red, &green, &blue, &alpha));
        CHECK(red == expectedRed);
        CHECK(green == expectedGreen);
        CHECK(blue == 0);
        CHECK(packet.cells.data() == cells);
        CHECK(assets.Texture(0) == texture);
    };
    draw(0, 255, 0);
    CHECK_FALSE(VisibleAnimationDelay(packet, assets.Definition(), 0, 800, 600, 0, 1, &playback));
    REQUIRE(playback.Activate(77, mapping, assets.Definition(), 100) == SpriteActivationResult::PlayedOnce);
    draw(100, 0, 255);
    CHECK(*VisibleAnimationDelay(packet, assets.Definition(), 109, 800, 600, 0, 1, &playback) == 1);
    draw(110, 255, 0);
    CHECK(*VisibleAnimationDelay(packet, assets.Definition(), 129, 800, 600, 0, 1, &playback) == 1);
    draw(130, 255, 0);
    CHECK_FALSE(VisibleAnimationDelay(packet, assets.Definition(), 130, 800, 600, 0, 1, &playback));
    REQUIRE(playback.Activate(77, mapping, assets.Definition(), 140) == SpriteActivationResult::PlayedOnce);
    draw(140, 0, 255);
    draw(UINT64_MAX, 255, 0); // Restoration jumps over elapsed/minimized time.

    auto clipped = packet;
    clipped.width = clipped.height = 17;
    clipped.cells.resize(17 * 17);
    clipped.cells.back() = packet.cells[0];
    clipped.cells[0] = {};
    REQUIRE(playback.Activate(77, mapping, assets.Definition(), 200) == SpriteActivationResult::PlayedOnce);
    CHECK_FALSE(VisibleAnimationDelay(clipped, assets.Definition(), 200, 480, 360, 16, 1, &playback));
    clipped.cells.back().visible = false;
    CHECK_FALSE(VisibleAnimationDelay(clipped, assets.Definition(), 200, 480, 360, 0, 1, &playback));
}

TEST_CASE("SDL sprite clicks use render coordinates and respect modal and map clipping", "[sdl][input][animation]") {
    RenderFixture render;
    TilesetDefinition definition;
    definition.cellWidth = definition.cellHeight = 32;
    definition.animations.push_back({"blink", {{1, 10}, {0, 30}}, 30});
    MapPacket packet;
    packet.width = packet.height = 1;
    packet.cells.resize(1);
    auto& cell = packet.cells[0];
    cell.visible = cell.hasOverlay = true;
    cell.itemVisual = {77, TestItem("toggle_lightbulb")};
    cell.overlaySprite = {TilesetMappingKind::Sprite, 0, SpriteActivationBinding{0, SpriteActivation::ToggleLoop}};
    SpritePlaybackController playback;
    for (float scale : {1.0f, 2.0f}) {
        const auto layout = MakeMapLayout(800, 600, 1, 1, 32, 32, 0, scale);
        SDL_MouseButtonEvent click{};
        click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        click.button = SDL_BUTTON_LEFT;
        // Exercise SDL's coordinate conversion with a non-unit renderer transform.
        REQUIRE(SDL_SetRenderScale(render.renderer, scale, scale));
        REQUIRE(SDL_RenderCoordinatesToWindow(render.renderer, float(layout.x + layout.cellSize / 2),
            float(layout.y + layout.cellHeight / 2), &click.x, &click.y));
        CHECK_FALSE(ActivateMapItem(render.renderer, click, true, packet, definition, playback, 0, 0, scale));
        CHECK_FALSE(playback.Sample(77, cell.overlaySprite, definition, 0).delayMilliseconds);
        const auto started = ActivateMapItem(render.renderer, click, false, packet, definition, playback, 0, 0, scale);
        REQUIRE(started);
        CHECK(started->item == cell.itemVisual);
        CHECK(started->activation == SpriteActivationResult::LoopStarted);
        CHECK_FALSE(ActivateMapItem(render.renderer, click, true, packet, definition, playback, 10, 0, scale));
        CHECK(playback.Sample(77, cell.overlaySprite, definition, 10).delayMilliseconds);
        const auto stopped = ActivateMapItem(render.renderer, click, false, packet, definition, playback, 10, 0, scale);
        REQUIRE(stopped);
        CHECK(stopped->activation == SpriteActivationResult::Stopped);
        click.button = SDL_BUTTON_RIGHT;
        CHECK_FALSE(ActivateMapItem(render.renderer, click, false, packet, definition, playback, 0, 0, scale));
        click.button = SDL_BUTTON_LEFT;
        cell.visible = false;
        CHECK_FALSE(ActivateMapItem(render.renderer, click, false, packet, definition, playback, 0, 0, scale));
        cell.visible = true;
        cell.hasOverlay = false;
        CHECK_FALSE(ActivateMapItem(render.renderer, click, false, packet, definition, playback, 0, 0, scale));
        cell.hasOverlay = true;
        CHECK_FALSE(MapItemAt(packet, definition, 799, 599, 800, 600, 0, scale));
        CHECK_FALSE(MapItemAt(packet, definition, -0.1f, 50, 800, 600, 0, scale));
        CHECK_FALSE(MapItemAt(packet, definition, 10, 0, 800, 600, 0, scale));
    }
}

TEST_CASE("SDL visual activation leaves replay snapshots saved RNG and content fingerprints unchanged", "[sdl][animation][save]") {
    Pack files;
    Server animated(TestContent());
    Server reference(TestContent());
    const auto fingerprint = network::ContentFingerprint(TestContent());
    TilesetDefinition definition;
    definition.animations.push_back({"blink", {{1, 10}, {0, 30}}, 30});
    const TilesetMapping loop{TilesetMappingKind::Sprite, 0, SpriteActivationBinding{0, SpriteActivation::ToggleLoop}};
    const TilesetMapping once{TilesetMappingKind::Sprite, 0, SpriteActivationBinding{0, SpriteActivation::PlayOnce}};
    REQUIRE_FALSE(animated.GetWorld().ItemsAt({2, 1}).Empty());
    const auto item = animated.GetWorld().GetItem(*animated.GetWorld().ItemsAt({2, 1}).begin())->id;
    SpritePlaybackController playback;
    playback.SetGeneration(1, 1);
    for (std::uint64_t step = 0; step < 6; ++step) {
        playback.Activate(item, step % 2 ? loop : once, definition, step * 100);
        for (const auto time : {step * 100, step * 100 + 9, step * 100 + 10000}) {
            (void)playback.Sample(item, loop, definition, time);
            (void)playback.Sample(item, once, definition, time);
        }
        const auto left = animated.Send({CommandKind::wait});
        const auto right = reference.Send({CommandKind::wait});
        CHECK(left.outcome == right.outcome);
        CHECK(left.tick == right.tick);
        network::wire::Writer actual, expected;
        REQUIRE(actual.View(animated.View()));
        REQUIRE(expected.View(reference.View()));
        CHECK(actual.bytes == expected.bytes);
    }
    CHECK(network::ContentFingerprint(TestContent()) == fingerprint);
    const auto actualPath = (files.root / "actual.sqlite").string();
    const auto expectedPath = (files.root / "expected.sqlite").string();
    REQUIRE(animated.CreateSave(actualPath.c_str()));
    REQUIRE(reference.CreateSave(expectedPath.c_str()));
    SaveDatabase actual(TestContent()), expected(TestContent());
    const auto actualSave = actual.Open(actualPath.c_str());
    const auto expectedSave = expected.Open(expectedPath.c_str());
    REQUIRE(actualSave);
    REQUIRE(expectedSave);
    CHECK(actualSave->nextId == expectedSave->nextId);
    CHECK(actualSave->session.rngState == expectedSave->session.rngState);
    CHECK(actualSave->session.tick == expectedSave->session.tick);
    CHECK(actualSave->session.playerPosition == expectedSave->session.playerPosition);
}
