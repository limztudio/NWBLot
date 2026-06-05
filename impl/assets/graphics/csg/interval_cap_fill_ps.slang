// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_INTERVAL_SAMPLE_SET 0

#include "interval_sample.slangi"

#include "../deferred/binding_slots.h"
#include "../mesh/runtime_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgIntervalCapFillPixelInput{
    float4 position : SV_Position;
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbCsgIntervalCapFillPixelOutput{
    [[vk::location(NWB_MESH_GBUFFER_BASE_COLOR_LOCATION)]] float4 baseColor : SV_Target0;
    [[vk::location(NWB_MESH_GBUFFER_NORMAL_LOCATION)]] float4 normal : SV_Target1;
    [[vk::location(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION)]] float4 worldPosition : SV_Target2;
    float depth : SV_Depth;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbCsgCapFillEncodeNormal(const float3 normal){
    const float lengthSquared = dot(normal, normal);
    const float3 normalVector = lengthSquared > 0.000001
        ? normal * rsqrt(lengthSquared)
        : float3(0.0, 0.0, 1.0)
    ;
    return normalVector * 0.5 + 0.5;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbCsgIntervalCapFillPixelOutput main(const NwbCsgIntervalCapFillPixelInput input){
    const NwbCsgIntervalCapPixel cap = nwbCsgSampleNearestIntervalCap(input.position);
    if(!cap.visible)
        discard;

    NwbCsgIntervalCapFillPixelOutput output;
    output.baseColor = float4(0.78, 0.72, 0.76, 1.0);
    output.normal = float4(nwbCsgCapFillEncodeNormal(cap.normal), 1.0);
    output.worldPosition = float4(cap.worldPosition, 1.0);
    output.depth = cap.depth;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

