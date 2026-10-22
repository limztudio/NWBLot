// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_CAUSTIC_PHOTON_TRANSPORT
#error "NWB_CAUSTIC_PHOTON_TRANSPORT is fixed by this photon shader source"
#endif
#define NWB_CAUSTIC_PHOTON_TRANSPORT 1

#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1

#include "caustic_hw_common.slangi"
#include "../raytrace/constants.h"
// Only raygen includes the bounce loop and backend hook, keeping TraceRay recursion at one.
#include "caustic_trace.slangi"
#if NWB_RT_CSG_ENABLED
#define NWB_CSG_RAY_HARDWARE 1
#include "csg_hit.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbOpticalTraceConfig nwbCausticOpticalConfig(){
    NwbOpticalTraceConfig config;
    config.instanceMaterialHeapSlot = g_NwbCausticHwMaterialContextSlots.sceneSlots.z;
    config.opticalSceneHeapSlot = g_NwbCausticHwMaterialContextSlots.materialSlots.y;
    config.opticalInstanceCount = g_NwbCausticHwMaterialContextSlots.materialSlots.z;
    config.maxQueries = NWB_OPTICAL_MAX_QUERIES;
    config.minDistance = 0.0;
    config.maxDistance = NWB_FLOAT_MAX;
    return config;
}

bool nwbCausticCollectRefractiveEvents(
    const NwbOpticalTraceConfig config, const float3 origin, const float3 direction, const float minimum, const float maximum, const bool reverse,
    __ref NwbOpticalBootstrapEvent events[NWB_OPTICAL_MAX_CANDIDATE_EVENTS], out uint eventCount, out NwbCausticBoundary boundary, out float tolerance, inout NwbOpticalTraceResult result
){
    eventCount = 0u;
    boundary = (NwbCausticBoundary)0;
    tolerance = NWB_OPTICS_POSITION_TOLERANCE_SCALE * nwbOpticsPositionUncertainty(origin);
    if(!nwbOpticalConsumeQuery(config, result))
        return false;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = minimum;
    ray.TMax = maximum;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_NON_OPAQUE, NWB_RT_REFRACTIVE_INSTANCE_MASK, ray);
    bool complete = false;
    [loop] for(uint candidate = 0u; candidate <= NWB_OPTICAL_MAX_CANDIDATE_EVENTS; ++candidate){
        if(!query.Proceed()){
            complete = true;
            break;
        }
        if(query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE){
            result.termination = NWB_OPTICAL_TERMINATION_UNSUPPORTED;
            query.Abort();
            return false;
        }
        const uint instance = query.CandidateInstanceID();
        const uint primitive = query.CandidatePrimitiveIndex();
        const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(config.instanceMaterialHeapSlot)[instance];
        const float3 normal = nwbRayTraceTriangleGeometricNormal(material, primitive, query.CandidateWorldToObject3x4());
        if(!nwbCausticAppendBoundary(config, origin, direction, query.CandidateTriangleRayT(), instance, primitive, normal, reverse, events, eventCount, boundary, tolerance, result)){
            query.Abort();
            return false;
        }
    }
    if(!complete){
        result.termination = NWB_OPTICAL_TERMINATION_EVENT_LIMIT;
        query.Abort();
        return false;
    }
#if NWB_RT_CSG_ENABLED
    return nwbCausticCollectCsgEvents(config, g_NwbCausticHwMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, reverse, events, eventCount, boundary, tolerance, result);
#else
    return true;
#endif
}

// Complete the nearest ordinary-geometry query before CSG refinement; the same custom instance record supplies its refractive classification.
NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    NwbCausticHwPayload p;
    p.distance = tMax;
    p.instance = NWB_CAUSTIC_HW_MISS_INSTANCE;

#if NWB_RT_CSG_ENABLED
    // Keep live optical state within the inline CSG query path instead of a native TraceRay continuation.
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RT_SHADOW_BASE_INSTANCE_MASK, ray);
    while(query.Proceed()){}
    if(query.CommittedStatus() == COMMITTED_TRIANGLE_HIT){
        p.distance = query.CommittedRayT();
        p.instance = query.CommittedInstanceID();
    }
#else
    TraceRay(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RAY_TRACING_ALL_INSTANCE_MASK, 0u, 0u, 0u, ray, p);
#endif

    NwbCausticHit hit = (NwbCausticHit)0;
    hit.hit = (p.instance != NWB_CAUSTIC_HW_MISS_INSTANCE);
    if(hit.hit)
        hit.isRefractive = (nwbHeapRtInstanceMaterials(g_NwbCausticHwMaterialContextSlots.sceneSlots.z)[p.instance].flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    hit.instance = p.instance;
    hit.t = p.distance;
    hit.worldPos = hit.hit ? ray.Origin + p.distance * ray.Direction : float3(0.0);
#if NWB_RT_CSG_ENABLED
    const uint contextSlot = g_NwbCausticHwMaterialContextSlots.materialSlots.w;
    const uint materialSlot = g_NwbCausticHwMaterialContextSlots.sceneSlots.z;
    const uint activeOffset = nwbHeapRawBuffer(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_BASE_OFFSET);
    const uint activeCount = nwbHeapRawBuffer(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_COUNT_OFFSET);
    for(uint index = 0u; index < activeCount; ++index){
        const uint instance = nwbHeapRawBuffer(contextSlot).Load(activeOffset + index * NWB_CSG_RAY_ACTIVE_INDEX_BYTES);
        const NwbCausticHit candidate = nwbCausticCsgInstanceClosest(contextSlot, materialSlot, instance, origin, direction, tMin, hit.t);
        if(candidate.failure != 0u)
            return candidate;
        if(candidate.hit)
            hit = candidate;
    }
#endif
    return hit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The hardware raygen mirrors the SW photon sequence over the selected temporal-budget 2D grid.
[shader("raygeneration")] void main(){
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

    const float2 cell = nwbCausticJitteredTemporalCell(photonIndex, g_NwbCausticHwPushConstants.gridSide, g_NwbCausticHwPushConstants.frameIndex, g_NwbCausticHwPushConstants.temporalPhaseCount);

    const NwbCausticEmission emission = nwbCausticEmitPhotonAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    const NwbCausticPhoton photon = nwbCausticCreatePhoton(
        emission, nwbCausticLightColor(light.colorIntensity), g_NwbCausticHwPushConstants.emissionTargetCount,
        g_NwbCausticHwPushConstants.photonCount, light.params.y >= NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX, light.position.xyz
    );

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

