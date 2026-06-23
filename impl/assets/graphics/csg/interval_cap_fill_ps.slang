// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_INTERVAL_SAMPLE_SET 0
#define NWB_CSG_REMOVED_INTERVAL_SAMPLE_ENABLED 1
#include "interval_sample.slangi"

#include "../deferred/binding_slots.h"
#include "../mesh/runtime_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Receiver-range view. The cap-fill PS only reads the receiver material info written by the CPU per
// removed-interval; the layout must stay byte-identical to NwbCsgReceiverRange in runtime.slangi
// (and to CsgReceiverRangeGpuData on the C++ side). The bind layout matches the CSG clip set bound
// alongside the interval-sample set in renderCsgIntervalCaps.
struct NwbCsgCapFillBounds{
    float3 minBounds;
    int minFlags;
    float3 maxBounds;
    int maxFlags;
};

struct NwbCsgCapFillReceiverRange{
    uint4 rangeInfo;    // x: firstCutter, y: cutterCount, z: flags, w: shadingModelId
    float4 worldToReceiverRow0;
    float4 worldToReceiverRow1;
    float4 worldToReceiverRow2;
    NwbCsgCapFillBounds localBounds;
    float4 baseColor;
};

[[vk::binding(NWB_CSG_BINDING_RECEIVER_RANGES, NWB_CSG_CLIP_SET)]]
StructuredBuffer<NwbCsgCapFillReceiverRange, Std430DataLayout> g_NwbCsgCapFillReceiverRanges;


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

    const NwbCsgCapFillReceiverRange receiver = g_NwbCsgCapFillReceiverRanges[cap.receiverIndex];

    NwbCsgIntervalCapFillPixelOutput output;
    // baseColor.rgb = receiver base color, baseColor.a = receiver cook-assigned shading-model id so
    // the carved cap is lit + shadowed by the same per-material BXDF as the receiver surface.
    output.baseColor = float4(receiver.baseColor.rgb, float(receiver.rangeInfo.w));
    // The mesh G-buffer PS packs spare BXDF params into normal.a / worldPosition.a; lambert/toon ignore
    // them, so 0.0 keeps default parameters for the carved cap.
    output.normal = float4(nwbCsgCapFillEncodeNormal(cap.normal), 0.0);
    output.worldPosition = float4(cap.worldPosition, 0.0);
    output.depth = cap.depth;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

