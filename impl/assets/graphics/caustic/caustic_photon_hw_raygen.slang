#include "caustic_hw_common.slangi"
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

    TraceRay(g_NwbCausticHwTlas, RAY_FLAG_FORCE_OPAQUE, 0xFFu, 0u, 0u, 0u, ray, p);

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


// One thread per photon over a GRID_SIDE x (GRID_SIDE/2) DispatchRays grid (the 2x temporal-reuse half budget). The
// photon index decompose + light pick + per-target emission + stratified hash jitter + flux + the shared bounce loop +
// the splat are BYTE-IDENTICAL to the SW kernel main() -- the only differences are the 2D->1D photon index
// (DispatchRaysIndex) and the HW splat handle. The grid side == the SW grid side, so photonIndex = y*W + x equals the
// SW SV_DispatchThreadID index per photon (same column/row, same light, same target); the row count is halved so the
// dispatch launches exactly the half-budget photonCount (no wasted early-out threads).
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

    // The light's principal travel direction (FROM the light); photons travel along the emission axis.
    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

    // Per-target emission (the make-or-break aim): assign each photon to ONE refractive target, aim it at that
    // target's world AABB. The target is picked by a HASH of photonIndex (not photonIndex % count) to decorrelate it
    // from the photonIndex-derived cell below -- a plain modulo gives each target a diagonal-striped grid subset that
    // images into diagonal caustic streaks shooting far across the receiver (see caustic_photon_sw_cs.slang). Shared
    // with the SW path so the two stay byte-parallel per photon.
    const uint targetIndex = nwbCausticHashU32(photonIndex) % g_NwbCausticHwPushConstants.emissionTargetCount;
    const float3 targetAabbMin = g_NwbCausticHwEmissionTargets[targetIndex].aabbMin.xyz;
    const float3 targetAabbMax = g_NwbCausticHwEmissionTargets[targetIndex].aabbMax.xyz;

    const float2 cell = nwbCausticJitteredCheckerboardCell(
        photonIndex,
        g_NwbCausticHwPushConstants.gridSide,
        g_NwbCausticHwPushConstants.frameIndex
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

