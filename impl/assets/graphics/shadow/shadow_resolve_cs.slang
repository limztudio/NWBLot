// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"

#ifndef NWB_SHADOW_RESOLVE_COMPILED_STAGE
#error NWB_SHADOW_RESOLVE_COMPILED_STAGE must select a resolve stage.
#endif
#if NWB_SHADOW_RESOLVE_COMPILED_STAGE == NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE
#include "shadow_guided_upsample.slangi"
#elif NWB_SHADOW_RESOLVE_COMPILED_STAGE == NWB_SHADOW_RESOLVE_STAGE_WAVELET
#include "shadow_wavelet.slangi"
#else
#error NWB_SHADOW_RESOLVE_COMPILED_STAGE must select the wavelet or upsample stage.
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint slotStart = g_NwbShadowResolvePushConstants.lightSlotStart;
    const uint slotEnd = min(slotStart + g_NwbShadowResolvePushConstants.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);
#if NWB_SHADOW_RESOLVE_COMPILED_STAGE == NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE
    NwbShadowGuidedUpsample.run(dispatchThreadID, slotStart, slotEnd);
#else
    NwbShadowWavelet.run(dispatchThreadID, groupThreadID, groupID, slotStart, slotEnd);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

