// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowCombinedTemporalPushConstants{
    Float44U prevWorldToClip = {};
    u32 receiverFactor = 2u;
    u32 lightSlotCount = 0u;
    u32 historyValid = 0u;
    u32 geometryCurrSlot = 0u;
    u32 geometryPrevSlot = 0u;
    u32 worldPositionSlot = 0u;
    u32 opaqueSoftTraceSlot = 0u;
    u32 opaqueHistoryInSlot = 0u;
    u32 opaqueMomentsInSlot = 0u;
    u32 opaqueHistoryOutSlot = 0u;
    u32 opaqueMomentsOutSlot = 0u;
    u32 transparentSoftTraceSlot = 0u;
    u32 transparentHistoryInSlot = 0u;
    u32 transparentMomentsInSlot = 0u;
    u32 transparentHistoryOutSlot = 0u;
    u32 transparentMomentsOutSlot = 0u;
};
static_assert(sizeof(ShadowCombinedTemporalPushConstants) == 128u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, receiverFactor) == 64u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, opaqueSoftTraceSlot) == 88u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, transparentSoftTraceSlot) == 108u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, transparentMomentsOutSlot) == 124u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

