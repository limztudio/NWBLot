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


struct NwbCombinedShadowReprojectMergePushConstants{
    row_major float4x4 prevWorldToClip;
    uint receiverFactor;
    uint lightSlotCount;
    uint historyValid;
    uint geometryCurrSlot;
    uint geometryPrevSlot;
    uint worldPositionSlot;
    uint opaqueSoftTraceSlot;
    uint opaqueHistoryInSlot;
    uint opaqueMomentsInSlot;
    uint opaqueHistoryOutSlot;
    uint opaqueMomentsOutSlot;
    uint transparentSoftTraceSlot;
    uint transparentHistoryInSlot;
    uint transparentMomentsInSlot;
    uint transparentHistoryOutSlot;
    uint transparentMomentsOutSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbCombinedShadowReprojectMergePushConstants> g_NwbShadowMergePush;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 h = dispatchThreadID.xy;
    uint halfWidth;
    uint halfHeight;
    nwbHeapSampledImage2D(g_NwbShadowMergePush.geometryCurrSlot).GetDimensions(halfWidth, halfHeight);
    if(h.x >= halfWidth || h.y >= halfHeight)
        return;

    const int hw = (int)halfWidth;
    const int hh = (int)halfHeight;
    const int2 texel = int2(h);

    const uint slotEnd = min(g_NwbShadowMergePush.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);

    uint fullWidth;
    uint fullHeight;
    nwbHeapSampledImage2D(g_NwbShadowMergePush.worldPositionSlot).GetDimensions(fullWidth, fullHeight);
    bool valid;
    NwbShadowMergeReprojection reprojection;
    bool historyAccepted;
    uint validTapMask;
    nwbShadowMergePreparePixel(
        h, hw, hh, halfWidth, halfHeight,
        fullWidth, fullHeight, g_NwbShadowMergePush.receiverFactor,
        g_NwbShadowMergePush.prevWorldToClip, g_NwbShadowMergePush.historyValid,
        g_NwbShadowMergePush.geometryCurrSlot, g_NwbShadowMergePush.geometryPrevSlot, g_NwbShadowMergePush.worldPositionSlot,
        valid, reprojection, historyAccepted, validTapMask
    );

    for(uint slot = 0u; slot < slotEnd; ++slot){
        [unroll] for(uint channel = 0u; channel < 2u; ++channel){
            const bool opaque = channel == 0u;
            const uint softTraceSlot = opaque ? g_NwbShadowMergePush.opaqueSoftTraceSlot : g_NwbShadowMergePush.transparentSoftTraceSlot;
            const uint historyInSlot = opaque ? g_NwbShadowMergePush.opaqueHistoryInSlot : g_NwbShadowMergePush.transparentHistoryInSlot;
            const uint momentsInSlot = opaque ? g_NwbShadowMergePush.opaqueMomentsInSlot : g_NwbShadowMergePush.transparentMomentsInSlot;
            const uint historyOutSlot = opaque ? g_NwbShadowMergePush.opaqueHistoryOutSlot : g_NwbShadowMergePush.transparentHistoryOutSlot;
            const uint momentsOutSlot = opaque ? g_NwbShadowMergePush.opaqueMomentsOutSlot : g_NwbShadowMergePush.transparentMomentsOutSlot;
            const NwbShadowMergeResult result = nwbShadowMergeChannel<true>(texel, reprojection.texel, hw, hh, slot, valid, historyAccepted, softTraceSlot, historyInSlot, momentsInSlot, g_NwbShadowMergePush.geometryCurrSlot, validTapMask);
            nwbHeapStorageImage2DArrayFloat4(historyOutSlot)[uint3(h, slot)] = result.history;
            nwbHeapStorageImage2DArrayFloat4(momentsOutSlot)[uint3(h, slot)] = result.moments;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

