// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "../scene/binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Edge-aware upsample of the HALF-resolution ray-traced shadow visibility into the full-res Texture2DArray the deferred
// lighting samples. The trace casts one occlusion ray per HALF-res pixel (1/4 the rays); this pass reconstructs the
// full-res per-light visibility from the 2x2 half-res neighbourhood with a GEOMETRY-STOPPED BILINEAR blend: each tap's
// bilinear weight is gated by world-distance + plane-distance + normal-similarity edge-stops, so the shadow stays
// confined to its receiver (a plain bilinear would smear it ~1 full pixel past a silhouette onto the wrong surface).
// The half-res trace is already clean per-ray (the RayQuery / SW per-occluder integration drops unbalanced silhouette
// clips), so a smooth blend is the right reconstruction for these soft transparent shadows -- no best-tap/nearest pick
// (which keeps an opaque 0/1 edge hard but reads as blocky half-res stair-steps on a soft overlap). All
// NWB_SCENE_SHADOW_SLOT_COUNT light slots share the geometry weights, computed once.

[[vk::binding(NWB_SHADOW_UPSAMPLE_BINDING_GBUFFER_WORLD_POSITION, NWB_SHADOW_UPSAMPLE_SET)]]
Texture2D<float4> g_NwbShadowUpsampleWorldPosition;

[[vk::binding(NWB_SHADOW_UPSAMPLE_BINDING_GBUFFER_NORMAL, NWB_SHADOW_UPSAMPLE_SET)]]
Texture2D<float4> g_NwbShadowUpsampleNormal;

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

// G-buffer normal is stored (n * 0.5 + 0.5); decode + normalize (matches the trace's normal decode).
float3 nwbShadowUpsampleDecodeNormal(const int2 texel){
    return normalize(g_NwbShadowUpsampleNormal.Load(int3(texel, 0)).xyz * 2.0 - 1.0);
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

    // Edge-stop sigmas from the full-res pixel's own world spacing (max of +x / +y for foreshortening on a grazing
    // receiver). World distance rejects far taps; plane distance (along the normal) rejects a different parallel plane;
    // the normal dot rejects a different orientation. Tighter than world distance alone so hard edges do not bleed.
    const int fw = (int)g_NwbShadowUpsamplePushConstants.width;
    const int fh = (int)g_NwbShadowUpsamplePushConstants.height;
    const float3 centerWorld = g_NwbShadowUpsampleWorldPosition.Load(int3(fullTexel, 0)).xyz;
    const float3 centerNormal = nwbShadowUpsampleDecodeNormal(fullTexel);
    const int2 rightFull = int2(min(fullTexel.x + 1, fw - 1), fullTexel.y);
    const int2 downFull = int2(fullTexel.x, min(fullTexel.y + 1, fh - 1));
    const float3 dxWorld = g_NwbShadowUpsampleWorldPosition.Load(int3(rightFull, 0)).xyz - centerWorld;
    const float3 dyWorld = g_NwbShadowUpsampleWorldPosition.Load(int3(downFull, 0)).xyz - centerWorld;
    const float spacing = max(max(length(dxWorld), length(dyWorld)), 1e-4);
    const float sigmaWorld = spacing * 2.0;
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);
    const float sigmaPlane = max(spacing * 0.75, 1e-4);
    const float invTwoSigmaPlaneSq = 1.0 / (2.0 * sigmaPlane * sigmaPlane);
    const float normalReject = 0.85;

    const int hw = (int)g_NwbShadowUpsamplePushConstants.halfWidth;
    const int hh = (int)g_NwbShadowUpsamplePushConstants.halfHeight;
    const float2 src = (float2(pixel) + 0.5) * 0.5 - 0.5; // full pixel centre -> half-res texel-centre space
    const int2 baseTexel = int2(floor(src));              // top-left of the 2x2 half-res gather
    const float2 lerpFrac = src - float2(baseTexel);      // bilinear fractions

    // 2x2 geometry-stopped bilinear weights. The half-res shadow is already clean per-ray (the RayQuery / SW trace
    // drops unbalanced silhouette clips), so the upsample just needs a SMOOTH reconstruction: a bilinear weight per
    // tap, gated by world-distance + plane-distance + normal-similarity edge-stops so it never blends across a
    // silhouette onto the wrong receiver. (A "best tap / nearest" pick keeps a 0/1 opaque edge hard, but at half-res
    // that reads as blocky stair-steps on these soft transparent-shadow overlaps -- the smooth blend is cleaner here.)
    float weights[4];
    int2 taps[4];
    float weightSum = 0.0;
    [unroll] for(int j = 0; j < 2; ++j){
        [unroll] for(int i = 0; i < 2; ++i){
            const int idx = j * 2 + i;
            const int2 tap = int2(clamp(baseTexel.x + i, 0, hw - 1), clamp(baseTexel.y + j, 0, hh - 1));
            taps[idx] = tap;

            const int2 tapFull = nwbShadowUpsampleHalfToFull(tap);
            float weight = 0.0;
            if(g_NwbShadowUpsampleDepth.Load(int3(tapFull, 0)).r < 0.999999){ // skip background taps
                const float bilinearWeight =
                    (i == 0 ? (1.0 - lerpFrac.x) : lerpFrac.x) *
                    (j == 0 ? (1.0 - lerpFrac.y) : lerpFrac.y);

                const float3 tapWorld = g_NwbShadowUpsampleWorldPosition.Load(int3(tapFull, 0)).xyz;
                const float3 tapNormal = nwbShadowUpsampleDecodeNormal(tapFull);
                const float normalDot = dot(centerNormal, tapNormal);
                if(normalDot > normalReject){
                    const float3 d = tapWorld - centerWorld;
                    const float planeDist = abs(dot(d, centerNormal));
                    const float worldW = exp(-dot(d, d) * invTwoSigmaWorldSq);
                    const float planeW = exp(-(planeDist * planeDist) * invTwoSigmaPlaneSq);
                    const float normalW = pow(saturate(normalDot), 32.0);
                    weight = bilinearWeight * worldW * planeW * normalW;
                }
            }
            weights[idx] = weight;
            weightSum += weight;
        }
    }

    // All taps rejected (an isolated full-res receiver sliver between half-res background/other-plane taps): fully lit.
    if(weightSum <= 0.0){
        for(uint slot = 0u; slot < slotCount; ++slot)
            g_NwbShadowUpsampleFullVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float invWeightSum = 1.0 / weightSum;
    for(uint slot = 0u; slot < slotCount; ++slot){
        float3 sum = float3(0.0, 0.0, 0.0);
        [unroll] for(int k = 0; k < 4; ++k){
            if(weights[k] > 0.0)
                sum += weights[k] * g_NwbShadowUpsampleHalfVisibility.Load(int4(taps[k], (int)slot, 0)).rgb;
        }
        g_NwbShadowUpsampleFullVisibility[uint3(pixel, slot)] = float4(saturate(sum * invWeightSum), 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
