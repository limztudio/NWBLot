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


struct NwbShadowReprojectMergePushConstants{
    row_major float4x4 prevWorldToClip;
    uint width;
    uint height;
    uint halfWidth;
    uint halfHeight;
    uint lightSlotStart;
    uint lightSlotCount;
    uint historyValid;
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

[[vk::push_constant]] ConstantBuffer<NwbShadowReprojectMergePushConstants> g_NwbShadowMergePush;

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
    const bool valid = gc.w > half(NWB_SHADOW_GEOMETRY_VALID_THRESHOLD);
    NwbShadowMergeReprojection reprojection;
    reprojection.texel = int2(0, 0);
    reprojection.onScreen = false;
    bool historyAccepted = false;
    uint validTapMask = 0u;
    if(valid){
        const float3 currentNormal = nwbOctDecodeNormal(float2(gc.xy));
        const float currentCameraDistance = float(gc.z);
        const int2 full = int2(min((int)h.x * g_NwbShadowMergePush.receiverFactor, (int)g_NwbShadowMergePush.width - 1), min((int)h.y * g_NwbShadowMergePush.receiverFactor, (int)g_NwbShadowMergePush.height - 1));
        const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbShadowMergePush.worldPositionSlot).Load(int3(full, 0)).xyz);
        const float3 currentWorldPosition = float3(worldPositionSample);
        reprojection = nwbShadowReprojectHistory(currentWorldPosition, g_NwbShadowMergePush.prevWorldToClip, g_NwbShadowMergePush.historyValid, halfWidth, halfHeight);
        historyAccepted = nwbShadowMergeHistoryAccepted(reprojection, true, g_NwbShadowMergePush.geometryPrevSlot, currentNormal, currentCameraDistance);
        [unroll] for(int dy = -1; dy <= 1; ++dy){
            [unroll] for(int dx = -1; dx <= 1; ++dx){
                const int2 tap = clamp(texel + int2(dx, dy), int2(0), int2(hw - 1, hh - 1));
                if(!(nwbShadowMergeGeometry(g_NwbShadowMergePush.geometryCurrSlot, tap).w <= half(NWB_SHADOW_GEOMETRY_VALID_THRESHOLD)))
                    validTapMask |= 1u << uint((dy + 1) * 3 + dx + 1);
            }
        }
    }

    for(uint slot = slotStart; slot < slotEnd; ++slot){
        const NwbShadowMergeResult result = nwbShadowMergeChannel<true>(
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
            validTapMask
        );
        g_NwbShadowMergeHistoryOut[uint3(h, slot)] = result.history;
        g_NwbShadowMergeMomentsOut[uint3(h, slot)] = result.moments;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

