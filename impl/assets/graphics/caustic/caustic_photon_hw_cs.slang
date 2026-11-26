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

#ifndef NWB_CAUSTIC_HW_MODE
#define NWB_CAUSTIC_HW_MODE 0
#endif

#ifndef NWB_RT_CSG_ENABLED
#define NWB_RT_CSG_ENABLED 0
#endif

#define NWB_CAUSTIC_HW_FAST (NWB_CAUSTIC_HW_MODE == 1 && !NWB_RT_CSG_ENABLED)
#define NWB_CAUSTIC_HW_DEFERRED (NWB_CAUSTIC_HW_MODE == 2 && !NWB_RT_CSG_ENABLED)

#include "hw_binding_slots.h"
#include "caustic_photon_common.slangi"
#include "../raytrace/constants.h"
#if NWB_CAUSTIC_HW_FAST
#include "../shadow/shadow_surface.slangi"
#include "../mesh/surface.slangi"
#include "../optics/ray_precision.slangi"
#include "../optics/dielectric.slangi"
#include "../raytrace/optical_scene_constants.h"
#include "../raytrace/optical_transport_constants.h"
#include "../raytrace/optical_bounds.slangi"
#include "../raytrace/surface_reconstruct.slangi"
#else
#include "caustic_trace.slangi"
#if NWB_RT_CSG_ENABLED
#define NWB_CSG_RAY_HARDWARE 1
#include "csg_hit.slangi"
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_CAUSTIC_HW_FAST


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const float NWB_OPTICAL_FACING_EPSILON = 1e-6;

groupshared uint g_NwbCausticDeferMaskLow;
groupshared uint g_NwbCausticDeferMaskHigh;
groupshared uint g_NwbCausticDeferBaseIndex;

struct NwbCausticFastSplat{
    bool valid;
    float3 worldPos;
    half3 flux;
};

bool nwbCausticFastCheckSingleBoundary(
    const uint instanceMaterialHeapSlot,
    const uint opticalSceneHeapSlot,
    const uint opticalInstanceCount,
    const float3 origin,
    const float3 direction,
    const float hitDistance,
    const float3 hitWorldPos,
    const uint hitInstance,
    out float3 outGeometricNormal,
    out bool outEntering
){
    outGeometricNormal = float3(0.0);
    outEntering = false;
    const float tolerance = NWB_OPTICS_POSITION_TOLERANCE_SCALE * max(nwbOpticsPositionUncertainty(origin), nwbOpticsPositionUncertainty(hitWorldPos));
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = max(0.0, hitDistance - tolerance);
    ray.TMax = hitDistance + tolerance;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_NON_OPAQUE, NWB_RT_REFRACTIVE_INSTANCE_MASK, ray);
    bool hasFirst = false;
    while(query.Proceed()){
        if(hasFirst || query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE){
            query.Abort();
            return false;
        }
        const uint instance = query.CandidateInstanceID();
        if(instance != hitInstance || instance >= opticalInstanceCount){
            query.Abort();
            return false;
        }
        const uint opticalOffset = NWB_RT_OPTICAL_SCENE_HEADER_BYTES + instance * NWB_RT_OPTICAL_INSTANCE_BYTES;
        const uint4 opticalRecord = nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load4(opticalOffset);
        const uint mode = opticalRecord.z;
        const uint flags = opticalRecord.w;
        if(
            (mode != NWB_RT_OPTICAL_BOUNDARY_CLOSED_NESTED && mode != NWB_RT_OPTICAL_BOUNDARY_CLOSED_PRIORITY)
            || ((flags & NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT) != 0u && (flags & NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID) == 0u)
        ){
            query.Abort();
            return false;
        }
        const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[instance];
        const float3 normal = nwbRayTraceTriangleGeometricNormal(material, query.CandidatePrimitiveIndex(), query.CandidateWorldToObject3x4());
        const float distance = query.CandidateTriangleRayT();
        const float3 position = origin + direction * distance;
        const float facing = dot(direction, normal);
        if(!isfinite(distance) || !all(isfinite(position)) || !isfinite(facing) || abs(facing) <= NWB_OPTICAL_FACING_EPSILON){
            query.Abort();
            return false;
        }
        hasFirst = true;
        outGeometricNormal = normal;
        outEntering = facing < 0.0;
    }
    return hasFirst;
}

void nwbCausticFastTracePhoton(const NwbCausticPhoton photon, out NwbCausticFastSplat outSplat, out bool outDefer){
    outSplat = (NwbCausticFastSplat)0;
    outDefer = false;
    if(!all(isfinite(photon.origin)))
        return;

    const uint instanceMaterialHeapSlot = g_NwbCausticMaterialContextSlots.sceneSlots.z;
    const uint opticalSceneHeapSlot = g_NwbCausticMaterialContextSlots.materialSlots.y;
    const uint opticalInstanceCount = g_NwbCausticMaterialContextSlots.materialSlots.z;
    if(opticalSceneHeapSlot == NWB_BINDLESS_HEAP_INVALID_SLOT){
        outDefer = true;
        return;
    }
    const uint headerFlags = nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load(NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET);
    const uint transparentCount = nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load(NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET);
    const float3 optMin = asfloat(nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET));
    const float3 optMax = asfloat(nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET));
    const bool opticalBoundsValid = (headerFlags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID) != 0u && all(isfinite(optMin)) && all(isfinite(optMax)) && all(optMin <= optMax);

    float3 origin = photon.origin;
    float3 physicalOrigin = photon.origin;
    float3 direction = photon.direction;
    half3 flux = photon.flux;
    float backShiftDistance = 0.0;
    bool needsBackwardCheck = false;
    if(photon.finiteOrigin){
        const float sourceTolerance = NWB_OPTICS_POSITION_TOLERANCE_SCALE * nwbOpticsPositionUncertainty(origin);
        if(!nwbOpticalHalfLineOutsideBounds(opticalSceneHeapSlot, origin, direction, sourceTolerance)){
            outDefer = true;
            return;
        }
    }
    else{
        float3 bMin;
        float3 bMax;
        uint bFlags;
        if(!opticalBoundsValid || !nwbCausticSceneBounds(bMin, bMax, bFlags) || !all(isfinite(bMin)) || !all(isfinite(bMax)) || any(bMin > bMax)){
            outDefer = true;
            return;
        }
        const float3 combinedMin = transparentCount != 0u ? min(bMin, optMin) : bMin;
        const float3 combinedMax = transparentCount != 0u ? max(bMax, optMax) : bMax;
        const float3 sceneCenter = (combinedMin + combinedMax) * 0.5;
        const float3 sceneHalf = (combinedMax - combinedMin) * 0.5;
        const float backProj = dot(sceneCenter - origin, -direction) + dot(abs(direction), sceneHalf);
        const float sceneMargin = NWB_CAUSTIC_EXTERIOR_MARGIN_MINIMUM + NWB_OPTICS_POSITION_TOLERANCE_SCALE * max(nwbOpticsPositionUncertainty(combinedMin), nwbOpticsPositionUncertainty(combinedMax)) + 1e-3 * length(combinedMax - combinedMin);
        const float backDist = max(0.0, backProj + sceneMargin);
        const float3 shiftedOrigin = origin - direction * backDist;
        if(!isfinite(backDist) || !all(isfinite(shiftedOrigin))){
            outDefer = true;
            return;
        }
        origin = shiftedOrigin;
        backShiftDistance = backDist;
        needsBackwardCheck = (bFlags != NWB_CAUSTIC_SCENE_BOUNDS_EXACT);
    }

    if(max(max(flux.x, flux.y), flux.z) < NWB_CAUSTIC_FLUX_EPSILON)
        return;

    const float maxDist0 = max(NWB_CAUSTIC_TRACE_T_MAX, backShiftDistance + NWB_CAUSTIC_TRACE_T_MAX);
    RayDesc ray0;
    ray0.Origin = origin;
    ray0.Direction = direction;
    ray0.TMin = 0.0;
    ray0.TMax = maxDist0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query0;
    query0.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RT_REFRACTIVE_INSTANCE_MASK, ray0);
    while(query0.Proceed()){}
    if(query0.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return;

    uint hitInstance = query0.CommittedInstanceID();
    float hitT = query0.CommittedRayT();
    float3 hitWorldPos = origin + hitT * direction;
    bool isRefractive = true;
    if(hitT > 0.0){
        const float occMax = asfloat(asuint(hitT) - 1u);
        if(occMax >= 0.0){
            RayDesc occRay;
            occRay.Origin = origin;
            occRay.Direction = direction;
            occRay.TMin = 0.0;
            occRay.TMax = occMax;
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> occQuery;
            occQuery.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, NWB_RT_NON_REFRACTIVE_INSTANCE_MASK, occRay);
            while(occQuery.Proceed()){}
            if(occQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
                return;
        }
    }
    if(needsBackwardCheck){
        RayDesc backRay;
        backRay.Origin = origin;
        backRay.Direction = -direction;
        backRay.TMin = 0.0;
        backRay.TMax = NWB_FLOAT_MAX;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> backQuery;
        backQuery.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, NWB_RAY_TRACING_ALL_INSTANCE_MASK, backRay);
        while(backQuery.Proceed()){}
        if(backQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT){
            outDefer = true;
            return;
        }
    }

    bool inside = false;
    uint activeInstance = 0u;
    half activeIor = half(1.0h);
    half3 activeAbsorption = half3(0.0h);
    uint refractiveCrossings = 0u;
    uint queryCount = needsBackwardCheck ? 2u : 1u;

    [loop] for(uint bounce = 0u; bounce < NWB_CAUSTIC_MAX_BOUNCES; ++bounce){
        if(bounce > 0u){
            if(max(max(flux.x, flux.y), flux.z) < NWB_CAUSTIC_FLUX_EPSILON)
                return;
            if(queryCount + 2u >= NWB_OPTICAL_MAX_QUERIES){
                outDefer = true;
                return;
            }
            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = direction;
            ray.TMin = 0.0;
            ray.TMax = NWB_CAUSTIC_TRACE_T_MAX;
            RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
            query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RAY_TRACING_ALL_INSTANCE_MASK, ray);
            while(query.Proceed()){}
            ++queryCount;
            if(query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
                return;
            hitInstance = query.CommittedInstanceID();
            hitT = query.CommittedRayT();
            hitWorldPos = origin + hitT * direction;
            isRefractive = (nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[hitInstance].flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
        }

        const float segmentDistance = length(hitWorldPos - physicalOrigin);
        if(!isfinite(segmentDistance) || segmentDistance <= 0.0)
            return;
        if(inside)
            flux *= nwbDielectricBeerTransmission(activeAbsorption, segmentDistance);
        if(!isRefractive){
            if(refractiveCrossings == 0u && !inside)
                return;
            outSplat.valid = true;
            outSplat.worldPos = hitWorldPos;
            outSplat.flux = flux;
            return;
        }

        ++queryCount;
        float3 boundaryNormal;
        bool entering;
        if(!nwbCausticFastCheckSingleBoundary(instanceMaterialHeapSlot, opticalSceneHeapSlot, opticalInstanceCount, origin, direction, hitT, hitWorldPos, hitInstance, boundaryNormal, entering)){
            outDefer = true;
            return;
        }

        NwbDielectricInterface interface;
        if(!inside){
            if(!entering){
                outDefer = true;
                return;
            }
            const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[hitInstance];
            NwbShadowHit input = (NwbShadowHit)0;
            input.meshInstanceIndex = material.meshInstanceIndex;
            input.surfaceDispatchId = material.surfaceDispatchId;
            input.materialConstantByteOffset = material.materialConstantByteOffset;
            input.worldPosition = hitWorldPos;
            input.worldNormal = float3(0.0, 1.0, 0.0);
            input.rayDirection = direction;
            const NwbMeshSurface surface = nwbShadowDispatchSurface(material.surfaceDispatchId, input);
            const float ior = float(surface.refractionIor);
            if(!isfinite(ior) || ior < 1.0 || !all(isfinite(float3(surface.shadowAbsorptionTint)))){
                outDefer = true;
                return;
            }
            interface = nwbDielectricTransmit(direction, boundaryNormal, half(1.0h), half(ior));
            if(interface.totalInternalReflection)
                return;
            flux *= half(1.0h) - half(interface.reflectedFraction);
            inside = true;
            activeInstance = hitInstance;
            activeIor = half(ior);
            activeAbsorption = nwbDielectricAbsorptionCoefficient(surface.shadowAbsorptionTint);
        }
        else{
            if(entering || hitInstance != activeInstance){
                outDefer = true;
                return;
            }
            interface = nwbDielectricTransmit(direction, -boundaryNormal, activeIor, half(1.0h));
            if(interface.totalInternalReflection)
                return;
            flux *= half(1.0h) - half(interface.reflectedFraction);
            inside = false;
        }

        ++refractiveCrossings;
        physicalOrigin = hitWorldPos;
        direction = interface.direction;
        const float3 offsetNormal = dot(direction, boundaryNormal) >= 0.0 ? boundaryNormal : -boundaryNormal;
        origin = hitWorldPos + offsetNormal * nwbOpticsPositionUncertainty(hitWorldPos);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#else


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbCausticHwWalkRefractiveCandidates<T : INwbCausticBoundaryVisitor>(
    const float3 origin,
    const float3 direction,
    const float minimum,
    const float maximum,
    __ref T visitor,
    inout NwbOpticalTraceResult result
){
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
        const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(g_NwbCausticMaterialContextSlots.sceneSlots.z)[instance];
        const float3 normal = nwbRayTraceTriangleGeometricNormal(material, primitive, query.CandidateWorldToObject3x4());
        if(!visitor.visit(instance, primitive, query.CandidateTriangleRayT(), normal, result)){
            query.Abort();
            return false;
        }
    }
    if(!complete){
        result.termination = NWB_OPTICAL_TERMINATION_EVENT_LIMIT;
        query.Abort();
        return false;
    }
    return true;
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

    NwbCausticCollectVisitor visitor = (NwbCausticCollectVisitor)0;
    visitor.config = config;
    visitor.origin = origin;
    visitor.direction = direction;
    visitor.reverse = reverse;
    visitor.tolerance = tolerance;
    if(!nwbCausticHwWalkRefractiveCandidates(origin, direction, minimum, maximum, visitor, result))
        return false;
#if NWB_RT_CSG_ENABLED
    if(!nwbCausticWalkCsgRefractiveCrossings(config, g_NwbCausticMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, visitor, result))
        return false;
#endif
    events = visitor.events;
    eventCount = visitor.eventCount;
    boundary = visitor.boundary;
    tolerance = visitor.tolerance;
    return true;
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

    NwbCausticCheckBoundaryVisitor visitor = (NwbCausticCheckBoundaryVisitor)0;
    visitor.config = config;
    visitor.origin = origin;
    visitor.direction = direction;
    visitor.expectedInstance = expectedInstance;
    visitor.hasFirst = hasFirst;
    visitor.firstEvent = firstEvent;
    visitor.candidateCount = candidateCount;
    visitor.boundary = boundary;
    visitor.optical = outOptical;
    if(!nwbCausticHwWalkRefractiveCandidates(origin, direction, minimum, maximum, visitor, result))
        return false;
#if NWB_RT_CSG_ENABLED
    if(!nwbCausticWalkCsgRefractiveCrossings(config, g_NwbCausticMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, visitor, result))
        return false;
#endif
    hasFirst = visitor.hasFirst;
    firstEvent = visitor.firstEvent;
    candidateCount = visitor.candidateCount;
    boundary = visitor.boundary;
    outOptical = visitor.optical;
    return true;
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
        hit.isRefractive = (nwbHeapRtInstanceMaterials(g_NwbCausticMaterialContextSlots.sceneSlots.z)[hit.instance].flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
        hit.worldPos = origin + hit.t * direction;
    }
#if NWB_RT_CSG_ENABLED
    hit = nwbCausticCsgClosestAll(g_NwbCausticMaterialContextSlots.materialSlots.w, g_NwbCausticMaterialContextSlots.sceneSlots.z, origin, direction, tMin, hit);
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


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_HW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID){
#if NWB_CAUSTIC_HW_FAST
    const uint photonIndex = dispatchThreadID.x;
    bool defer = false;
    NwbCausticPhoton photon;
    if(nwbCausticEmitPhoton(photonIndex, photon)){
        NwbCausticFastSplat splat;
        nwbCausticFastTracePhoton(photon, splat, defer);
        if(!defer && splat.valid)
            nwbCausticSplat(splat.worldPos, splat.flux);
    }

    if(groupThreadID.x == 0u){
        g_NwbCausticDeferMaskLow = 0u;
        g_NwbCausticDeferMaskHigh = 0u;
        g_NwbCausticDeferBaseIndex = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if(defer){
        const uint laneBit = 1u << (groupThreadID.x & 31u);
        uint previousMask;
        if(groupThreadID.x < 32u)
            InterlockedOr(g_NwbCausticDeferMaskLow, laneBit, previousMask);
        else
            InterlockedOr(g_NwbCausticDeferMaskHigh, laneBit, previousMask);
    }
    GroupMemoryBarrierWithGroupSync();
    if(groupThreadID.x == 0u){
        const uint totalDeferred = countbits(g_NwbCausticDeferMaskLow) + countbits(g_NwbCausticDeferMaskHigh);
        if(totalDeferred != 0u){
            g_NwbHeapStorageBuffers[g_NwbCausticPushConstants.deferredArgsSlot].InterlockedAdd(12u, totalDeferred, g_NwbCausticDeferBaseIndex);
            const uint endCount = min(g_NwbCausticDeferBaseIndex + totalDeferred, g_NwbCausticPushConstants.deferredCapacity);
            const uint groups = (endCount + NWB_CAUSTIC_HW_GROUP_SIZE - 1u) / NWB_CAUSTIC_HW_GROUP_SIZE;
            uint ignored;
            g_NwbHeapStorageBuffers[g_NwbCausticPushConstants.deferredArgsSlot].InterlockedMax(0u, groups, ignored);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if(defer){
        const uint laneBit = 1u << (groupThreadID.x & 31u);
        const uint belowMask = laneBit - 1u;
        const uint localOffset = groupThreadID.x < 32u
            ? countbits(g_NwbCausticDeferMaskLow & belowMask)
            : countbits(g_NwbCausticDeferMaskLow) + countbits(g_NwbCausticDeferMaskHigh & belowMask);
        const uint dstIndex = g_NwbCausticDeferBaseIndex + localOffset;
        if(dstIndex < g_NwbCausticPushConstants.deferredCapacity)
            g_NwbHeapStorageBuffers[g_NwbCausticPushConstants.deferredQueueSlot].Store(dstIndex * 4u, photonIndex);
    }
#else
#if NWB_CAUSTIC_HW_DEFERRED
    const uint deferredCount = min(nwbHeapRawBufferUniform(g_NwbCausticPushConstants.deferredArgsSlot).Load(12u), g_NwbCausticPushConstants.deferredCapacity);
    if(dispatchThreadID.x >= deferredCount)
        return;
    const uint photonIndex = nwbHeapRawBufferUniform(g_NwbCausticPushConstants.deferredQueueSlot).Load(dispatchThreadID.x * 4u);
#else
    const uint photonIndex = dispatchThreadID.x;
#endif
    NwbCausticPhoton photon;
    if(!nwbCausticEmitPhoton(photonIndex, photon))
        return;

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

