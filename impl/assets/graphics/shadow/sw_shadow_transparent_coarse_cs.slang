// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-2 COARSE transparent trace (downscaled). One thread per coarse texel: trace ONE transparent occlusion ray per
// shadowed light from the block's top-left full-res G-buffer texel and store the colored transmittance into the coarse
// buffer (transmittance only, NOT yet multiplied onto the opaque mask). The cheap base layer the adaptive transparent
// resolve interpolates over the flat interior and refines at edges. Composes the SAME coarse trace as the opaque-coarse
// pass; the only difference is the TRANSPARENT occluder class (colored transmittance, not a binary blocker).


#include "sw_binding_slots.h"

// TRANSPARENT occluder class: skip opaque occluders -> colored Beer-Lambert transmittance.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

struct NwbSwShadowTransparentCoarsePushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint coarseWidth;
    uint coarseHeight;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentCoarsePushConstants> g_NwbSwShadowPush;

#include "sw_shadow_coarse.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbSwShadowCoarseTrace(dispatchThreadID.xy);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

