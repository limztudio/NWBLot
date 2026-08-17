// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "../bindless/bindless_heap.slangi"
#include "../common/hdr10.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbImguiPushConstants{
    float4 scaleTranslate;
    uint textureSlot;
    uint samplerSlot;
    uint presentationMode;
    uint _padding;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbImguiPixelOutput main(NwbImguiPixelInput input){
    const Texture2D<float4> texture = NwbHeapSampledImage2D(g_NwbImguiPushConstants.textureSlot);
    const SamplerState sampler = NwbHeapSampler(g_NwbImguiPushConstants.samplerSlot);
    // ImGui vertex color is RGBA8 and UI textures are normalized display data. Keep the sampled/blended payload in
    // FP16, then promote only for the HDR10 transfer function before narrowing to the presentation attachment.
    const half4 source = input.color * half4(texture.Sample(sampler, input.uv));

    NwbImguiPixelOutput output;
    const float3 presentationColor = g_NwbImguiPushConstants.presentationMode == NWB_IMGUI_PRESENTATION_HDR10
        ? nwbHdr10EncodeUiSrgb(float3(source.rgb))
        : float3(source.rgb)
    ;
    output.color = half4(half3(presentationColor), source.a);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

