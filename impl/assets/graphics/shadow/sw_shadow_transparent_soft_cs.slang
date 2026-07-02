// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// SOFT TRANSPARENT half-res trace (Stage 5 of the soft-ray-traced-shadow feature, all light types). The COLORED
// (Beer-Lambert / Fresnel) analog of sw_shadow_soft_opaque_cs: one thread per HALF-res pixel casts NWB_SW_SHADOW_SOFT_SPP
// cone-jittered occlusion rays inside the cone the light's physical source subtends and writes the AVERAGED single-frame
// COLORED transmittance (RGB, not a binary) into the parallel half-res soft transparent buffer. Byte-identical to the
// opaque soft trace EXCEPT: (a) the occluder class is TRANSPARENT (below) so nwbSwShadowLightVisible skips opaque blockers
// and integrates only the colored crossings (glass tints), keeping the RGB Beer-Lambert/Fresnel product instead of a
// binary mask; (b) the cone-jitter low-discrepancy sample index is offset by a decorrelation SALT so this estimator
// samples the source at points INDEPENDENT of the opaque soft trace -- the two signals are separately temporally
// denoised (opaque binary Bernoulli vs colored chord-variance RGB product; denoise(A)*denoise(B) != denoise(A*B)), so
// their per-frame noise must not be correlated, and they are folded (multiplied) only at the final full-res upsample.
// The SAME cone jitter (directional angularRadius params2.x / punctual asin(sourceRadius/dist)) means the colored
// penumbra softens for FREE -- the physics is reused verbatim; only the class + the kept-RGB differ. The separate
// shadow_resolve pipeline (RGB variant) then temporally-merges + a-trous-denoises + fold-multiply-upsamples the result.


#include "sw_binding_slots.h"

// TRANSPARENT occluder class: the soft transparent trace skips opaque blockers and integrates the colored crossings only.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_TRANSPARENT

#include "sw_shadow_gbuffer.slangi"

struct NwbSwShadowTransparentSoftPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    // The renderer frame counter, seeding the per-pixel low-discrepancy cone-jitter sample so the jittered rays walk the
    // source across frames (decorrelated per pixel by a hash rotation). The SALT below further decorrelates this stream
    // from the opaque soft trace's.
    uint frameIndex;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentSoftPushConstants> g_NwbSwShadowPush;

// sw_shadow_ray.slangi pulls shadow_sampling.slangi (nwbShadowLowDiscrepancy2D + the cone jitter) transitively.
#include "sw_shadow_ray.slangi"

// The half-res soft TRANSPARENT transmittance buffer (one layer per shadow slot) this pass writes the cone-jittered
// colored sample into; the separate RGB shadow_resolve pipeline denoises + fold-multiplies it onto the full-res
// visibility (which already holds the soft OPAQUE result).
[[vk::binding(NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowTransparentSoftHalf;


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

    // Background: fully LIT / UNTINTED (white) for every slot -- the multiplicative identity the RGB resolve carries and
    // the value the fold-upsample leaves the opaque result untouched at, so the two agree at the geometry/background edge.
    if(nwbSwShadowIsBackground(texel)){
        for(uint i = 0u; i < lightCount; ++i){
            const float slot = g_NwbSceneLights[i].params.z;
            if(slot < 0.0)
                continue;
            g_NwbSwShadowTransparentSoftHalf[uint3(halfPixel, uint(slot))] = float4(1.0, 1.0, 1.0, 1.0);
        }
        return;
    }

    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;
        // TRANSPARENT-only jittered colored transmittance. Each sample is the RGB Beer-Lambert/Fresnel product through the
        // colored occluders along its jittered ray; averaging NWB_SW_SHADOW_SOFT_SPP of them per frame -- each walking the
        // source via its OWN low-discrepancy 2D point (per pixel, per frame, per sample index) -- yields the smooth colored
        // penumbra the RGB value-aware resolve needs. The SALT offset decorrelates this stream from the opaque soft trace
        // (independent estimators, separately denoised).
        half3 accum = half3(0.0, 0.0, 0.0);
        [loop] for(uint s = 0u; s < NWB_SW_SHADOW_SOFT_SPP; ++s){
            const float2 sampleJitter = nwbShadowLowDiscrepancy2D(halfPixel, g_NwbSwShadowPush.frameIndex * NWB_SW_SHADOW_SOFT_SPP + s + NWB_SW_SHADOW_TRANSPARENT_JITTER_SALT);
            accum += nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], sampleJitter);
        }
        const half3 transmittance = accum * half(1.0 / float(NWB_SW_SHADOW_SOFT_SPP));
        g_NwbSwShadowTransparentSoftHalf[uint3(halfPixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

