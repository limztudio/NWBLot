// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../deferred/binding_slots.h"
#include "../mesh/runtime_constants.h"

// The cap-fill fullscreen draw reuses the ordinary 64-byte mesh push ABI.  Its fourth frame-slot word is unused by
// this path, so it selects the global CSG context UniformBuffer without growing any push range.
struct NwbCsgCapFillPushConstants{
    uint4 dispatch;
    float4 viewportRect;
    float4 scissorRect;
    uint4 frameHeapSlots;
};

[[vk::push_constant]]
ConstantBuffer<NwbCsgCapFillPushConstants> g_NwbCsgCapFillPushConstants;

#define NWB_CSG_CONTEXT_HEAP_SLOT (g_NwbCsgCapFillPushConstants.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_GENERATED_VERTEX])
#define NWB_CSG_MESH_INSTANCE_INDEX 0u
#include "runtime.slangi"
#undef NWB_CSG_MESH_INSTANCE_INDEX

#define NWB_CSG_REMOVED_INTERVAL_SAMPLE_ENABLED 1
#include "interval_sample.slangi"
#undef NWB_CSG_REMOVED_INTERVAL_SAMPLE_ENABLED
#undef NWB_CSG_CONTEXT_HEAP_SLOT

// CSG caps have no source triangle, but they still evaluate the receiver's COMPLETE cook-generated surface hook.
// The heap-selected clip context selects typed words and mutable mesh-instance storage; cap-material positions b6/b12
// remain intentional ABI gaps.
uint nwbMeshFrameMaterialTypedHeapSlot(){
    return g_NwbCsgClipContextSlots.materialTyped;
}
uint nwbMeshFrameInstanceHeapSlot(){
    return g_NwbCsgClipContextSlots.meshInstances;
}
// interval_sample.slangi already included bindless_heap_mesh.slangi with its b18-selected mesh-view alias. Its include
// guard keeps this include focused on the cap-fill material/instance accessors above.
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
    [[vk::location(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION)]] half4 worldPosition : SV_Target2;
    float depth : SV_Depth;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half3 nwbCsgCapFillEncodeNormal(const float3 normal){
    const float lengthSquared = dot(normal, normal);
    const float3 normalVector = lengthSquared > NWB_CSG_CAP_NORMALIZATION_LENGTH_SQUARED_EPSILON
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
    output.baseColor = half4(clamp(surface.baseColor, half3(0.0h), half3(1.0h)), half(float(receiver.rangeInfo.w)));
    output.normal = half4(nwbCsgCapFillEncodeNormal(surface.normal), surface.param0);
    output.worldPosition = half4(half3(cap.worldPosition), surface.param1);
    output.depth = cap.depth;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

