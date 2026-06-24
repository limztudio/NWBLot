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
// avoids self-intersection acne. Returns the per-channel transmittance accumulated by the any-hit stage:
// float3(1) when fully lit, tinted toward zero through transparent occluders, zero behind an opaque one.
float3 nwbShadowLightVisible(const float3 worldPosition, const float3 normal, const NwbSceneLight light){
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

    // A light closer than the self-intersection bias shell leaves no room for an occluder between it
    // and the surface, so treat it as fully visible instead of tracing a degenerate [tMin, tMin] ray
    // (point/spot lights within ~3mm would otherwise collapse to an empty interval and read as lit by
    // luck rather than by construction).
    const float tMin = 0.001;
    const float tEnd = tMax - 0.002;
    if(tEnd <= tMin)
        return float3(1.0, 1.0, 1.0);

    RayDesc ray;
    ray.Origin = worldPosition + normal * tMin;
    ray.Direction = toLight;
    ray.TMin = tMin;
    ray.TMax = tEnd;

    // Seed full transmittance; the any-hit stage multiplies in each transparent occluder's tint and an opaque
    // hit zeroes it + ends the search. The miss stage is a no-op, so whatever survives is the answer. The trace
    // must visit every occluder, so it cannot skip the any-hit or accept the first hit.
    NwbShadowOcclusionPayload payload;
    payload.transmittance = float3(1.0, 1.0, 1.0);
    TraceRay(
        g_NwbShadowTlas,
        RAY_FLAG_NONE,
        0xFFu,
        0u,
        0u,
        0u,
        ray,
        payload
    );
    return payload.transmittance;
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
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = g_NwbShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    // Trace only the lights that hold a shadow slot (params.z >= 0); write each light's visibility into its
    // own slot layer. Lights without a slot are left at the cleared "all lit" white default.
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;

        const float3 transmittance = nwbShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i]);
        g_NwbShadowVisibility[uint3(pixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

