// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_ALL


#include "shadow/shadow_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbShadowTraversalTestPush{
    uint materialContextSlotsHeapSlot;
    uint outputSlot;
    uint instanceCount;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowTraversalTestPush> g_NwbSwShadowPush;

#define g_NwbSwShadowMaterialContextSlots NwbHeapShadowMaterialContextSlots(g_NwbSwShadowPush.materialContextSlotsHeapSlot)
#include "shadow/sw_shadow_traverse.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(8, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID){
    const uint caseIndex = dispatchId.x;
    if(caseIndex >= 6u)
        return;
    const float3 origin = caseIndex == 2u ? float3(4.0, 0.0, 0.0)
        : caseIndex == 3u ? float3(0.0, 0.0, 3.0)
        : caseIndex == 5u ? float3(0.0, 0.0, 1.5)
        : float3(0.0, 0.0, 0.0);
    const float3 direction = float3(0.0, 0.0, 1.0);
    const float tMax = caseIndex == 1u ? 3.0 : 10.0;
    const uint instanceIndex = caseIndex == 4u ? 1u : 0u;
    NwbShadowInstanceOptics optics = nwbShadowInstanceOpticsBegin();
    const NwbShadowInstanceStatus status = nwbSwShadowInstanceOptics(instanceIndex, origin, direction, 0.001, tMax, optics);
    NwbShadowVisibilityAccumulator visibility = nwbShadowVisibilityBegin();
    nwbSwShadowInstanceOccluded(instanceIndex, origin, direction, 0.001, tMax, visibility);
    const half3 transmission = nwbShadowFinalizeVisibility(visibility);
    const uint offset = caseIndex * 8u;
    RWStructuredBuffer<uint, Std430DataLayout> output = NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.outputSlot);
    output[offset] = uint(status);
    output[offset + 1u] = optics.crossingCount;
    output[offset + 2u] = asuint(nwbShadowInstanceChordLength(optics));
    output[offset + 3u] = asuint(float(transmission.x));
    output[offset + 4u] = asuint(float(transmission.y));
    output[offset + 5u] = asuint(float(transmission.z));
    output[offset + 6u] = optics.crossingOverflow;
    output[offset + 7u] = asuint(float(visibility.interfaceTransmittance));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

