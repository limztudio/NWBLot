#define NWB_CSG_INTERVAL_SAMPLE_SET 0
#define NWB_CSG_REMOVED_INTERVAL_SAMPLE_ENABLED 1
#include "interval_sample.slangi"

#include "../deferred/binding_slots.h"
#include "../mesh/runtime_constants.h"

// CSG caps have no source triangle, but they still evaluate the receiver's COMPLETE cook-generated surface hook.
// Keep the typed words + per-instance mutable storage in a dedicated third binding set; this lets heterogeneous
// .bind interfaces share the generated surface dispatcher without guessing an albedo field.
#undef NWB_MESH_SET
#define NWB_MESH_SET NWB_CSG_CAP_FILL_MATERIAL_SET
#include "../shadow/shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"
#undef NWB_MESH_SET


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
    uint4 materialContext; // x: surface dispatch id, y: constant byte offset, z: mesh instance index
};

[[vk::binding(NWB_CSG_BINDING_RECEIVER_RANGES, NWB_CSG_CLIP_SET)]]
StructuredBuffer<NwbCsgCapFillReceiverRange, Std430DataLayout> g_NwbCsgCapFillReceiverRanges;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgIntervalCapFillPixelInput{
    float4 position : SV_Position;
    [[vk::location(NWB_DEFERRED_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbCsgIntervalCapFillPixelOutput{
    [[vk::location(NWB_MESH_GBUFFER_BASE_COLOR_LOCATION)]] half4 baseColor : SV_Target0;
    [[vk::location(NWB_MESH_GBUFFER_NORMAL_LOCATION)]] half4 normal : SV_Target1;
    [[vk::location(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION)]] float4 worldPosition : SV_Target2;
    float depth : SV_Depth;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half3 nwbCsgCapFillEncodeNormal(const float3 normal){
    const float lengthSquared = dot(normal, normal);
    const float3 normalVector = lengthSquared > 0.000001
        ? normal * rsqrt(lengthSquared)
        : float3(0.0, 0.0, 1.0)
    ;
    return half3(normalVector * 0.5 + 0.5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbCsgIntervalCapFillPixelOutput main(const NwbCsgIntervalCapFillPixelInput input){
    const NwbCsgIntervalCapPixel cap = nwbCsgSampleNearestIntervalCap(input.position);
    if(!cap.visible)
        discard;

    const NwbCsgCapFillReceiverRange receiver = g_NwbCsgCapFillReceiverRanges[cap.receiverIndex];
    NwbShadowHit hit;
    hit.meshInstanceIndex = receiver.materialContext.z;
    hit.shadowTransmittanceModelId = receiver.materialContext.x;
    hit.materialConstantByteOffset = receiver.materialContext.y;
    // A cap has no receiver-triangle UV/color/tangent. Match the documented non-rasterized surface inputs used by
    // the generated dispatcher, while preserving every declared typed material field and both BXDF parameters.
    hit.uv0 = float2(0.0);
    hit.worldNormal = cap.normal;
    hit.worldPosition = cap.worldPosition;
    hit.rayDirection = -cap.normal;
    const NwbMeshSurface surface = nwbShadowDispatchSurface(hit.shadowTransmittanceModelId, hit);

    NwbCsgIntervalCapFillPixelOutput output;
    // baseColor.a carries the deferred BXDF id, while the other packed fields exactly match the mesh G-buffer.
    output.baseColor = half4(clamp(surface.baseColor, half3(0.0), half3(1.0)), half(float(receiver.rangeInfo.w)));
    output.normal = half4(nwbCsgCapFillEncodeNormal(surface.normal), surface.param0);
    output.worldPosition = float4(cap.worldPosition, surface.param1);
    output.depth = cap.depth;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

