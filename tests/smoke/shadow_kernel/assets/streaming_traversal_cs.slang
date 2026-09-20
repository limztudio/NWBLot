// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "shadow/shadow_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbStreamingShadowTestPush{
    uint materialContextSlotsHeapSlot;
    uint outputSlot;
    uint instanceCount;
    uint reserved;
    float4 origin;
    float4 direction;
};

[[vk::push_constant]]
ConstantBuffer<NwbStreamingShadowTestPush> g_NwbSwShadowPush;

#define g_NwbSwShadowMaterialContextSlots NwbHeapShadowMaterialContextSlots(g_NwbSwShadowPush.materialContextSlotsHeapSlot)
#include "shadow/sw_shadow_traverse.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(const uint3 dispatchId : SV_DispatchThreadID){
    if(dispatchId.x != 0u)
        return;
    NwbShadowVisibilityAccumulator visibility = nwbShadowVisibilityBegin();
    nwbSwShadowOccluded(
        g_NwbSwShadowPush.origin.xyz, g_NwbSwShadowPush.direction.xyz,
        g_NwbSwShadowPush.origin.w, g_NwbSwShadowPush.direction.w, visibility
    );
    const half3 transmission = nwbShadowFinalizeVisibility(visibility);
    g_NwbHeapStorageBuffers[g_NwbSwShadowPush.outputSlot].Store4(0u, asuint(float4(float3(transmission), 1.0)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

