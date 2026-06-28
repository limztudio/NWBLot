// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// EDGE-ADAPTIVE full-res reconstruction of the half-res ray-traced shadow visibility. The trace casts one occlusion
// ray per HALF-res pixel (1/4 the rays); the deferred lighting samples a FULL-res visibility array. A plain bilinear
// upsample leaves the shadow silhouette ~2-3 full-res px wide -- visibly coarser than the 1px scene geometry it falls
// next to (a resolution mismatch). On a FLAT receiver the upsample cannot recover a crisp edge: the shadow silhouette
// is a light-space feature, so the full-res G-buffer of the plane carries nothing to reconstruct against.
//
// So this pass is adaptive: per full-res pixel it inspects the 2x2 half-res neighbourhood. Where the neighbourhood is
// uniform (a flat shadow interior / fully-lit region) it uses the cheap geometry-stopped bilinear. Where it STRADDLES
// a shadow edge (the taps disagree, or the full-res receiver matches none of them) it RE-TRACES this pixel's occlusion
// ray at full-res via the SHARED nwbShadowRqLightVisible -- the identical colored-transmittance physics, now sampled at
// the pixel's own full-res G-buffer surface, so the silhouette lands on the full-res grid (1px, matching geometry).
// Only the silhouette band pays the extra ray, so the cost stays near the half-res trace's.


#include "binding_slots.h"
#include "shadow_rayquery.slangi" // shared trace bindings (occlusion.slangi) + nwbShadowRqLightVisible; g_NwbShadowVisibility is the FULL-res output here


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The HALF-res ray-traced visibility (the trace's output, read here as an SRV). g_NwbShadowVisibility (occlusion.slangi,
// binding NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT) is the FULL-res UAV this pass writes.
[[vk::binding(NWB_SHADOW_RT_BINDING_HALF_VISIBILITY, NWB_SHADOW_RT_SET)]]
Texture2DArray<float4> g_NwbShadowHalfVisibility;

struct NwbShadowResolvePushConstants{
    uint width;          // full-res width
    uint height;         // full-res height
    uint halfWidth;      // half-res width (the ray-traced buffer)
    uint halfHeight;     // half-res height
    uint slotCount;      // active shadow slots this frame (= min(lightCount, NWB_SCENE_SHADOW_SLOT_COUNT)); only these
                         // are processed -- inactive slots stay at the full buffer's cleared "lit" white default.
    float edgeThreshold; // max per-channel spread across the 2x2 half-res taps below which a pixel is "flat" (bilinear);
                         // above it the pixel straddles a shadow edge and is RE-TRACED at full-res.
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowResolvePushConstants> g_NwbShadowResolvePushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The full-res texel a half-res texel point-sampled when it traced (top-left of its 2x2 block), for the tap's geometry.
int2 nwbShadowResolveHalfToFull(const int2 halfTexel){
    return int2(
        min(halfTexel.x * 2, (int)g_NwbShadowResolvePushConstants.width - 1),
        min(halfTexel.y * 2, (int)g_NwbShadowResolvePushConstants.height - 1)
    );
}

// G-buffer normal is stored (n * 0.5 + 0.5); decode + normalize (matches the trace's normal decode).
float3 nwbShadowResolveDecodeNormal(const int2 texel){
    return normalize(g_NwbShadowGBufferNormal.Load(int3(texel, 0)).xyz * 2.0 - 1.0);
}

struct NwbShadowResolveSurfaceBasis{
    float3 worldPosition;
    float3 normal;
    float3 dxWorld;
    float3 dyWorld;
};

bool nwbShadowResolveNeighbourUsable(const int2 texel, const float3 centerNormal){
    if(g_NwbShadowGBufferDepth.Load(int3(texel, 0)).r >= 0.999999)
        return false;
    return dot(centerNormal, nwbShadowResolveDecodeNormal(texel)) > 0.85;
}

// Estimate the receiver-plane world-space footprint of one full-res pixel from the local G-buffer. Edge retrace
// anti-aliases by tracing a few subpixel positions through this basis; if a neighbour crosses a geometry edge, that
// axis falls back to a one-sided or zero derivative so the samples stay tied to the current receiver.
NwbShadowResolveSurfaceBasis nwbShadowResolveLoadSurfaceBasis(const int2 fullTexel){
    const int fw = (int)g_NwbShadowResolvePushConstants.width;
    const int fh = (int)g_NwbShadowResolvePushConstants.height;

    NwbShadowResolveSurfaceBasis basis;
    basis.worldPosition = g_NwbShadowGBufferWorldPosition.Load(int3(fullTexel, 0)).xyz;
    basis.normal = nwbShadowResolveDecodeNormal(fullTexel);
    basis.dxWorld = float3(0.0, 0.0, 0.0);
    basis.dyWorld = float3(0.0, 0.0, 0.0);

    const int2 leftTexel = int2(max(fullTexel.x - 1, 0), fullTexel.y);
    const int2 rightTexel = int2(min(fullTexel.x + 1, fw - 1), fullTexel.y);
    const int2 upTexel = int2(fullTexel.x, max(fullTexel.y - 1, 0));
    const int2 downTexel = int2(fullTexel.x, min(fullTexel.y + 1, fh - 1));

    const bool hasLeft = leftTexel.x != fullTexel.x && nwbShadowResolveNeighbourUsable(leftTexel, basis.normal);
    const bool hasRight = rightTexel.x != fullTexel.x && nwbShadowResolveNeighbourUsable(rightTexel, basis.normal);
    const bool hasUp = upTexel.y != fullTexel.y && nwbShadowResolveNeighbourUsable(upTexel, basis.normal);
    const bool hasDown = downTexel.y != fullTexel.y && nwbShadowResolveNeighbourUsable(downTexel, basis.normal);

    if(hasLeft && hasRight){
        const float3 leftWorld = g_NwbShadowGBufferWorldPosition.Load(int3(leftTexel, 0)).xyz;
        const float3 rightWorld = g_NwbShadowGBufferWorldPosition.Load(int3(rightTexel, 0)).xyz;
        basis.dxWorld = (rightWorld - leftWorld) * 0.5;
    }
    else if(hasRight){
        const float3 rightWorld = g_NwbShadowGBufferWorldPosition.Load(int3(rightTexel, 0)).xyz;
        basis.dxWorld = rightWorld - basis.worldPosition;
    }
    else if(hasLeft){
        const float3 leftWorld = g_NwbShadowGBufferWorldPosition.Load(int3(leftTexel, 0)).xyz;
        basis.dxWorld = basis.worldPosition - leftWorld;
    }

    if(hasUp && hasDown){
        const float3 upWorld = g_NwbShadowGBufferWorldPosition.Load(int3(upTexel, 0)).xyz;
        const float3 downWorld = g_NwbShadowGBufferWorldPosition.Load(int3(downTexel, 0)).xyz;
        basis.dyWorld = (downWorld - upWorld) * 0.5;
    }
    else if(hasDown){
        const float3 downWorld = g_NwbShadowGBufferWorldPosition.Load(int3(downTexel, 0)).xyz;
        basis.dyWorld = downWorld - basis.worldPosition;
    }
    else if(hasUp){
        const float3 upWorld = g_NwbShadowGBufferWorldPosition.Load(int3(upTexel, 0)).xyz;
        basis.dyWorld = basis.worldPosition - upWorld;
    }

    return basis;
}

// RE-TRACE this full-res pixel's occlusion ray for every active light slot and write the crisp colored visibility.
// Uses the pixel's OWN full-res G-buffer surface (not the half-res representative texel), and averages four subpixel
// receiver positions so transparent shadow silhouettes resolve as an anti-aliased full-res edge rather than a
// point-sampled stair step. Identical physics to the half-res trace (shared nwbShadowRqLightVisible).
void nwbShadowResolveRetrace(const uint2 pixel, const int2 fullTexel, const uint slotCount){
    const NwbShadowResolveSurfaceBasis basis = nwbShadowResolveLoadSurfaceBasis(fullTexel);
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    const float2 offsets[4] = {
        float2(-0.25, -0.25),
        float2( 0.25, -0.25),
        float2(-0.25,  0.25),
        float2( 0.25,  0.25)
    };
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0 || (uint)slot >= slotCount)
            continue;
        half3 transmittance = half3(0.0, 0.0, 0.0);
        [unroll] for(uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex){
            const float2 offset = offsets[sampleIndex];
            const float3 sampleWorldPosition = basis.worldPosition + basis.dxWorld * offset.x + basis.dyWorld * offset.y;
            transmittance += nwbShadowRqLightVisible(sampleWorldPosition, basis.normal, g_NwbSceneLights[i]);
        }
        g_NwbShadowVisibility[uint3(pixel, uint(slot))] = float4(saturate(transmittance * half(0.25)), 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RT_RESOLVE_GROUP_SIZE, NWB_SHADOW_RT_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbShadowResolvePushConstants.width || pixel.y >= g_NwbShadowResolvePushConstants.height)
        return;

    const int2 fullTexel = int2(pixel);

    // Only the active shadow slots are processed; inactive slots (slotCount..N) stay at the full buffer's cleared white.
    const uint slotCount = min(g_NwbShadowResolvePushConstants.slotCount, (uint)NWB_SCENE_SHADOW_SLOT_COUNT);

    // Background (no receiver) is fully lit across every active slot -- matches the trace's background default + clear.
    if(g_NwbShadowGBufferDepth.Load(int3(fullTexel, 0)).r >= 0.999999){
        for(uint slot = 0u; slot < slotCount; ++slot)
            g_NwbShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    // 2x2 half-res gather: bilinear fractions + the four tap texels. Same point->half mapping the trace used.
    const int hw = (int)g_NwbShadowResolvePushConstants.halfWidth;
    const int hh = (int)g_NwbShadowResolvePushConstants.halfHeight;
    const float2 src = (float2(pixel) + 0.5) * 0.5 - 0.5; // full pixel centre -> half-res texel-centre space
    const int2 baseTexel = int2(floor(src));              // top-left of the 2x2 half-res gather
    const float2 lerpFrac = src - float2(baseTexel);      // bilinear fractions

    int2 taps[4];
    bool tapValid[4];
    [unroll] for(int j = 0; j < 2; ++j){
        [unroll] for(int i = 0; i < 2; ++i){
            const int idx = j * 2 + i;
            const int2 tap = int2(clamp(baseTexel.x + i, 0, hw - 1), clamp(baseTexel.y + j, 0, hh - 1));
            taps[idx] = tap;
            tapValid[idx] = g_NwbShadowGBufferDepth.Load(int3(nwbShadowResolveHalfToFull(tap), 0)).r < 0.999999;
        }
    }

    // EDGE TEST: the largest per-channel spread of the half-res visibility across the nearby valid taps, over all
    // active slots. A flat interior has ~0 spread (cheap bilinear); a silhouette straddle exceeds edgeThreshold
    // (re-trace). The expanded neighbourhood grows the 2x2 bilinear footprint by one half texel, so low-contrast
    // colored-shadow edges do not leak a visible half-res block.
    float maxSpread = 0.0;
    bool anyBackgroundTap = false;
    [unroll] for(int k = 0; k < 4; ++k)
        anyBackgroundTap = anyBackgroundTap || !tapValid[k];
    for(uint slot = 0u; slot < slotCount; ++slot){
        float3 lo = float3(1.0, 1.0, 1.0);
        float3 hi = float3(0.0, 0.0, 0.0);
        for(int y = -1; y <= 2; ++y){
            for(int x = -1; x <= 2; ++x){
                const int2 tap = int2(clamp(baseTexel.x + x, 0, hw - 1), clamp(baseTexel.y + y, 0, hh - 1));
                const int2 tapFull = nwbShadowResolveHalfToFull(tap);
                if(g_NwbShadowGBufferDepth.Load(int3(tapFull, 0)).r < 0.999999){
                    const float3 v = g_NwbShadowHalfVisibility.Load(int4(tap, (int)slot, 0)).rgb;
                    lo = min(lo, v);
                    hi = max(hi, v);
                }
                else{
                    anyBackgroundTap = true;
                }
            }
        }
        const float3 spread = hi - lo;
        maxSpread = max(maxSpread, max(spread.x, max(spread.y, spread.z)));
    }

    if(anyBackgroundTap || maxSpread > g_NwbShadowResolvePushConstants.edgeThreshold){
        // Shadow-edge pixel: re-trace at full-res so the silhouette resolves on the full-res grid (matching the scene
        // geometry) instead of the half-res 2x2 stair-step a bilinear would leave.
        nwbShadowResolveRetrace(pixel, fullTexel, slotCount);
        return;
    }

    // FLAT region: cheap geometry-stopped bilinear of the half-res taps. Edge-stops (world distance + plane distance +
    // normal similarity) keep the blend from crossing onto a different receiver surface; on the flat interior they are
    // a no-op, but they guard a flat shadow region that sits next to a receiver silhouette.
    const int fw = (int)g_NwbShadowResolvePushConstants.width;
    const int fh = (int)g_NwbShadowResolvePushConstants.height;
    const float3 centerWorld = g_NwbShadowGBufferWorldPosition.Load(int3(fullTexel, 0)).xyz;
    const float3 centerNormal = nwbShadowResolveDecodeNormal(fullTexel);
    const int2 rightFull = int2(min(fullTexel.x + 1, fw - 1), fullTexel.y);
    const int2 downFull = int2(fullTexel.x, min(fullTexel.y + 1, fh - 1));
    const float3 dxWorld = g_NwbShadowGBufferWorldPosition.Load(int3(rightFull, 0)).xyz - centerWorld;
    const float3 dyWorld = g_NwbShadowGBufferWorldPosition.Load(int3(downFull, 0)).xyz - centerWorld;
    const float spacing = max(max(length(dxWorld), length(dyWorld)), 1e-4);
    const float sigmaWorld = spacing * 2.0;
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);
    const float sigmaPlane = max(spacing * 0.75, 1e-4);
    const float invTwoSigmaPlaneSq = 1.0 / (2.0 * sigmaPlane * sigmaPlane);
    const float normalReject = 0.85;

    float weights[4];
    float weightSum = 0.0;
    [unroll] for(int j = 0; j < 2; ++j){
        [unroll] for(int i = 0; i < 2; ++i){
            const int idx = j * 2 + i;
            float weight = 0.0;
            if(tapValid[idx]){
                const int2 tapFull = nwbShadowResolveHalfToFull(taps[idx]);
                const float bilinearWeight =
                    (i == 0 ? (1.0 - lerpFrac.x) : lerpFrac.x) *
                    (j == 0 ? (1.0 - lerpFrac.y) : lerpFrac.y);

                const float3 tapWorld = g_NwbShadowGBufferWorldPosition.Load(int3(tapFull, 0)).xyz;
                const float3 tapNormal = nwbShadowResolveDecodeNormal(tapFull);
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

    // All taps rejected by the geometry stops (an isolated full-res receiver sliver between half-res taps on other
    // planes): the half-res neighbourhood is not representative -> re-trace this pixel at full-res.
    if(weightSum <= 0.0){
        nwbShadowResolveRetrace(pixel, fullTexel, slotCount);
        return;
    }

    const float invWeightSum = 1.0 / weightSum;
    for(uint slot = 0u; slot < slotCount; ++slot){
        half3 sum = half3(0.0, 0.0, 0.0);
        [unroll] for(int k = 0; k < 4; ++k){
            if(weights[k] > 0.0)
                sum += half(weights[k]) * half3(g_NwbShadowHalfVisibility.Load(int4(taps[k], (int)slot, 0)).rgb);
        }
        g_NwbShadowVisibility[uint3(pixel, slot)] = float4(saturate(sum * half(invWeightSum)), 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
