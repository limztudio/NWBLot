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
    [[vk::location(NWB_IMGUI_COLOR_TARGET_LOCATION)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbImguiPixelOutput main(NwbImguiPixelInput input){
    const float4 color = float4(input.color);
    const Texture2D<float4> texture = NwbHeapSampledImage2D(g_NwbImguiPushConstants.textureSlot);
    const SamplerState sampler = NwbHeapSampler(g_NwbImguiPushConstants.samplerSlot);
    const float4 source = color * texture.Sample(sampler, input.uv);

    NwbImguiPixelOutput output;
    const float3 presentationColor = g_NwbImguiPushConstants.presentationMode == NWB_IMGUI_PRESENTATION_HDR10
        ? nwbHdr10EncodeUiSrgb(source.rgb)
        : source.rgb
    ;
    output.color = float4(presentationColor, source.a);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

