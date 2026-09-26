// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "shadow_resolve_filter_constants.slangi"
#include "constants.h"
#include "sw_binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compile each stage separately so the full-resolution upsample does not reserve the wavelet shared-memory tile.
#ifndef NWB_SHADOW_RESOLVE_COMPILED_STAGE
#error NWB_SHADOW_RESOLVE_COMPILED_STAGE must select a resolve stage.
#endif
#if NWB_SHADOW_RESOLVE_COMPILED_STAGE != NWB_SHADOW_RESOLVE_STAGE_WAVELET \
    && NWB_SHADOW_RESOLVE_COMPILED_STAGE != NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE
#error NWB_SHADOW_RESOLVE_COMPILED_STAGE must select the wavelet or upsample stage.
#endif

// Scalar opaque, RGB transparent, and combined upsample variants share the same filtering equations.
#ifndef NWB_SHADOW_RESOLVE_CHANNELS
#define NWB_SHADOW_RESOLVE_CHANNELS NWB_SHADOW_RESOLVE_CHANNELS_SCALAR
#endif

#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_COMBINED
    #if NWB_SHADOW_RESOLVE_COMPILED_STAGE != NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE
        #error Combined visibility is an upsample-only stage.
    #endif
    typedef half4 NwbShadowResolveVisVec;
    typedef float4 NwbShadowResolveVisFVec;
#elif NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    typedef half   NwbShadowResolveVis;
    typedef half3  NwbShadowResolveVisVec;
    typedef float3 NwbShadowResolveVisFVec;
#else
    typedef half   NwbShadowResolveVis;
    typedef half   NwbShadowResolveVisVec;
    typedef float  NwbShadowResolveVisFVec;
#endif


// Half-resolution edge-aware a-trous soft-shadow resolve.
// Visibility uses 1.0 as its lit/multiplicative identity; invalid taps must never write 0.

// Typed global-heap view for the camera position.
struct NwbShadowResolveSceneShading{
    float4 cameraPosition;
};
#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed UniformBuffer heap view.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbShadowResolveSceneShading, Std140DataLayout> g_NwbHeapShadowResolveSceneShadings[];
#pragma warning(pop)

struct NwbShadowResolvePushConstants{
    // Must match ShadowResolvePushConstants in C++; resource slots select global heap views.
    uint width;
    uint height;
    uint halfWidth;
    uint halfHeight;
    uint stepWidth;
    uint stage;
    uint lightSlotStart;
    uint lightSlotCount;
    uint momentsValid; // May sample temporal moments only when nonzero.
    uint upsampleFold; // 0 overwrites opaque visibility; 1 multiplies transparent visibility and leaves background unchanged.
    uint geometrySlot;
    uint depthSlot;
    uint worldPositionSlot;
    uint normalSlot;
    uint softHalfSlot;
    uint inputColorSlot;
    uint momentsSlot;
    uint outputStorageSlot;
    uint visibilityStorageSlot;
    uint sceneShadingSlot;
    uint opaqueInputColorSlot; // Combined upsample reads the opaque half-resolution input from this slot.
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowResolvePushConstants> g_NwbShadowResolvePushConstants;

#define g_NwbShadowResolveOutput NwbHeapStorageImage2DArrayFloat4(g_NwbShadowResolvePushConstants.outputStorageSlot)
#define g_NwbShadowResolveVisibility NwbHeapStorageImage2DArrayFloat4(g_NwbShadowResolvePushConstants.visibilityStorageSlot)
#define g_NwbShadowResolveSceneShading g_NwbHeapShadowResolveSceneShadings[g_NwbShadowResolvePushConstants.sceneShadingSlot]

#if NWB_SHADOW_RESOLVE_COMPILED_STAGE == NWB_SHADOW_RESOLVE_STAGE_WAVELET
// Groupshared tile for small-dilation wavelet passes.
groupshared NwbShadowResolveVisVec s_NwbShadowResolveTileVis[NWB_SHADOW_RESOLVE_TILE_TEXELS];
groupshared half2  s_NwbShadowResolveTileNormal[NWB_SHADOW_RESOLVE_TILE_TEXELS];
// The half-resolution geometry cache is RGBA16F. Retain its scalar camera distance as half in LDS, then widen only
// for the Gaussian distance term so this tile does not spend twice the shared memory on already-quantized data.
groupshared half   s_NwbShadowResolveTileDist[NWB_SHADOW_RESOLVE_TILE_TEXELS];
groupshared uint   s_NwbShadowResolveTileValid[NWB_SHADOW_RESOLVE_TILE_TEXELS];
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half4 nwbShadowResolveGeomAtHalf(const int2 halfTexel){
    return half4(NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.geometrySlot).Load(int3(halfTexel, 0)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Guided upsample parameters.
#define NWB_SHADOW_RESOLVE_GUIDED_RADIUS 1
#define NWB_SHADOW_RESOLVE_GUIDED_EPS_FRAC 0.02
#define NWB_SHADOW_RESOLVE_GUIDED_NORMAL_GATE 0.5
#define NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE (2 * NWB_SHADOW_RESOLVE_GUIDED_RADIUS + 1)
#define NWB_SHADOW_RESOLVE_GUIDED_TAP_COUNT (NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE)

// Quadratic B-spline weights preserve affine signals and vanish as taps leave the rounded three-sample support.
// The offset is src-round(src) in [-0.5, 0.5], so every weight lands in [0, 0.75]; half math is sufficient here.
half3 nwbShadowResolveSpatialWeights(const half offset){
    const half2 side = half2(half(0.5h) - offset, half(0.5h) + offset);
    return half3(half(0.5h) * side.x * side.x, half(0.75h) - offset * offset, half(0.5h) * side.y * side.y);
}

half nwbShadowResolveNormalWeightVec(const float3 centerNormal, const half2 tapOct){
    // The normal-agreement weight is a (0, 1) bounded filter factor, so half arithmetic is sufficient here.
    const float3 tn = nwbOctDecodeNormal(float2(tapOct));
    return half(pow(max(dot(centerNormal, tn), 0.0), NWB_SHADOW_RESOLVE_NORMAL_POWER));
}

// Use temporal variance only after enough history samples.
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    #define NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE NWB_SHADOW_RESOLVE_RGB_SIGMA_SCALE
#else
    #define NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE NWB_SHADOW_RESOLVE_SCALAR_SIGMA_SCALE
#endif
#define NWB_SHADOW_RESOLVE_SIGMA_LUM (NWB_SHADOW_RESOLVE_SIGMA_BASE * NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE)
#define NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS (NWB_SHADOW_RESOLVE_SIGMA_EPS_BASE * NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE)

float nwbShadowResolveLuma(const float v){
    return v;
}
half nwbShadowResolveLuma(const half v){
    return v;
}
float nwbShadowResolveLuma(const float3 c){
    return nwbLuminance(c);
}
half nwbShadowResolveLuma(const half3 c){
    return nwbLuminance(c);
}

#if NWB_SHADOW_RESOLVE_CHANNELS != NWB_SHADOW_RESOLVE_CHANNELS_COMBINED
half nwbShadowResolveValueWeightVec(const NwbShadowResolveVisFVec centerVis, const NwbShadowResolveVisFVec tapVis, const float sigmaLum){
    // The value-similarity weight is a (0, 1) bounded filter factor: run the luma delta in half, matching RGBA16F storage.
    return half(exp(-abs(half(nwbShadowResolveLuma(tapVis)) - half(nwbShadowResolveLuma(centerVis))) / half(sigmaLum)));
}

#endif

NwbShadowResolveVisVec nwbShadowResolveLoadInput(const int2 tap, const uint slot){
    const half4 s = half4(NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.inputColorSlot).Load(int4(tap, (int)slot, 0)));
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_COMBINED
    const half opaque = half(NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.opaqueInputColorSlot).Load(int4(tap, (int)slot, 0)).r);
    return half4(opaque, s.rgb);
#elif NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    return NwbShadowResolveVisVec(s.rgb);
#else
    return NwbShadowResolveVisVec(s.r);
#endif
}

float4 nwbShadowResolveStore(const NwbShadowResolveVisFVec v){
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_COMBINED
    // Retain opaque half precision before multiplication; image-store rounding may select an adjacent half value.
    return float4(float(half(v.x)) * v.yzw, 1.0);
#elif NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    return float4(v, 1.0);
#else
    return float4(v, v, v, 1.0);
#endif
}

// Use temporal moments when valid; otherwise estimate spatial variance.
float nwbShadowResolveCenterSigmaLum(const int2 texel, const uint slot, const int hw, const int hh, const uint momentsValid){
    if(momentsValid != 0u){
        const half3 momentSample = half3(NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.momentsSlot).Load(int4(texel, (int)slot, 0)).xyz);
        const float3 mom = float3(momentSample);
        const float n = mom.z;
        if(n >= NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY){
            const float varTemporal = max(mom.y - mom.x * mom.x, 0.0);
            return NWB_SHADOW_RESOLVE_SIGMA_LUM * sqrt(varTemporal) + NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
        }
    }
    // Spatial visibility moments stay in (0, 1), so half accumulation is sufficient here.
    half mean = half(0.0h);
    half mean2 = half(0.0h);
    int cnt = 0;
    [unroll] for(int dy = -1; dy <= 1; ++dy){
        [unroll] for(int dx = -1; dx <= 1; ++dx){
            const int2 t = int2(clamp(texel.x + dx, 0, hw - 1), clamp(texel.y + dy, 0, hh - 1));
            if(nwbShadowResolveGeomAtHalf(t).w <= half(0.5h))
                continue;
            const half4 sColor = half4(NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.inputColorSlot).Load(int4(t, (int)slot, 0)));
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
            const half v = nwbShadowResolveLuma(sColor.rgb);
#else
            const half v = sColor.r;
#endif
            mean += v;
            mean2 += v * v;
            ++cnt;
        }
    }
    if(cnt <= 0)
        return NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
    const float invCount = 1.0 / (float)cnt;
    // Visibility moments stay in (0, 1), so compute the spatial variance in half; FP16 has headroom here.
    const half varSpatial = max(half(mean2) * half(invCount) - (half(mean) * half(invCount)) * (half(mean) * half(invCount)), half(0.0h));
    return NWB_SHADOW_RESOLVE_SIGMA_LUM * sqrt(float(varSpatial)) + NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
}


[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint stage = NWB_SHADOW_RESOLVE_COMPILED_STAGE;
    const uint slotStart = g_NwbShadowResolvePushConstants.lightSlotStart;
    const uint slotEnd = min(slotStart + g_NwbShadowResolvePushConstants.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);

    // Guided edge-aware upsample.
    if(stage == NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE){
        const uint2 pixel = dispatchThreadID.xy;
        if(pixel.x >= g_NwbShadowResolvePushConstants.width || pixel.y >= g_NwbShadowResolvePushConstants.height)
            return;

        const int2 fullTexel = int2(pixel);
        const bool background = NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.depthSlot).Load(int3(fullTexel, 0)).r >= NWB_SCENE_BACKGROUND_DEPTH;

#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
        // The caller barriers the opaque upsample before transparent read-modify-write.
        const uint fold = g_NwbShadowResolvePushConstants.upsampleFold;
#endif
        // Background pixels have a fixed lit identity. Exit before loading G-buffer guidance that cannot affect it.
        if(background){
            for(uint slot = slotStart; slot < slotEnd; ++slot){
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
                if(fold == 0u)
#endif
                    g_NwbShadowResolveVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
            }
            return;
        }

        const int hw = (int)g_NwbShadowResolvePushConstants.halfWidth;
        const int hh = (int)g_NwbShadowResolvePushConstants.halfHeight;
        // Trace and geometry samples lie at full pixel index factor*h, rather than the center of each block.
        const float2 src = float2(pixel) / float(NWB_SW_SHADOW_SOFT_FACTOR);
        const int2 baseTexel = int2(round(src));
        const float2 offset = src - float2(baseTexel);
        const half3 weightsX = nwbShadowResolveSpatialWeights(half(offset.x));
        const half3 weightsY = nwbShadowResolveSpatialWeights(half(offset.y));

        const half3 centerWorldSample = half3(NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.worldPositionSlot).Load(int3(fullTexel, 0)).xyz);
        const half3 centerNormalSample = half3(NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.normalSlot).Load(int3(fullTexel, 0)).xyz);
        const float3 centerWorld = float3(centerWorldSample);
        const float3 centerNormal = normalize(float3(centerNormalSample) * 2.0 - 1.0);
        const float centerDist = length(centerWorld - g_NwbShadowResolveSceneShading.cameraPosition.xyz);
        const float guidedEps = NWB_SHADOW_RESOLVE_GUIDED_EPS_FRAC * centerDist;
        const float guidedEpsSq = guidedEps * guidedEps;

        // Geometry and regression guidance are identical for every light layer at this pixel. Build the compact 3x3
        // classification once, then retain the original tap order while each layer loads only its own visibility values.
        uint validTapMask = 0u;
        uint guidedTapMask = 0u;
        float tapCoefficients[NWB_SHADOW_RESOLVE_GUIDED_TAP_COUNT];
        float sumG = 0.0;
        float sumG2 = 0.0;
        float guidedWeightSum = 0.0;
        float validWeightSum = 0.0;
        [unroll] for(int dy = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dy <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dy){
            [unroll] for(int dx = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dx <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dx){
                const uint tapIndex = (uint)((dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS) * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE + dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS);
                const uint tapBit = 1u << tapIndex;
                // Widen the (0, 1) bounded half product at the float regression-accumulator boundary.
                const float spatialWeight = float(weightsX[dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS]
                    * weightsY[dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS]);
                if(spatialWeight <= 0.0)
                    continue;
                const int2 tap = int2(clamp(baseTexel.x + dx, 0, hw - 1), clamp(baseTexel.y + dy, 0, hh - 1));
                const half4 tapGeom = nwbShadowResolveGeomAtHalf(tap);
                if(tapGeom.w <= half(0.5h))
                    continue;
                validTapMask |= tapBit;
                validWeightSum += spatialWeight;
                if(dot(centerNormal, nwbOctDecodeNormal(float2(tapGeom.xy))) < NWB_SHADOW_RESOLVE_GUIDED_NORMAL_GATE)
                    continue;
                const float g = float(tapGeom.z) - centerDist;
                guidedTapMask |= tapBit;
                tapCoefficients[tapIndex] = g;
                sumG += spatialWeight * g;
                sumG2 += spatialWeight * g * g;
                guidedWeightSum += spatialWeight;
            }
        }
        const bool hasGuidedTaps = guidedWeightSum > 0.0;
        const uint tapMask = hasGuidedTaps ? guidedTapMask : validTapMask;
        const float weightSum = hasGuidedTaps ? guidedWeightSum : validWeightSum;
        const float invWeightSum = weightSum > 0.0 ? (1.0 / weightSum) : 0.0;
        const float guidedMeanG = sumG * invWeightSum;
        const float guidedVarG = max(sumG2 * invWeightSum - guidedMeanG * guidedMeanG, 0.0);
        const float regressionScale = hasGuidedTaps ? guidedMeanG / (guidedVarG + guidedEpsSq) : 0.0;
        // mean(P) - mean(G) * cov(G,P) / (var(G) + epsilon) is linear in each visibility tap.
        // Fold its scalar coefficients once, preserving the geometry gates and support for every light layer.
        [unroll] for(int dy = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dy <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dy){
            [unroll] for(int dx = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dx <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dx){
                const uint tapIndex = (uint)((dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS) * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE + dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS);
                if((tapMask & (1u << tapIndex)) == 0u)
                    continue;
                const float spatialWeight = float(weightsX[dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS]
                    * weightsY[dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS]);
                const float regressionWeight = hasGuidedTaps ? 1.0 - regressionScale * (tapCoefficients[tapIndex] - guidedMeanG) : 1.0;
                tapCoefficients[tapIndex] = spatialWeight * invWeightSum * regressionWeight;
            }
        }

        for(uint slot = slotStart; slot < slotEnd; ++slot){
            NwbShadowResolveVisFVec result = (NwbShadowResolveVisFVec)1.0;
            if(tapMask != 0u){
                const uint anchorTap = firstbitlow(tapMask);
                const int2 anchorOffset = int2(int(anchorTap % NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE),
                    int(anchorTap / NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE)) - int2(NWB_SHADOW_RESOLVE_GUIDED_RADIUS);
                const int2 anchorTexel = clamp(baseTexel + anchorOffset, int2(0), int2(hw - 1, hh - 1));
                const NwbShadowResolveVisFVec anchor = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(anchorTexel, slot);
                // The coefficient sum is one analytically. Anchor the affine sum so constant visibility stays exact.
                result = anchor;
                [unroll] for(int dy = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dy <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dy){
                    [unroll] for(int dx = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dx <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dx){
                        const uint tapIndex = (uint)((dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS) * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE + dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS);
                        if(tapIndex == anchorTap || (tapMask & (1u << tapIndex)) == 0u)
                            continue;
                        const int2 tap = int2(clamp(baseTexel.x + dx, 0, hw - 1), clamp(baseTexel.y + dy, 0, hh - 1));
                        const NwbShadowResolveVisFVec value = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(tap, slot);
                        result += tapCoefficients[tapIndex] * (value - anchor);
                    }
                }
                if(hasGuidedTaps)
                    result = clamp(result, (NwbShadowResolveVisFVec)0.0, (NwbShadowResolveVisFVec)1.0);
            }
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
            if(fold == 0u){
                g_NwbShadowResolveVisibility[uint3(pixel, slot)] = nwbShadowResolveStore(result);
            }
            else{
                const half3 prior = half3(g_NwbShadowResolveVisibility[uint3(pixel, slot)].rgb);
                g_NwbShadowResolveVisibility[uint3(pixel, slot)] = float4(float3(prior) * result, 1.0);
            }
#else
            g_NwbShadowResolveVisibility[uint3(pixel, slot)] = nwbShadowResolveStore(result);
#endif
        }
        return;
    }

#if NWB_SHADOW_RESOLVE_COMPILED_STAGE == NWB_SHADOW_RESOLVE_STAGE_WAVELET
    const uint2 pixel = dispatchThreadID.xy;
    const int hw = (int)g_NwbShadowResolvePushConstants.halfWidth;
    const int hh = (int)g_NwbShadowResolvePushConstants.halfHeight;
    const int2 texel = int2(pixel);
    const bool inBounds = (pixel.x < g_NwbShadowResolvePushConstants.halfWidth && pixel.y < g_NwbShadowResolvePushConstants.halfHeight);

    const int step = (int)max(g_NwbShadowResolvePushConstants.stepWidth, 1u);

    const half4 centerGeom = inBounds ? nwbShadowResolveGeomAtHalf(texel) : half4(0.0h, 0.0h, 0.0h, 0.0h);
    const float centerGeomDistance = float(centerGeom.z);
    const float sigmaDist = max(centerGeomDistance * NWB_SHADOW_RESOLVE_WAVELET_DIST_FRAC * (float)step, 1e-3);
    const float invTwoSigmaDistSq = 1.0 / (2.0 * sigmaDist * sigmaDist);
    const float3 centerNormalWavelet = nwbOctDecodeNormal(float2(centerGeom.xy));

    const uint momentsValid = g_NwbShadowResolvePushConstants.momentsValid;

    // Every thread must reach the LDS barriers before the next slot reload.
    if(step <= NWB_SHADOW_RESOLVE_LDS_MAX_STEP){
        const int tileW = NWB_SHADOW_RESOLVE_GROUP_SIZE + 4 * step;
        const int2 tileOrigin = int2(groupID.xy) * NWB_SHADOW_RESOLVE_GROUP_SIZE - int2(2 * step, 2 * step);
        const uint threadCount = NWB_SHADOW_RESOLVE_GROUP_SIZE * NWB_SHADOW_RESOLVE_GROUP_SIZE;
        const uint threadIndex = groupThreadID.y * NWB_SHADOW_RESOLVE_GROUP_SIZE + groupThreadID.x;
        const uint tileTexels = (uint)(tileW * tileW);

        for(uint slot = slotStart; slot < slotEnd; ++slot){
            for(uint i = threadIndex; i < tileTexels; i += threadCount){
                const int2 t = tileOrigin + int2((int)(i % (uint)tileW), (int)(i / (uint)tileW));
                NwbShadowResolveVisVec tapVis = NwbShadowResolveVisVec(1.0);
                half2 tapNormal = half2(0.0h, 0.0h);
                half tapDist = half(0.0h);
                uint tapValid = 0u;
                if(t.x >= 0 && t.y >= 0 && t.x < hw && t.y < hh){
                    const half4 g = nwbShadowResolveGeomAtHalf(t);
                    if(g.w > half(0.5h)){
                        tapVis = NwbShadowResolveVisVec(nwbShadowResolveLoadInput(t, slot));
                        tapNormal = half2(g.xy);
                        tapDist = g.z;
                        tapValid = 1u;
                    }
                }
                s_NwbShadowResolveTileVis[i] = tapVis;
                s_NwbShadowResolveTileNormal[i] = tapNormal;
                s_NwbShadowResolveTileDist[i] = tapDist;
                s_NwbShadowResolveTileValid[i] = tapValid;
            }
            GroupMemoryBarrierWithGroupSync();

            if(inBounds){
                if(centerGeom.w <= half(0.5h)){
                    g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
                }
                else{
                    const uint centerIdx = (uint)((int(groupThreadID.y) + 2 * step) * tileW + (int(groupThreadID.x) + 2 * step));
                    const NwbShadowResolveVisFVec centerVis = (NwbShadowResolveVisFVec)s_NwbShadowResolveTileVis[centerIdx];
                    const float sigmaLum = nwbShadowResolveCenterSigmaLum(texel, slot, hw, hh, momentsValid);
                    NwbShadowResolveVisFVec sum = (NwbShadowResolveVisFVec)0.0;
                    // The bilateral weight sum is a bounded filter factor, so half arithmetic is sufficient here.
                    half weightSum = half(0.0h);
                    [unroll] for(int ky = -2; ky <= 2; ++ky){
                        [unroll] for(int kx = -2; kx <= 2; ++kx){
                            const uint idx = (uint)((int(groupThreadID.y) + (ky + 2) * step) * tileW + (int(groupThreadID.x) + (kx + 2) * step));
                            if(s_NwbShadowResolveTileValid[idx] == 0u)
                                continue;
                            const NwbShadowResolveVisFVec tapVis = (NwbShadowResolveVisFVec)s_NwbShadowResolveTileVis[idx];
                            const float dDist = float(s_NwbShadowResolveTileDist[idx]) - centerGeomDistance;
                            // Every filter weight is a (0, 1) bounded factor, so half arithmetic is sufficient here.
                            const half distWeight = half(exp(-dDist * dDist * invTwoSigmaDistSq));
                            const half normalWeight = nwbShadowResolveNormalWeightVec(centerNormalWavelet, s_NwbShadowResolveTileNormal[idx]);
                            const half valueWeight = nwbShadowResolveValueWeightVec(centerVis, tapVis, sigmaLum);
                            const half weight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distWeight * normalWeight * valueWeight;
                            sum += weight * tapVis;
                            weightSum += weight;
                        }
                    }
                    const NwbShadowResolveVisFVec vis = (weightSum > half(0.0h)) ? (sum / weightSum) : (NwbShadowResolveVisFVec)1.0;
                    g_NwbShadowResolveOutput[uint3(pixel, slot)] = nwbShadowResolveStore(vis);
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
        return;
    }

    // Large dilations access textures directly.
    if(!inBounds)
        return;
    for(uint slot = slotStart; slot < slotEnd; ++slot){
        if(centerGeom.w <= half(0.5h)){
            g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
            continue;
        }
        const NwbShadowResolveVisFVec centerVis = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(texel, slot);
        const float sigmaLum = nwbShadowResolveCenterSigmaLum(texel, slot, hw, hh, momentsValid);
        NwbShadowResolveVisFVec sum = (NwbShadowResolveVisFVec)0.0;
        // The bilateral weight sum is a bounded filter factor, so half arithmetic is sufficient here.
        half weightSum = half(0.0h);
        [unroll] for(int ky = -2; ky <= 2; ++ky){
            [unroll] for(int kx = -2; kx <= 2; ++kx){
                const int2 tap = int2(texel.x + kx * step, texel.y + ky * step);
                if(tap.x < 0 || tap.y < 0 || tap.x >= hw || tap.y >= hh)
                    continue;
                const half4 tapGeom = nwbShadowResolveGeomAtHalf(tap);
                if(tapGeom.w <= half(0.5h))
                    continue;
                const NwbShadowResolveVisFVec tapVis = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(tap, slot);
                const float dDist = float(tapGeom.z) - centerGeomDistance;
                // Every filter weight is a (0, 1) bounded factor, so half arithmetic is sufficient here.
                const half distWeight = half(exp(-dDist * dDist * invTwoSigmaDistSq));
                const half normalWeight = nwbShadowResolveNormalWeightVec(centerNormalWavelet, tapGeom.xy);
                const half valueWeight = nwbShadowResolveValueWeightVec(centerVis, tapVis, sigmaLum);
                const half weight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distWeight * normalWeight * valueWeight;
                sum += weight * tapVis;
                weightSum += weight;
            }
        }
        const NwbShadowResolveVisFVec vis = (weightSum > half(0.0h)) ? (sum / weightSum) : (NwbShadowResolveVisFVec)1.0;
        g_NwbShadowResolveOutput[uint3(pixel, slot)] = nwbShadowResolveStore(vis);
    }
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

