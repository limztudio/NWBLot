// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustic resolve pass (P3): one thread per pixel converts the R32_UINT fixed-point splat accumulators into the
// RGBA16F irradiance buffer the deferred lighting pass adds pre-tonemap. It un-scales the fixed point, divides by
// the receiver area subtended per pixel (this is what makes photon DENSITY become physical BRIGHTNESS -- where the
// lens focuses many photons into a small receiver area, accumulated flux is high AND the per-photon area is small,
// so irradiance spikes), and applies the single causticIntensity artist multiplier.
#define NWB_CAUSTIC_RESOLVE_SET 0

// The R32_UINT accumulators (Texture2DArray, one layer per RGB channel) read as an SRV.
#define NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR 0
// The G-buffer world-position SRV: the resolve estimates the receiver area subtended by each pixel from the
// world-space spacing of neighbouring pixels (a screen-space area Jacobian), so the area-normalization is physical.
#define NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION 1
// The G-buffer depth SRV: background pixels (no receiver) write zero irradiance.
#define NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH 2
// The RGBA16F irradiance UAV the resolve writes (the deferred lighting pass samples it).
#define NWB_CAUSTIC_RESOLVE_BINDING_IRRADIANCE 3
// The RGBA16F temporal history UAV (read previous + write blended) -- the EMA accumulator that averages the per-frame
// jittered splat across frames into a smooth caustic. Persisted across frames (NOT cleared per frame) and seeded on
// the first producer frame; the resolve writes the blended result to BOTH this history and the sampled irradiance.
#define NWB_CAUSTIC_RESOLVE_BINDING_HISTORY 4

// 8x8 = 64 threads per group (one thread per pixel).
#define NWB_CAUSTIC_RESOLVE_GROUP_SIZE 8


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

