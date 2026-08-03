// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "constants.h"
#include "sw_binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scalar opaque and RGB transparent variants share this resolve.
#ifndef NWB_SHADOW_RESOLVE_CHANNELS
#define NWB_SHADOW_RESOLVE_CHANNELS NWB_SHADOW_RESOLVE_CHANNELS_SCALAR
#endif

#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
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
    uint _pad0;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowResolvePushConstants> g_NwbShadowResolvePushConstants;

#define g_NwbShadowResolveOutput NwbHeapStorageImage2DArrayFloat4(g_NwbShadowResolvePushConstants.outputStorageSlot)
#define g_NwbShadowResolveVisibility NwbHeapStorageImage2DArrayFloat4(g_NwbShadowResolvePushConstants.visibilityStorageSlot)
#define g_NwbShadowResolveSceneShading g_NwbHeapShadowResolveSceneShadings[g_NwbShadowResolvePushConstants.sceneShadingSlot]

// Groupshared tile for small-dilation wavelet passes.
groupshared NwbShadowResolveVisVec s_NwbShadowResolveTileVis[NWB_SHADOW_RESOLVE_TILE_TEXELS];
groupshared half2  s_NwbShadowResolveTileNormal[NWB_SHADOW_RESOLVE_TILE_TEXELS];
groupshared float  s_NwbShadowResolveTileDist[NWB_SHADOW_RESOLVE_TILE_TEXELS];
groupshared uint   s_NwbShadowResolveTileValid[NWB_SHADOW_RESOLVE_TILE_TEXELS];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float4 nwbShadowResolveGeomAtHalf(const int2 halfTexel){
    return NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.geometrySlot).Load(int3(halfTexel, 0));
}
bool nwbShadowResolveValidAtHalf(const int2 halfTexel){
    return NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.geometrySlot).Load(int3(halfTexel, 0)).w > 0.5;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// 5x5 B-spline a-trous kernel.
static const float s_NwbShadowResolveKernel[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };

#define NWB_SHADOW_RESOLVE_NORMAL_POWER 8.0

#define NWB_SHADOW_RESOLVE_UPSAMPLE_DIST_FRAC 0.02
#define NWB_SHADOW_RESOLVE_WAVELET_DIST_FRAC 0.02

// Guided upsample parameters.
#define NWB_SHADOW_RESOLVE_GUIDED_RADIUS 1
#define NWB_SHADOW_RESOLVE_GUIDED_EPS_FRAC 0.02
#define NWB_SHADOW_RESOLVE_GUIDED_NORMAL_GATE 0.5
#define NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE (2 * NWB_SHADOW_RESOLVE_GUIDED_RADIUS + 1)
#define NWB_SHADOW_RESOLVE_GUIDED_TAP_COUNT (NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE)

float nwbShadowResolveNormalWeightVec(const float3 centerNormal, const float2 tapOct){
    const float3 tn = nwbOctDecodeNormal(tapOct);
    return pow(max(dot(centerNormal, tn), 0.0), NWB_SHADOW_RESOLVE_NORMAL_POWER);
}

// Use temporal variance only after enough history samples.
#define NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY 4.0
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    #define NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE 0.20
#else
    #define NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE 1.0
#endif
#define NWB_SHADOW_RESOLVE_SIGMA_LUM (4.0 * NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE)
#define NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS (0.05 * NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE)

float nwbShadowResolveValueWeight(const float centerVis, const float tapVis, const float sigmaLum){
    return exp(-abs(tapVis - centerVis) / sigmaLum);
}

float nwbShadowResolveLuma(const float v){
    return v;
}
float nwbShadowResolveLuma(const float3 c){
    return nwbLuminance(c);
}

float nwbShadowResolveValueWeightVec(const NwbShadowResolveVisFVec centerVis, const NwbShadowResolveVisFVec tapVis, const float sigmaLum){
    return exp(-abs(nwbShadowResolveLuma(tapVis) - nwbShadowResolveLuma(centerVis)) / sigmaLum);
}

NwbShadowResolveVisVec nwbShadowResolveLoadInput(const int2 tap, const uint slot){
    const half4 s = half4(NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.inputColorSlot).Load(int4(tap, (int)slot, 0)));
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    return NwbShadowResolveVisVec(s.rgb);
#else
    return NwbShadowResolveVisVec(s.r);
#endif
}

float4 nwbShadowResolveStore(const NwbShadowResolveVisFVec v){
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    return float4(v, 1.0);
#else
    return float4(v, v, v, 1.0);
#endif
}

// Use temporal moments when valid; otherwise estimate spatial variance.
float nwbShadowResolveCenterSigmaLum(const int2 texel, const uint slot, const int hw, const int hh, const uint momentsValid){
    if(momentsValid != 0u){
        const float3 mom = NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.momentsSlot).Load(int4(texel, (int)slot, 0)).xyz;
        const float n = mom.z;
        if(n >= NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY){
            const float varTemporal = max(mom.y - mom.x * mom.x, 0.0);
            return NWB_SHADOW_RESOLVE_SIGMA_LUM * sqrt(varTemporal) + NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
        }
    }
    float mean = 0.0;
    float mean2 = 0.0;
    int cnt = 0;
    [unroll] for(int dy = -1; dy <= 1; ++dy){
        [unroll] for(int dx = -1; dx <= 1; ++dx){
            const int2 t = int2(clamp(texel.x + dx, 0, hw - 1), clamp(texel.y + dy, 0, hh - 1));
            if(NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.geometrySlot).Load(int3(t, 0)).w <= 0.5)
                continue;
            const float4 sColor = NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.inputColorSlot).Load(int4(t, (int)slot, 0));
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
            const float v = nwbShadowResolveLuma(sColor.rgb);
#else
            const float v = sColor.r;
#endif
            mean += v;
            mean2 += v * v;
            ++cnt;
        }
    }
    if(cnt <= 0)
        return NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
    const float invCount = 1.0 / (float)cnt;
    const float varSpatial = max(mean2 * invCount - (mean * invCount) * (mean * invCount), 0.0);
    return NWB_SHADOW_RESOLVE_SIGMA_LUM * sqrt(varSpatial) + NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
}


[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint stage = g_NwbShadowResolvePushConstants.stage;
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
        const float2 src = (float2(pixel) + 0.5) / float(NWB_SW_SHADOW_SOFT_FACTOR) - 0.5;
        const int2 baseTexel = int2(round(src));

        const float3 centerWorld = NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.worldPositionSlot).Load(int3(fullTexel, 0)).xyz;
        const float3 centerNormal = normalize(NwbHeapSampledImage2D(g_NwbShadowResolvePushConstants.normalSlot).Load(int3(fullTexel, 0)).xyz * 2.0 - 1.0);
        const float centerDist = length(centerWorld - g_NwbShadowResolveSceneShading.cameraPosition.xyz);
        const float guidedEps = NWB_SHADOW_RESOLVE_GUIDED_EPS_FRAC * centerDist;
        const float guidedEpsSq = guidedEps * guidedEps;

        // Geometry and regression guidance are identical for every light layer at this pixel. Build the compact 3x3
        // classification once, then retain the original tap order while each layer loads only its own visibility values.
        uint validTapMask = 0u;
        uint guidedTapMask = 0u;
        float guidedTapG[NWB_SHADOW_RESOLVE_GUIDED_TAP_COUNT];
        float sumG = 0.0;
        float sumG2 = 0.0;
        float guidedCount = 0.0;
        float anyCount = 0.0;
        [unroll] for(int dy = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dy <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dy){
            [unroll] for(int dx = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dx <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dx){
                const uint tapIndex = (uint)((dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS) * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE + dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS);
                const uint tapBit = 1u << tapIndex;
                const int2 tap = int2(clamp(baseTexel.x + dx, 0, hw - 1), clamp(baseTexel.y + dy, 0, hh - 1));
                const float4 tapGeom = nwbShadowResolveGeomAtHalf(tap);
                if(tapGeom.w <= 0.5)
                    continue;
                validTapMask |= tapBit;
                anyCount += 1.0;
                if(dot(centerNormal, nwbOctDecodeNormal(tapGeom.xy)) < NWB_SHADOW_RESOLVE_GUIDED_NORMAL_GATE)
                    continue;
                const float g = tapGeom.z - centerDist;
                guidedTapMask |= tapBit;
                guidedTapG[tapIndex] = g;
                sumG += g;
                sumG2 += g * g;
                guidedCount += 1.0;
            }
        }
        const bool hasGuidedTaps = guidedCount > 0.0;
        const float guidedInvCount = hasGuidedTaps ? (1.0 / guidedCount) : 0.0;
        const float guidedMeanG = sumG * guidedInvCount;
        const float guidedVarG = max(sumG2 * guidedInvCount - guidedMeanG * guidedMeanG, 0.0);

        for(uint slot = slotStart; slot < slotEnd; ++slot){
            NwbShadowResolveVisFVec result;
            if(hasGuidedTaps){
                // The unguided fallback is unreachable here. Avoid loading and accumulating its taps; only
                // guided samples contribute to the regression result.
                NwbShadowResolveVisFVec sumP = (NwbShadowResolveVisFVec)0.0;
                NwbShadowResolveVisFVec sumGP = (NwbShadowResolveVisFVec)0.0;
                [unroll] for(int dy = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dy <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dy){
                    [unroll] for(int dx = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dx <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dx){
                        const uint tapIndex = (uint)((dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS) * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE + dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS);
                        if((guidedTapMask & (1u << tapIndex)) == 0u)
                            continue;
                        const int2 tap = int2(clamp(baseTexel.x + dx, 0, hw - 1), clamp(baseTexel.y + dy, 0, hh - 1));
                        const NwbShadowResolveVisFVec tapVis = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(tap, slot);
                        const float g = guidedTapG[tapIndex];
                        sumP += tapVis;
                        sumGP += g * tapVis;
                    }
                }
                const NwbShadowResolveVisFVec meanP = sumP * guidedInvCount;
                const NwbShadowResolveVisFVec covGP = sumGP * guidedInvCount - guidedMeanG * meanP;
                const NwbShadowResolveVisFVec a = covGP / (guidedVarG + guidedEpsSq);
                result = clamp(meanP - a * guidedMeanG, (NwbShadowResolveVisFVec)0.0, (NwbShadowResolveVisFVec)1.0);
            }
            else{
                NwbShadowResolveVisFVec anySum = (NwbShadowResolveVisFVec)0.0;
                [unroll] for(int dy = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dy <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dy){
                    [unroll] for(int dx = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dx <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dx){
                        const uint tapIndex = (uint)((dy + NWB_SHADOW_RESOLVE_GUIDED_RADIUS) * NWB_SHADOW_RESOLVE_GUIDED_TAP_SIDE + dx + NWB_SHADOW_RESOLVE_GUIDED_RADIUS);
                        if((validTapMask & (1u << tapIndex)) == 0u)
                            continue;
                        const int2 tap = int2(clamp(baseTexel.x + dx, 0, hw - 1), clamp(baseTexel.y + dy, 0, hh - 1));
                        anySum += (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(tap, slot);
                    }
                }
                result = (anyCount > 0.0) ? (anySum / anyCount) : (NwbShadowResolveVisFVec)1.0;
            }
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
            if(fold == 0u){
                g_NwbShadowResolveVisibility[uint3(pixel, slot)] = nwbShadowResolveStore(result);
            }
            else{
                const float3 prior = g_NwbShadowResolveVisibility[uint3(pixel, slot)].rgb;
                g_NwbShadowResolveVisibility[uint3(pixel, slot)] = float4(prior * result, 1.0);
            }
#else
            g_NwbShadowResolveVisibility[uint3(pixel, slot)] = nwbShadowResolveStore(result);
#endif
        }
        return;
    }

    const uint2 pixel = dispatchThreadID.xy;
    const int hw = (int)g_NwbShadowResolvePushConstants.halfWidth;
    const int hh = (int)g_NwbShadowResolvePushConstants.halfHeight;
    const int2 texel = int2(pixel);
    const bool inBounds = (pixel.x < g_NwbShadowResolvePushConstants.halfWidth && pixel.y < g_NwbShadowResolvePushConstants.halfHeight);

    if(stage == NWB_SHADOW_RESOLVE_STAGE_PREPARE){
        if(inBounds){
            const bool valid = nwbShadowResolveValidAtHalf(texel);
            for(uint slot = slotStart; slot < slotEnd; ++slot){
                const half3 vis = valid
                    ? half3(NwbHeapSampledImage2DArray(g_NwbShadowResolvePushConstants.softHalfSlot).Load(int4(texel, slot, 0)).rgb)
                    : half3(1.0h, 1.0h, 1.0h)
                ;
                g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(float3(vis), 1.0);
            }
        }
        return;
    }

    if(stage != NWB_SHADOW_RESOLVE_STAGE_WAVELET)
        return;

    const int step = (int)max(g_NwbShadowResolvePushConstants.stepWidth, 1u);

    const float4 centerGeom = inBounds ? nwbShadowResolveGeomAtHalf(texel) : float4(0.0, 0.0, 0.0, 0.0);
    const float sigmaDist = max(centerGeom.z * NWB_SHADOW_RESOLVE_WAVELET_DIST_FRAC * (float)step, 1e-3);
    const float invTwoSigmaDistSq = 1.0 / (2.0 * sigmaDist * sigmaDist);
    const float3 centerNormalWavelet = nwbOctDecodeNormal(centerGeom.xy);

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
                float tapDist = 0.0;
                uint tapValid = 0u;
                if(t.x >= 0 && t.y >= 0 && t.x < hw && t.y < hh){
                    const float4 g = nwbShadowResolveGeomAtHalf(t);
                    if(g.w > 0.5){
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
                if(centerGeom.w <= 0.5){
                    g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
                }
                else{
                    const uint centerIdx = (uint)((int(groupThreadID.y) + 2 * step) * tileW + (int(groupThreadID.x) + 2 * step));
                    const NwbShadowResolveVisFVec centerVis = (NwbShadowResolveVisFVec)s_NwbShadowResolveTileVis[centerIdx];
                    const float sigmaLum = nwbShadowResolveCenterSigmaLum(texel, slot, hw, hh, momentsValid);
                    NwbShadowResolveVisFVec sum = (NwbShadowResolveVisFVec)0.0;
                    float weightSum = 0.0;
                    [unroll] for(int ky = -2; ky <= 2; ++ky){
                        [unroll] for(int kx = -2; kx <= 2; ++kx){
                            const uint idx = (uint)((int(groupThreadID.y) + (ky + 2) * step) * tileW + (int(groupThreadID.x) + (kx + 2) * step));
                            if(s_NwbShadowResolveTileValid[idx] == 0u)
                                continue;
                            const NwbShadowResolveVisFVec tapVis = (NwbShadowResolveVisFVec)s_NwbShadowResolveTileVis[idx];
                            const float dDist = s_NwbShadowResolveTileDist[idx] - centerGeom.z;
                            const float distWeight = exp(-dDist * dDist * invTwoSigmaDistSq);
                            const float normalWeight = nwbShadowResolveNormalWeightVec(centerNormalWavelet, float2(s_NwbShadowResolveTileNormal[idx]));
                            const float valueWeight = nwbShadowResolveValueWeightVec(centerVis, tapVis, sigmaLum);
                            const float weight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distWeight * normalWeight * valueWeight;
                            sum += weight * tapVis;
                            weightSum += weight;
                        }
                    }
                    const NwbShadowResolveVisFVec vis = (weightSum > 0.0) ? (sum / weightSum) : (NwbShadowResolveVisFVec)1.0;
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
        if(centerGeom.w <= 0.5){
            g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
            continue;
        }
        const NwbShadowResolveVisFVec centerVis = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(texel, slot);
        const float sigmaLum = nwbShadowResolveCenterSigmaLum(texel, slot, hw, hh, momentsValid);
        NwbShadowResolveVisFVec sum = (NwbShadowResolveVisFVec)0.0;
        float weightSum = 0.0;
        [unroll] for(int ky = -2; ky <= 2; ++ky){
            [unroll] for(int kx = -2; kx <= 2; ++kx){
                const int2 tap = int2(texel.x + kx * step, texel.y + ky * step);
                if(tap.x < 0 || tap.y < 0 || tap.x >= hw || tap.y >= hh)
                    continue;
                const float4 tapGeom = nwbShadowResolveGeomAtHalf(tap);
                if(tapGeom.w <= 0.5)
                    continue;
                const NwbShadowResolveVisFVec tapVis = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(tap, slot);
                const float dDist = tapGeom.z - centerGeom.z;
                const float distWeight = exp(-dDist * dDist * invTwoSigmaDistSq);
                const float normalWeight = nwbShadowResolveNormalWeightVec(centerNormalWavelet, tapGeom.xy);
                const float valueWeight = nwbShadowResolveValueWeightVec(centerVis, tapVis, sigmaLum);
                const float weight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distWeight * normalWeight * valueWeight;
                sum += weight * tapVis;
                weightSum += weight;
            }
        }
        const NwbShadowResolveVisFVec vis = (weightSum > 0.0) ? (sum / weightSum) : (NwbShadowResolveVisFVec)1.0;
        g_NwbShadowResolveOutput[uint3(pixel, slot)] = nwbShadowResolveStore(vis);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

