// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_ALL

#include "shadow/shadow_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbSceneRefitTraversalPush{
    uint materialContextSlotsHeapSlot;
    uint outputSlot;
    uint instanceCount;
    uint outputWordOffset;
    float4 origin;
    float4 direction;
};

[[vk::push_constant]]
ConstantBuffer<NwbSceneRefitTraversalPush> g_NwbSwShadowPush;

#define g_NwbSwShadowMaterialContextSlots NwbHeapShadowMaterialContextSlots(g_NwbSwShadowPush.materialContextSlotsHeapSlot)
#include "shadow/sw_shadow_traverse.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(1, 1, 1)]
void main(){
    const float3 origin = g_NwbSwShadowPush.origin.xyz;
    const float3 direction = g_NwbSwShadowPush.direction.xyz;
    const float maximum = g_NwbSwShadowPush.direction.w;
    NwbShadowVisibilityAccumulator visibility = nwbShadowVisibilityBegin();
    nwbSwShadowOccluded(origin, direction, 0.001, maximum, visibility);
    NwbShadowInstanceOptics optics = nwbShadowInstanceOpticsBegin();
    const NwbShadowInstanceStatus direct = nwbSwShadowInstanceOptics(0u, origin, direction, 0.001, maximum, optics);
    const half3 transmission = nwbShadowFinalizeVisibility(visibility);
    const uint offset = g_NwbSwShadowPush.outputWordOffset;
    RWStructuredBuffer<uint, Std430DataLayout> output = NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.outputSlot);
    output[offset] = asuint(float(transmission.x));
    output[offset + 1u] = asuint(float(transmission.y));
    output[offset + 2u] = asuint(float(transmission.z));
    output[offset + 3u] = uint(direct);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

