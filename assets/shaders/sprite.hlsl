Texture2D atlas : register(t0, space2);
SamplerState atlasSampler : register(s0, space2);

cbuffer Perception : register(b0, space3)
{
    float4 red;
    float4 green;
    float4 blue;
};

// Match SDL release-3.4.20's embedded tri_texture.vert.dxil.h, whose
// shadercross-generated signature differs from the original HLSL semantics.
// Declaration order also matches SPIR-V locations 0 (color), 1 (UV).
struct PSInput {
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target
{
    float4 sampled = atlas.Sample(atlasSampler, input.uv);
    float4 rgb = float4(sampled.rgb, 1.0);
    float3 transformed = saturate(float3(dot(rgb, red), dot(rgb, green), dot(rgb, blue)));
    return float4(transformed, sampled.a) * input.color;
}
