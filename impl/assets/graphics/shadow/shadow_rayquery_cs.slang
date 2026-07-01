// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware RayQuery (inline ray tracing) FULL-resolution OPAQUE shadow trace. One compute thread per output pixel
// inline-traces each light's occlusion ray against the TLAS (shared nwbShadowRqLightVisible) and writes a binary
// "any opaque hit -> shadowed" mask straight into the full-res visibility array. The colored TRANSPARENT shadow is
// cast separately by the software traversal (shadow_sw_traversal_cs) and multiplied onto this opaque mask -- the
// hybrid split, because the hardware intersector and the software Moeller-Trumbore disagree by +/-1 crossing at
// grazing silhouettes (invisible for a binary opaque test, but it corrupts the colored Beer-Lambert chord).


#include "binding_slots.h"
#include "shadow_rayquery.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Frame counter for the soft directional cone-jitter, seeding the per-pixel low-discrepancy sample so the single
// jittered opaque ray walks the sun disk across frames. A soft directional light softens THIS full-res HW opaque
// trace directly (per-frame shimmer expected this stage; the SW path adds the half-res trace + a-trous denoise, the
// HW path here softens at full res without the denoise -- see the renderer notes). A directional light with a zero
// angular radius (params2.x == 0) cone-jitters to the axis exactly -> the unchanged hard shadow, so this is a no-op
// for a point-source sun.
struct NwbShadowRqPushConstants{
    uint frameIndex;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowRqPushConstants> g_NwbShadowRqPushConstants;


[numthreads(NWB_SHADOW_RT_GROUP_SIZE, NWB_SHADOW_RT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    // FULL-RESOLUTION: one inline-RayQuery occlusion ray per output pixel, written straight into the full-res
    // visibility array the deferred lighting samples -- structurally identical to the software traversal's main()
    // (shadow_sw_traversal_cs), so the hardware and software backends produce the same per-pixel result.
    uint width;
    uint height;
    uint slotElems;
    g_NwbShadowVisibility.GetDimensions(width, height, slotElems);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= width || pixel.y >= height)
        return;

    const int3 texel = int3(int2(pixel), 0);

    // Background (no geometry) is fully lit; the lighting pass ignores it anyway.
    if(g_NwbShadowGBufferDepth.Load(texel).r >= 0.999999){
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = g_NwbShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    // Trace only the lights that hold a shadow slot (params.z >= 0); write each light's visibility into its own slot
    // layer. Lights without a slot are left at the cleared "all lit" white default.
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;

        // Per-pixel low-discrepancy cone-jitter sample (hard for a zero-radius directional / any point/spot light).
        const float2 jitter = nwbShadowLowDiscrepancy2D(pixel, g_NwbShadowRqPushConstants.frameIndex);
        const half3 transmittance = nwbShadowRqLightVisible(worldPosition, normal, g_NwbSceneLights[i], jitter);
        g_NwbShadowVisibility[uint3(pixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

