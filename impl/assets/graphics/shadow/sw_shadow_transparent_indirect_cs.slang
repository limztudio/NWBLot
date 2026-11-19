// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"
#include "sw_shadow_edge_record.slangi"

#include "sw_shadow_ray.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSwShadowEdgeCounter nwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeCounterStorageSlot)
#define g_NwbSwShadowEdgeList nwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeListStorageSlot)

#define g_NwbSwShadowVisibility nwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.visibilityStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbSwShadowIndirectTrace(const uint linearId){
    if(linearId >= g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_TRACE])
        return;
    const uint base = linearId * NWB_SW_SHADOW_EDGE_RECORD_WORDS;
    const uint2 pixel = nwbSwShadowUnpackPixel(g_NwbSwShadowEdgeList[base]);
    const uint lightIndex = g_NwbSwShadowEdgeList[base + 1u];
    const NwbSceneLight light = g_NwbSceneLights[lightIndex];
    const uint slot = uint(light.params.z);
    const int3 texel = int3(int2(pixel), 0);
    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);
    const half3 transmittance = nwbSwShadowLightVisible(worldPosition, normal, light, float2(0.0, 0.0));

    const half3 opaqueMask = half3(g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb);
    g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * transmittance, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex){
    nwbSwShadowIndirectTrace(groupID.x * g_NwbSwShadowPush.traceGroupSize + groupIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

