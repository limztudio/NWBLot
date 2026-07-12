
#pragma once


#include <impl/global.h>

#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCaustic{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The software caustic photon producer (P3) + the resolve pass shader virtual paths, and the hardware ray-traced
// producer (P4) raygen / closest-hit / miss virtual paths. The HW producer shares the resolve with the SW path.
inline constexpr Name s_SwPhotonShaderName("engine/graphics/caustic/caustic_photon_sw_cs");
inline constexpr Name s_ResolveShaderName("engine/graphics/caustic/caustic_resolve_cs");
// Pre-pass that downsamples the full-res G-buffer world position + receiver-validity into the half-res geometry cache
// the resolve's wavelet passes read (so they tap one half-res texel instead of re-reading full-res world+depth).
inline constexpr Name s_GeometryDownsampleShaderName("engine/graphics/caustic/caustic_geometry_downsample_cs");
// Splat-space temporal EMA decay pre-pass: multiplies the resident accumulator by decayFactor before the producer splats
// this frame's photons (accum_N = decay*accum_{N-1} + photons_N), reprojection-free -> no ghosting. Enabled by env
// NWB_CAUSTIC_TEMPORAL_DECAY (>0); disabled (<=0) keeps the per-frame clear + no normalization change.
inline constexpr Name s_AccumulatorDecayShaderName("engine/graphics/caustic/caustic_accumulator_decay_cs");
inline constexpr Name s_HwRaygenShaderName("engine/graphics/caustic/caustic_photon_hw_raygen");
inline constexpr Name s_HwClosestHitShaderName("engine/graphics/caustic/caustic_photon_hw_chit");
inline constexpr Name s_HwMissShaderName("engine/graphics/caustic/caustic_photon_hw_miss");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

