// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "splat_constants.slangi"
#include "../bindless/bindless_heap_uint_array.slangi"
#include "../scene/binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Spatial-only filtering avoids reprojection ghosts from non-rigid caustic motion.
// Half-resolution a-trous resolve: prepare, wavelet, then edge-aware upsample.

struct NwbCausticResolvePushConstants{
    // Must match CausticResolvePushConstants in C++; heap slots select every stage resource.
    uint width;
    uint height;
    uint halfWidth;
    uint halfHeight;
    float causticIntensity;
    uint stepWidth;
    uint stage;
    uint pad;
    uint worldPositionSlot;
    uint depthSlot;
    uint inputColorSlot;
    uint geometrySlot;
    uint accumulatorSlot;
    uint outputStorageSlot;
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticResolvePushConstants> g_NwbCausticResolvePushConstants;

#define g_NwbCausticResolveOutput NwbHeapStorageImage2DFloat4(g_NwbCausticResolvePushConstants.outputStorageSlot)

// Groupshared tile for small-dilation wavelet passes.
groupshared half3  s_NwbCausticResolveTileColor[NWB_CAUSTIC_RESOLVE_TILE_TEXELS];
// The geometry cache is RGBA16F. Keep its sampled world positions compact in LDS and widen only for the edge-stop
// arithmetic below; storing them as float3 here would double the shared-memory footprint without adding precision.
groupshared half3  s_NwbCausticResolveTileWorld[NWB_CAUSTIC_RESOLVE_TILE_TEXELS];
groupshared uint   s_NwbCausticResolveTileValid[NWB_CAUSTIC_RESOLVE_TILE_TEXELS];
groupshared uint   s_NwbCausticResolveTileActive;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half3 nwbCausticResolveWorldAtHalf(const int2 halfTexel){
    return half3(NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.geometrySlot).Load(int3(halfTexel, 0)).xyz);
}

bool nwbCausticResolveValidAtHalf(const int2 halfTexel){
    return NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.geometrySlot).Load(int3(halfTexel, 0)).w > 0.5;
}

// World-space area subtended by one half-resolution pixel.
float nwbCausticResolveHalfPixelArea(const int2 halfTexel){
    const float3 center = float3(nwbCausticResolveWorldAtHalf(halfTexel));
    const int2 right = int2(min(halfTexel.x + 1, (int)g_NwbCausticResolvePushConstants.halfWidth - 1), halfTexel.y);
    const int2 down = int2(halfTexel.x, min(halfTexel.y + 1, (int)g_NwbCausticResolvePushConstants.halfHeight - 1));
    const float3 dx = float3(nwbCausticResolveWorldAtHalf(right)) - center;
    const float3 dy = float3(nwbCausticResolveWorldAtHalf(down)) - center;
    return max(length(cross(dx, dy)), 1e-4);
}

// 5x5 B-spline a-trous kernel.
// Geometry-only edge stop: luminance is sparse-splat noise and must not gate filtering.
static const float s_NwbCausticResolveKernel[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };

[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint stage = g_NwbCausticResolvePushConstants.stage;

    // Edge-aware upsample to the full-resolution irradiance buffer.
    if(stage == NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE){
        const uint2 pixel = dispatchThreadID.xy;
        if(pixel.x >= g_NwbCausticResolvePushConstants.width || pixel.y >= g_NwbCausticResolvePushConstants.height)
            return;

        const int fw = (int)g_NwbCausticResolvePushConstants.width;
        const int fh = (int)g_NwbCausticResolvePushConstants.height;
        const int2 fullTexel = int2(pixel);
        if(NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.depthSlot).Load(int3(fullTexel, 0)).r >= NWB_SCENE_BACKGROUND_DEPTH){
            g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        const float3 centerWorld = NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.worldPositionSlot).Load(int3(fullTexel, 0)).xyz;
        const int2 rightFull = int2(min(fullTexel.x + 1, fw - 1), fullTexel.y);
        const int2 downFull = int2(fullTexel.x, min(fullTexel.y + 1, fh - 1));
        const float3 dxWorld = NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.worldPositionSlot).Load(int3(rightFull, 0)).xyz - centerWorld;
        const float3 dyWorld = NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.worldPositionSlot).Load(int3(downFull, 0)).xyz - centerWorld;
        const float spacing = max(max(length(dxWorld), length(dyWorld)), 1e-4);
        const float sigmaWorld = spacing * 3.0;
        const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

        const int hw = (int)g_NwbCausticResolvePushConstants.halfWidth;
        const int hh = (int)g_NwbCausticResolvePushConstants.halfHeight;
        // Center-aligned full-to-half texel mapping; changing it shifts the upsample.
        const float2 src = (float2(pixel) + 0.5) * 0.5 - 0.5;
        const int2 baseTexel = int2(floor(src));
        const float2 lerpFrac = src - float2(baseTexel);

        float3 sum = float3(0.0, 0.0, 0.0);
        float weightSum = 0.0;
        [unroll] for(int j = 0; j < 2; ++j){
            [unroll] for(int i = 0; i < 2; ++i){
                const int2 tap = int2(clamp(baseTexel.x + i, 0, hw - 1), clamp(baseTexel.y + j, 0, hh - 1));
                if(!nwbCausticResolveValidAtHalf(tap))
                    continue;
                const float bilinearWeight = (i == 0 ? (1.0 - lerpFrac.x) : lerpFrac.x) * (j == 0 ? (1.0 - lerpFrac.y) : lerpFrac.y);
                const float3 d = float3(nwbCausticResolveWorldAtHalf(tap)) - centerWorld;
                const float weight = bilinearWeight * exp(-dot(d, d) * invTwoSigmaWorldSq);
                const half3 tapColor = half3(NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.inputColorSlot).Load(int3(tap, 0)).rgb);
                sum += weight * float3(tapColor);
                weightSum += weight;
            }
        }
        g_NwbCausticResolveOutput[pixel] = (weightSum > 0.0) ? float4(sum / weightSum, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const uint2 pixel = dispatchThreadID.xy;
    const int hw = (int)g_NwbCausticResolvePushConstants.halfWidth;
    const int hh = (int)g_NwbCausticResolvePushConstants.halfHeight;
    const int2 texel = int2(pixel);
    const bool inBounds = (pixel.x < g_NwbCausticResolvePushConstants.halfWidth && pixel.y < g_NwbCausticResolvePushConstants.halfHeight);

    // Sum 2x2 flux and divide by the corresponding half-res receiver area to preserve average irradiance.
    if(stage == NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE){
        if(inBounds){
            if(!nwbCausticResolveValidAtHalf(texel)){
                g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
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
                            (float)NwbHeapSampledImage2DArrayUint(g_NwbCausticResolvePushConstants.accumulatorSlot).Load(int4(fpx, NWB_CAUSTIC_ACCUMULATOR_CHANNEL_RED, 0)).r,
                            (float)NwbHeapSampledImage2DArrayUint(g_NwbCausticResolvePushConstants.accumulatorSlot).Load(int4(fpx, NWB_CAUSTIC_ACCUMULATOR_CHANNEL_GREEN, 0)).r,
                            (float)NwbHeapSampledImage2DArrayUint(g_NwbCausticResolvePushConstants.accumulatorSlot).Load(int4(fpx, NWB_CAUSTIC_ACCUMULATOR_CHANNEL_BLUE, 0)).r
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

    if(stage != NWB_CAUSTIC_RESOLVE_STAGE_WAVELET)
        return;

    const int step = (int)max(g_NwbCausticResolvePushConstants.stepWidth, 1u);

    // All threads must complete the cooperative load and barriers before any bounds-dependent return.
    if(step <= NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP){
        const int tileW = NWB_CAUSTIC_RESOLVE_GROUP_SIZE + 4 * step;
        const int2 tileOrigin = int2(groupID.xy) * NWB_CAUSTIC_RESOLVE_GROUP_SIZE - int2(2 * step, 2 * step);
        const uint threadCount = NWB_CAUSTIC_RESOLVE_GROUP_SIZE * NWB_CAUSTIC_RESOLVE_GROUP_SIZE;
        const uint threadIndex = groupThreadID.y * NWB_CAUSTIC_RESOLVE_GROUP_SIZE + groupThreadID.x;
        const uint tileTexels = (uint)(tileW * tileW);
        if(threadIndex == 0u)
            s_NwbCausticResolveTileActive = 0u;
        GroupMemoryBarrierWithGroupSync();

        for(uint i = threadIndex; i < tileTexels; i += threadCount){
            const int2 t = tileOrigin + int2((int)(i % (uint)tileW), (int)(i / (uint)tileW));
            half3 tapColor = half3(0.0h, 0.0h, 0.0h);
            half3 tapWorld = half3(0.0h, 0.0h, 0.0h);
            uint tapValid = 0u;
            if(t.x >= 0 && t.y >= 0 && t.x < hw && t.y < hh){
                if(nwbCausticResolveValidAtHalf(t)){
                    tapColor = half3(NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.inputColorSlot).Load(int3(t, 0)).rgb);
                    tapWorld = nwbCausticResolveWorldAtHalf(t);
                    tapValid = 1u;
                    if(any(tapColor > half3(0.0h, 0.0h, 0.0h)))
                        InterlockedOr(s_NwbCausticResolveTileActive, 1u);
                }
            }
            s_NwbCausticResolveTileColor[i] = tapColor;
            s_NwbCausticResolveTileWorld[i] = tapWorld;
            s_NwbCausticResolveTileValid[i] = tapValid;
        }
        GroupMemoryBarrierWithGroupSync();

        // Skip all-zero tiles.
        if(s_NwbCausticResolveTileActive == 0u){
            if(inBounds)
                g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        if(!inBounds)
            return;
        if(!nwbCausticResolveValidAtHalf(texel)){
            g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        const float3 centerWorld = float3(nwbCausticResolveWorldAtHalf(texel));
        const int2 rightTexel = int2(min(texel.x + 1, hw - 1), texel.y);
        const float pixelSpacing = max(length(float3(nwbCausticResolveWorldAtHalf(rightTexel)) - centerWorld), 1e-4);
        const float sigmaWorld = pixelSpacing * (float)step * 2.0;
        const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

        float3 sum = float3(0.0, 0.0, 0.0);
        float weightSum = 0.0;
        [unroll] for(int ky = -2; ky <= 2; ++ky){
            [unroll] for(int kx = -2; kx <= 2; ++kx){
                const uint idx = (uint)((int(groupThreadID.y) + (ky + 2) * step) * tileW + (int(groupThreadID.x) + (kx + 2) * step));
                if(s_NwbCausticResolveTileValid[idx] == 0u)
                    continue;
                const float3 d = float3(s_NwbCausticResolveTileWorld[idx]) - centerWorld;
                const float weight = s_NwbCausticResolveKernel[kx + 2] * s_NwbCausticResolveKernel[ky + 2] * exp(-dot(d, d) * invTwoSigmaWorldSq);
                sum += weight * float3(s_NwbCausticResolveTileColor[idx]);
                weightSum += weight;
            }
        }
        g_NwbCausticResolveOutput[pixel] = (weightSum > 0.0) ? float4(sum / weightSum, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Large dilations access textures directly.
    if(!inBounds)
        return;
    if(!nwbCausticResolveValidAtHalf(texel)){
        g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float3 centerWorld = float3(nwbCausticResolveWorldAtHalf(texel));
    const int2 rightTexel = int2(min(texel.x + 1, hw - 1), texel.y);
    const float pixelSpacing = max(length(float3(nwbCausticResolveWorldAtHalf(rightTexel)) - centerWorld), 1e-4);
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
                continue;
            const float3 d = float3(nwbCausticResolveWorldAtHalf(tap)) - centerWorld;
            const float weight = s_NwbCausticResolveKernel[kx + 2] * s_NwbCausticResolveKernel[ky + 2] * exp(-dot(d, d) * invTwoSigmaWorldSq);
            const half3 tapColor = half3(NwbHeapSampledImage2D(g_NwbCausticResolvePushConstants.inputColorSlot).Load(int3(tap, 0)).rgb);
            sum += weight * float3(tapColor);
            weightSum += weight;
        }
    }

    g_NwbCausticResolveOutput[pixel] = (weightSum > 0.0) ? float4(sum / weightSum, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}


#undef g_NwbCausticResolveOutput


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

