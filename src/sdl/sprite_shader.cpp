#include "sdl/sprite_shader.h"

#include <EASTL/unique_ptr.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_init.h>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <dxcapi.h>

namespace cataclysmx::sdl {
namespace {

template<class T>
struct ReleaseCom {
    void operator()(T* object) const { object->Release(); }
};

}

SpriteShader::SpriteShader(SDL_Renderer* renderer) : renderer_(renderer) {
    if (!SDL_IsMainThread()) { std::abort(); }
    if (!renderer_) {
        SDL_SetError("Sprite shader requires a renderer");
        return;
    }
    device_ = SDL_GetGPURendererDevice(renderer_);
    if (!device_) { return; }
    const auto formats = SDL_GetGPUShaderFormats(device_);
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else {
        SDL_SetError("Sprite shader requires DXIL or SPIR-V support");
        return;
    }

    size_t sourceSize = 0;
    eastl::unique_ptr<void, decltype(&SDL_free)> source(
        SDL_LoadFile(CATACLYSMX_SPRITE_SHADER, &sourceSize), &SDL_free);
    if (!source) { return; }

    IDxcCompiler3* compilerRaw = nullptr;
    const HRESULT createStatus = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compilerRaw));
    eastl::unique_ptr<IDxcCompiler3, ReleaseCom<IDxcCompiler3>> compiler(compilerRaw);
    if (FAILED(createStatus)) {
        SDL_SetError("Sprite shader: DxcCreateInstance failed (0x%08x)", static_cast<unsigned>(createStatus));
        return;
    }
    const DxcBuffer buffer{source.get(), sourceSize, DXC_CP_UTF8};
    const wchar_t* arguments[] = {L"-E", L"main", L"-T", L"ps_6_0", L"-spirv"};
    const UINT32 argumentCount = format == SDL_GPU_SHADERFORMAT_SPIRV ? 5 : 4;
    IDxcResult* resultRaw = nullptr;
    const HRESULT compileStatus = compiler->Compile(&buffer, arguments, argumentCount,
        nullptr, IID_PPV_ARGS(&resultRaw));
    eastl::unique_ptr<IDxcResult, ReleaseCom<IDxcResult>> result(resultRaw);
    if (FAILED(compileStatus)) {
        SDL_SetError("Sprite shader: DXC Compile failed (0x%08x)", static_cast<unsigned>(compileStatus));
        return;
    }
    IDxcBlobUtf8* diagnosticsRaw = nullptr;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnosticsRaw), nullptr);
    eastl::unique_ptr<IDxcBlobUtf8, ReleaseCom<IDxcBlobUtf8>> diagnostics(diagnosticsRaw);
    const char* message = diagnostics && diagnostics->GetStringLength() != 0
        ? diagnostics->GetStringPointer() : "no DXC diagnostics";
    HRESULT shaderStatus = E_FAIL;
    const HRESULT statusResult = result->GetStatus(&shaderStatus);
    if (FAILED(statusResult) || FAILED(shaderStatus)) {
        SDL_SetError("Sprite shader compilation failed: %s", message);
        return;
    }
    IDxcBlob* objectRaw = nullptr;
    const HRESULT objectStatus = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&objectRaw), nullptr);
    eastl::unique_ptr<IDxcBlob, ReleaseCom<IDxcBlob>> object(objectRaw);
    if (FAILED(objectStatus) || !object) {
        SDL_SetError("Sprite shader: DXC produced no object: %s", message);
        return;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(object->GetBufferPointer());
    info.code_size = object->GetBufferSize();
    info.entrypoint = "main";
    info.format = format;
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
    if (!(SDL_GetGPUShaderFormats(device) & (SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV))) {
        return SDL_SetError("Sprite shader requires DXIL or SPIR-V support");
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
    // Three packed float4 rows, matching the HLSL cbuffer without struct-padding assumptions.
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
