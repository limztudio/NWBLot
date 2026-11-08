// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_resolve_prepare.slangi"
#include "caustic_resolve_wavelet.slangi"
#include "caustic_resolve_upsample.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
#if NWB_CAUSTIC_RESOLVE_COMPILED_STAGE == NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_DYNAMIC
    const uint stage = g_NwbCausticResolvePushConstants.stage;
#elif NWB_CAUSTIC_RESOLVE_COMPILED_STAGE == NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_DIRECT \
    || NWB_CAUSTIC_RESOLVE_COMPILED_STAGE == NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_ONE \
    || NWB_CAUSTIC_RESOLVE_COMPILED_STAGE == NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_TWO
    const uint stage = NWB_CAUSTIC_RESOLVE_STAGE_WAVELET;
#else
    const uint stage = NWB_CAUSTIC_RESOLVE_COMPILED_STAGE;
#endif

    if(stage == NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE){
        NwbCausticResolveUpsample.run(dispatchThreadID);
        return;
    }
    if(stage == NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE){
        NwbCausticResolvePrepare.run(dispatchThreadID, groupThreadID, groupID);
        return;
    }
    if(stage != NWB_CAUSTIC_RESOLVE_STAGE_WAVELET)
        return;
    NwbCausticResolveWavelet.run(dispatchThreadID, groupThreadID, groupID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_NwbCausticResolveOutput
#undef NWB_CAUSTIC_RESOLVE_STEP_WIDTH
#undef NWB_CAUSTIC_RESOLVE_TILE_CAPACITY_STEP
#undef NWB_CAUSTIC_RESOLVE_TILE_SIDE
#undef NWB_CAUSTIC_RESOLVE_TILE_TEXELS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

