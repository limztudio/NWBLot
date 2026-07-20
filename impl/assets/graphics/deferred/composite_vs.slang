#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredCompositeVertexOutput{
    float4 position : SV_Position;
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredCompositeVertexOutput main(uint vertexIndex : SV_VertexID){
    const float2 positions[3] = float2[3](
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    );

    const float2 position = positions[vertexIndex];
    NwbDeferredCompositeVertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.uv = position * float2(0.5, -0.5) + float2(0.5);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

