// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbUiOutputVertexOutput{
    float4 position : SV_Position;
    [[vk::location(NWB_UI_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbUiOutputVertexOutput main(uint vertexIndex : SV_VertexID){
    const half2 positions[NWB_UI_FULLSCREEN_TRIANGLE_VERTEX_COUNT] = half2[NWB_UI_FULLSCREEN_TRIANGLE_VERTEX_COUNT](half2(NWB_UI_FULLSCREEN_CLIP_MIN, NWB_UI_FULLSCREEN_CLIP_MIN), half2(NWB_UI_FULLSCREEN_CLIP_MAX, NWB_UI_FULLSCREEN_CLIP_MIN), half2(NWB_UI_FULLSCREEN_CLIP_MIN, NWB_UI_FULLSCREEN_CLIP_MAX));

    const half2 position = positions[vertexIndex];
    NwbUiOutputVertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2(position * half2(0.5h, -0.5h) + half2(0.5h));
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

