#include "sdl/sprite_shader.h"
#include "sdl/gpu_renderer.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/unique_ptr.h>
#include <SDL3/SDL.h>
#include <cmath>

namespace {
using namespace cataclysmx;

struct VideoSession {
    ~VideoSession() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
};

struct Style {
    Visibility visibility = Visibility::Visible;
    LightBand light = LightBand::Ordinary;
    unsigned depth = 0;
    bool background = false;
};

// Deliberately use RenderGeometry directly: this verifies the custom fragment
// shader against SDL's textured vertex shader, independently of map batching.
void DrawQuad(SDL_Renderer* renderer, SDL_Texture* texture, float x, float y,
    float u, float alpha = 1.0f, SDL_FColor color = {1, 1, 1, 1}) {
    color.a = alpha;
    const SDL_Vertex vertices[] = {
        {{x, y}, color, {u, 0.5f}}, {{x + 4, y}, color, {u, 0.5f}},
        {{x + 4, y + 4}, color, {u, 0.5f}}, {{x, y + 4}, color, {u, 0.5f}}};
    const int indices[] = {0, 1, 2, 0, 2, 3};
    REQUIRE(SDL_RenderGeometry(renderer, texture, vertices, 4, indices, 6));
}

void CheckPixel(SDL_Surface* surface, int x, int y, float red, float green, float blue) {
    Uint8 r = 0, g = 0, b = 0, a = 0;
    REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
    CAPTURE(x, y, r, g, b, a, red, green, blue);
    CHECK(std::abs(float(r) - red) <= 3);
    CHECK(std::abs(float(g) - green) <= 3);
    CHECK(std::abs(float(b) - blue) <= 3);
    CHECK(a == 255);
}

void CheckShader() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { SKIP("SDL video unavailable: " << SDL_GetError()); }
    VideoSession video;
    eastl::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        sdl::CreateVulkanRenderer(nullptr), &SDL_DestroyRenderer);
    if (!renderer) { SKIP("Offscreen Vulkan renderer unavailable: " << SDL_GetError()); }
    REQUIRE(sdl::SpriteShader::Supported(renderer.get()));
    REQUIRE(SDL_strcmp(SDL_GetGPUDeviceDriver(SDL_GetGPURendererDevice(renderer.get())), "vulkan") == 0);
    sdl::SpriteShader shader(renderer.get());
    INFO("Shader initialization: " << SDL_GetError());
    REQUIRE(shader.Ready());

    const SDL_Color colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
        {255, 255, 255, 255}, {0, 0, 0, 255}, {80, 140, 210, 255},
        {230, 70, 140, 0}, {80, 140, 210, 128}};
    constexpr int colorCount = 8;
    const Style styles[] = {
        {}, {Visibility::Visible, LightBand::Dark}, {Visibility::Visible, LightBand::Dim},
        {Visibility::Visible, LightBand::Bright}, {Visibility::Visible, LightBand::Ordinary, 1},
        {Visibility::Visible, LightBand::Ordinary, 3}, {Visibility::Visible, LightBand::Ordinary, 20},
        {Visibility::Remembered}, {Visibility::Remembered, LightBand::Bright, 3},
        {Visibility::Visible, LightBand::Dark, 3, true},
        // Reuse both cached styles, interleaved, before any readback/present.
        {Visibility::Visible, LightBand::Dark}, {Visibility::Remembered},
        {Visibility::Visible, LightBand::Dark}, {Visibility::Remembered}};
    constexpr int styleCount = static_cast<int>(sizeof(styles) / sizeof(styles[0]));
    eastl::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> atlas(
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, colorCount, 1),
        &SDL_DestroyTexture);
    REQUIRE(atlas);
    REQUIRE(SDL_UpdateTexture(atlas.get(), nullptr, colors, sizeof(colors)));
    REQUIRE(SDL_SetTextureScaleMode(atlas.get(), SDL_SCALEMODE_NEAREST));
    REQUIRE(SDL_SetTextureBlendMode(atlas.get(), SDL_BLENDMODE_BLEND));
    eastl::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> target(
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
            colorCount * 4, (styleCount * 2 + 2) * 4), &SDL_DestroyTexture);
    REQUIRE(target);
    REQUIRE(SDL_SetRenderTarget(renderer.get(), target.get()));
    constexpr SDL_Color background{24, 40, 56, 255};
    REQUIRE(SDL_SetRenderDrawColor(renderer.get(), background.r, background.g, background.b, background.a));
    REQUIRE(SDL_RenderClear(renderer.get()));

    for (int row = 0; row < styleCount; ++row) {
        const auto& style = styles[row];
        REQUIRE(shader.Apply(MakePerceptionTransform(style.visibility, style.light, style.depth, style.background)));
        for (int alphaRow = 0; alphaRow < 2; ++alphaRow) {
            for (int column = 0; column < colorCount; ++column) {
                DrawQuad(renderer.get(), atlas.get(), float(column * 4), float((row * 2 + alphaRow) * 4),
                    (float(column) + 0.5f) / colorCount, alphaRow == 0 ? 1.0f : 0.5f);
            }
        }
    }
    REQUIRE(shader.Clear());
    for (int column = 0; column < colorCount; ++column) {
        DrawQuad(renderer.get(), atlas.get(), float(column * 4), float(styleCount * 8),
            (float(column) + 0.5f) / colorCount);
    }
    DrawQuad(renderer.get(), nullptr, 0, float(styleCount * 8 + 4), 0, 1, {0.2f, 0.4f, 0.8f, 1});

    // Read while the target is bound, before present. All earlier draws are queued.
    eastl::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
        SDL_RenderReadPixels(renderer.get(), nullptr), &SDL_DestroySurface);
    INFO("GPU readback: " << SDL_GetError());
    REQUIRE(pixels);
    for (int row = 0; row <= styleCount; ++row) {
        const Style style = row == styleCount ? Style{} : styles[row];
        for (int alphaRow = 0; alphaRow < (row == styleCount ? 1 : 2); ++alphaRow) {
            for (int column = 0; column < colorCount; ++column) {
                const auto source = colors[column];
                const auto expected = PerceptionColor({source.r, source.g, source.b},
                    style.visibility, style.light, style.depth, style.background);
                const float alpha = float(source.a) / 255.0f * (alphaRow == 0 ? 1.0f : 0.5f);
                CheckPixel(pixels.get(), column * 4 + 2, (row * 2 + alphaRow) * 4 + 2,
                    expected.red * alpha + background.r * (1 - alpha),
                    expected.green * alpha + background.g * (1 - alpha),
                    expected.blue * alpha + background.b * (1 - alpha));
            }
        }
    }
    CheckPixel(pixels.get(), 2, styleCount * 8 + 6, 51, 102, 204);
    REQUIRE(SDL_RenderPresent(renderer.get()));
}

TEST_CASE("SDL Vulkan runtime GLSL shader preserves perception, queued states, and alpha", "[sdl][gpu][render]") {
    CheckShader();
}

TEST_CASE("SDL sprite shader rejects null and software renderers", "[sdl][render]") {
    CHECK_FALSE(sdl::SpriteShader::Supported(nullptr));
    sdl::SpriteShader missing(nullptr);
    CHECK_FALSE(missing.Ready());
    CHECK_FALSE(missing.Apply(MakePerceptionTransform(Visibility::Visible, LightBand::Ordinary, 0)));
    CHECK_FALSE(missing.Clear());

    eastl::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32), &SDL_DestroySurface);
    REQUIRE(surface);
    eastl::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), &SDL_DestroyRenderer);
    REQUIRE(renderer);
    CHECK_FALSE(sdl::SpriteShader::Supported(renderer.get()));
    sdl::SpriteShader software(renderer.get());
    CHECK_FALSE(software.Ready());
}

}
