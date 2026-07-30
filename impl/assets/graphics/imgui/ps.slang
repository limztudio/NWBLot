// limztudio@gmail.com


#include "binding_slots.h"
#include "../bindless/bindless_heap.slangi"


struct NwbImguiPushConstants{
    float4 scaleTranslate;
    uint textureSlot;
    uint samplerSlot;
    uint2 _padding;
};

[[vk::push_constant]]
ConstantBuffer<NwbImguiPushConstants> g_NwbImguiPushConstants;

struct NwbImguiPixelInput{
    [[vk::location(NWB_IMGUI_RASTER_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_IMGUI_RASTER_COLOR_LOCATION)]] half4 color : COLOR0;
};

struct NwbImguiPixelOutput{
    [[vk::location(NWB_IMGUI_COLOR_TARGET_LOCATION)]] half4 color : SV_Target0;
};


NwbImguiPixelOutput main(NwbImguiPixelInput input){
    const half4 color = input.color;
    const Texture2D<float4> texture = NwbHeapSampledImage2D(g_NwbImguiPushConstants.textureSlot);
    const SamplerState sampler = NwbHeapSampler(g_NwbImguiPushConstants.samplerSlot);
    const half4 texel = half4(texture.Sample(sampler, input.uv));

    NwbImguiPixelOutput output;
    output.color = color * texel;
    return output;
}


