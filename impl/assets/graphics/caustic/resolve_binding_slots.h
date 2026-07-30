// limztudio@gmail.com


#ifndef NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_RESOLVE_BINDING_SLOTS_H


// Caustic resolve pass: an EDGE-AVOIDING A-TROUS WAVELET denoise run as N ping-pong compute passes. The first
// pass converts the R32_UINT fixed-point splat accumulators into RGBA16F irradiance (un-scale the fixed point, divide
// by the receiver area subtended per pixel -- the photon-DENSITY -> physical-BRIGHTNESS conversion -- and apply the
// causticIntensity exposure) AND does the first wavelet step; later passes only run the wavelet at a doubled dilation.
// Purely spatial filtering avoids ghosting under non-rigid / morphing caustic motion. The output of the final pass is the
// irradiance buffer the lighting adds. Every input and output is selected from the global descriptor heap with the
// corresponding push-constant slot; the pass-local interface contains only that push block.

// Accumulator decay pre-pass: the SPLAT-SPACE temporal EMA. Before the producer
// splats this frame's photons, each accumulator texel is multiplied by decayFactor (accum_N = decay*accum_{N-1}); the
// producer then atomic-adds this frame's photons on top, so the accumulator holds the EMA and the static steady state
// is photons/(1-decayFactor). The resolve pre-multiplies causticIntensity by (1-decayFactor) so the STATIC brightness
// is unchanged. Reprojection-free (no image-space warp -> no ghosting on the spinning refractor).

// 8x8 = 64 threads per group (one thread per pixel).
#define NWB_CAUSTIC_RESOLVE_GROUP_SIZE 8

// A-trous wavelet pass count (the dispatch runs a PREPARE pass first, then this many wavelet passes at dilation
// 1,2,4,8,16,...). 25 taps/pass. Run at HALF resolution (see caustic_resolve_cs.slang). Cumulative world reach ~= 2x the
// sum of the dilations. 3 half-res passes (~16px full support) under-smoothed sparse/sharp caustics into visible speckle
// (confirmed still too grainy under motion even with the temporal accumulation); 5 passes (dilations 1,2,4,8,16 ==
// 2,4,8,16,32 full-equivalent, ~64px full support) smooth the sparse photon splat cleanly -- a clear visual win over 4 on
// the spinning refractors, for a small added cost (the 8,16 half-res passes fall back to the direct-texture-tap path, no
// LDS). Perf comes instead from the empty-tile early-out in the wavelet (the caustic is sparse; all-zero tiles skip),
// which preserves this quality EXACTLY. The dispatch handles any parity (it seeds the ping-pong so the final pass always
// lands in half-B, which the upsample reads into the full-res irradiance buffer the lighting samples).
#define NWB_CAUSTIC_RESOLVE_PASS_COUNT 5

// LDS (groupshared) tiling for the wavelet: passes with dilation stepWidth <= LDS_MAX_STEP cooperatively load the
// group's tile + 2*stepWidth halo into groupshared ONCE, then tap from LDS instead of re-Loading textures per tap
// (the small-dilation passes have heavy neighbour reuse). Larger-dilation passes tap textures directly (their taps are
// far apart -> negligible reuse, and the tile would exceed LDS). Tile side = GROUP_SIZE + 2*halo = GROUP_SIZE +
// 4*stepWidth; the groupshared arrays are sized for the largest LDS-tiled step (8 + 4*4 = 24 -> 576 texels, ~20 KB).
#define NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP 4
#define NWB_CAUSTIC_RESOLVE_TILE_SIDE (NWB_CAUSTIC_RESOLVE_GROUP_SIZE + 4 * NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP)
#define NWB_CAUSTIC_RESOLVE_TILE_TEXELS (NWB_CAUSTIC_RESOLVE_TILE_SIDE * NWB_CAUSTIC_RESOLVE_TILE_SIDE)


#endif


