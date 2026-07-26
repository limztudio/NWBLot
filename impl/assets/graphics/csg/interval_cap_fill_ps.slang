// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_INTERVAL_SAMPLE_SET 0
#define NWB_CSG_REMOVED_INTERVAL_SAMPLE_ENABLED 1
#include "interval_sample.slangi"

#include "../deferred/binding_slots.h"
#include "../mesh/runtime_constants.h"

// Reuse the clip runtime's heap-selected receiver-range view. Cap fill has no receiver mesh invocation, so suppress
// runtime.slangi's otherwise-unused fallback mesh push constant while retaining its typed receiver/cutter aliases.
#define NWB_CSG_MESH_INSTANCE_INDEX 0u
#include "runtime.slangi"
#undef NWB_CSG_MESH_INSTANCE_INDEX

// CSG caps have no source triangle, but they still evaluate the receiver's COMPLETE cook-generated surface hook.
// The clip-context cbuffer selects typed words and mutable mesh-instance storage from the global heap, leaving the
// historical dedicated cap-material set (b6/b12) as an intentional gap.
#define NWB_MESH_FRAME_RESOURCES_BINDLESS 1
uint nwbMeshFrameMaterialTypedHeapSlot(){
    return g_NwbCsgClipContextSlots.materialTyped;
}
uint nwbMeshFrameInstanceHeapSlot(){
    return g_NwbCsgClipContextSlots.meshInstances;
}
// interval_sample.slangi has already declared the mesh-view cbuffer locally at set 0/b13. Its include guard
// keeps bindless_heap_mesh.slangi from revisiting that declaration, so only material/instance access is heap-based.
#include "../bindless/bindless_heap_mesh.slangi"
#include "../shadow/shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"


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

    const NwbCsgReceiverRange receiver = g_NwbCsgReceiverRanges[cap.receiverIndex];
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

