#include "sdl/gpu_renderer.h"

#include <SDL3/SDL_init.h>
#include <cstdlib>

namespace cataclysmx::sdl {

SDL_Renderer* CreateVulkanRenderer(SDL_Window* window) {
    if (!SDL_IsMainThread()) { std::abort(); }
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties) { return nullptr; }
    const bool configured = SDL_SetStringProperty(properties, SDL_PROP_RENDERER_CREATE_NAME_STRING, SDL_GPU_RENDERER)
        && SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window)
        && SDL_SetStringProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan")
        && SDL_SetBooleanProperty(properties, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN, true);
    SDL_Renderer* renderer = configured ? SDL_CreateRendererWithProperties(properties) : nullptr;
    SDL_DestroyProperties(properties);
    if (!renderer) { return nullptr; }
    SDL_GPUDevice* device = SDL_GetGPURendererDevice(renderer);
    const char* driver = device ? SDL_GetGPUDeviceDriver(device) : nullptr;
    if (!driver || SDL_strcmp(driver, "vulkan") != 0
        || !(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        SDL_DestroyRenderer(renderer);
        SDL_SetError("SDL frontend requires Vulkan with SPIR-V support");
        return nullptr;
    }
    return renderer;
}

}
