
// SOFT TRANSPARENT half-res trace (all light types). The COLORED
// (Beer-Lambert / Fresnel) analog of sw_shadow_soft_opaque_cs: one thread per HALF-res pixel casts NWB_SW_SHADOW_TRANSPARENT_SPP
// cone-jittered occlusion rays inside the cone the light's physical source subtends and writes the AVERAGED single-frame
// COLORED transmittance (RGB, not a binary) into the parallel half-res soft transparent buffer. Byte-identical to the
// opaque soft trace EXCEPT: (a) the occluder class is TRANSPARENT (below) so nwbSwShadowLightVisible skips opaque blockers
// and integrates only the colored crossings (glass tints), keeping the RGB Beer-Lambert/Fresnel product instead of a
// binary mask; (b) the cone-jitter low-discrepancy sample index is offset by a decorrelation SALT so this estimator
// samples the source at points INDEPENDENT of the opaque soft trace -- the two signals are separately temporally
// denoised (opaque binary Bernoulli vs colored chord-variance RGB product; denoise(A)*denoise(B) != denoise(A*B)), so
// their per-frame noise must not be correlated, and they are folded (multiplied) only at the final full-res upsample.
// The SAME cone jitter (directional angularRadius params2.x / punctual asin(sourceRadius/dist)) means the colored
// penumbra softens for FREE -- the physics is reused verbatim; only the class + the kept-RGB differ. The separate
// shadow_resolve pipeline (RGB variant) then temporally-merges + a-trous-denoises + fold-multiply-upsamples the result.


#include "sw_binding_slots.h"

// TRANSPARENT occluder class: the soft transparent trace skips opaque blockers and integrates the colored crossings only.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"

struct NwbSwShadowTransparentSoftPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    // The renderer frame counter, seeding the per-pixel low-discrepancy cone-jitter sample so the jittered rays walk the
    // source across frames (decorrelated per pixel by a hash rotation). The SALT below further decorrelates this stream
    // from the opaque soft trace's.
    uint frameIndex;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentSoftPushConstants> g_NwbSwShadowPush;

// sw_shadow_ray.slangi pulls shadow_sampling.slangi (nwbShadowLowDiscrepancy2D + the cone jitter) transitively.
#include "sw_shadow_ray.slangi"

// The half-res soft TRANSPARENT transmittance buffer (one layer per shadow slot) this pass writes the cone-jittered
// colored sample into; the separate RGB shadow_resolve pipeline denoises + fold-multiplies it onto the full-res
// visibility (which already holds the soft OPAQUE result).
[[vk::binding(NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowTransparentSoftHalf;

#define NWB_SW_SHADOW_SOFT_TRACE_OUTPUT g_NwbSwShadowTransparentSoftHalf
#define NWB_SW_SHADOW_SOFT_TRACE_SPP NWB_SW_SHADOW_TRANSPARENT_SPP
#define NWB_SW_SHADOW_SOFT_TRACE_JITTER_SALT NWB_SW_SHADOW_TRANSPARENT_JITTER_SALT
#include "sw_shadow_soft_trace.slangi"
#undef NWB_SW_SHADOW_SOFT_TRACE_JITTER_SALT
#undef NWB_SW_SHADOW_SOFT_TRACE_SPP
#undef NWB_SW_SHADOW_SOFT_TRACE_OUTPUT


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbSwShadowTraceSoft(dispatchThreadID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

