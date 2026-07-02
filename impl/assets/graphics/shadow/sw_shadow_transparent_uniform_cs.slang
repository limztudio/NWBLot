// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// UNIFORM transparent multiply at HALF resolution (the non-adaptive Stage-1 baseline). One transparent trace per 2x2
// block, folded onto EACH of the block's four full-res pixels' own opaque mask -- so the opaque (hard) shadow keeps full
// resolution while the soft colored transparent shadow is shared 2x2. Kept for A/B comparison against the adaptive path
// (dispatched when NWB_SW_SHADOW_ADAPTIVE=0).
//
// PRESERVED-AS-IS: the block stride is a LITERAL *2u / 2x2 fold, NOT NWB_SW_SHADOW_COARSE_FACTOR. This pass predates the
// adaptive coarse factor and always downscaled by exactly 2; at the current COARSE_SHIFT=2 the dispatch grid is the
// COARSE grid (quarter-res), so a *2u block covers only half the screen. That is a PRE-EXISTING latent property of this
// A/B-only baseline; it is carried forward byte-for-byte here (this is a behavior-preserving decomposition, not a fix).


#include "sw_binding_slots.h"

// TRANSPARENT occluder class: colored transmittance multiplied onto the opaque mask.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"

struct NwbSwShadowTransparentUniformPushConstants{
    uint width;
    uint height;
    uint instanceCount;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentUniformPushConstants> g_NwbSwShadowPush;

#include "sw_shadow_ray.slangi"

// The full-res visibility array: read the opaque mask, fold the block's transmittance onto each full-res pixel, write back.
[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowVisibility;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPush.width || pixel.y >= g_NwbSwShadowPush.height)
        return;

    // HALF-res: each thread owns a 2x2 full-res block, sampling the G-buffer at (and tracing once from) the block's
    // top-left full-res texel. LITERAL *2u -- see the preserved-as-is note above.
    const uint2 base = pixel * 2u;
    if(base.x >= g_NwbSwShadowPush.width || base.y >= g_NwbSwShadowPush.height)
        return;
    const int3 texel = int3(int2(base), 0);

    // Background (no geometry) is fully lit; the opaque pass already wrote the (white) background, so leave it untouched.
    if(nwbSwShadowIsBackground(texel))
        return;

    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);

    // Trace only the lights that hold a shadow slot (params.z >= 0); fold each light's visibility onto its own slot layer.
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;

        // One transparent traversal (the expensive part) per thread -- shared across the 2x2 block.
        const half3 transmittance = nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0));

        // Fold this block's transparent transmittance onto EACH full-res pixel's own opaque binary mask, so the
        // opaque (hard) shadow keeps full resolution while the soft colored transparent shadow is shared 2x2.
        for(uint dy = 0u; dy < 2u; ++dy)
        for(uint dx = 0u; dx < 2u; ++dx){
            const uint2 sub = base + uint2(dx, dy);
            if(sub.x >= g_NwbSwShadowPush.width || sub.y >= g_NwbSwShadowPush.height)
                continue;
            const float3 opaqueMask = g_NwbSwShadowVisibility[uint3(sub, uint(slot))].rgb;
            g_NwbSwShadowVisibility[uint3(sub, uint(slot))] = float4(opaqueMask * float3(transmittance), 1.0);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

