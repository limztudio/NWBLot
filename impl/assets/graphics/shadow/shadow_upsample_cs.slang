// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "../scene/binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Edge-aware upsample of the HALF-resolution ray-traced shadow visibility into the full-res Texture2DArray the deferred
// lighting samples. The raygen now traces one occlusion ray per HALF-res pixel (1/4 the rays); this pass reconstructs the
// full-res per-light visibility. A plain bilinear upsample would smear the shadow ~1 full pixel past a silhouette onto
// the wrong receiver, so each of the 4 half-res taps is weighted by a world-distance edge-stop to THIS full-res pixel's
// receiver and background taps are dropped -- the shadow stays confined to the surface it belongs to (degrades to plain
// bilinear on a flat receiver). All NWB_SCENE_SHADOW_SLOT_COUNT light slots share the geometry weights, computed once.

[[vk::binding(NWB_SHADOW_UPSAMPLE_BINDING_GBUFFER_WORLD_POSITION, NWB_SHADOW_UPSAMPLE_SET)]]
Texture2D<float4> g_NwbShadowUpsampleWorldPosition;

[[vk::binding(NWB_SHADOW_UPSAMPLE_BINDING_GBUFFER_DEPTH, NWB_SHADOW_UPSAMPLE_SET)]]
Texture2D<float4> g_NwbShadowUpsampleDepth;

[[vk::binding(NWB_SHADOW_UPSAMPLE_BINDING_HALF_VISIBILITY, NWB_SHADOW_UPSAMPLE_SET)]]
Texture2DArray<float4> g_NwbShadowUpsampleHalfVisibility;

[[vk::binding(NWB_SHADOW_UPSAMPLE_BINDING_FULL_VISIBILITY, NWB_SHADOW_UPSAMPLE_SET)]]
RWTexture2DArray<float4> g_NwbShadowUpsampleFullVisibility;

struct NwbShadowUpsamplePushConstants{
    uint width;       // full-res width
    uint height;      // full-res height
    uint halfWidth;   // half-res width (the ray-traced buffer)
    uint halfHeight;  // half-res height
    uint slotCount;   // active shadow slots this frame (= min(lightCount, NWB_SCENE_SHADOW_SLOT_COUNT)); only these
                      // are processed -- inactive slots stay at the full buffer's cleared "lit" white default.
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowUpsamplePushConstants> g_NwbShadowUpsamplePushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The full-res texel a half-res texel point-sampled when it traced (top-left of its 2x2 block), for the tap's geometry.
int2 nwbShadowUpsampleHalfToFull(const int2 halfTexel){
    return int2(
        min(halfTexel.x * 2, (int)g_NwbShadowUpsamplePushConstants.width - 1),
        min(halfTexel.y * 2, (int)g_NwbShadowUpsamplePushConstants.height - 1)
    );
}


[numthreads(NWB_SHADOW_UPSAMPLE_GROUP_SIZE, NWB_SHADOW_UPSAMPLE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbShadowUpsamplePushConstants.width || pixel.y >= g_NwbShadowUpsamplePushConstants.height)
        return;

    const int2 fullTexel = int2(pixel);

    // Only the active shadow slots are processed; inactive slots (slotCount..N) stay at the full buffer's cleared white.
    const uint slotCount = min(g_NwbShadowUpsamplePushConstants.slotCount, (uint)NWB_SCENE_SHADOW_SLOT_COUNT);

    // Background (no receiver) is fully lit across every active slot -- matches the raygen's background default + clear.
    if(g_NwbShadowUpsampleDepth.Load(int3(fullTexel, 0)).r >= 0.999999){
        for(uint slot = 0u; slot < slotCount; ++slot)
            g_NwbShadowUpsampleFullVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    // Edge-stop sigma from the full-res pixel's own world spacing (max of +x / +y for foreshortening on a grazing
    // receiver); a few pixels of tolerance keeps same-surface taps while rejecting a silhouette jump.
    const int fw = (int)g_NwbShadowUpsamplePushConstants.width;
    const int fh = (int)g_NwbShadowUpsamplePushConstants.height;
    const float3 centerWorld = g_NwbShadowUpsampleWorldPosition.Load(int3(fullTexel, 0)).xyz;
    const int2 rightFull = int2(min(fullTexel.x + 1, fw - 1), fullTexel.y);
    const int2 downFull = int2(fullTexel.x, min(fullTexel.y + 1, fh - 1));
    const float3 dxWorld = g_NwbShadowUpsampleWorldPosition.Load(int3(rightFull, 0)).xyz - centerWorld;
    const float3 dyWorld = g_NwbShadowUpsampleWorldPosition.Load(int3(downFull, 0)).xyz - centerWorld;
    const float spacing = max(max(length(dxWorld), length(dyWorld)), 1e-4);
    const float sigmaWorld = spacing * 3.0;
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

    const int hw = (int)g_NwbShadowUpsamplePushConstants.halfWidth;
    const int hh = (int)g_NwbShadowUpsamplePushConstants.halfHeight;
    const float2 src = (float2(pixel) + 0.5) * 0.5 - 0.5; // full pixel centre -> half-res texel-centre space
    const int2 baseTexel = int2(round(src)); // nearest half texel; gather a 3x3 neighbourhood around it
    const float spatialSigma = 1.0;          // in half-res texels
    const float invTwoSpatialSq = 1.0 / (2.0 * spatialSigma * spatialSigma);

    // 3x3 edge-aware GAUSSIAN reconstruction (denoise + upsample in one): the ray-traced shadow carries sharp
    // grazing-Fresnel dark detail at refractor silhouettes (present at full-res too, just finer) that reads as dark
    // speckle -- worst where two refractors' silhouette projections overlap. A small geometry-stopped Gaussian over the
    // half-res neighbourhood smooths those dark outliers, while the world-distance edge-stop + background-tap drop keep
    // it confined to the receiver (no bleed across silhouettes; degrades to a plain blur on a flat receiver). One weight
    // set, reused for every slot.
    float weights[9];
    int2 taps[9];
    float weightSum = 0.0;
    [unroll] for(int j = -1; j <= 1; ++j){
        [unroll] for(int i = -1; i <= 1; ++i){
            const int idx = (j + 1) * 3 + (i + 1);
            const int2 tap = int2(clamp(baseTexel.x + i, 0, hw - 1), clamp(baseTexel.y + j, 0, hh - 1));
            taps[idx] = tap;
            const int2 tapFull = nwbShadowUpsampleHalfToFull(tap);
            float weight = 0.0;
            if(g_NwbShadowUpsampleDepth.Load(int3(tapFull, 0)).r < 0.999999){ // skip background taps
                const float2 spatial = float2(tap) - src;
                const float3 d = g_NwbShadowUpsampleWorldPosition.Load(int3(tapFull, 0)).xyz - centerWorld;
                weight = exp(-dot(spatial, spatial) * invTwoSpatialSq) * exp(-dot(d, d) * invTwoSigmaWorldSq);
            }
            weights[idx] = weight;
            weightSum += weight;
        }
    }

    // All four taps rejected (an isolated full-res receiver sliver between half-res background taps): leave it fully lit.
    if(weightSum <= 0.0){
        for(uint slot = 0u; slot < slotCount; ++slot)
            g_NwbShadowUpsampleFullVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float invWeightSum = 1.0 / weightSum;
    for(uint slot = 0u; slot < slotCount; ++slot){
        float3 sum = float3(0.0, 0.0, 0.0);
        [unroll] for(int k = 0; k < 9; ++k){
            if(weights[k] > 0.0)
                sum += weights[k] * g_NwbShadowUpsampleHalfVisibility.Load(int4(taps[k], (int)slot, 0)).rgb;
        }
        g_NwbShadowUpsampleFullVisibility[uint3(pixel, slot)] = float4(saturate(sum * invWeightSum), 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
