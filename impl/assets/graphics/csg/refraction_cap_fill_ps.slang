// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cap_surface.slangi"
#include "../avboit/binding_slots.h"
#include "../avboit/refraction_identity.slangi"
#include "../refraction/ior.slangi"
#include "../deferred/bindless_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgRefractionCapFillPixelOutput{
    [[vk::location(NWB_AVBOIT_ACCUM_COLOR_LOCATION)]] float4 normalIor : SV_Target0;
    [[vk::location(NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION)]] float4 tintCoverage : SV_Target1;
    [[vk::location(NWB_AVBOIT_ACCUM_FOREGROUND_COLOR_LOCATION)]] float4 instance : SV_Target2;
    [[vk::location(NWB_AVBOIT_ACCUM_FOREGROUND_EXTINCTION_LOCATION)]] float4 specularRoughness : SV_Target3;
    float depth : SV_Depth;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbCsgRefractionCapFillPixelOutput main(const NwbCsgCapFillPixelInput input){
    // Each draw selects one receiver so a non-refractive nearer cap cannot hide a refractive cap behind it.
    const uint receiverIndex = g_NwbCsgCapFillPushConstants.dispatch[NWB_MESH_PUSH_DISPATCH_INSTANCE_INDEX];
    const NwbCsgIntervalCapPixel cap = nwbCsgSampleNearestIntervalCap(input.position, receiverIndex);
    if(!cap.visible)
        discard;
    const uint frameSlot = g_NwbCsgCapFillPushConstants.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_VIEW];
    const float opaqueDepth = nwbHeapSampledImage2D(nwbHeapDeferredBindlessResources(frameSlot).gbufferSlots.w).Load(int3(int2(input.position.xy), 0)).r;
    if(cap.depth > opaqueDepth)
        discard;

    const NwbCsgReceiverRange receiver = g_NwbCsgReceiverRanges[cap.receiverIndex];
    const float3 incidentDirection = nwbCsgIntervalSampleNormalizeOr(cap.worldPosition - g_NwbMeshCameraPosition.xyz, -cap.normal);
    const NwbMeshSurface surface = nwbCsgCapSurface(cap, receiver, incidentDirection);
    const float ior = float(surface.refractionIor);
    if(!nwbRefractionIorEligible(ior))
        discard;

    NwbCsgRefractionCapFillPixelOutput output;
    output.normalIor = float4(nwbCsgIntervalSampleNormalizeOr(surface.normal, float3(0.0, 0.0, 1.0)), ior);
    output.tintCoverage = float4(float3(half3(clamp(half3(surface.shadowAbsorptionTint), half3(0.0h), half3(1.0h)))), clamp(float(surface.renderCoverage), 0.0, 1.0));
    output.instance = float4(nwbAvboitPackRefractionEntityId(g_NwbMeshInstances[receiver.materialContext.z].entityId), 0.0, 0.0);
    output.specularRoughness = nwbPackMeshSurfaceReflection(surface.specularF0, surface.perceptualRoughness);
    output.depth = cap.depth;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

