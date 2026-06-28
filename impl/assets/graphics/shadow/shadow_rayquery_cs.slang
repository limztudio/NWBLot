// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware RayQuery (inline ray tracing) HALF-resolution shadow trace. One compute thread per half-res shadow pixel
// inline-traces each light's occlusion ray against the TLAS (shared nwbShadowRqLightVisible), gathering the colored
// transparent crossings into a LOCAL per-occluder accumulator and stopping at an opaque hit. Output is the half-res
// visibility Texture2DArray that the edge-adaptive shadow_resolve_cs then reconstructs to full-res (re-tracing only
// the silhouette pixels, so the cheap half-res interior + the crisp full-res edge share this exact trace).


#include "binding_slots.h"
#include "shadow_rayquery.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RT_GROUP_SIZE, NWB_SHADOW_RT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    // HALF-RESOLUTION: one thread per half-res pixel; the output visibility array IS the half-res buffer, so its
    // dimensions bound the dispatch. Each half-res pixel reads its representative full-res G-buffer surface at the
    // top-left of its 2x2 block (pixel * 2). shadow_resolve_cs then edge-adaptively reconstructs to full-res.
    uint halfWidth;
    uint halfHeight;
    uint slotElems;
    g_NwbShadowVisibility.GetDimensions(halfWidth, halfHeight, slotElems);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= halfWidth || pixel.y >= halfHeight)
        return;

    const int3 texel = int3(int2(pixel) * 2, 0);

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

        const float3 transmittance = nwbShadowRqLightVisible(worldPosition, normal, g_NwbSceneLights[i]);
        g_NwbShadowVisibility[uint3(pixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
