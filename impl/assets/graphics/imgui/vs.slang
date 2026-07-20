#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbImguiPushConstants{
    float4 scaleTranslate;
};

[[vk::push_constant]]
ConstantBuffer<NwbImguiPushConstants> g_NwbImguiPushConstants;

struct NwbImguiVertexInput{
    [[vk::location(NWB_IMGUI_VERTEX_POSITION_LOCATION)]] float2 position : POSITION0;
    [[vk::location(NWB_IMGUI_VERTEX_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_IMGUI_VERTEX_COLOR_LOCATION)]] half4 color : COLOR0;
};

struct NwbImguiVertexOutput{
    float4 position : SV_Position;
    [[vk::location(NWB_IMGUI_RASTER_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_IMGUI_RASTER_COLOR_LOCATION)]] half4 color : COLOR0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbImguiVertexOutput main(NwbImguiVertexInput input){
    const float4 scaleTranslate = g_NwbImguiPushConstants.scaleTranslate;

    NwbImguiVertexOutput output;
    output.uv = input.uv;
    output.color = input.color;
    output.position = float4(input.position * scaleTranslate.xy + scaleTranslate.zw, 0.0, 1.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

