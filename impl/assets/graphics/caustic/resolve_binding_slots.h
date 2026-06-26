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

// A-trous wavelet pass count. Pass 0 normalizes the accumulator + does the step-1 wavelet; passes 1..N-1 run the
// wavelet at dilation 2,4,8,... giving an effective support of ~2^(N+1) px with only 25 taps per pass. 5 passes ->
// ~32px support, enough to fill the inter-photon gaps into a smooth caustic. Odd count so the final write lands in the
// irradiance buffer (pass 0 -> irradiance, then alternate scratch/irradiance, ending in irradiance).
#define NWB_CAUSTIC_RESOLVE_PASS_COUNT 5


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

