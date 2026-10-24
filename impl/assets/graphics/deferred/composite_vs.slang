// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredCompositeVertexOutput{
    float4 position : SV_Position;
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredCompositeVertexOutput main(uint vertexIndex : SV_VertexID){
    const half2 positions[NWB_DEFERRED_FULLSCREEN_TRIANGLE_VERTEX_COUNT] = half2[NWB_DEFERRED_FULLSCREEN_TRIANGLE_VERTEX_COUNT](half2(NWB_DEFERRED_FULLSCREEN_CLIP_MIN, NWB_DEFERRED_FULLSCREEN_CLIP_MIN), half2(NWB_DEFERRED_FULLSCREEN_CLIP_MAX, NWB_DEFERRED_FULLSCREEN_CLIP_MIN), half2(NWB_DEFERRED_FULLSCREEN_CLIP_MIN, NWB_DEFERRED_FULLSCREEN_CLIP_MAX));

    const half2 position = positions[vertexIndex];
    NwbDeferredCompositeVertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.uv = position * half2(0.5h, -0.5h) + half2(0.5h);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

