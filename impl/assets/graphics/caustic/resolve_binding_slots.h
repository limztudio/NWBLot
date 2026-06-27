// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustic resolve pass (P3): an EDGE-AVOIDING A-TROUS WAVELET denoise run as N ping-pong compute passes. The first
// pass converts the R32_UINT fixed-point splat accumulators into RGBA16F irradiance (un-scale the fixed point, divide
// by the receiver area subtended per pixel -- the photon-DENSITY -> physical-BRIGHTNESS conversion -- and apply the
// causticIntensity exposure) AND does the first wavelet step; later passes only run the wavelet at a doubled dilation.
// This replaces the temporal-EMA + variance-clamp + motion-vector path: purely spatial -> ghost-free for any (even
// non-rigid / morphing) caustic motion. The output of the final pass is the irradiance buffer the lighting adds.
#define NWB_CAUSTIC_RESOLVE_SET 0

// The R32_UINT accumulators (Texture2DArray, one layer per RGB channel) read as an SRV (first pass only).
#define NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR 0
// The G-buffer world-position SRV: the resolve estimates the receiver area subtended by each pixel from the
// world-space spacing of neighbouring pixels (a screen-space area Jacobian), so the area-normalization is physical;
// it is ALSO the wavelet's geometry edge-stop (the caustic never bleeds across a receiver depth/silhouette jump).
#define NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION 1
// The G-buffer depth SRV: background pixels (no receiver) write zero, and background taps are skipped by the wavelet.
#define NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH 2
// The RGBA16F wavelet OUTPUT UAV for this pass. The C++ ping-pongs the two RGBA16F buffers (irradiance + scratch) as
// (input,output) each pass; the FINAL pass writes the caustic irradiance buffer the deferred lighting samples.
#define NWB_CAUSTIC_RESOLVE_BINDING_OUTPUT 3
// The previous pass's RGBA16F color, read as an SRV (the OTHER ping-pong buffer). Unused on the first pass (which reads
// the accumulator instead) but always bound so the descriptor is valid.
#define NWB_CAUSTIC_RESOLVE_BINDING_INPUT_COLOR 4

// 8x8 = 64 threads per group (one thread per pixel).
#define NWB_CAUSTIC_RESOLVE_GROUP_SIZE 8

// A-trous wavelet pass count (the dispatch runs a PREPARE pass first, then this many wavelet passes at dilation
// 1,2,4,8,16,...). 25 taps/pass. Run at HALF resolution (see caustic_resolve_cs.slang). Cumulative world reach ~= 2x the
// sum of the dilations. 3 half-res passes (~16px full support) under-smoothed sparse/sharp caustics into visible speckle;
// 5 passes (dilations 1,2,4,8,16 == 2,4,8,16,32 full-equivalent, ~64px full support) smooth the sparse photon splat
// cleanly -- a clear visual win over 4 on the spinning refractors, for a small added cost (the 8,16 half-res passes fall
// back to the direct-texture-tap path, no LDS). The dispatch handles any parity (it seeds the ping-pong so the final
// pass always lands in half-B, which the upsample reads into the full-res irradiance buffer the lighting samples).
#define NWB_CAUSTIC_RESOLVE_PASS_COUNT 5

// LDS (groupshared) tiling for the wavelet: passes with dilation stepWidth <= LDS_MAX_STEP cooperatively load the
// group's tile + 2*stepWidth halo into groupshared ONCE, then tap from LDS instead of re-Loading textures per tap
// (the small-dilation passes have heavy neighbour reuse). Larger-dilation passes tap textures directly (their taps are
// far apart -> negligible reuse, and the tile would exceed LDS). Tile side = GROUP_SIZE + 2*halo = GROUP_SIZE +
// 4*stepWidth; the groupshared arrays are sized for the largest LDS-tiled step (8 + 4*4 = 24 -> 576 texels, ~20 KB).
#define NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP 4
#define NWB_CAUSTIC_RESOLVE_TILE_SIDE (NWB_CAUSTIC_RESOLVE_GROUP_SIZE + 4 * NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP)
#define NWB_CAUSTIC_RESOLVE_TILE_TEXELS (NWB_CAUSTIC_RESOLVE_TILE_SIDE * NWB_CAUSTIC_RESOLVE_TILE_SIDE)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

