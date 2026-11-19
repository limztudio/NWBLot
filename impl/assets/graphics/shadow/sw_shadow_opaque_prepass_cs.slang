// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#define NWB_SW_SHADOW_OCCLUDER NWB_SW_SHADOW_OCCLUDER_OPAQUE

#include "sw_shadow_gbuffer.slangi"

#include "sw_shadow_ray.slangi"

#define g_NwbSwShadowVisibility nwbHeapStorageImage2DArrayFloat4(g_NwbSwShadowPush.visibilityStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPush.width || pixel.y >= g_NwbSwShadowPush.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    if(nwbSwShadowIsBackground(texel)){
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = nwbSwShadowLoadWorldPosition(texel);
    const float3 normal = nwbSwShadowLoadNormal(texel);

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

