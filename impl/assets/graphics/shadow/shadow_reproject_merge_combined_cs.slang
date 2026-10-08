// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_reproject_merge_binding_slots.h"
#include "shadow_resolve_binding_slots.h"
#include "sw_binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both channels share receiver reprojection and neighborhood validity, retaining independent histories and moments.

struct NwbCombinedShadowReprojectMergePushConstants{
    row_major float4x4 prevWorldToClip;
    uint receiverFactor;
    uint lightSlotCount;
    uint historyValid;
    uint geometryCurrSlot;
    uint geometryPrevSlot;
    uint worldPositionSlot;
    uint opaqueSoftTraceSlot;
    uint opaqueHistoryInSlot;
    uint opaqueMomentsInSlot;
    uint opaqueHistoryOutSlot;
    uint opaqueMomentsOutSlot;
    uint transparentSoftTraceSlot;
    uint transparentHistoryInSlot;
    uint transparentMomentsInSlot;
    uint transparentHistoryOutSlot;
    uint transparentMomentsOutSlot;
};

[[vk::push_constant]]
ConstantBuffer<NwbCombinedShadowReprojectMergePushConstants> g_NwbShadowMergePush;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float nwbShadowTemporalWorldSpacing(const float cameraDistance){
    return max(cameraDistance * NWB_SHADOW_TEMPORAL_WORLD_SPACING_SCALE, NWB_SHADOW_TEMPORAL_WORLD_SPACING_MIN);
}

half4 nwbShadowMergeGeomCurr(const int2 halfTexel){
    return half4(nwbHeapSampledImage2D(g_NwbShadowMergePush.geometryCurrSlot).Load(int3(halfTexel, 0)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 h = dispatchThreadID.xy;
    uint halfWidth;
    uint halfHeight;
    nwbHeapSampledImage2D(g_NwbShadowMergePush.geometryCurrSlot).GetDimensions(halfWidth, halfHeight);
    if(h.x >= halfWidth || h.y >= halfHeight)
        return;

    const int hw = (int)halfWidth;
    const int hh = (int)halfHeight;
    const int2 texel = int2(h);

    const uint slotEnd = min(g_NwbShadowMergePush.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);

    const half4 gc = nwbShadowMergeGeomCurr(texel);
    const bool valid = gc.w > half(0.5h);
    const float3 Nc = nwbOctDecodeNormal(float2(gc.xy));
    const float currentCameraDistance = float(gc.z);

    // Ceil-sized receiver grids keep every top-left source coordinate inside the full-resolution target.
    const int2 full = int2(h * g_NwbShadowMergePush.receiverFactor);
    const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbShadowMergePush.worldPositionSlot).Load(int3(full, 0)).xyz);
    const float3 Wc = float3(worldPositionSample);

    // Reproject the current world position into previous history.
    const float4 clipPrev = mul(g_NwbShadowMergePush.prevWorldToClip, float4(Wc, 1.0));
    bool onScreen = false;
    int2 hp = int2(0, 0);
    if(clipPrev.w > 0.0 && g_NwbShadowMergePush.historyValid != 0u){
        const float3 ndc = clipPrev.xyz / clipPrev.w;
        const float2 uvPrev = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if(uvPrev.x >= 0.0 && uvPrev.x <= 1.0 && uvPrev.y >= 0.0 && uvPrev.y <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0){
            const float2 histTexelF = uvPrev * float2((float)halfWidth, (float)halfHeight) - 0.5;
            hp = int2(clamp((int)round(histTexelF.x), 0, hw - 1), clamp((int)round(histTexelF.y), 0, hh - 1));
            onScreen = true;
        }
    }

    // Accept history only for matching receiver geometry.
    bool historyAccepted = false;
    if(onScreen && valid){
        const half4 gp = half4(nwbHeapSampledImage2D(g_NwbShadowMergePush.geometryPrevSlot).Load(int3(hp, 0)));
        if(gp.w > half(0.5h)){
            const float3 Np = nwbOctDecodeNormal(float2(gp.xy));
            const bool planeOk = abs(currentCameraDistance - float(gp.z)) < (NWB_SHADOW_TEMPORAL_PLANE_K * nwbShadowTemporalWorldSpacing(currentCameraDistance));
            const bool normalOk = dot(Nc, Np) > NWB_SHADOW_TEMPORAL_NORMAL_MIN;
            historyAccepted = planeOk && normalOk;
        }
    }

    uint validTapMask = 0u;
    if(valid){
        [unroll]
        for(int dy = -1; dy <= 1; ++dy){
            [unroll]
            for(int dx = -1; dx <= 1; ++dx){
                const int2 tap = clamp(texel + int2(dx, dy), int2(0), int2(hw - 1, hh - 1));
                if(!(nwbShadowMergeGeomCurr(tap).w <= half(0.5h)))
                    validTapMask |= 1u << uint((dy + 1) * 3 + dx + 1);
            }
        }
    }

    for(uint slot = 0u; slot < slotEnd; ++slot){
        [unroll]
        for(uint channel = 0u; channel < 2u; ++channel){
            const bool opaque = channel == 0u;
            const uint softTraceSlot = opaque ? g_NwbShadowMergePush.opaqueSoftTraceSlot : g_NwbShadowMergePush.transparentSoftTraceSlot;
            const uint historyInSlot = opaque ? g_NwbShadowMergePush.opaqueHistoryInSlot : g_NwbShadowMergePush.transparentHistoryInSlot;
            const uint momentsInSlot = opaque ? g_NwbShadowMergePush.opaqueMomentsInSlot : g_NwbShadowMergePush.transparentMomentsInSlot;
            const uint historyOutSlot = opaque ? g_NwbShadowMergePush.opaqueHistoryOutSlot : g_NwbShadowMergePush.transparentHistoryOutSlot;
            const uint momentsOutSlot = opaque ? g_NwbShadowMergePush.opaqueMomentsOutSlot : g_NwbShadowMergePush.transparentMomentsOutSlot;
            const half3 currentVisibilitySample = half3(nwbHeapSampledImage2DArray(softTraceSlot).Load(int4(texel, slot, 0)).rgb);
            const float3 cur = float3(currentVisibilitySample);

            // Invalid receiver: pass through the current sample and reset history.
            if(!valid){
                nwbHeapStorageImage2DArrayFloat4(historyOutSlot)[uint3(h, slot)] = float4(cur, 1.0);
                // Visibility luma stays in (0, 1), so half math is sufficient here; widen only at the float4 moments store.
                const half lumaCur0 = nwbLuminance(currentVisibilitySample);
                nwbHeapStorageImage2DArrayFloat4(momentsOutSlot)[uint3(h, slot)] = float4(float(lumaCur0), float(lumaCur0) * float(lumaCur0), 0.0, 0.0);
                continue;
            }

            // Rejected history resets to the current sample.
            float3 hist = cur;
            float n = 0.0;
            float m1Prev = 0.0;
            float m2Prev = 0.0;
            if(historyAccepted){
                const half3 historySample = half3(nwbHeapSampledImage2DArray(historyInSlot).Load(int4(hp, slot, 0)).rgb);
                const half3 momentSample = half3(nwbHeapSampledImage2DArray(momentsInSlot).Load(int4(hp, slot, 0)).xyz);
                hist = float3(historySample);
                const float3 mom = float3(momentSample);
                m1Prev = mom.x;
                m2Prev = mom.y;
                n = mom.z;
            }

            // Clamp history to the current neighbourhood. Visibility luma stays in (0, 1), so half math is sufficient here.
            const half lumaCur = half(nwbLuminance(cur));
            const half lumaHistRaw = half(nwbLuminance(hist));
            const half temporalDelta = lumaCur - lumaHistRaw;
            const half temporalDeltaAbs = abs(temporalDelta);
            float3 mu = float3(0.0, 0.0, 0.0);
            float3 mu2 = float3(0.0, 0.0, 0.0);
            float lumaM1 = 0.0;
            float lumaM2 = 0.0;
            int cnt = 0;
            uint temporalChangeSupport = 0u;
            [unroll]
            for(int dy = -1; dy <= 1; ++dy){
                [unroll]
                for(int dx = -1; dx <= 1; ++dx){
                    const int2 unclampedTap = texel + int2(dx, dy);
                    // Keep clamped taps for the original moments, but count each supporting pixel only once at image edges.
                    const bool supportTapInBounds = unclampedTap.x >= 0 && unclampedTap.y >= 0 && unclampedTap.x < hw && unclampedTap.y < hh;
                    const int2 t = int2(clamp(unclampedTap.x, 0, hw - 1), clamp(unclampedTap.y, 0, hh - 1));
                    const uint tapBit = 1u << uint((dy + 1) * 3 + dx + 1);
                    if((validTapMask & tapBit) == 0u)
                        continue;
                    // Tap visibility and its luma stay in (0, 1), so half math is sufficient here.
                    const half3 s = half3(nwbHeapSampledImage2DArray(softTraceSlot).Load(int4(t, slot, 0)).rgb);
                    mu += float3(s);
                    mu2 += float3(s * s);
                    const half sl = half(nwbLuminance(float3(s)));
                    lumaM1 += float(sl);
                    lumaM2 += float(sl * sl);
                    const half tapDelta = sl - lumaHistRaw;
                    if(supportTapInBounds
                        && temporalDeltaAbs > 0.0
                        && tapDelta * temporalDelta > 0.0
                        && abs(tapDelta) >= temporalDeltaAbs * NWB_SHADOW_TEMPORAL_ANTILAG_SUPPORT_FRACTION
                    )
                        ++temporalChangeSupport;
                    ++cnt;
                }
            }
            float3 histClamped = hist;
            float sigmaLuma = 0.0;
            if(cnt > 0){
                const float invCount = 1.0 / (float)cnt;
                mu *= invCount;
                const float3 variance = max(mu2 * invCount - mu * mu, float3(0.0, 0.0, 0.0));
                const float3 sigma = sqrt(variance);
                const float3 lo = mu - NWB_SHADOW_TEMPORAL_GAMMA * sigma;
                const float3 hi = mu + NWB_SHADOW_TEMPORAL_GAMMA * sigma;
                histClamped = clamp(hist, lo, hi);
                const float lumaMean = lumaM1 * invCount;
                sigmaLuma = sqrt(max(lumaM2 * invCount - lumaMean * lumaMean, 0.0));
            }

            // Reduce history at temporal edges after removing local noise. Visibility luma stays in (0, 1), so half math is sufficient here.
            const half lumaHist = half(nwbLuminance(histClamped));
            const bool temporalChangeCoherent = temporalChangeSupport >= NWB_SHADOW_TEMPORAL_ANTILAG_MIN_SUPPORT;
            // Edge signal, gradient, and effective history count feed only the (0, 1) saturate below; variance accumulators above and the float4 moments-store count below stay on float. Widen only at those uses.
            const half edgeSignal = temporalChangeCoherent
                ? max(half(0.0h), abs(lumaCur - lumaHist) - half(NWB_SHADOW_TEMPORAL_NOISE_SLACK) * half(sigmaLuma))
                : half(0.0h)
            ;
            const half tempGrad = edgeSignal / max(max(lumaCur, lumaHist), half(0.001h));
            const half nEff = half(n) * saturate(half(1.0h) - half(NWB_SHADOW_TEMPORAL_ANTILAG) * tempGrad);

            // Bound the current-sample blend weight. The merge alpha and luminance moments are (0, 1) bounded factors, so half arithmetic is sufficient here.
            const half alpha = half(max(1.0 / (float(nEff) + 1.0), NWB_SHADOW_TEMPORAL_ALPHA_MIN));
            const half3 outVis = half3(lerp(histClamped, cur, float(alpha)));
            const float nNext = min(float(nEff) + 1.0, NWB_SHADOW_TEMPORAL_MAX_HISTORY);

            // Integrate current-sample luminance moments with the same alpha.
            const half m1 = half(lerp(m1Prev, lumaCur, float(alpha)));
            const half m2 = half(lerp(m2Prev, lumaCur * lumaCur, float(alpha)));

            nwbHeapStorageImage2DArrayFloat4(historyOutSlot)[uint3(h, slot)] = float4(float3(outVis), 1.0);
            nwbHeapStorageImage2DArrayFloat4(momentsOutSlot)[uint3(h, slot)] = float4(float(m1), float(m2), nNext, 0.0);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

