// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1

#include "caustic_hw_common.slangi"
#include "../raytrace/constants.h"
// Only raygen includes the bounce loop and backend hook, keeping TraceRay recursion at one.
#include "caustic_trace.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// HW closest-hit hook per bounce segment: one TraceRay commits nearest (FORCE_OPAQUE); refractive/opaque is a material flag, not a traversal decision (as in the SW min-t trace). Loop-driven bounces, recursion stays 1.
NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    NwbCausticHwPayload p;
    p.normalAndDistance = float4(0.0, 0.0, 1.0, tMax);
    p.transmissionAndIor = float4(1.0);
    p.hit = 0u;
    p.isRefractive = 0u;

    TraceRay(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RAY_TRACING_ALL_INSTANCE_MASK, 0u, 0u, 0u, ray, p);

    NwbCausticHit hit;
    hit.hit = (p.hit != 0u);
    hit.isRefractive = (p.isRefractive != 0u);
    hit.t = p.normalAndDistance.w;
    hit.ior = p.transmissionAndIor.w;
    hit.worldPos = hit.hit ? ray.Origin + p.normalAndDistance.w * ray.Direction : float3(0.0);
    hit.shadingNormal = p.normalAndDistance.xyz;
    hit.transmission = half3(p.transmissionAndIor.xyz);
    return hit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The hardware raygen mirrors the SW photon sequence over the selected temporal-budget 2D grid.
[shader("raygeneration")]
void main(){
    const uint photonIndex = DispatchRaysIndex().y * DispatchRaysDimensions().x + DispatchRaysIndex().x;
    if(photonIndex >= g_NwbCausticHwPushConstants.photonCount)
        return;
    if(g_NwbCausticHwPushConstants.emissionTargetCount == 0u || g_NwbCausticHwPushConstants.instanceCount == 0u)
        return;

    NwbSceneLight light;
    if(!nwbCausticPickLight(photonIndex, light))
        return;

    // Photons travel along the light's emission axis.
    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

    // Hash target selection to decorrelate it from the photon grid.
    const uint targetIndex = nwbCausticHashU32(photonIndex) % g_NwbCausticHwPushConstants.emissionTargetCount;
    const NwbCausticEmissionTarget target = nwbCausticHwEmissionTargets()[targetIndex];
    const float3 targetAabbMin = target.aabbMin.xyz;
    const float3 targetAabbMax = target.aabbMax.xyz;

    const float2 cell = nwbCausticJitteredTemporalCell(
        photonIndex,
        g_NwbCausticHwPushConstants.gridSide,
        g_NwbCausticHwPushConstants.frameIndex,
        g_NwbCausticHwPushConstants.temporalPhaseCount
    );

    const NwbCausticEmission emission = nwbCausticEmitPhotonAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    const NwbCausticPhoton photon = nwbCausticCreatePhoton(
        emission,
        nwbCausticLightColor(light.colorIntensity),
        g_NwbCausticHwPushConstants.emissionTargetCount,
        g_NwbCausticHwPushConstants.photonCount
    );

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

