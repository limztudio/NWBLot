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
// avoids self-intersection acne. Returns the per-channel visibility the engine integrates over the volume path
// (transmittance * exp(-opticalDepth)): float3(1) when fully lit, tinted toward zero through transparent
// occluders, zero behind an opaque one.
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

    // Seed full visibility: the any-hit stage folds in each transparent surface crossing (Fresnel into
    // transmittance + signed Beer-Lambert into opticalDepth) and an opaque hit zeroes the transmittance + ends the
    // search. The miss stage is a no-op, so whatever survives is the answer. The trace must visit every occluder, so
    // it cannot skip the any-hit or accept the first hit.
    NwbShadowOcclusionPayload payload;
    payload.transmittance = float3(1.0, 1.0, 1.0);
    payload.opticalDepth = float3(0.0, 0.0, 0.0);
    payload.opticalDepthSign = float3(0.0, 0.0, 0.0);
    payload.crossingTSum = 0.0;
    payload.crossingCount = 0.0;
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
    // Finalize: the Fresnel-transmitted product times the Beer-Lambert attenuation integrated over the true
    // entry->exit path. The signed-t optical depth carries an absolute-t bias from any UNBALANCED crossing (a
    // tangential silhouette clip with no matching face); subtract tBias * opticalDepthSign to cancel it. The
    // correction is INERT for balanced occluders (opticalDepthSign == 0 there, so the chord is untouched) and
    // removes an unbalanced clip's coeff*t spike (exactly for a lone clip; bounded by the crossing spread for a
    // clip riding alongside real crossings). tBias is the mean crossing distance. max(.,0) then clamps a residual
    // negative partial (a physical path length is non-negative; an early-terminated trace can leave one, which
    // would make exp(-veryNegative)=Inf -> 0*Inf=NaN -> magenta); saturate guards [0,1]. Matches the SW finalize.
    const float tBias = (payload.crossingCount > 0.0) ? (payload.crossingTSum / payload.crossingCount) : 0.0;
    const float3 opticalDepth = payload.opticalDepth - tBias * payload.opticalDepthSign;
    return saturate(payload.transmittance * exp(-max(opticalDepth, float3(0.0, 0.0, 0.0))));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[shader("raygeneration")]
void main(){
    // HALF-RESOLUTION dispatch: one occlusion ray per half-res pixel (1/4 the rays). `pixel` is a half-res coord and the
    // visibility buffer bound here is the half-res Texture2DArray; the G-buffer is full-res, so each half-res pixel reads
    // its representative surface at the top-left of its 2x2 full-res block. The shadow_upsample_cs pass then edge-aware
    // upsamples this into the full-res visibility buffer the lighting samples.
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    if(pixel.x >= dimensions.x || pixel.y >= dimensions.y)
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

