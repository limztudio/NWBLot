// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// HARDWARE (RayQuery) SOFT OPAQUE half-res trace. The HW analog of sw_shadow_soft_opaque_cs: one thread per HALF-res
// pixel casts NWB_SW_SHADOW_SOFT_SPP OPAQUE inline-RayQuery occlusion rays CONE-JITTERED inside the cone the light's
// physical source subtends (via shadow_rayquery.slangi's nwbShadowRqLightVisible, already type-aware) and writes the
// averaged single-frame visibility into the SAME half-res soft buffer (shadowSoftHalfA, bound at the trace's visibility-
// output slot) the SW path writes. The SEPARATE shadow_resolve pipeline then denoises + upsamples it into the full-res
// visibility exactly as on the SW path, so the HW opaque shadow becomes a half-res jittered -> temporal -> a-trous ->
// upsampled soft shadow. The half-res grid geometry (SHIFT/FACTOR), SPP count, background=white-per-slot policy, and
// block top-left texel sample are all shared with the SW trace so the downsample/resolve tap alignment is byte-identical.


#include "binding_slots.h"
#include "shadow_rayquery.slangi"
// Pure #defines (no binding/resource declarations) shared with the SW trace: the half-res SHIFT/FACTOR and the per-frame
// SPP, so the HW soft trace's half-res grid + sample count match the SW trace's exactly (no magic numbers).
#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Explicit half-res grid dims + the frame counter seeding the per-pixel low-discrepancy cone-jitter sample (so the
// jittered rays walk the source across frames, decorrelated per pixel). Mirrors NwbSwShadowSoftOpaquePushConstants (minus
// instanceCount, which the HW path does not need -- the TLAS bounds the trace) so the half-res grid guard is structured
// identically and the downsample/resolve tap alignment stays byte-identical.
struct NwbShadowRqSoftPushConstants{
    uint width;
    uint height;
    uint frameIndex;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowRqSoftPushConstants> g_NwbShadowRqSoftPushConstants;


[numthreads(NWB_SHADOW_RT_GROUP_SIZE, NWB_SHADOW_RT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfPixel = dispatchThreadID.xy;
    // The half-res grid is width>>SOFT_SHIFT: guard against the padded tail. The trace samples the block's top-left
    // full-res texel (matching the geometry downsample's + resolve's 2x point sample) -- the SAME grid the SW trace uses.
    const uint halfWidth = (g_NwbShadowRqSoftPushConstants.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) >> NWB_SW_SHADOW_SOFT_SHIFT;
    const uint halfHeight = (g_NwbShadowRqSoftPushConstants.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) >> NWB_SW_SHADOW_SOFT_SHIFT;
    if(halfPixel.x >= halfWidth || halfPixel.y >= halfHeight)
        return;

    const uint2 base = halfPixel << NWB_SW_SHADOW_SOFT_SHIFT;
    const int3 texel = int3(int2(min(base.x, g_NwbShadowRqSoftPushConstants.width - 1u), min(base.y, g_NwbShadowRqSoftPushConstants.height - 1u)), 0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);

    // Background: fully LIT (white) for every slot -- the multiplicative identity the resolve carries and the value the
    // upsample writes for a background full-res pixel, so the two agree at the geometry/background edge. Same depth
    // threshold as the SW trace (NWB_SW_SHADOW_BACKGROUND_DEPTH == the HW full-res trace's 0.999999).
    if(g_NwbShadowGBufferDepth.Load(texel).r >= NWB_SW_SHADOW_BACKGROUND_DEPTH){
        for(uint i = 0u; i < lightCount; ++i){
            const float slot = g_NwbSceneLights[i].params.z;
            if(slot < 0.0)
                continue;
            g_NwbShadowVisibility[uint3(halfPixel, uint(slot))] = float4(1.0, 1.0, 1.0, 1.0);
        }
        return;
    }

    const float3 worldPosition = g_NwbShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;
        // OPAQUE-only jittered occlusion. Each sample is binary; averaging NWB_SW_SHADOW_SOFT_SPP of them per frame --
        // each walking the source via its OWN low-discrepancy 2D point (per pixel, per frame, per sample index) --
        // yields the smooth 0..1 coverage the value-aware resolve needs (a single binary sample would leave a dithered
        // penumbra the a-trous could not smooth without washing the umbra out). Temporal accumulation carries the sampling
        // across frames.
        half3 accum = half3(0.0, 0.0, 0.0);
        [loop] for(uint s = 0u; s < NWB_SW_SHADOW_SOFT_SPP; ++s){
            const float2 sampleJitter = nwbShadowLowDiscrepancy2D(halfPixel, g_NwbShadowRqSoftPushConstants.frameIndex * NWB_SW_SHADOW_SOFT_SPP + s);
            accum += nwbShadowRqLightVisible(worldPosition, normal, g_NwbSceneLights[i], sampleJitter);
        }
        const half3 transmittance = accum * half(1.0 / float(NWB_SW_SHADOW_SOFT_SPP));
        g_NwbShadowVisibility[uint3(halfPixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
