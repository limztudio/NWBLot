// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "shadow_resolve_filter_constants.slangi"
#include "shadow_heap_resources.slangi"
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The first 84 bytes match the independent resolve. Each channel retains its own moments and output image.
struct NwbCombinedShadowWaveletPushConstants{
    uint width;
    uint height;
    uint halfWidth;
    uint halfHeight;
    uint stepWidth;
    uint stage;
    uint lightSlotStart;
    uint lightSlotCount;
    uint momentsValid;
    uint upsampleFold;
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
    uint opaqueInputColorSlot;
    uint opaqueMomentsSlot;
    uint opaqueMomentsValid;
    uint opaqueOutputStorageSlot;
};

[[vk::push_constant]]
ConstantBuffer<NwbCombinedShadowWaveletPushConstants> g_NwbCombinedShadowWaveletPush;

// This fixed first-wavelet program needs only the step-one halo; the independent kernels retain their larger halos.
static const int s_NwbCombinedShadowWaveletStep = 1;
static const int s_NwbCombinedShadowWaveletTileSide = NWB_SHADOW_RESOLVE_GROUP_SIZE + 4;
static const uint s_NwbCombinedShadowWaveletTileTexels = s_NwbCombinedShadowWaveletTileSide * s_NwbCombinedShadowWaveletTileSide;

groupshared half4 s_NwbCombinedShadowWaveletVis[s_NwbCombinedShadowWaveletTileTexels];
groupshared half2 s_NwbCombinedShadowWaveletNormal[s_NwbCombinedShadowWaveletTileTexels];
groupshared half s_NwbCombinedShadowWaveletDist[s_NwbCombinedShadowWaveletTileTexels];
groupshared uint s_NwbCombinedShadowWaveletValid[s_NwbCombinedShadowWaveletTileTexels];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half4 nwbCombinedShadowWaveletGeometry(const int2 texel){
    return half4(NwbHeapSampledImage2D(g_NwbCombinedShadowWaveletPush.geometrySlot).Load(int3(texel, 0)));
}

half4 nwbCombinedShadowWaveletVisibility(const int2 texel, const uint slot){
    const half opaque = half(NwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.opaqueInputColorSlot).Load(int4(texel, slot, 0)).r);
    const half3 rgb = half3(NwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.inputColorSlot).Load(int4(texel, slot, 0)).rgb);
    return half4(opaque, rgb);
}

float2 nwbCombinedShadowWaveletSigma(const int2 texel, const uint slot){
    const float2 scale = float2(NWB_SHADOW_RESOLVE_SCALAR_SIGMA_SCALE, NWB_SHADOW_RESOLVE_RGB_SIGMA_SCALE);
    const float2 sigmaBase = NWB_SHADOW_RESOLVE_SIGMA_BASE * scale;
    const float2 sigmaEps = NWB_SHADOW_RESOLVE_SIGMA_EPS_BASE * scale;
    float2 sigma = sigmaEps;
    bool2 temporal = bool2(false, false);
    if(g_NwbCombinedShadowWaveletPush.opaqueMomentsValid != 0u){
        const half3 sample = half3(NwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.opaqueMomentsSlot).Load(int4(texel, slot, 0)).xyz);
        const float3 moment = float3(sample);
        if(moment.z >= NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY){
            const float variance = max(moment.y - moment.x * moment.x, 0.0);
            sigma.x = sigmaBase.x * sqrt(variance) + sigmaEps.x;
            temporal.x = true;
        }
    }
    if(g_NwbCombinedShadowWaveletPush.momentsValid != 0u){
        const half3 sample = half3(NwbHeapSampledImage2DArray(g_NwbCombinedShadowWaveletPush.momentsSlot).Load(int4(texel, slot, 0)).xyz);
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
    const int2 maximum = int2(g_NwbCombinedShadowWaveletPush.halfWidth, g_NwbCombinedShadowWaveletPush.halfHeight) - int2(1, 1);
    [unroll] for(int dy = -1; dy <= 1; ++dy){
        [unroll] for(int dx = -1; dx <= 1; ++dx){
            const int2 tap = clamp(texel + int2(dx, dy), int2(0, 0), maximum);
            if(nwbCombinedShadowWaveletGeometry(tap).w <= half(0.5h))
                continue;
            const half4 visibility = nwbCombinedShadowWaveletVisibility(tap, slot);
            const half2 value = half2(visibility.x, nwbLuminance(visibility.yzw));
            mean += value;
            mean2 += value * value;
            ++count;
        }
    }
    if(count > 0){
        const float invCount = 1.0 / (float)count;
        const half2 variance = max(mean2 * half(invCount) - (mean * half(invCount)) * (mean * half(invCount)), half2(0.0h));
        const float2 spatial = sigmaBase * sqrt(float2(variance)) + sigmaEps;
        if(!temporal.x)
            sigma.x = spatial.x;
        if(!temporal.y)
            sigma.y = spatial.y;
    }
    return sigma;
}

void nwbCombinedShadowWaveletStore(const uint2 pixel, const uint slot, const float opaque, const float3 rgb){
    NwbHeapStorageImage2DArrayFloat4(g_NwbCombinedShadowWaveletPush.opaqueOutputStorageSlot)[uint3(pixel, slot)] = float4(opaque, opaque, opaque, 1.0);
    NwbHeapStorageImage2DArrayFloat4(g_NwbCombinedShadowWaveletPush.outputStorageSlot)[uint3(pixel, slot)] = float4(rgb, 1.0);
}

[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint2 pixel = dispatchThreadID.xy;
    const int2 texel = int2(pixel);
    const int hw = (int)g_NwbCombinedShadowWaveletPush.halfWidth;
    const int hh = (int)g_NwbCombinedShadowWaveletPush.halfHeight;
    const bool inBounds = pixel.x < (uint)hw && pixel.y < (uint)hh;
    const int step = s_NwbCombinedShadowWaveletStep;
    // The host selects this pipeline only for the first wavelet with distinct retained temporal inputs.
    const int tileWidth = s_NwbCombinedShadowWaveletTileSide;
    const uint tileTexels = s_NwbCombinedShadowWaveletTileTexels;
    const int2 tileOrigin = int2(groupID.xy) * NWB_SHADOW_RESOLVE_GROUP_SIZE - int2(2 * step, 2 * step);
    const uint lane = groupThreadID.y * NWB_SHADOW_RESOLVE_GROUP_SIZE + groupThreadID.x;
    const uint threadCount = NWB_SHADOW_RESOLVE_GROUP_SIZE * NWB_SHADOW_RESOLVE_GROUP_SIZE;
    const uint slotEnd = min(g_NwbCombinedShadowWaveletPush.lightSlotStart + g_NwbCombinedShadowWaveletPush.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);
    const half4 centerGeom = inBounds ? nwbCombinedShadowWaveletGeometry(texel) : half4(0.0h);
    const float centerDistance = float(centerGeom.z);
    const float sigmaDistance = max(centerDistance * NWB_SHADOW_RESOLVE_WAVELET_DIST_FRAC * (float)step, 1e-3);
    const float invTwoSigmaDistanceSq = 1.0 / (2.0 * sigmaDistance * sigmaDistance);
    const float3 centerNormal = nwbOctDecodeNormal(float2(centerGeom.xy));
    for(uint slot = g_NwbCombinedShadowWaveletPush.lightSlotStart; slot < slotEnd; ++slot){
        for(uint index = lane; index < tileTexels; index += threadCount){
            const int2 tap = tileOrigin + int2((int)(index % (uint)tileWidth), (int)(index / (uint)tileWidth));
            half4 visibility = half4(1.0h);
            half2 normal = half2(0.0h);
            half distance = half(0.0h);
            uint valid = 0u;
            if(tap.x >= 0 && tap.y >= 0 && tap.x < hw && tap.y < hh){
                const half4 geometry = nwbCombinedShadowWaveletGeometry(tap);
                if(geometry.w > half(0.5h)){
                    visibility = nwbCombinedShadowWaveletVisibility(tap, slot);
                    normal = geometry.xy;
                    distance = geometry.z;
                    valid = 1u;
                }
            }
            s_NwbCombinedShadowWaveletVis[index] = visibility;
            s_NwbCombinedShadowWaveletNormal[index] = normal;
            s_NwbCombinedShadowWaveletDist[index] = distance;
            s_NwbCombinedShadowWaveletValid[index] = valid;
        }
        GroupMemoryBarrierWithGroupSync();
        if(inBounds){
            if(centerGeom.w <= half(0.5h))
                nwbCombinedShadowWaveletStore(pixel, slot, 1.0, float3(1.0));
            else{
                const uint centerIndex = (uint)((int(groupThreadID.y) + 2 * step) * tileWidth + (int(groupThreadID.x) + 2 * step));
                const float4 center = float4(s_NwbCombinedShadowWaveletVis[centerIndex]);
                const float2 sigma = nwbCombinedShadowWaveletSigma(texel, slot);
                float opaqueSum = 0.0;
                float3 rgbSum = float3(0.0);
                half opaqueWeightSum = half(0.0h);
                half rgbWeightSum = half(0.0h);
                [unroll] for(int ky = -2; ky <= 2; ++ky){
                    [unroll] for(int kx = -2; kx <= 2; ++kx){
                        const uint index = (uint)((int(groupThreadID.y) + (ky + 2) * step) * tileWidth + (int(groupThreadID.x) + (kx + 2) * step));
                        if(s_NwbCombinedShadowWaveletValid[index] == 0u)
                            continue;
                        const float4 tap = float4(s_NwbCombinedShadowWaveletVis[index]);
                        const float deltaDistance = float(s_NwbCombinedShadowWaveletDist[index]) - centerDistance;
                        const half distanceWeight = half(exp(-deltaDistance * deltaDistance * invTwoSigmaDistanceSq));
                        const float3 tapNormal = nwbOctDecodeNormal(float2(s_NwbCombinedShadowWaveletNormal[index]));
                        const half normalWeight = half(pow(max(dot(centerNormal, tapNormal), 0.0), NWB_SHADOW_RESOLVE_NORMAL_POWER));
                        const half opaqueValueWeight = half(exp(-abs(half(tap.x) - half(center.x)) / half(sigma.x)));
                        const half rgbValueWeight = half(exp(-abs(half(nwbLuminance(tap.yzw)) - half(nwbLuminance(center.yzw))) / half(sigma.y)));
                        // Keep each half multiplication and accumulation in the original order, including separate denominators.
                        const half opaqueWeight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distanceWeight * normalWeight * opaqueValueWeight;
                        const half rgbWeight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distanceWeight * normalWeight * rgbValueWeight;
                        opaqueSum += opaqueWeight * tap.x;
                        rgbSum += rgbWeight * tap.yzw;
                        opaqueWeightSum += opaqueWeight;
                        rgbWeightSum += rgbWeight;
                    }
                }
                const half opaque = opaqueWeightSum > half(0.0h) ? half(opaqueSum / float(opaqueWeightSum)) : half(1.0);
                const half3 rgb = rgbWeightSum > half(0.0h) ? half3(rgbSum / float(rgbWeightSum)) : half3(1.0h);
                nwbCombinedShadowWaveletStore(pixel, slot, opaque, rgb);
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

