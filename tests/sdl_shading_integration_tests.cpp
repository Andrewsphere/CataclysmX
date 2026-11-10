#include "sdl/renderer.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/unique_ptr.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::sdl;
namespace fs = std::filesystem;

struct Video { ~Video() { SDL_QuitSubSystem(SDL_INIT_VIDEO); } };
struct Pack {
    fs::path root;
    Pack() {
        root = fs::temp_directory_path() / ("catax-shading-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(fs::create_directories(root / "tile"));
        REQUIRE(fs::create_directories(root / "sprite"));
    }
    ~Pack() { std::error_code error; fs::remove_all(root, error); }
    String Directory() const { auto p = root.string(); return String(p.data(), p.size()); }
    void Write(const char* path, const char* contents) {
        std::ofstream file(root / path); REQUIRE(file.good()); file << contents; REQUIRE(file.good());
    }
    void Image(const char* path, int width, int height, SDL_Color color, bool half = false) {
        eastl::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
            SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), &SDL_DestroySurface);
        REQUIRE(image);
        REQUIRE(SDL_ClearSurface(image.get(), 0, 0, 0, 0));
        const SDL_Rect area{half ? width / 2 : 0, 0, half ? width / 2 : width, height};
        REQUIRE(SDL_FillSurfaceRect(image.get(), &area, SDL_MapSurfaceRGBA(image.get(), color.r, color.g, color.b, color.a)));
        REQUIRE(SDL_SavePNG(image.get(), (root / path).string().c_str()));
    }
};
struct Wake {
    std::mutex mutex; std::condition_variable condition; bool ready = false;
    static void Signal(void* data) {
        auto& self = *static_cast<Wake*>(data);
        { std::lock_guard lock(self.mutex); self.ready = true; }
        self.condition.notify_one();
    }
    void Finish(SdlAssets& assets, SDL_Renderer* renderer) {
        std::unique_lock lock(mutex);
        REQUIRE(condition.wait_for(lock, std::chrono::seconds(10), [&] { return ready; }));
        lock.unlock();
        for (int i = 0; i < 70 && assets.State() == AssetState::Loading; ++i) { assets.Poll(renderer); }
        REQUIRE(assets.State() == AssetState::Ready);
    }
};
void CheckPixel(SDL_Surface* pixels, int x, int y, float r, float g, float b) {
    Uint8 actualR, actualG, actualB, alpha;
    REQUIRE(SDL_ReadSurfacePixel(pixels, x, y, &actualR, &actualG, &actualB, &alpha));
    CAPTURE(x, y, actualR, actualG, actualB, r, g, b);
    CHECK(std::abs(float(actualR) - r) <= 3);
    CHECK(std::abs(float(actualG) - g) <= 3);
    CHECK(std::abs(float(actualB) - b) <= 3);
}
void CheckPixel(SDL_Surface* pixels, int x, int y, RgbColor c) { CheckPixel(pixels, x, y, c.red, c.green, c.blue); }

struct Gpu {
    Video video;
    eastl::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, &SDL_DestroyRenderer};
    eastl::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> target{nullptr, &SDL_DestroyTexture};
    Gpu() {
        if (!SDL_Init(SDL_INIT_VIDEO)) { SKIP("SDL video unavailable: " << SDL_GetError()); }
        renderer.reset(SDL_CreateGPURenderer(nullptr, nullptr));
        if (!renderer) { SKIP("Offscreen GPU renderer unavailable: " << SDL_GetError()); }
        target.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 800, 600));
        REQUIRE(target);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), target.get()));
    }
    SDL_Surface* Read() { auto* result = SDL_RenderReadPixels(renderer.get(), nullptr); REQUIRE(result); return result; }
};
}

TEST_CASE("GPU map rendering shades terrain and translucent overlays independently", "[sdl][gpu][render][perception]") {
    Gpu gpu;
    SpriteShader shader(gpu.renderer.get()); INFO(SDL_GetError()); REQUIRE(shader.Ready());
    Pack pack;
    pack.Image("tile/base.png", 32, 32, {80, 140, 210, 255});
    pack.Image("sprite/overlay.png", 32, 32, {210, 80, 40, 128}, true);
    pack.Write("tileset.json5", "{format_version:2,id:'test',name:'Test',cell_width:32,cell_height:32,terrain:{floor:{visual:'tile.base'}},generic:{npc:{visual:'sprite.overlay'}}}");
    Wake wake; SdlAssets assets(&TestContent(), pack.Directory(), Wake::Signal, &wake); wake.Finish(assets, gpu.renderer.get());
    SdlText text(gpu.renderer.get()); REQUIRE(text.Ready());
    WorldView view; view.width = view.height = 1; view.cells.resize(1);
    auto& cell = view.cells[0];
    cell.tile.terrain = TestTerrain("floor"); cell.actor = 99;
    cell.terrainVisibility = cell.overlayVisibility = Visibility::Visible;
    cell.terrainLight = LightBand::Dim; cell.terrainDepth = 2;
    cell.overlayLight = LightBand::Bright; cell.overlayDepth = 1;
    auto packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    const auto layout = MakeMapLayout(800, 600, 1, 1);
    REQUIRE(DrawFrame(gpu.renderer.get(), text, assets, &view, TestContent(), "", "", 0, &packet, 0, 1, {}, nullptr, &shader));
    eastl::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(gpu.Read(), &SDL_DestroySurface);
    const auto terrain = PerceptionColor({80, 140, 210}, Visibility::Visible, LightBand::Dim, 2, false);
    const auto overlay = PerceptionColor({210, 80, 40}, Visibility::Visible, LightBand::Bright, 1, false);
    CheckPixel(pixels.get(), layout.x + layout.cellSize / 4, layout.y + layout.cellHeight / 2, terrain);
    constexpr float a = 128.0f / 255.0f;
    CheckPixel(pixels.get(), layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight / 2,
        overlay.red * a + terrain.red * (1-a), overlay.green * a + terrain.green * (1-a), overlay.blue * a + terrain.blue * (1-a));
}

TEST_CASE("GPU map rendering freezes remembered terrain and furniture in sepia", "[sdl][gpu][render][perception]") {
    Gpu gpu;
    SpriteShader shader(gpu.renderer.get()); INFO(SDL_GetError()); REQUIRE(shader.Ready());
    Pack pack;
    pack.Image("tile/first.png", 32, 32, {80, 140, 210, 255});
    pack.Image("tile/second.png", 32, 32, {240, 20, 10, 255});
    pack.Image("sprite/chair.png", 32, 32, {210, 80, 40, 255}, true);
    pack.Image("sprite/second.png", 32, 32, {240, 20, 10, 255});
    pack.Write("tile/cycle.meta", "{kind:'animation',frames:[{sprite:'tile.first',duration_ms:10},{sprite:'tile.second',duration_ms:10}]}");
    pack.Write("sprite/cycle.meta", "{kind:'animation',frames:[{sprite:'sprite.chair',duration_ms:10},{sprite:'sprite.second',duration_ms:10}]}");
    pack.Write("tileset.json5", "{format_version:2,id:'test',name:'Test',cell_width:32,cell_height:32,terrain:{floor:{visual:'tile.cycle'}},generic:{object_disabled:{visual:'sprite.cycle'}}}");
    Wake wake; SdlAssets assets(&TestContent(), pack.Directory(), Wake::Signal, &wake); wake.Finish(assets, gpu.renderer.get());
    SdlText text(gpu.renderer.get()); REQUIRE(text.Ready());
    WorldView view; view.width = view.height = 1; view.cells.resize(1);
    auto& cell = view.cells[0]; cell.tile.terrain = TestTerrain("floor");
    cell.terrainVisibility = cell.overlayVisibility = Visibility::Remembered;
    cell.terrainLight = LightBand::Bright; cell.overlayLight = LightBand::Dark;
    cell.terrainDepth = 3; cell.overlayDepth = 1; cell.objectAppearance.kind = ObjectKind::Chair;
    const auto packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    REQUIRE(packet.cells[0].hasOverlay);
    REQUIRE(packet.cells[0].terrainSprite.kind == TilesetMappingKind::Sprite);
    REQUIRE(packet.cells[0].overlaySprite.kind == TilesetMappingKind::Sprite);
    const auto layout = MakeMapLayout(800, 600, 1, 1);
    const auto terrain = PerceptionColor({80, 140, 210}, Visibility::Remembered, LightBand::Bright, 3, false);
    const auto chair = PerceptionColor({210, 80, 40}, Visibility::Remembered, LightBand::Dark, 1, false);
    for (auto time : {0U, 15U, 1000U}) {
        CHECK_FALSE(VisibleAnimationDelay(packet, assets.Definition(), time, 800, 600));
        REQUIRE(DrawFrame(gpu.renderer.get(), text, assets, &view, TestContent(), "", "", time, &packet, 0, 1, {}, nullptr, &shader));
        eastl::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(gpu.Read(), &SDL_DestroySurface);
        CheckPixel(pixels.get(), layout.x + layout.cellSize / 4, layout.y + layout.cellHeight / 2, terrain);
        CheckPixel(pixels.get(), layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight / 2, chair);
    }
}

TEST_CASE("GPU upright overhang retains its source perception across fog", "[sdl][gpu][render][perception]") {
    Gpu gpu;
    SpriteShader shader(gpu.renderer.get()); INFO(SDL_GetError()); REQUIRE(shader.Ready());
    Pack pack;
    pack.Image("tile/base.png", 32, 32, {80, 140, 210, 255});
    pack.Image("sprite/tree.png", 32, 64, {210, 80, 40, 255});
    pack.Write("tileset.json5", "{format_version:2,id:'test',name:'Test',cell_width:32,cell_height:32,terrain:{floor:{visual:'tile.base'}},generic:{npc:{visual:'sprite.tree'}}}");
    Wake wake; SdlAssets assets(&TestContent(), pack.Directory(), Wake::Signal, &wake); wake.Finish(assets, gpu.renderer.get());
    SdlText text(gpu.renderer.get()); REQUIRE(text.Ready());
    WorldView view; view.width = 1; view.height = 2; view.cells.resize(2);
    auto& source = view.cells[1]; source.tile.terrain = TestTerrain("floor"); source.actor = 99;
    source.terrainVisibility = source.overlayVisibility = Visibility::Visible;
    source.terrainLight = LightBand::Ordinary; source.overlayLight = LightBand::Dim; source.overlayDepth = 2;
    auto packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    const auto layout = MakeMapLayout(800, 600, 1, 2);
    packet.cells[1].overlaySprite.transform.flipX = true;
    const int x = layout.x + layout.cellSize / 2, y = layout.y + layout.cellHeight / 2;
    REQUIRE(DrawFrame(gpu.renderer.get(), text, assets, &view, TestContent(), "", "", 0, &packet, 0, 1, {}, nullptr, &shader));
    eastl::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(gpu.Read(), &SDL_DestroySurface);
    CheckPixel(pixels.get(), x, y, PerceptionColor({210, 80, 40}, Visibility::Visible, LightBand::Dim, 2, false));
    source.terrainVisibility = source.overlayVisibility = Visibility::Unknown;
    packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    REQUIRE(DrawFrame(gpu.renderer.get(), text, assets, &view, TestContent(), "", "", 0, &packet, 0, 1, {}, nullptr, &shader));
    pixels.reset(gpu.Read());
    CheckPixel(pixels.get(), x, y, 12, 16, 20);
}
