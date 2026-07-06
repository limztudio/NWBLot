// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    hit.transmission = p.transmission;
    return hit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One thread per photon over a GRID_SIDE x GRID_SIDE DispatchRays grid. The photon index decompose + light pick +
// per-target emission + stratified hash jitter + flux + the shared bounce loop + the splat are BYTE-IDENTICAL to the SW kernel
// main() -- the only differences are the 2D->1D photon index (DispatchRaysIndex) and the HW splat handle. The grid
// side == the SW grid side, so photonIndex = y*W + x equals the SW SV_DispatchThreadID index per photon (same
// column/row, same light, same target).
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

    // Per-photon grid cell with a PER-PHOTON deterministic stratified jitter (shared with the SW path so the two stay
    // byte-parallel per photon). Per-photon keeps a single frame's splat from being a rigid lattice (which a flat
    // refractor maps to visible stripes); frame-independent, so a static scene is flicker-free without temporal averaging.
    const uint gridSide = max(g_NwbCausticHwPushConstants.gridSide, 1u);
    const uint column = photonIndex % gridSide;
    const uint row = (photonIndex / gridSide) % gridSide;
    const float2 jitter = nwbCausticEmissionJitter(photonIndex);
    const float2 cell = (float2(column, row) + jitter) / (float)gridSide;

    const NwbCausticEmission emission = nwbCausticEmitPhotonAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    // PHYSICAL per-photon flux (energy conservation): the power through this target's emission domain is
    // lightIrradiance * domainArea, carried by the ~photonCount/emissionTargetCount photons aimed at it, so
    // perPhoton = lightColor * domainArea * emissionTargetCount / photonCount. No fudge constant -- the focused
    // caustic is physically bright while sparse far prism-scatter falls below the exposure floor. Byte-identical to
    // the SW kernel (shared emission.domainArea + the same push fields).
    const float3 lightColor = clamp(light.colorIntensity.xyz, float3(0.0), float3(1.5)) * min(max(light.colorIntensity.w, 0.0), 2.0);
    const float perPhoton = emission.domainArea * (float)g_NwbCausticHwPushConstants.emissionTargetCount / (float)max(g_NwbCausticHwPushConstants.photonCount, 1u);

    NwbCausticPhoton photon;
    photon.origin = emission.origin;
    photon.direction = emission.direction;
    photon.flux = lightColor * perPhoton;
    photon.inside = false;

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticHwSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

