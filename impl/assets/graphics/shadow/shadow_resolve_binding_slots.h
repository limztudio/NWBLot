// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_RESOLVE_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Soft shadow RESOLVE: the spatial denoise + upsample of the half-res jittered visibility produced by the soft trace.
// It runs an edge-avoiding a-trous wavelet at half-res as N ping-pong compute passes, bracketed by a geometry downsample
// pre-pass + a bilateral upsample. Shadow-specific signal rules:
//  - the signal is a per-slot 0..1 VISIBILITY / colored transmittance, NOT irradiance, so ALL the caustic photon-
//    density / area-Jacobian / exposure math is dropped: the PREPARE pass just copies the half-res traced visibility.
//  - the MULTIPLICATIVE IDENTITY is 1.0 (fully LIT): background/invalid taps and the all-taps-rejected sentinel write
//    1.0, never the caustic 0.0 (which is the ADDITIVE identity -- writing 0.0 here would leak black shadow).
//  - the edge-stop adds a NORMAL term (dot of packed geometry normals) alongside the world-distance term, so the
//    penumbra never smooths across a surface-orientation crease the way a flat caustic on one receiver could.
// Per-slot: the half-res visibility is a Texture2DArray (one layer per shadow slot); the resolve processes a range of
// active shadow slots [lightSlotStart, lightSlotStart+lightSlotCount) carried in the push constants.
#define NWB_SHADOW_RESOLVE_SET 0

// The half-res soft visibility Texture2DArray (one layer per shadow slot). SRV role: read by PREPARE
// (copy) + WAVELET (the previous pass's color, via the ping-pong -- see below). The FINAL upsample reads it too.
#define NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF 0
// Half-res GEOMETRY CACHE SRV, produced once by the shadow geometry downsample pre-pass. RGBA16F packs the FOUR
// values the shadow edge-stop needs into one texel (the full world position is NOT stored -- shadow needs only a
// discontinuity metric, not the area Jacobian the caustic derived from world spacing): .xy = the receiver normal
// OCTAHEDRAL-packed to [-1,1]^2, .z = the WORLD-space distance from the camera (a linear, world-unit depth proxy --
// robust to the RGBA16F mantissa unlike a [0,1] hyperbolic depth, and it needs no projection matrix, just the camera
// position from the scene-shading CB), .w = receiver validity (1 = receiver, 0 = background). The wavelet + the
// upsample read this single half-res texel per tap for the distance + normal edge-stops + the background skip.
#define NWB_SHADOW_RESOLVE_BINDING_GEOMETRY 1
// The G-buffer depth SRV: background pixels (no receiver) write the LIT identity (1.0), and background taps are
// skipped by the wavelet. Consumed by the full-res UPSAMPLE's own centre pixel (the half-res passes use the cache).
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH 2
// The half-res ping-pong OUTPUT UAV for this pass. The C++ ping-pongs the two half-res buffers (soft-A / soft-B) as
// (input,output) each pass; with an ODD pass count the FINAL WAVELET pass lands in soft-A, then the UPSAMPLE reads
// soft-A and writes the FULL-res visibility.
#define NWB_SHADOW_RESOLVE_BINDING_OUTPUT 3
// The previous pass's half-res color, read as an SRV (the OTHER ping-pong buffer). The PREPARE pass reads the
// SOFT_HALF trace buffer instead (bound at slot 0); this slot is the wavelet's inter-pass input. Always bound.
#define NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR 4
// The FULL-res visibility Texture2DArray UAV (g_NwbSwShadowVisibility): the UPSAMPLE stage's output -- the same
// image the deferred lighting samples. Only written by the UPSAMPLE stage (bound but unused on PREPARE/WAVELET).
#define NWB_SHADOW_RESOLVE_BINDING_VISIBILITY 5

// The TEMPORAL MOMENTS Texture2DArray SRV (the reproject-merge's moments-out: .x = integrated luma mean m1, .y =
// integrated luma second-moment m2, .z = history length n). The WAVELET stage reads it to drive the SVGF variance-
// coupled luminance edge-stop (variance = max(m2 - m1*m1, 0)) where history is long. Bound ONLY when temporal is live
// (push.momentsValid == 1); the non-temporal / first-frame path binds a valid-but-unused dummy half-res array and the
// shader guards every read behind momentsValid == 0 -> the SPATIAL variance fallback, so a dummy is never sampled.
#define NWB_SHADOW_RESOLVE_BINDING_MOMENTS 6
// The FULL-res G-buffer world-position SRV (targets.worldPosition). The UPSAMPLE stage reads THIS output pixel's own
// full-res world position as the joint-bilateral CENTRE (via the camera distance below) so two adjacent full-res
// pixels straddling a geometry edge inside one half-res block pick DIFFERENT half-res taps -> the shadow terminator
// snaps to the full-res silhouette instead of stair-stepping at the half-res block boundary. Bound on all sets.
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS 7
// The FULL-res G-buffer normal SRV (targets.normal), [0,1]-encoded (matching the traces' `n*2-1` decode). The
// UPSAMPLE stage decodes it as the full-res bilateral centre normal for the per-tap normal edge-stop. Bound on all sets.
#define NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL 8
// The scene-shading CB (m_sceneShadingBuffer; xyz = camera world position). The UPSAMPLE stage derives the full-res
// centre's camera distance as length(centerWorld - cameraPosition) so its distance edge-stop is in the SAME world-unit
// space as the half-res geometry cache's stored .z, keeping the wavelet + upsample distance metric consistent end-to-end.
#define NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING 9

// Shadow geometry downsample pre-pass (its own pipeline + binding layout): reads the full-res G-buffer world position
// + normal + depth + the scene-shading CB (for the camera position), writes the packed half-res geometry cache above
// (octahedral normal + camera distance + validity).
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_SET 0
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION 0
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_NORMAL 1
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH 2
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING 3
#define NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT 4

// 8x8 = 64 threads per group (one thread per pixel), matching the caustic resolve + the SW traversal.
#define NWB_SHADOW_RESOLVE_GROUP_SIZE 8

// A-trous wavelet channel count. The resolve source (shadow_resolve_cs.slang) parameterizes ONLY its wavelet
// accumulator + tap read/write by this: 1 = SCALAR (the soft OPAQUE grayscale visibility -- the default; keeps 1x ALU +
// 1x LDS on the common path), 3 = RGB (the soft COLORED TRANSPARENT transmittance -- denoises color, the value edge-stop
// uses the LUMA of the RGB delta). ONE source, TWO cooked pipelines (shadow_resolve_cs = scalar default, shadow_resolve_
// rgb_cs = the =3 wrapper), so the common opaque path is never charged the 3x wavelet ALU/LDS for an identically-gray
// signal while the transparent path denoises the colored penumbra. The kernel, edge-stops, LDS load/branch, and the
// upsample (already RGB end-to-end) are shared; only the wavelet vector type + tap channel count differ.
#define NWB_SHADOW_RESOLVE_CHANNELS_SCALAR 1
#define NWB_SHADOW_RESOLVE_CHANNELS_RGB    3

// A-trous wavelet pass count (the dispatch runs a PREPARE copy first, then this many wavelet passes at dilation
// 1,2,4,8,16). Run at HALF resolution. A single-sample jittered penumbra is NOISY (one sample per pixel, no temporal),
// so a WIDE support is needed to reconstruct the smooth gradient -- 5 half-res passes (dilations 1,2,4,8,16 ==
// 2,4,8,16,32 full-equivalent, ~64px full support) matches the caustic sparse-splat reconstruction, which is the same
// class of problem (a sparse/noisy signal denoised purely spatially). The dispatch seeds the ping-pong so the final
// wavelet pass always lands in soft-A (PASS_COUNT is ODD), which the upsample reads into the full-res visibility.
#define NWB_SHADOW_RESOLVE_PASS_COUNT 5

// A-trous wavelet pass count for the soft COLORED-TRANSPARENT resolve. FEWER than the opaque 5: the transparent tint is a
// SMOOTH low-frequency signal (Beer-Lambert/Fresnel colored penumbra), not the opaque path's sharp binary blocker edge, so it
// needs far less spatial support to reconstruct -- 3 half-res passes (dilations 1,2,4 == 2,4,8 full-equivalent, ~8px full
// support) suffice for the smooth colored gradient, cutting the transparent a-trous cost from 5 to 3 passes. It is temporally
// accumulated (reproject-merge) on top. MUST be ODD (like the opaque 5): the dispatch seeds the ping-pong so the final wavelet
// lands in soft-A ONLY for an odd count, and the fixed upsample binding set reads soft-A. Both 5 and 3 are odd.
#define NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT 3

// LDS (groupshared) tiling for the wavelet: passes with dilation stepWidth <= LDS_MAX_STEP cooperatively load the
// group's tile + 2*stepWidth halo into groupshared ONCE, then tap from LDS. Larger-dilation passes tap the textures
// directly. Tile side = GROUP_SIZE + 4*stepWidth; sized for the largest LDS-tiled step (8 + 4*4 = 24 -> 576 texels).
#define NWB_SHADOW_RESOLVE_LDS_MAX_STEP 4
#define NWB_SHADOW_RESOLVE_TILE_SIDE (NWB_SHADOW_RESOLVE_GROUP_SIZE + 4 * NWB_SHADOW_RESOLVE_LDS_MAX_STEP)
#define NWB_SHADOW_RESOLVE_TILE_TEXELS (NWB_SHADOW_RESOLVE_TILE_SIDE * NWB_SHADOW_RESOLVE_TILE_SIDE)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

