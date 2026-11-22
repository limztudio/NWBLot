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

bool nwbCausticSceneBounds(out float3 boundsMin, out float3 boundsMax, out uint flags){
    boundsMin = float3(g_NwbCausticHwPushConstants.sceneBoundsMinX, g_NwbCausticHwPushConstants.sceneBoundsMinY, g_NwbCausticHwPushConstants.sceneBoundsMinZ);
    boundsMax = float3(g_NwbCausticHwPushConstants.sceneBoundsMaxX, g_NwbCausticHwPushConstants.sceneBoundsMaxY, g_NwbCausticHwPushConstants.sceneBoundsMaxZ);
    flags = g_NwbCausticHwPushConstants.sceneBoundsFlags;
    return flags != NWB_CAUSTIC_SCENE_BOUNDS_INVALID;
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

bool nwbCausticCheckRefractiveBoundary(
    const NwbOpticalTraceConfig config,
    const float3 origin,
    const float3 direction,
    const float minimum,
    const float maximum,
    const uint expectedInstance,
    inout bool hasFirst,
    inout NwbOpticalBootstrapEvent firstEvent,
    inout uint candidateCount,
    inout NwbCausticBoundary boundary,
    inout NwbOpticalInstance outOptical,
    inout NwbOpticalTraceResult result
){
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
        if(!nwbCausticVisitBoundaryCandidate(config, origin, direction, query.CandidateTriangleRayT(), instance, primitive, normal, expectedInstance, hasFirst, firstEvent, candidateCount, boundary, outOptical, result)){
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
    return nwbCausticCheckCsgBoundary(config, g_NwbCausticHwMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, expectedInstance, hasFirst, firstEvent, candidateCount, boundary, outOptical, result);
#else
    return true;
#endif
}

NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
#if NWB_RT_CSG_ENABLED
    query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RT_SHADOW_BASE_INSTANCE_MASK, ray);
#else
    query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RAY_TRACING_ALL_INSTANCE_MASK, ray);
#endif
    while(query.Proceed()){}

    NwbCausticHit hit = (NwbCausticHit)0;
    hit.t = tMax;
    if(query.CommittedStatus() == COMMITTED_TRIANGLE_HIT){
        hit.hit = true;
        hit.instance = query.CommittedInstanceID();
        hit.t = query.CommittedRayT();
        hit.isRefractive = (nwbHeapRtInstanceMaterials(g_NwbCausticHwMaterialContextSlots.sceneSlots.z)[hit.instance].flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
        hit.worldPos = origin + hit.t * direction;
    }
#if NWB_RT_CSG_ENABLED
    const uint contextSlot = g_NwbCausticHwMaterialContextSlots.materialSlots.w;
    const uint materialSlot = g_NwbCausticHwMaterialContextSlots.sceneSlots.z;
    const uint activeOffset = nwbHeapRawBufferUniform(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_BASE_OFFSET);
    const uint activeCount = nwbHeapRawBufferUniform(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_COUNT_OFFSET);
    for(uint index = 0u; index < activeCount; ++index){
        const uint instance = nwbHeapRawBufferUniform(contextSlot).Load(activeOffset + index * NWB_CSG_RAY_ACTIVE_INDEX_BYTES);
        const NwbCausticHit candidate = nwbCausticCsgInstanceClosest(contextSlot, materialSlot, instance, origin, direction, tMin, hit.t);
        if(candidate.failure != 0u)
            return candidate;
        if(candidate.hit)
            hit = candidate;
    }
#endif
    return hit;
}

#if !NWB_RT_CSG_ENABLED
NwbCausticHit nwbCausticTraceBounceZero(float3 origin, float3 direction, float tMin, float tMax){
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RT_REFRACTIVE_INSTANCE_MASK, ray);
    while(query.Proceed()){}

    NwbCausticHit hit = (NwbCausticHit)0;
    hit.t = tMax;
    if(query.CommittedStatus() == COMMITTED_TRIANGLE_HIT){
        hit.hit = true;
        hit.isRefractive = true;
        hit.instance = query.CommittedInstanceID();
        hit.t = query.CommittedRayT();
        hit.worldPos = origin + hit.t * direction;
    }
    if(!hit.hit || !hit.isRefractive)
        return hit;
    if(hit.t > tMin){
        const float occMax = asfloat(asuint(hit.t) - 1u);
        if(occMax >= tMin){
            RayDesc occRay;
            occRay.Origin = origin;
            occRay.Direction = direction;
            occRay.TMin = tMin;
            occRay.TMax = occMax;
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> occQuery;
            occQuery.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, NWB_RT_NON_REFRACTIVE_INSTANCE_MASK, occRay);
            while(occQuery.Proceed()){}
            if(occQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT){
                hit.hit = false;
                hit.isRefractive = false;
            }
        }
    }
    return hit;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_HW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint photonIndex = dispatchThreadID.x;
    if(photonIndex >= g_NwbCausticHwPushConstants.photonCount)
        return;
    if(g_NwbCausticHwPushConstants.emissionTargetCount == 0u || g_NwbCausticHwPushConstants.instanceCount == 0u)
        return;

    NwbSceneLight light;
    uint targetIndex;
    uint localIndex;
    uint localCount;
    uint combo;
    uint totalCombos;
    if(!nwbCausticSelectLightAndTarget(photonIndex, g_NwbCausticHwPushConstants.photonCount, g_NwbCausticHwPushConstants.emissionTargetCount, light, targetIndex, localIndex, localCount, combo, totalCombos))
        return;

    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

    const NwbCausticEmissionTarget target = nwbCausticHwEmissionTargets()[targetIndex];
    const float3 targetAabbMin = target.aabbMin.xyz;
    const float3 targetAabbMax = target.aabbMax.xyz;

    const float2 cell = totalCombos == 1u
        ? NwbCausticPhotonEmission.temporalCell(photonIndex, g_NwbCausticHwPushConstants.gridSide, g_NwbCausticHwPushConstants.frameIndex, g_NwbCausticHwPushConstants.temporalPhaseCount)
        : NwbCausticPhotonEmission.stratifiedBlockCell(localIndex, localCount, g_NwbCausticHwPushConstants.frameIndex, g_NwbCausticHwPushConstants.temporalPhaseCount, combo);

    const NwbCausticEmission emission = NwbCausticPhotonEmission.emitAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    const NwbCausticPhoton photon = NwbCausticPhotonEmission.createPhoton(
        emission, NwbCausticPhotonEmission.lightColor(light.colorIntensity), g_NwbCausticHwPushConstants.emissionTargetCount,
        g_NwbCausticHwPushConstants.photonCount, light.params.y >= NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX, light.position.xyz
    );

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

