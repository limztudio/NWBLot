// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_reproject_merge_binding_slots.h"
#include "shadow_resolve_binding_slots.h"
#include "sw_binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reprojects half-resolution soft-shadow history from the previous world-to-clip matrix.
// Geometry validation, variance clamping, and antilag reject invalid or changing history.

struct NwbShadowReprojectMergePushConstants{
    // Must match ShadowReprojectMergePushConstants in C++ and its row-major Float44U layout.
    row_major float4x4 prevWorldToClip;
    uint width;
    uint height;
    uint halfWidth;
    uint halfHeight;
    uint lightSlotStart;
    uint lightSlotCount;
    uint historyValid; // 0 forces pure-current output and prevents history reads.
    uint softTraceSlot;
    uint historyInSlot;
    uint momentsInSlot;
    uint geometryCurrSlot;
    uint geometryPrevSlot;
    uint worldPositionSlot;
    uint historyOutputStorageSlot;
    uint momentsOutputStorageSlot;
    uint pad2;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowReprojectMergePushConstants> g_NwbShadowMergePush;

#define g_NwbShadowMergeHistoryOut NwbHeapStorageImage2DArrayFloat4(g_NwbShadowMergePush.historyOutputStorageSlot)
#define g_NwbShadowMergeMomentsOut NwbHeapStorageImage2DArrayFloat4(g_NwbShadowMergePush.momentsOutputStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float nwbShadowTemporalWorldSpacing(const float cameraDistance){
    return max(cameraDistance * NWB_SHADOW_TEMPORAL_WORLD_SPACING_SCALE, NWB_SHADOW_TEMPORAL_WORLD_SPACING_MIN);
}

half4 nwbShadowMergeGeomCurr(const int2 halfTexel){
    return half4(NwbHeapSampledImage2D(g_NwbShadowMergePush.geometryCurrSlot).Load(int3(halfTexel, 0)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 h = dispatchThreadID.xy;
    const uint halfWidth = g_NwbShadowMergePush.halfWidth;
    const uint halfHeight = g_NwbShadowMergePush.halfHeight;
    if(h.x >= halfWidth || h.y >= halfHeight)
        return;

    const int hw = (int)halfWidth;
    const int hh = (int)halfHeight;
    const int2 texel = int2(h);

    const uint slotStart = g_NwbShadowMergePush.lightSlotStart;
    const uint slotEnd = min(slotStart + g_NwbShadowMergePush.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);

    const half4 gc = nwbShadowMergeGeomCurr(texel);
    const bool valid = gc.w > half(0.5h);
    const float3 Nc = nwbOctDecodeNormal(float2(gc.xy));
    const float currentCameraDistance = float(gc.z);

    const int2 full = int2(
        min((int)h.x * NWB_SW_SHADOW_SOFT_FACTOR, (int)g_NwbShadowMergePush.width - 1),
        min((int)h.y * NWB_SW_SHADOW_SOFT_FACTOR, (int)g_NwbShadowMergePush.height - 1)
    );
    const half3 worldPositionSample = half3(NwbHeapSampledImage2D(g_NwbShadowMergePush.worldPositionSlot).Load(int3(full, 0)).xyz);
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
        const half4 gp = half4(NwbHeapSampledImage2D(g_NwbShadowMergePush.geometryPrevSlot).Load(int3(hp, 0)));
        if(gp.w > half(0.5h)){
            const float3 Np = nwbOctDecodeNormal(float2(gp.xy));
            const bool planeOk = abs(currentCameraDistance - float(gp.z)) < (NWB_SHADOW_TEMPORAL_PLANE_K * nwbShadowTemporalWorldSpacing(currentCameraDistance));
            const bool normalOk = dot(Nc, Np) > NWB_SHADOW_TEMPORAL_NORMAL_MIN;
            historyAccepted = planeOk && normalOk;
        }
    }

    for(uint slot = slotStart; slot < slotEnd; ++slot){
        const half3 currentVisibilitySample = half3(NwbHeapSampledImage2DArray(g_NwbShadowMergePush.softTraceSlot).Load(int4(texel, slot, 0)).rgb);
        const float3 cur = float3(currentVisibilitySample);

        // Invalid receiver: pass through the current sample and reset history.
        if(!valid){
            g_NwbShadowMergeHistoryOut[uint3(h, slot)] = float4(cur, 1.0);
            const float lumaCur0 = nwbLuminance(cur);
            g_NwbShadowMergeMomentsOut[uint3(h, slot)] = float4(lumaCur0, lumaCur0 * lumaCur0, 0.0, 0.0);
            continue;
        }

        // Rejected history resets to the current sample.
        float3 hist = cur;
        float n = 0.0;
        float m1Prev = 0.0;
        float m2Prev = 0.0;
        if(historyAccepted){
            const half3 historySample = half3(NwbHeapSampledImage2DArray(g_NwbShadowMergePush.historyInSlot).Load(int4(hp, slot, 0)).rgb);
            const half3 momentSample = half3(NwbHeapSampledImage2DArray(g_NwbShadowMergePush.momentsInSlot).Load(int4(hp, slot, 0)).xyz);
            hist = float3(historySample);
            const float3 mom = float3(momentSample);
            m1Prev = mom.x;
            m2Prev = mom.y;
            n = mom.z;
        }

        // Clamp history to the current neighbourhood.
        const float lumaCur = nwbLuminance(cur);
        const float lumaHistRaw = nwbLuminance(hist);
        const float temporalDelta = lumaCur - lumaHistRaw;
        const float temporalDeltaAbs = abs(temporalDelta);
        float3 mu = float3(0.0, 0.0, 0.0);
        float3 mu2 = float3(0.0, 0.0, 0.0);
        float lumaM1 = 0.0;
        float lumaM2 = 0.0;
        int cnt = 0;
        uint temporalChangeSupport = 0u;
        [unroll] for(int dy = -1; dy <= 1; ++dy){
            [unroll] for(int dx = -1; dx <= 1; ++dx){
                const int2 unclampedTap = texel + int2(dx, dy);
                // Keep clamped taps for the original moments, but count each supporting pixel only once at image edges.
                const bool supportTapInBounds =
                    unclampedTap.x >= 0 && unclampedTap.y >= 0 && unclampedTap.x < hw && unclampedTap.y < hh
                ;
                const int2 t = int2(clamp(unclampedTap.x, 0, hw - 1), clamp(unclampedTap.y, 0, hh - 1));
                if(nwbShadowMergeGeomCurr(t).w <= half(0.5h))
                    continue;
                const half3 visibilitySample = half3(NwbHeapSampledImage2DArray(g_NwbShadowMergePush.softTraceSlot).Load(int4(t, slot, 0)).rgb);
                const float3 s = float3(visibilitySample);
                mu += s;
                mu2 += s * s;
                const float sl = nwbLuminance(s);
                lumaM1 += sl;
                lumaM2 += sl * sl;
                const float tapDelta = sl - lumaHistRaw;
                if(
                    supportTapInBounds
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

        // Reduce history at temporal edges after removing local noise.
        const float lumaHist = nwbLuminance(histClamped);
        const bool temporalChangeCoherent = temporalChangeSupport >= NWB_SHADOW_TEMPORAL_ANTILAG_MIN_SUPPORT;
        const float edgeSignal = temporalChangeCoherent
            ? max(0.0, abs(lumaCur - lumaHist) - NWB_SHADOW_TEMPORAL_NOISE_SLACK * sigmaLuma)
            : 0.0
        ;
        const float tempGrad = edgeSignal / max(max(lumaCur, lumaHist), 1e-3);
        const float nEff = n * saturate(1.0 - NWB_SHADOW_TEMPORAL_ANTILAG * tempGrad);

        // Bound the current-sample blend weight.
        const float alpha = max(1.0 / (nEff + 1.0), NWB_SHADOW_TEMPORAL_ALPHA_MIN);
        const float3 outVis = lerp(histClamped, cur, alpha);
        const float nNext = min(nEff + 1.0, NWB_SHADOW_TEMPORAL_MAX_HISTORY);

        // Integrate current-sample luminance moments with the same alpha.
        const float m1 = lerp(m1Prev, lumaCur, alpha);
        const float m2 = lerp(m2Prev, lumaCur * lumaCur, alpha);

        g_NwbShadowMergeHistoryOut[uint3(h, slot)] = float4(outVis, 1.0);
        g_NwbShadowMergeMomentsOut[uint3(h, slot)] = float4(m1, m2, nNext, 0.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

