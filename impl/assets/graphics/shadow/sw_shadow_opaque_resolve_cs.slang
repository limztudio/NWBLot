// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Adaptive-opaque ADAPTIVE RESOLVE (full resolution). The adaptive-opaque analog of the full-res opaque pre-pass: it
// WRITES the binary opaque blocker mask (the later transparent pass multiplies its colored shadow onto it). The coarse
// pass wrote one OPAQUE-only any-hit result (binary 0/1) per block into the coarse buffer; here, per pixel/light, read
// the coarse neighbourhood -- where the binary values AGREE (flat interior: fully lit or fully shadowed) write that
// value, else (a silhouette edge straddles the block) RE-TRACE this pixel full-res. Like the transparent resolve but
// OVERWRITE (it is the mask, not a multiply) and binary, and the edge TEST is DILATED (anti-pop, see
// NWB_SW_SHADOW_OPAQUE_EDGE_DILATE) so a sweeping hard-shadow silhouette is caught full-res before it crosses the next
// coarse sample.


#include "sw_binding_slots.h"

// OPAQUE occluder class: the edge re-trace casts a binary blocker.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

#include "sw_shadow_gbuffer.slangi"

struct NwbSwShadowOpaqueResolvePushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint coarseWidth;
    uint coarseHeight;
    float edgeThreshold;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowOpaqueResolvePushConstants> g_NwbSwShadowPush;

// The coarse concern declares the COARSE input (read here for the neighbourhood) and pulls the tracing engine used for
// the edge re-trace.
#include "sw_shadow_coarse.slangi"

// The full-res visibility array this pass OVERWRITES with the binary opaque mask (the transparent pass multiplies onto it).
[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowVisibility;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPush.width || pixel.y >= g_NwbSwShadowPush.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    // Background is fully lit: overwrite every slot white (matching the full-res opaque pre-pass), so the transparent
    // pass leaves the white background untouched.
    if(nwbSwShadowIsBackground(texel)){
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);

    const float2 coarseCoord = float2(pixel) * (1.0 / float(NWB_SW_SHADOW_COARSE_FACTOR));
    const uint2 c0 = uint2(floor(coarseCoord));
    const uint2 c1 = min(c0 + uint2(1u, 1u), uint2(g_NwbSwShadowPush.coarseWidth - 1u, g_NwbSwShadowPush.coarseHeight - 1u));
    const float2 w = coarseCoord - float2(c0);

    const float edgeThreshold = g_NwbSwShadowPush.edgeThreshold;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slotF = g_NwbSceneLights[i].params.z;
        if(slotF < 0.0)
            continue;
        const uint slot = uint(slotF);

        // 2x2 coarse neighbourhood (binary opaque blocker values written by the coarse pass) -- used for the flat-interior interp.
        const float3 t00 = g_NwbSwShadowCoarse[uint3(c0.x, c0.y, slot)].rgb;
        const float3 t10 = g_NwbSwShadowCoarse[uint3(c1.x, c0.y, slot)].rgb;
        const float3 t01 = g_NwbSwShadowCoarse[uint3(c0.x, c1.y, slot)].rgb;
        const float3 t11 = g_NwbSwShadowCoarse[uint3(c1.x, c1.y, slot)].rgb;

        // Edge TEST over a DILATED coarse neighbourhood (anti-pop, see NWB_SW_SHADOW_OPAQUE_EDGE_DILATE): re-trace where a
        // hard-shadow edge lies within the dilated band of this pixel, so a sweeping edge is caught full-res before it
        // crosses the next sample. The interpolation below still uses ONLY the tight bracketing 2x2 (t00..t11).
        float3 lo = float3(1.0, 1.0, 1.0);
        float3 hi = float3(0.0, 0.0, 0.0);
        const int coarseW = int(g_NwbSwShadowPush.coarseWidth);
        const int coarseH = int(g_NwbSwShadowPush.coarseHeight);
        [unroll] for(int dy = -NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; dy <= 1 + NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; ++dy){
            [unroll] for(int dx = -NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; dx <= 1 + NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; ++dx){
                const int sx = clamp(int(c0.x) + dx, 0, coarseW - 1);
                const int sy = clamp(int(c0.y) + dy, 0, coarseH - 1);
                const float3 s = g_NwbSwShadowCoarse[uint3(uint(sx), uint(sy), slot)].rgb;
                lo = min(lo, s);
                hi = max(hi, s);
            }
        }
        const float range = max(max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);

        float3 transmittance;
        if(range > edgeThreshold){
            // Silhouette edge: re-trace this exact full-res pixel (opaque-only -> binary).
            transmittance = float3(nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0)));
        }
        else{
            // Flat interior: the agreed binary value (bilinear of equal values == that value).
            const float3 tx0 = lerp(t00, t10, w.x);
            const float3 tx1 = lerp(t01, t11, w.x);
            transmittance = lerp(tx0, tx1, w.y);
        }

        g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(transmittance, 1.0); // OVERWRITE -- this IS the opaque mask
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
