// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "shadow_resolve_filter_constants.slangi"
#include "shadow_heap_resources.slangi"
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCombinedShadowWaveletPushConstants{
    NwbShadowResolvePushConstants resolve;
    uint opaqueMomentsSlot;
    uint opaqueMomentsValid;
    uint opaqueOutputStorageSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbCombinedShadowWaveletPushConstants> g_NwbCombinedShadowWaveletPush;

static const int NWB_COMBINED_SHADOW_WAVELET_STEP = 1;
static const int NWB_COMBINED_SHADOW_WAVELET_TILE_SIDE = NWB_SHADOW_RESOLVE_GROUP_SIZE + 4;
static const uint NWB_COMBINED_SHADOW_WAVELET_TILE_TEXELS = NWB_COMBINED_SHADOW_WAVELET_TILE_SIDE * NWB_COMBINED_SHADOW_WAVELET_TILE_SIDE;

groupshared half4 g_NwbCombinedShadowWaveletVis[NWB_COMBINED_SHADOW_WAVELET_TILE_TEXELS];
groupshared half4 g_NwbCombinedShadowWaveletGeom[NWB_COMBINED_SHADOW_WAVELET_TILE_TEXELS];
groupshared uint g_NwbCombinedShadowWaveletNonLit;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half4 nwbCombinedShadowWaveletGeometry(const int2 texel){
    return half4(nwbHeapSampledImage2D(g_NwbCombinedShadowWaveletPush.resolve.geometrySlot).Load(int3(texel, 0)));
}

half4 nwbCombinedShadowWaveletVisibility(const int2 texel, const uint slot){
    const half opaque = half(nwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.resolve.opaqueInputColorSlot).Load(int4(texel, slot, 0)).r);
    const half3 rgb = half3(nwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.resolve.inputColorSlot).Load(int4(texel, slot, 0)).rgb);
    return half4(opaque, rgb);
}

float2 nwbCombinedShadowWaveletSigma(const int2 texel, const int2 tileOrigin, const int hw, const int hh, const uint slot){
    const float2 scale = float2(NWB_SHADOW_RESOLVE_SCALAR_SIGMA_SCALE, NWB_SHADOW_RESOLVE_RGB_SIGMA_SCALE);
    const float2 sigmaBase = NWB_SHADOW_RESOLVE_SIGMA_BASE * scale;
    const float2 sigmaEps = NWB_SHADOW_RESOLVE_SIGMA_EPS_BASE * scale;
    float2 sigma = sigmaEps;
    bool2 temporal = bool2(false, false);
    if(g_NwbCombinedShadowWaveletPush.opaqueMomentsValid != 0u){
        const half3 sample = half3(nwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.opaqueMomentsSlot).Load(int4(texel, slot, 0)).xyz);
        const float3 moment = float3(sample);
        if(moment.z >= NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY){
            const float variance = max(moment.y - moment.x * moment.x, 0.0);
            sigma.x = sigmaBase.x * sqrt(variance) + sigmaEps.x;
            temporal.x = true;
        }
    }
    if(g_NwbCombinedShadowWaveletPush.resolve.momentsValid != 0u){
        const half3 sample = half3(nwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.resolve.momentsSlot).Load(int4(texel, slot, 0)).xyz);
        const float3 moment = float3(sample);
        if(moment.z >= NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY){
            const float variance = max(moment.y - moment.x * moment.x, 0.0);
            sigma.y = sigmaBase.y * sqrt(variance) + sigmaEps.y;
            temporal.y = true;
        }
    }
    if(all(temporal))
        return sigma;

    half2 mean = half2(0.0h);
    half2 mean2 = half2(0.0h);
    int count = 0;
    const int2 maximum = int2(hw - 1, hh - 1);
    const int tileWidth = NWB_COMBINED_SHADOW_WAVELET_TILE_SIDE;
    [unroll] for(int dy = -1; dy <= 1; ++dy){
        [unroll] for(int dx = -1; dx <= 1; ++dx){
            const int2 tap = clamp(texel + int2(dx, dy), int2(0, 0), maximum);
            const int2 local = tap - tileOrigin;
            const uint tileIndex = (uint)(local.y * tileWidth + local.x);
            if(g_NwbCombinedShadowWaveletGeom[tileIndex].w < half(0.0h))
                continue;
            const half4 visibility = g_NwbCombinedShadowWaveletVis[tileIndex];
            const half2 value = half2(visibility.x, nwbLuminance(visibility.yzw));
            mean += value;
            mean2 += value * value;
            ++count;
        }
    }
    if(count > 0){
        const half invCount = half(1.0h) / half(count);
        const half2 variance = max(mean2 * invCount - (mean * invCount) * (mean * invCount), half2(0.0h));
        const float2 spatial = sigmaBase * sqrt(float2(variance)) + sigmaEps;
        if(!temporal.x)
            sigma.x = spatial.x;
        if(!temporal.y)
            sigma.y = spatial.y;
    }
    return sigma;
}

void nwbCombinedShadowWaveletStore(const uint2 pixel, const uint slot, const half opaque, const half3 rgb){
    nwbHeapStorageImage2DArrayFloat4(g_NwbCombinedShadowWaveletPush.opaqueOutputStorageSlot)[uint3(pixel, slot)] = float4(opaque, opaque, opaque, 1.0);
    nwbHeapStorageImage2DArrayFloat4(g_NwbCombinedShadowWaveletPush.resolve.outputStorageSlot)[uint3(pixel, slot)] = float4(rgb, 1.0);
}

[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint2 pixel = dispatchThreadID.xy;
    const int2 texel = int2(pixel);
    const int hw = (int)g_NwbCombinedShadowWaveletPush.resolve.halfWidth;
    const int hh = (int)g_NwbCombinedShadowWaveletPush.resolve.halfHeight;
    const bool inBounds = pixel.x < (uint)hw && pixel.y < (uint)hh;
    const int step = NWB_COMBINED_SHADOW_WAVELET_STEP;
    const int tileWidth = NWB_COMBINED_SHADOW_WAVELET_TILE_SIDE;
    const uint tileTexels = NWB_COMBINED_SHADOW_WAVELET_TILE_TEXELS;
    const int2 tileOrigin = int2(groupID.xy) * NWB_SHADOW_RESOLVE_GROUP_SIZE - int2(NWB_SHADOW_RESOLVE_KERNEL_RADIUS * step, NWB_SHADOW_RESOLVE_KERNEL_RADIUS * step);
    const uint lane = groupThreadID.y * NWB_SHADOW_RESOLVE_GROUP_SIZE + groupThreadID.x;
    const uint threadCount = NWB_SHADOW_RESOLVE_GROUP_SIZE * NWB_SHADOW_RESOLVE_GROUP_SIZE;
    const uint slotStart = g_NwbCombinedShadowWaveletPush.resolve.lightSlotStart;
    const uint slotEnd = min(slotStart + g_NwbCombinedShadowWaveletPush.resolve.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);
    const half4 centerGeom = inBounds ? nwbCombinedShadowWaveletGeometry(texel) : half4(0.0h);
    const bool centerValid = inBounds && centerGeom.w > half(NWB_SHADOW_GEOMETRY_VALID_THRESHOLD);
    const float centerDistance = float(centerGeom.z);
    const float sigmaDistance = max(centerDistance * NWB_SHADOW_RESOLVE_WAVELET_DIST_FRAC * (float)step, NWB_SHADOW_RESOLVE_SIGMA_DISTANCE_MIN);
    const float invTwoSigmaDistanceSq = 1.0 / (2.0 * sigmaDistance * sigmaDistance);
    const float3 centerNormal = centerValid ? nwbOctDecodeNormal(float2(centerGeom.xy)) : float3(0.0, 0.0, 1.0);

    for(uint index = lane; index < tileTexels; index += threadCount){
        const int2 tap = tileOrigin + int2((int)(index % (uint)tileWidth), (int)(index / (uint)tileWidth));
        half4 packedGeom = half4(0.0h, 0.0h, 0.0h, half(-1.0h));
        if(tap.x >= 0 && tap.y >= 0 && tap.x < hw && tap.y < hh){
            const half4 geometry = nwbCombinedShadowWaveletGeometry(tap);
            if(geometry.w > half(NWB_SHADOW_GEOMETRY_VALID_THRESHOLD))
                packedGeom = half4(half3(nwbOctDecodeNormal(float2(geometry.xy))), geometry.z);
        }
        g_NwbCombinedShadowWaveletGeom[index] = packedGeom;
    }

    const int kernelSide = NWB_SHADOW_RESOLVE_KERNEL_SIDE;
    const int kernelCount = kernelSide * kernelSide;
    bool geomPrepared = false;
    half geoWeights[kernelCount];
    [unroll] for(int kInit = 0; kInit < kernelCount; ++kInit)
        geoWeights[kInit] = half(0.0h);

    for(uint slot = slotStart; slot < slotEnd; ++slot){
        if(lane == 0u)
            g_NwbCombinedShadowWaveletNonLit = 0u;
        GroupMemoryBarrierWithGroupSync();
        bool nonLit = false;
        for(uint index = lane; index < tileTexels; index += threadCount){
            half4 visibility = half4(1.0h);
            if(g_NwbCombinedShadowWaveletGeom[index].w >= half(0.0h)){
                const int2 tap = tileOrigin + int2((int)(index % (uint)tileWidth), (int)(index / (uint)tileWidth));
                visibility = nwbCombinedShadowWaveletVisibility(tap, slot);
                nonLit = nonLit || any(visibility != half4(1.0h));
            }
            g_NwbCombinedShadowWaveletVis[index] = visibility;
        }
        if(nonLit)
            InterlockedOr(g_NwbCombinedShadowWaveletNonLit, 1u);
        GroupMemoryBarrierWithGroupSync();
        if(inBounds){
            if(!centerValid || g_NwbCombinedShadowWaveletNonLit == 0u)
                nwbCombinedShadowWaveletStore(pixel, slot, 1.0h, half3(1.0h));
            else{
                if(!geomPrepared){
                    [unroll] for(int ky = -NWB_SHADOW_RESOLVE_KERNEL_RADIUS; ky <= NWB_SHADOW_RESOLVE_KERNEL_RADIUS; ++ky){
                        const half kyWeight = NWB_SHADOW_RESOLVE_KERNEL[ky + NWB_SHADOW_RESOLVE_KERNEL_RADIUS];
                        [unroll] for(int kx = -NWB_SHADOW_RESOLVE_KERNEL_RADIUS; kx <= NWB_SHADOW_RESOLVE_KERNEL_RADIUS; ++kx){
                            const uint index = (uint)((int(groupThreadID.y) + (ky + NWB_SHADOW_RESOLVE_KERNEL_RADIUS) * step) * tileWidth + (int(groupThreadID.x) + (kx + NWB_SHADOW_RESOLVE_KERNEL_RADIUS) * step));
                            const half4 tapGeom = g_NwbCombinedShadowWaveletGeom[index];
                            if(tapGeom.w < half(0.0h))
                                continue;
                            const float deltaDistance = float(tapGeom.w) - centerDistance;
                            const half distWeight = half(exp(-deltaDistance * deltaDistance * invTwoSigmaDistanceSq));
                            const half c = half(max(dot(centerNormal, float3(tapGeom.xyz)), 0.0));
                            const half c2 = c * c;
                            const half c4 = c2 * c2;
                            const half normalWeight = c4 * c4;
                            const half kxWeight = NWB_SHADOW_RESOLVE_KERNEL[kx + NWB_SHADOW_RESOLVE_KERNEL_RADIUS];
                            geoWeights[(ky + NWB_SHADOW_RESOLVE_KERNEL_RADIUS) * kernelSide + (kx + NWB_SHADOW_RESOLVE_KERNEL_RADIUS)] = kxWeight * kyWeight * normalWeight * distWeight;
                        }
                    }
                    geomPrepared = true;
                }
                const uint centerIndex = (uint)((int(groupThreadID.y) + NWB_SHADOW_RESOLVE_KERNEL_RADIUS * step) * tileWidth + (int(groupThreadID.x) + NWB_SHADOW_RESOLVE_KERNEL_RADIUS * step));
                const half4 center = g_NwbCombinedShadowWaveletVis[centerIndex];
                const half centerRgbLum = nwbLuminance(center.yzw);
                const float2 sigma = nwbCombinedShadowWaveletSigma(texel, tileOrigin, hw, hh, slot);
                const float invSigmaX = 1.0 / sigma.x;
                const float invSigmaY = 1.0 / sigma.y;
                float opaqueSum = 0.0;
                float3 rgbSum = float3(0.0);
                half opaqueWeightSum = half(0.0h);
                half rgbWeightSum = half(0.0h);
                [unroll] for(int ky = -NWB_SHADOW_RESOLVE_KERNEL_RADIUS; ky <= NWB_SHADOW_RESOLVE_KERNEL_RADIUS; ++ky){
                    [unroll] for(int kx = -NWB_SHADOW_RESOLVE_KERNEL_RADIUS; kx <= NWB_SHADOW_RESOLVE_KERNEL_RADIUS; ++kx){
                        const half baseWeight = geoWeights[(ky + NWB_SHADOW_RESOLVE_KERNEL_RADIUS) * kernelSide + (kx + NWB_SHADOW_RESOLVE_KERNEL_RADIUS)];
                        if(baseWeight <= half(0.0h))
                            continue;
                        const uint index = (uint)((int(groupThreadID.y) + (ky + NWB_SHADOW_RESOLVE_KERNEL_RADIUS) * step) * tileWidth + (int(groupThreadID.x) + (kx + NWB_SHADOW_RESOLVE_KERNEL_RADIUS) * step));
                        const half4 tap = g_NwbCombinedShadowWaveletVis[index];
                        const half opaqueWeight = baseWeight * half(exp(-abs(float(tap.x - center.x)) * invSigmaX));
                        const half rgbWeight = baseWeight * half(exp(-abs(float(nwbLuminance(tap.yzw) - centerRgbLum)) * invSigmaY));
                        opaqueSum += float(opaqueWeight) * float(tap.x);
                        rgbSum += float(rgbWeight) * float3(tap.yzw);
                        opaqueWeightSum += opaqueWeight;
                        rgbWeightSum += rgbWeight;
                    }
                }
                const half opaque = opaqueWeightSum > half(0.0h) ? half(opaqueSum / float(opaqueWeightSum)) : half(1.0h);
                const half3 rgb = rgbWeightSum > half(0.0h) ? half3(rgbSum / float(rgbWeightSum)) : half3(1.0h);
                nwbCombinedShadowWaveletStore(pixel, slot, opaque, rgb);
            }
        }
        if(slot + 1u < slotEnd)
            GroupMemoryBarrierWithGroupSync();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

