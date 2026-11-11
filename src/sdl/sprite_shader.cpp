#include "sdl/sprite_shader.h"

#include <EASTL/unique_ptr.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_init.h>
#include <cstdlib>
#include <shaderc/shaderc.h>

namespace cataclysmx::sdl {

SpriteShader::SpriteShader(SDL_Renderer* renderer) : renderer_(renderer) {
    if (!SDL_IsMainThread()) { std::abort(); }
    if (!Supported(renderer_)) { return; }
    device_ = SDL_GetGPURendererDevice(renderer_);

    size_t sourceSize = 0;
    eastl::unique_ptr<void, decltype(&SDL_free)> source(
        SDL_LoadFile(CATACLYSMX_SPRITE_SHADER, &sourceSize), &SDL_free);
    if (!source) { return; }

    eastl::unique_ptr<shaderc_compiler, decltype(&shaderc_compiler_release)> compiler(
        shaderc_compiler_initialize(), &shaderc_compiler_release);
    eastl::unique_ptr<shaderc_compile_options, decltype(&shaderc_compile_options_release)> options(
        shaderc_compile_options_initialize(), &shaderc_compile_options_release);
    if (!compiler || !options) { std::abort(); }
    shaderc_compile_options_set_source_language(options.get(), shaderc_source_language_glsl);
    shaderc_compile_options_set_target_env(options.get(), shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_target_spirv(options.get(), shaderc_spirv_version_1_0);
    eastl::unique_ptr<shaderc_compilation_result, decltype(&shaderc_result_release)> result(
        shaderc_compile_into_spv(compiler.get(), static_cast<const char*>(source.get()), sourceSize,
            shaderc_fragment_shader, CATACLYSMX_SPRITE_SHADER, "main", options.get()), &shaderc_result_release);
    if (!result) { std::abort(); }
    const auto status = shaderc_result_get_compilation_status(result.get());
    if (status == shaderc_compilation_status_null_result_object) { std::abort(); }
    if (status != shaderc_compilation_status_success) {
        SDL_SetError("Sprite shader compilation failed (%s): %s", CATACLYSMX_SPRITE_SHADER,
            shaderc_result_get_error_message(result.get()));
        return;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code = reinterpret_cast<const Uint8*>(shaderc_result_get_bytes(result.get()));
    info.code_size = shaderc_result_get_length(result.get());
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1;
    info.num_uniform_buffers = 1;
    shader_ = SDL_CreateGPUShader(device_, &info);
}

SpriteShader::~SpriteShader() {
    if (!SDL_IsMainThread()) { std::abort(); }
    if (shader_) {
        SDL_SetGPURenderState(renderer_, nullptr);
        for (const auto& cached : states_) { SDL_DestroyGPURenderState(cached.state); }
        SDL_ReleaseGPUShader(device_, shader_);
    }
}

bool SpriteShader::Ready() const { return shader_ != nullptr; }

bool SpriteShader::Supported(SDL_Renderer* renderer) {
    if (!renderer) { return SDL_SetError("Sprite shader requires a renderer"); }
    auto* device = SDL_GetGPURendererDevice(renderer);
    if (!device) { return false; }
    const char* driver = SDL_GetGPUDeviceDriver(device);
    if (!driver || SDL_strcmp(driver, "vulkan") != 0
        || !(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        return SDL_SetError("Sprite shader requires Vulkan with SPIR-V support");
    }
    return true;
}

bool SpriteShader::Apply(const PerceptionTransform& transform) {
    if (!SDL_IsMainThread()) { std::abort(); }
    if (!Ready()) { return SDL_SetError("Sprite shader is not ready"); }
    for (const auto& cached : states_) {
        if (cached.transform == transform) { return SDL_SetGPURenderState(renderer_, cached.state); }
    }
    SDL_GPURenderStateCreateInfo info{};
    info.fragment_shader = shader_;
    SDL_GPURenderState* state = SDL_CreateGPURenderState(renderer_, &info);
    if (!state) { return false; }
    // Three packed vec4 rows match the GLSL std140 block without struct-padding assumptions.
    const float rows[12]{transform.red[0], transform.red[1], transform.red[2], transform.red[3],
        transform.green[0], transform.green[1], transform.green[2], transform.green[3],
        transform.blue[0], transform.blue[1], transform.blue[2], transform.blue[3]};
    if (!SDL_SetGPURenderStateFragmentUniforms(state, 0, rows, sizeof(rows))) {
        SDL_DestroyGPURenderState(state);
        return false;
    }
    states_.push_back({transform, state});
    return SDL_SetGPURenderState(renderer_, state);
}

bool SpriteShader::Clear() {
    if (!SDL_IsMainThread()) { std::abort(); }
    if (!Ready()) { return SDL_SetError("Sprite shader is not ready"); }
    return SDL_SetGPURenderState(renderer_, nullptr);
}

}
