// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "splat_constants.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustic resolve = an EDGE-AVOIDING A-TROUS WAVELET denoise (Dammertz et al.), run at HALF RESOLUTION as N ping-pong
// compute passes, bracketed by a 2x2 downsample (the prepare pass) and a bilinear upsample. This REPLACES the temporal-
// EMA + variance-clamp + motion-vector path: a refractor's caustic moves NON-RIGIDLY (a spinning prism MORPHS its
// caustic), so no motion vector tracks it and any temporal accumulation trails it (the ghost). A-trous denoises each
// frame PURELY SPATIALLY -> the sparse photon splat smooths to a clean caustic with NO history -> ghost-free for any
// caustic motion. The producer's per-photon emission is a deterministic stratified distribution (no per-frame jitter),
// so a static scene is flicker-free without temporal averaging, and the wavelet's geometry edge-stop keeps the
// smoothing bounded to the receiver surface.
//
// HALF-RES PIPELINE (3 stages, selected by pushConstants.stage):
//   stage 0 PREPARE+DOWNSAMPLE (half-res dispatch): each half-res pixel sums the 2x2 full-res R32_UINT splat-accumulator
//     block it covers, un-scales the fixed point, AREA-NORMALIZES by the half-res pixel's receiver area (flux density ->
//     irradiance) and applies the causticIntensity exposure ONCE, writing the half-res prepared buffer (half-A). Summing
//     2x2 flux / (4x larger half-res area) == the full-res average irradiance, so brightness is preserved.
//   stage 1 WAVELET (half-res dispatch): edge-avoiding a-trous step at dilation stepWidth (1,2,4 half-res texels). Reads
//     the previous half-res color (half-A/half-B ping-pong) + the FULL-res G-buffer sampled at the half pixel's 2x
//     location for the world-distance edge-stop. Each half-res dilation reaches 2x the world extent of the same full-res
//     dilation, so 3 half-res passes match the old 5 full-res passes' support at a quarter the per-pass pixel count.
//   stage 2 UPSAMPLE (full-res dispatch): bilinearly samples the final half-res caustic into the full-res irradiance
//     buffer the deferred lighting adds; background pixels (no receiver) write zero (the additive no-op).

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2DArray<uint> g_NwbCausticResolveAccumulator;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveGBufferWorldPosition;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveGBufferDepth;

// The pass output (UAV). For PREPARE + WAVELET this is a HALF-res ping-pong buffer (half-A / half-B); for UPSAMPLE this
// is the FULL-res caustic irradiance buffer the deferred lighting adds. The C++ swaps the bound textures per pass.
[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_OUTPUT, NWB_CAUSTIC_RESOLVE_SET)]]
RWTexture2D<float4> g_NwbCausticResolveOutput;

// The input color (SRV). For WAVELET this is the previous pass's HALF-res color (the other ping-pong buffer); for
// UPSAMPLE it is the final half-res caustic. Unused on PREPARE (which reads the accumulator) but always bound.
[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_INPUT_COLOR, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveInputColor;

// Half-res geometry cache (xyz = world position, w = receiver validity 1/0), produced once by the geometry downsample
// pre-pass. The PREPARE + WAVELET passes read this single half-res texel for the edge-stop geometry + area + background
// skip, instead of re-reading the full-res world-position + depth G-buffer at the half pixel's 2x location every tap.
[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_GEOMETRY, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveGeometry;

struct NwbCausticResolvePushConstants{
    // All-scalar -> 8 x 4 = 32 bytes, no alignment padding; matches the C++ mirror CausticResolvePushConstants.
    uint width;             // FULL-res width (G-buffer dim; UPSAMPLE dispatch/output dim)
    uint height;            // FULL-res height
    uint halfWidth;         // HALF-res width (the resolve buffers; PREPARE/WAVELET dispatch dim)
    uint halfHeight;        // HALF-res height
    float causticIntensity; // exposure, applied once during the area-normalize on the PREPARE pass
    uint stepWidth;         // a-trous dilation for this wavelet pass (1,2,4), in HALF-res texels
    uint stage;             // 0 = PREPARE+DOWNSAMPLE, 1 = WAVELET (half-res), 2 = UPSAMPLE (-> full-res irradiance)
    uint pad;               // pad the tail to a 16B multiple (32) so C++ + shader sizes match exactly
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticResolvePushConstants> g_NwbCausticResolvePushConstants;

// Groupshared (LDS) tile for the small-dilation wavelet passes: the group cooperatively loads its half-res tile + halo
// here once, then every thread taps from LDS instead of re-Loading the G-buffer + color textures per tap. Sized for the
// largest LDS-tiled step (24x24 = 576 texels).
groupshared float3 s_NwbCausticResolveTileColor[NWB_CAUSTIC_RESOLVE_TILE_TEXELS];
groupshared float3 s_NwbCausticResolveTileWorld[NWB_CAUSTIC_RESOLVE_TILE_TEXELS];
groupshared uint   s_NwbCausticResolveTileValid[NWB_CAUSTIC_RESOLVE_TILE_TEXELS];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Half-res receiver geometry, read from the precomputed cache (one half-res texel, vs the old per-tap full-res world +
// depth re-reads). xyz = world position (the 2x point sample of the full-res G-buffer), w = receiver validity (1/0).
float3 nwbCausticResolveWorldAtHalf(const int2 halfTexel){
    return g_NwbCausticResolveGeometry.Load(int3(halfTexel, 0)).xyz;
}

bool nwbCausticResolveValidAtHalf(const int2 halfTexel){
    return g_NwbCausticResolveGeometry.Load(int3(halfTexel, 0)).w > 0.5;
}

// The world-space receiver area subtended by one HALF-res pixel, from the screen-space spacing of the half-res world
// position to the +x / +y half-res neighbours (the area Jacobian of the screen->receiver map at half resolution). This
// converts photon DENSITY into physical BRIGHTNESS; since a half-res pixel subtends ~4x a full-res pixel's area and the
// prepare pass sums the 2x2 (~4x) flux, the resulting irradiance matches the full-res average. Clamped away from zero.
float nwbCausticResolveHalfPixelArea(const int2 halfTexel){
    const float3 center = nwbCausticResolveWorldAtHalf(halfTexel);
    const int2 right = int2(min(halfTexel.x + 1, (int)g_NwbCausticResolvePushConstants.halfWidth - 1), halfTexel.y);
    const int2 down = int2(halfTexel.x, min(halfTexel.y + 1, (int)g_NwbCausticResolvePushConstants.halfHeight - 1));
    const float3 dx = nwbCausticResolveWorldAtHalf(right) - center;
    const float3 dy = nwbCausticResolveWorldAtHalf(down) - center;
    return max(length(cross(dx, dy)), 1e-4);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// 5x5 B-spline a-trous kernel (separable [1,4,6,4,1]/16), sampled at +-{1,2}*step. Edge-AVOIDING: each tap is weighted
// by the spatial kernel AND a world-distance edge-stop so the caustic never bleeds across a depth/silhouette
// discontinuity, while the sparse inter-photon gaps fill into a smooth caustic. No luminance edge-stop: the sparse
// splat's bright deposits ARE the noise to remove.
static const float s_NwbCausticResolveKernel[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };

[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint stage = g_NwbCausticResolvePushConstants.stage;

    // UPSAMPLE (stage 2, FULL-res dispatch): EDGE-AWARE (joint-bilateral) resample of the final half-res caustic into the
    // full-res irradiance buffer. A plain bilinear upsample would blend the 4 surrounding half-res taps regardless of what
    // surface they sit on, smearing the caustic ~1 full pixel PAST a silhouette onto the wrong receiver (or into a
    // shadowed neighbour) -- the caustic "exceeds" its region. Instead each tap is weighted by the bilinear weight AND a
    // world-distance edge-stop to THIS full-res pixel's receiver, and background taps are dropped, so the caustic stays
    // confined to the surface it belongs to. On a flat receiver all taps pass -> it degrades to plain bilinear.
    if(stage == 2u){
        const uint2 pixel = dispatchThreadID.xy;
        if(pixel.x >= g_NwbCausticResolvePushConstants.width || pixel.y >= g_NwbCausticResolvePushConstants.height)
            return;

        const int fw = (int)g_NwbCausticResolvePushConstants.width;
        const int fh = (int)g_NwbCausticResolvePushConstants.height;
        const int2 fullTexel = int2(pixel);
        if(g_NwbCausticResolveGBufferDepth.Load(int3(fullTexel, 0)).r >= 0.999999){
            g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0); // background: additive no-op
            return;
        }

        // Edge-stop sigma from the full-res pixel's own world spacing (max of +x / +y to handle foreshortening on a
        // grazing receiver); a few pixels of tolerance keeps same-surface taps while rejecting a silhouette jump.
        const float3 centerWorld = g_NwbCausticResolveGBufferWorldPosition.Load(int3(fullTexel, 0)).xyz;
        const int2 rightFull = int2(min(fullTexel.x + 1, fw - 1), fullTexel.y);
        const int2 downFull = int2(fullTexel.x, min(fullTexel.y + 1, fh - 1));
        const float3 dxWorld = g_NwbCausticResolveGBufferWorldPosition.Load(int3(rightFull, 0)).xyz - centerWorld;
        const float3 dyWorld = g_NwbCausticResolveGBufferWorldPosition.Load(int3(downFull, 0)).xyz - centerWorld;
        const float spacing = max(max(length(dxWorld), length(dyWorld)), 1e-4);
        const float sigmaWorld = spacing * 3.0;
        const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

        const int hw = (int)g_NwbCausticResolvePushConstants.halfWidth;
        const int hh = (int)g_NwbCausticResolvePushConstants.halfHeight;
        const float2 src = (float2(pixel) + 0.5) * 0.5 - 0.5; // full pixel centre -> half-res texel-centre space
        const int2 baseTexel = int2(floor(src));
        const float2 lerpFrac = src - float2(baseTexel);

        float3 sum = float3(0.0, 0.0, 0.0);
        float weightSum = 0.0;
        [unroll] for(int j = 0; j < 2; ++j){
            [unroll] for(int i = 0; i < 2; ++i){
                const int2 tap = int2(clamp(baseTexel.x + i, 0, hw - 1), clamp(baseTexel.y + j, 0, hh - 1));
                if(!nwbCausticResolveValidAtHalf(tap))
                    continue; // tap sits on background -> no receiver, would bleed the dark surround
                const float bilinearWeight = (i == 0 ? (1.0 - lerpFrac.x) : lerpFrac.x) * (j == 0 ? (1.0 - lerpFrac.y) : lerpFrac.y);
                const float3 d = nwbCausticResolveWorldAtHalf(tap) - centerWorld;
                const float weight = bilinearWeight * exp(-dot(d, d) * invTwoSigmaWorldSq);
                sum += weight * g_NwbCausticResolveInputColor.Load(int3(tap, 0)).rgb;
                weightSum += weight;
            }
        }
        // All four taps rejected (an isolated full-res receiver sliver between half-res background taps): no caustic.
        g_NwbCausticResolveOutput[pixel] = (weightSum > 0.0) ? float4(sum / weightSum, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // PREPARE + WAVELET both run at HALF resolution.
    const uint2 pixel = dispatchThreadID.xy;
    const int hw = (int)g_NwbCausticResolvePushConstants.halfWidth;
    const int hh = (int)g_NwbCausticResolvePushConstants.halfHeight;
    const int2 texel = int2(pixel);
    const bool inBounds = (pixel.x < g_NwbCausticResolvePushConstants.halfWidth && pixel.y < g_NwbCausticResolvePushConstants.halfHeight);

    // PREPARE+DOWNSAMPLE (stage 0): sum the 2x2 full-res accumulator block, un-scale, area-normalize (flux density ->
    // irradiance via the half-res screen->receiver area Jacobian) and apply the exposure -- ONCE per half-res pixel into
    // the buffer the wavelet passes read. No LDS / no wavelet here; all threads return.
    if(stage == 0u){
        if(inBounds){
            if(!nwbCausticResolveValidAtHalf(texel)){
                g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0); // background: additive no-op
            }
            else{
                const int fw = (int)g_NwbCausticResolvePushConstants.width;
                const int fh = (int)g_NwbCausticResolvePushConstants.height;
                const int2 base = int2(texel.x * 2, texel.y * 2);
                float3 flux = float3(0.0, 0.0, 0.0);
                [unroll] for(int oy = 0; oy < 2; ++oy){
                    [unroll] for(int ox = 0; ox < 2; ++ox){
                        const int2 fpx = int2(min(base.x + ox, fw - 1), min(base.y + oy, fh - 1));
                        flux += float3(
                            (float)g_NwbCausticResolveAccumulator.Load(int4(fpx, 0, 0)).r,
                            (float)g_NwbCausticResolveAccumulator.Load(int4(fpx, 1, 0)).r,
                            (float)g_NwbCausticResolveAccumulator.Load(int4(fpx, 2, 0)).r
                        );
                    }
                }
                flux /= NWB_CAUSTIC_FIXED_POINT_SCALE;
                const float area = nwbCausticResolveHalfPixelArea(texel);
                g_NwbCausticResolveOutput[pixel] = float4(flux / area * g_NwbCausticResolvePushConstants.causticIntensity, 1.0);
            }
        }
        return;
    }

    // WAVELET (stage 1), at HALF resolution. step is in half-res texels.
    const int step = (int)max(g_NwbCausticResolvePushConstants.stepWidth, 1u);

    // SMALL-dilation passes (uniform branch): cooperatively load the group's half-res tile + 2*step halo into LDS once,
    // then tap from LDS. ALL threads must reach the cooperative load + barrier before any per-pixel early-out.
    if(step <= NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP){
        const int tileW = NWB_CAUSTIC_RESOLVE_GROUP_SIZE + 4 * step; // group + 2*step halo each side
        const int2 tileOrigin = int2(groupID.xy) * NWB_CAUSTIC_RESOLVE_GROUP_SIZE - int2(2 * step, 2 * step);
        const uint threadCount = NWB_CAUSTIC_RESOLVE_GROUP_SIZE * NWB_CAUSTIC_RESOLVE_GROUP_SIZE;
        const uint threadIndex = groupThreadID.y * NWB_CAUSTIC_RESOLVE_GROUP_SIZE + groupThreadID.x;
        const uint tileTexels = (uint)(tileW * tileW);
        for(uint i = threadIndex; i < tileTexels; i += threadCount){
            const int2 t = tileOrigin + int2((int)(i % (uint)tileW), (int)(i / (uint)tileW));
            float3 tapColor = float3(0.0, 0.0, 0.0);
            float3 tapWorld = float3(0.0, 0.0, 0.0);
            uint tapValid = 0u;
            // Only receiver texels inside the half-res image contribute (matches the texture path's OOB + background skips).
            if(t.x >= 0 && t.y >= 0 && t.x < hw && t.y < hh){
                if(nwbCausticResolveValidAtHalf(t)){
                    tapColor = g_NwbCausticResolveInputColor.Load(int3(t, 0)).rgb;
                    tapWorld = nwbCausticResolveWorldAtHalf(t);
                    tapValid = 1u;
                }
            }
            s_NwbCausticResolveTileColor[i] = tapColor;
            s_NwbCausticResolveTileWorld[i] = tapWorld;
            s_NwbCausticResolveTileValid[i] = tapValid;
        }
        GroupMemoryBarrierWithGroupSync();

        if(!inBounds)
            return;
        if(!nwbCausticResolveValidAtHalf(texel)){
            g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        const float3 centerWorld = nwbCausticResolveWorldAtHalf(texel);
        const int2 rightTexel = int2(min(texel.x + 1, hw - 1), texel.y);
        const float pixelSpacing = max(length(nwbCausticResolveWorldAtHalf(rightTexel) - centerWorld), 1e-4);
        const float sigmaWorld = pixelSpacing * (float)step * 2.0;
        const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

        float3 sum = float3(0.0, 0.0, 0.0);
        float weightSum = 0.0;
        [unroll] for(int ky = -2; ky <= 2; ++ky){
            [unroll] for(int kx = -2; kx <= 2; ++kx){
                // Tap LDS coord = local thread + (k+2)*step (tile is offset by the 2*step halo). Always in [0, tileW).
                const uint idx = (uint)((int(groupThreadID.y) + (ky + 2) * step) * tileW + (int(groupThreadID.x) + (kx + 2) * step));
                if(s_NwbCausticResolveTileValid[idx] == 0u)
                    continue;
                const float3 d = s_NwbCausticResolveTileWorld[idx] - centerWorld;
                const float weight = s_NwbCausticResolveKernel[kx + 2] * s_NwbCausticResolveKernel[ky + 2] * exp(-dot(d, d) * invTwoSigmaWorldSq);
                sum += weight * s_NwbCausticResolveTileColor[idx];
                weightSum += weight;
            }
        }
        g_NwbCausticResolveOutput[pixel] = (weightSum > 0.0) ? float4(sum / weightSum, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // LARGE-dilation passes (step > LDS max): taps far apart -> no LDS reuse, tap the half-res textures directly. Dead
    // for the 3-pass (1,2,4) config but kept general. Identical math to the LDS path.
    if(!inBounds)
        return;
    if(!nwbCausticResolveValidAtHalf(texel)){
        g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float3 centerWorld = nwbCausticResolveWorldAtHalf(texel);
    const int2 rightTexel = int2(min(texel.x + 1, hw - 1), texel.y);
    const float pixelSpacing = max(length(nwbCausticResolveWorldAtHalf(rightTexel) - centerWorld), 1e-4);
    const float sigmaWorld = pixelSpacing * (float)step * 2.0;
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    [unroll] for(int ky = -2; ky <= 2; ++ky){
        [unroll] for(int kx = -2; kx <= 2; ++kx){
            const int2 tap = int2(texel.x + kx * step, texel.y + ky * step);
            if(tap.x < 0 || tap.y < 0 || tap.x >= hw || tap.y >= hh)
                continue;
            if(!nwbCausticResolveValidAtHalf(tap))
                continue; // background tap: no receiver -> skip so the dark surround cannot dilute the caustic edge
            const float3 d = nwbCausticResolveWorldAtHalf(tap) - centerWorld;
            const float weight = s_NwbCausticResolveKernel[kx + 2] * s_NwbCausticResolveKernel[ky + 2] * exp(-dot(d, d) * invTwoSigmaWorldSq);
            sum += weight * g_NwbCausticResolveInputColor.Load(int3(tap, 0)).rgb;
            weightSum += weight;
        }
    }

    g_NwbCausticResolveOutput[pixel] = (weightSum > 0.0) ? float4(sum / weightSum, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
