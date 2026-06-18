// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SCENE_SHADING_SET NWB_SHADOW_RT_SET
#define NWB_SCENE_SHADING_BINDING NWB_SHADOW_RT_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SHADOW_RT_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SHADOW_RT_BINDING_LIGHT_LIST

#include "occlusion.slangi"
#include "../scene/buffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per light: cast one occlusion ray from the surface toward the light. Directional lights aim along the
// toward-light axis to "infinity"; point/spot lights aim at the light position with tMax = the distance,
// so geometry past the light does not shadow the surface. Bias along the geometric normal + a small tMin
// avoids self-intersection acne.
bool nwbShadowLightVisible(const float3 worldPosition, const float3 normal, const NwbSceneLight light){
    float3 toLight;
    float tMax;
    if(light.params.y < 0.5){
        toLight = normalize(light.direction.xyz);
        tMax = 1.0e7;
    }
    else{
        const float3 delta = light.position.xyz - worldPosition;
        tMax = length(delta);
        toLight = (tMax > 1.0e-5) ? (delta / tMax) : float3(0.0, 1.0, 0.0);
    }

    RayDesc ray;
    ray.Origin = worldPosition + normal * 0.001;
    ray.Direction = toLight;
    ray.TMin = 0.001;
    ray.TMax = max(tMax - 0.002, 0.001);

    NwbShadowOcclusionPayload payload;
    payload.visibility = 0.0;
    TraceRay(
        g_NwbShadowTlas,
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFFu,
        0u,
        0u,
        0u,
        ray,
        payload
    );
    return payload.visibility > 0.5;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[shader("raygeneration")]
void main(){
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    if(pixel.x >= dimensions.x || pixel.y >= dimensions.y)
        return;

    const int3 texel = int3(int2(pixel), 0);

    // Background (no geometry) is fully lit; the lighting pass ignores it anyway.
    if(g_NwbShadowGBufferDepth.Load(texel).r >= 0.999999){
        g_NwbShadowVisibility[pixel] = 0xFFFFFFFFu;
        return;
    }

    const float3 worldPosition = g_NwbShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SHADOW_RT_MAX_SHADOWED_LIGHTS);
    uint visibilityMask = 0u;
    for(uint i = 0u; i < lightCount; ++i){
        if(nwbShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i]))
            visibilityMask |= (1u << i);
    }

    g_NwbShadowVisibility[pixel] = visibilityMask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

