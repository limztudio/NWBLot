// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software-shadow OPAQUE pre-pass (full resolution). One thread per full-res pixel: trace one OPAQUE-only occlusion ray
// per shadowed light and OVERWRITE the visibility with the binary blocker mask -- the software analog of the HW RayQuery
// opaque pass, run before the transparent multiply. The A/B baseline for the adaptive-opaque coarse+resolve path
// (dispatched when NWB_SW_SHADOW_ADAPTIVE_OPAQUE=0). No coarse buffer; the whole grid is traced full-res.


#include "sw_binding_slots.h"

// OPAQUE occluder class: skip transparent occluders -> a binary blocker mask.
#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

#include "sw_shadow_gbuffer.slangi"

// This pass declares its OWN push struct + output before the tracing engine, so the shared traverse concern reads
// g_NwbSwShadowPush.instanceCount (the field-name contract) without a monolithic multiplyMode.
struct NwbSwShadowOpaquePrepassPushConstants{
    uint width;
    uint height;
    uint instanceCount;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowOpaquePrepassPushConstants> g_NwbSwShadowPush;

#include "sw_shadow_ray.slangi"

// The full-res visibility array the deferred lighting samples: this pass writes the binary opaque mask (later
// multiplied by the transparent pass), one layer per shadow slot.
[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowVisibility;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPush.width || pixel.y >= g_NwbSwShadowPush.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    // Background (no geometry) is fully lit; overwrite every slot white so the transparent pass leaves it untouched.
    if(nwbSwShadowIsBackground(texel)){
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);

    // Trace only the lights that hold a shadow slot (params.z >= 0); write each light's visibility into its own slot
    // layer. Lights without a slot are left at the cleared "all lit" white default.
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;
        const half3 transmittance = nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0));
        g_NwbSwShadowVisibility[uint3(pixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
