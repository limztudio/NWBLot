
#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_IMGUI_BINDING_TEXTURE, NWB_IMGUI_SET)]] Texture2D<float4> g_UiTexture;
[[vk::binding(NWB_IMGUI_BINDING_SAMPLER, NWB_IMGUI_SET)]] SamplerState g_UiSampler;

struct NwbImguiPixelInput{
    [[vk::location(NWB_IMGUI_RASTER_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_IMGUI_RASTER_COLOR_LOCATION)]] half4 color : COLOR0;
};

struct NwbImguiPixelOutput{
    [[vk::location(NWB_IMGUI_COLOR_TARGET_LOCATION)]] half4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbImguiPixelOutput main(NwbImguiPixelInput input){
    const half4 color = input.color;
    const half4 texel = half4(g_UiTexture.Sample(g_UiSampler, input.uv));

    NwbImguiPixelOutput output;
    output.color = color * texel;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

