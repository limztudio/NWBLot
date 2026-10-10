// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_reproject_merge_binding_slots.h"
#include "shadow_resolve_binding_slots.h"
#include "sw_binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "../scene/binding_slots.h"
#include "../common/math.slangi"
#include "shadow_reproject_merge.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reprojects half-resolution soft-shadow history from the previous world-to-clip matrix.
// Geometry validation, variance clamping, and antilag reject invalid or changing history.

struct NwbShadowReprojectMergePushConstants{
    // Must match ShadowReprojectMergePushConstants in C++ and its row-major Float44U layout.
    row_major float4x4 prevWorldToClip;
    uint width;
    uint height;
    uint halfWidth;
    uint halfHeight;
    uint lightSlotStart;
    uint lightSlotCount;
    uint historyValid; // 0 forces pure-current output and prevents history reads.
    uint softTraceSlot;
    uint historyInSlot;
    uint momentsInSlot;
    uint geometryCurrSlot;
    uint geometryPrevSlot;
    uint worldPositionSlot;
    uint historyOutputStorageSlot;
    uint momentsOutputStorageSlot;
    uint receiverFactor;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowReprojectMergePushConstants> g_NwbShadowMergePush;

#define g_NwbShadowMergeHistoryOut nwbHeapStorageImage2DArrayFloat4(g_NwbShadowMergePush.historyOutputStorageSlot)
#define g_NwbShadowMergeMomentsOut nwbHeapStorageImage2DArrayFloat4(g_NwbShadowMergePush.momentsOutputStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 h = dispatchThreadID.xy;
    const uint halfWidth = g_NwbShadowMergePush.halfWidth;
    const uint halfHeight = g_NwbShadowMergePush.halfHeight;
    if(h.x >= halfWidth || h.y >= halfHeight)
        return;

    const int hw = (int)halfWidth;
    const int hh = (int)halfHeight;
    const int2 texel = int2(h);

    const uint slotStart = g_NwbShadowMergePush.lightSlotStart;
    const uint slotEnd = min(slotStart + g_NwbShadowMergePush.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);

    const half4 gc = nwbShadowMergeGeometry(g_NwbShadowMergePush.geometryCurrSlot, texel);
    const bool valid = gc.w > half(0.5h);
    const float3 Nc = nwbOctDecodeNormal(float2(gc.xy));
    const float currentCameraDistance = float(gc.z);

    const int2 full = int2(
        min((int)h.x * g_NwbShadowMergePush.receiverFactor, (int)g_NwbShadowMergePush.width - 1),
        min((int)h.y * g_NwbShadowMergePush.receiverFactor, (int)g_NwbShadowMergePush.height - 1)
    );
    const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbShadowMergePush.worldPositionSlot).Load(int3(full, 0)).xyz);
    const float3 Wc = float3(worldPositionSample);

    const NwbShadowMergeReprojection reprojection = nwbShadowReprojectHistory(
        Wc,
        g_NwbShadowMergePush.prevWorldToClip,
        g_NwbShadowMergePush.historyValid,
        halfWidth,
        halfHeight
    );
    const bool historyAccepted = nwbShadowMergeHistoryAccepted(
        reprojection,
        valid,
        g_NwbShadowMergePush.geometryPrevSlot,
        Nc,
        currentCameraDistance
    );

    for(uint slot = slotStart; slot < slotEnd; ++slot){
        const NwbShadowMergeResult result = nwbShadowMergeChannel<false>(
            texel,
            reprojection.texel,
            hw,
            hh,
            slot,
            valid,
            historyAccepted,
            g_NwbShadowMergePush.softTraceSlot,
            g_NwbShadowMergePush.historyInSlot,
            g_NwbShadowMergePush.momentsInSlot,
            g_NwbShadowMergePush.geometryCurrSlot,
            0u
        );
        g_NwbShadowMergeHistoryOut[uint3(h, slot)] = result.history;
        g_NwbShadowMergeMomentsOut[uint3(h, slot)] = result.moments;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

