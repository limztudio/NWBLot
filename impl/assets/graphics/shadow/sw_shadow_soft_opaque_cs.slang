// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// SOFT OPAQUE half-res trace (soft-ray-traced-shadow feature, all light types). One thread per HALF-res pixel: for each
// light holding a shadow slot, cast NWB_SW_SHADOW_SOFT_SPP OPAQUE occlusion rays CONE-JITTERED inside the cone the
// light's physical source subtends and write the averaged single-frame visibility into the half-res soft buffer. A
// DIRECTIONAL light jitters inside its constant angular radius (params2.x); a POINT/SPOT light inside the distance-
// dependent half-angle asin(params2.y/dist) its source sphere (radius params2.y) subtends -- both handled inside
// nwbSwShadowLightVisible, so this pass is light-type-agnostic and just processes every slot light. The separate
// shadow_resolve pipeline then denoises + upsamples the result into the full-res visibility. The jitter is the per-
// pixel/per-frame low-discrepancy sample, seeded by the half-res pixel + frameIndex so each pixel walks the source over
// frames while neighbours stay decorrelated (the denoise then averages independent estimates).


#include "sw_binding_slots.h"

// OPAQUE occluder class: the soft trace is opaque-only.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

#include "sw_shadow_gbuffer.slangi"

struct NwbSwShadowSoftOpaquePushConstants{
    uint width;
    uint height;
    uint instanceCount;
    // The renderer frame counter, seeding the per-pixel low-discrepancy cone-jitter sample so the jittered rays walk
    // the source across frames (decorrelated per pixel by a hash rotation).
    uint frameIndex;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowSoftOpaquePushConstants> g_NwbSwShadowPush;

// sw_shadow_ray.slangi pulls shadow_sampling.slangi (nwbShadowLowDiscrepancy2D + the cone jitter) transitively.
#include "sw_shadow_ray.slangi"

// The half-res soft opaque visibility buffer (one layer per shadow slot) this pass writes the cone-jittered sample
// into; the separate shadow_resolve pipeline denoises + upsamples it into the full-res visibility.
[[vk::binding(NWB_SW_SHADOW_BINDING_SOFT_HALF, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowSoftHalf;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfPixel = dispatchThreadID.xy;
    // The half-res grid is width>>SOFT_SHIFT: guard against the padded tail. The trace samples the block's top-left
    // full-res texel (matching the geometry downsample's + resolve's 2x point sample).
    const uint halfWidth = (g_NwbSwShadowPush.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) >> NWB_SW_SHADOW_SOFT_SHIFT;
    const uint halfHeight = (g_NwbSwShadowPush.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) >> NWB_SW_SHADOW_SOFT_SHIFT;
    if(halfPixel.x >= halfWidth || halfPixel.y >= halfHeight)
        return;

    const uint2 base = halfPixel << NWB_SW_SHADOW_SOFT_SHIFT;
    const int3 texel = int3(int2(min(base.x, g_NwbSwShadowPush.width - 1u), min(base.y, g_NwbSwShadowPush.height - 1u)), 0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);

    // Background: fully LIT (white) for every slot -- the multiplicative identity the resolve carries and the value the
    // upsample writes for a background full-res pixel, so the two agree at the geometry/background edge.
    if(nwbSwShadowIsBackground(texel)){
        for(uint i = 0u; i < lightCount; ++i){
            const float slot = g_NwbSceneLights[i].params.z;
            if(slot < 0.0)
                continue;
            g_NwbSwShadowSoftHalf[uint3(halfPixel, uint(slot))] = float4(1.0, 1.0, 1.0, 1.0);
        }
        return;
    }

    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;
        // OPAQUE-only jittered occlusion. Each sample is binary; averaging NWB_SW_SHADOW_SOFT_SPP of them per frame --
        // each walking the source via its OWN low-discrepancy 2D point (per pixel, per frame, per sample index) --
        // yields the smooth 0..1 coverage the value-aware resolve needs (a single binary sample would leave a dithered
        // penumbra the a-trous could not smooth without washing the umbra out). Stage 3's temporal accumulation
        // replaces this per-frame loop.
        half3 accum = half3(0.0, 0.0, 0.0);
        [loop] for(uint s = 0u; s < NWB_SW_SHADOW_SOFT_SPP; ++s){
            const float2 sampleJitter = nwbShadowLowDiscrepancy2D(halfPixel, g_NwbSwShadowPush.frameIndex * NWB_SW_SHADOW_SOFT_SPP + s);
            accum += nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], sampleJitter);
        }
        const half3 transmittance = accum * half(1.0 / float(NWB_SW_SHADOW_SOFT_SPP));
        g_NwbSwShadowSoftHalf[uint3(halfPixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

