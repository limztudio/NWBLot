// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "push_constants.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbUiVertexInput{
    [[vk::location(NWB_UI_VERTEX_POSITION_LOCATION)]] float2 position : POSITION0;
    [[vk::location(NWB_UI_VERTEX_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_UI_VERTEX_COLOR_LOCATION)]] float4 color : COLOR0;
};

struct NwbUiVertexOutput{
    float4 position : SV_Position;
    [[vk::location(NWB_UI_RASTER_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_UI_RASTER_COLOR_LOCATION)]] float4 color : COLOR0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbUiVertexOutput main(NwbUiVertexInput input){
    NwbUiVertexOutput output;
    output.position = float4(input.position * g_NwbUiPushConstants.scale + g_NwbUiPushConstants.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

