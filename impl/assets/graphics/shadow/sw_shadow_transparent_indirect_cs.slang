// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-3 indirect trace + scatter: one compacted edge record per thread, re-traced full-res and folded onto the
// pristine opaque mask. Tile-major order keeps rays screen-local; every lane is a real edge ray.


#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// TRANSPARENT occluder class: the edge re-trace casts colored transmittance.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"
#include "sw_shadow_edge_record.slangi"

#include "sw_shadow_ray.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The clamped trace count ([1]) guarding the tail + the compacted edge-record list this pass consumes.
#define g_NwbSwShadowEdgeCounter NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeCounterStorageSlot)
#define g_NwbSwShadowEdgeList NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeListStorageSlot)

// The full-res visibility array: fold this edge's transmittance onto the pristine opaque mask with a single overwrite.
#define g_NwbSwShadowVisibility NwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.visibilityStorageSlot)


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
    // 1D over the compacted edge list: flat record index = groupID.x * traceGroupSize + groupIndex. The group is
    // NWB_SW_SHADOW_TRACE_GROUP threads and the build-args pass sized groupsX from that same constant.
    nwbSwShadowIndirectTrace(groupID.x * g_NwbSwShadowPush.traceGroupSize + groupIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

