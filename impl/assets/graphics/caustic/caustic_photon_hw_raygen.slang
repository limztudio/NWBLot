// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_hw_common.slangi"
#include "../raytrace/constants.h"
// The shared transport: the photon struct, the emission aim, the per-photon stratified hash jitter, the iterative bounce loop. It declares
// the backend hook nwbCausticTraceClosest, defined below over a single TraceRay per segment. Pulled in ONLY here
// (not the closest-hit/miss) so the hook + TraceRay live solely in the raygen, keeping recursion at 1.
#include "caustic_trace.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The HARDWARE backend closest-hit hook the shared bounce loop calls per segment. One TraceRay finds the nearest
// surface (RAY_FLAG_FORCE_OPAQUE: skip any-hit, just commit the closest -- the refractive/opaque distinction is the
// material flag the closest-hit reads, NOT a traversal decision, exactly as the SW trace tracks min-t over all
// geometry then checks isRefractive). The payload the closest-hit/miss fill is converted back to the shared
// NwbCausticHit. Recursion stays 1: the loop, not shader recursion, drives the bounces.
NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    NwbCausticHwPayload p;
    p.worldPos = float3(0.0, 0.0, 0.0);
    p.shadingNormal = float3(0.0, 0.0, 1.0);
    p.transmission = float3(1.0, 1.0, 1.0);
    p.t = tMax;
    p.ior = 1.0;
    p.hit = 0u;
    p.isRefractive = 0u;

    TraceRay(NwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RAY_TRACING_ALL_INSTANCE_MASK, 0u, 0u, 0u, ray, p);

    NwbCausticHit hit;
    hit.hit = (p.hit != 0u);
    hit.isRefractive = (p.isRefractive != 0u);
    hit.t = p.t;
    hit.ior = p.ior;
    hit.worldPos = p.worldPos;
    hit.shadingNormal = p.shadingNormal;
    hit.transmission = half3(p.transmission);
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

