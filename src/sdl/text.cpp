#include "sdl/text.h"
#include <EASTL/string_view.h>

#include <cstdlib>
#include <cmath>

namespace cataclysmx::sdl {
namespace {

void AssertMainThread() {
    if (!SDL_IsMainThread()) { std::abort(); }
}

bool SameColor(SDL_Color left, SDL_Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

}

SdlText::SdlText(SDL_Renderer* renderer, const char* fontPath, float pointSize) {
    AssertMainThread();
    if (!renderer || !std::isfinite(pointSize) || pointSize <= 0.0f || !TTF_Init()) { return; }
    ttfInitialized_ = true;
    if (!fontPath) { fontPath = CATACLYSMX_DEFAULT_FONT; }
    font_ = TTF_OpenFont(fontPath, pointSize);
    if (!font_) { return; }
    monospaced_ = TTF_FontIsFixedWidth(font_);
    engine_ = TTF_CreateRendererTextEngine(renderer);
    if (!engine_) { return; }
    for (Slot& slot : slots_) {
        slot.text = TTF_CreateText(engine_, font_, "", 0);
        if (!slot.text) { return; }
    }
    ready_ = true;
}

SdlText::~SdlText() {
    AssertMainThread();
    for (Slot& slot : slots_) {
        if (slot.text) { TTF_DestroyText(slot.text); }
    }
    if (engine_) { TTF_DestroyRendererTextEngine(engine_); }
    if (font_) { TTF_CloseFont(font_); }
    if (ttfInitialized_) { TTF_Quit(); }
}

bool SdlText::Draw(unsigned int slotIndex, const char* utf8, float x, float y, SDL_Color color, int wrapWidth) {
    AssertMainThread();
    if (!ready_ || slotIndex >= slotCount || wrapWidth < 0) { return false; }
    Slot& slot = slots_[slotIndex];
    const char* value = utf8 ? utf8 : "";
    if (slot.utf8 != value) {
        if (!TTF_SetTextString(slot.text, value, 0)) { return false; }
        slot.utf8 = value;
    }
    if (slot.wrapWidth != wrapWidth) {
        if (!TTF_SetTextWrapWidth(slot.text, wrapWidth)) { return false; }
        slot.wrapWidth = wrapWidth;
    }
    if (!slot.hasColor || !SameColor(slot.color, color)) {
        if (!TTF_SetTextColor(slot.text, color.r, color.g, color.b, color.a)) { return false; }
        slot.color = color;
        slot.hasColor = true;
    }
    return TTF_DrawRendererText(slot.text, x, y);
}

bool SdlText::DrawCells(unsigned int slot, const char* utf8, std::uint32_t cells,
    float x, float y, SDL_Color color, int cellWidth) {
    AssertMainThread();
    if (!ready_ || slot >= slotCount || cellWidth <= 0) { return false; }
    if (!cells) { return true; }
    if (monospaced_) {
        if (!Draw(slot, utf8, -10000, -10000, color)) { return false; }
        // A fallback font can supply a differently sized Unicode glyph even
        // when the primary font is mono. Preserve grid spacing in that case.
        if (static_cast<std::uint64_t>(Width(slot)) == static_cast<std::uint64_t>(cells) * cellWidth) {
            return Draw(slot, utf8, x, y, color);
        }
    }
    const eastl::string_view run(utf8 ? utf8 : "");
    std::uint32_t column = 0;
    for (std::size_t byte = 0; byte < run.size() && column < cells; ++column) {
        const auto first = static_cast<unsigned char>(run[byte]);
        std::size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        if (bytes > run.size() - byte) { bytes = run.size() - byte; }
        const String glyph(run.data() + byte, bytes);
        unsigned hash = 2166136261U;
        for (std::size_t n = 0; n < bytes; ++n) { hash = (hash ^ static_cast<unsigned char>(glyph[n])) * 16777619U; }
        if (!Draw(256 + hash % 64, glyph.c_str(), x + column * cellWidth, y, color)) { return false; }
        byte += bytes;
    }
    return true;
}

int SdlText::LineHeight() const {
    AssertMainThread();
    if (!ready_) { std::abort(); }
    return TTF_GetFontHeight(font_);
}

int SdlText::Height(unsigned int slot) const {
    AssertMainThread();
    if (!ready_ || slot >= slotCount) { std::abort(); }
    int width = 0, height = 0;
    if (!TTF_GetTextSize(slots_[slot].text, &width, &height)) { return 0; }
    return height;
}

int SdlText::Width(unsigned int slot) const {
    AssertMainThread();
    if (!ready_ || slot >= slotCount) { std::abort(); }
    int width = 0, height = 0;
    if (!TTF_GetTextSize(slots_[slot].text, &width, &height)) { return 0; }
    return width;
}

}
