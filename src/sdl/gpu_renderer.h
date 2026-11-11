#pragma once

#include <SDL3/SDL_render.h>

namespace cataclysmx::sdl {

// Main-thread only; null window creates an offscreen renderer. SDL errors on failure.
// The caller owns the renderer and its device through SDL_DestroyRenderer.
SDL_Renderer* CreateVulkanRenderer(SDL_Window* window);

}
