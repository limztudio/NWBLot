// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-3 INDIRECT TRACE + SCATTER (compacted transparent resolve). DispatchIndirect, one compacted edge record per
// thread: unpack the pixel + light, re-trace that single light full-res via nwbSwShadowLightVisible (preserving the
// per-mesh NonUniformResourceIndex wrapping), and fold onto the PRISTINE opaque mask the classify pass left in the
// texel. Every lane in the wave is a real edge ray (no divergence); the tile-major append keeps the records -- and thus
// the traced rays -- screen-local. The 1D dispatch reconstructs its flat record index as
// groupID.x * traceGroupSize + SV_GroupIndex (the group is [numthreads(8,8,1)] = 64 threads, sized by the build-args pass).


#include "sw_binding_slots.h"

// TRANSPARENT occluder class: the edge re-trace casts colored transmittance.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"
#include "sw_shadow_edge_record.slangi"

struct NwbSwShadowTransparentIndirectPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    // Threads per indirect-trace group (= NWB_SW_SHADOW_TRACE_GROUP): the flat record index is
    // groupID.x * traceGroupSize + SV_GroupIndex.
    uint traceGroupSize;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentIndirectPushConstants> g_NwbSwShadowPush;

#include "sw_shadow_ray.slangi"

// The clamped trace count ([1]) guarding the tail + the compacted edge-record list this pass consumes.
[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeCounter;

[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_LIST, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeList;

// The full-res visibility array: fold this edge's transmittance onto the pristine opaque mask with a single overwrite.
[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowVisibility;


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

[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex){
    // 1D over the compacted edge list: flat record index = groupID.x * traceGroupSize + groupIndex. The group is
    // [numthreads(8,8,1)] = 64 threads and the build-args pass sized groupsX = ceil(traceCount / 64).
    nwbSwShadowIndirectTrace(groupID.x * g_NwbSwShadowPush.traceGroupSize + groupIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

