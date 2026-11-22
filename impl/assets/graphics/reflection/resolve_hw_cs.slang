// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1

#if NWB_RT_CSG_ENABLED && NWB_REFLECTION_OPTICAL_TRANSPORT
#define NWB_CSG_RAY_OPTICAL_REUSE 1
#endif

#define NWB_REFLECTION_OPTICAL_TRANSPORT_UNSPECIFIED 2

#if NWB_REFLECTION_OPTICAL_TRANSPORT == NWB_REFLECTION_OPTICAL_TRANSPORT_UNSPECIFIED
#define NWB_OPTICAL_CLOSED_MEDIA 0
#endif

#include "common.slangi"
#include "../raytrace/material_context_slots.slangi"

#define NWB_REFLECTION_OPTICAL_FAST (NWB_REFLECTION_OPTICAL_TRANSPORT == NWB_REFLECTION_OPTICAL_TRANSPORT_FAST && !NWB_RT_CSG_ENABLED)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbReflectionMaterialContext nwbHeapMaterialContextSlots(g_NwbReflectionParameters.materialContextSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbMeshFrameMaterialTypedHeapSlot(){ return g_NwbReflectionMaterialContext.sceneSlots.w; }
uint nwbMeshFrameInstanceHeapSlot(){ return g_NwbReflectionMaterialContext.materialSlots.x; }
uint nwbMeshFrameViewHeapSlot(){ return g_NwbReflectionParameters.viewSlot; }
#include "../mesh/bindless_heap_mesh.slangi"

#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbReflectionResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbReflectionResources.avboitSlots.w
#if NWB_RT_CSG_ENABLED
#define NWB_RT_CSG_CONTEXT_SLOT g_NwbReflectionMaterialContext.materialSlots.w
#define NWB_RT_CSG_MATERIAL_SLOT g_NwbReflectionMaterialContext.sceneSlots.z
#endif
#include "../raytrace/surface_hit.slangi"
#include "../raytrace/surface_lighting.slangi"
#if NWB_REFLECTION_OPTICAL_FAST
#include "../raytrace/optical_bounds.slangi"
#include "../raytrace/optical_transport_constants.h"
#include "../optics/ray_precision.slangi"
#elif NWB_REFLECTION_OPTICAL_TRANSPORT
#include "../raytrace/optical_bounds.slangi"
#include "../raytrace/optical_transport.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_REFLECTION_OPTICAL_FAST


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared uint g_NwbOpticalDeferMaskLow;
groupshared uint g_NwbOpticalDeferMaskHigh;
groupshared uint g_NwbOpticalDeferBaseIndex;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#elif NWB_REFLECTION_OPTICAL_TRANSPORT


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half3 nwbOpticalSurfaceRadiance(const NwbRayTraceSurfaceHit hit, const float3 observerDirection){
    return nwbRayTraceSurfaceRadiance(hit, observerDirection);
}

half3 nwbOpticalEnvironmentRadiance(const float3 direction){
    return nwbReflectionEnvironment(direction);
}

void nwbReflectionRecordOpticalDiagnostics(const NwbOpticalTraceResult result){
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS, result.queryBudgetUnits);
#if NWB_RT_CSG_ENABLED
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_PHYSICAL_QUERIES, result.physicalQueries);
#endif
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_BOOTSTRAP_EVENTS, result.bootstrapEventCount);
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_TRANSPARENT_PATHS, result.transparentPath ? 1u : 0u);
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_TIR_EVENTS, result.tirCount);
    if(result.termination == NWB_OPTICAL_TERMINATION_UNSUPPORTED)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_UNSUPPORTED_PATHS, 1u);
    else if(result.termination == NWB_OPTICAL_TERMINATION_MEDIUM_LIMIT)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_MEDIUM_OVERFLOW_PATHS, 1u);
    else if(result.termination == NWB_OPTICAL_TERMINATION_AMBIGUOUS)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_AMBIGUOUS_PATHS, 1u);
    else if(result.termination == NWB_OPTICAL_TERMINATION_QUERY_LIMIT || result.termination == NWB_OPTICAL_TERMINATION_EVENT_LIMIT || result.termination == NWB_OPTICAL_TERMINATION_RANGE)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_LIMITED_PATHS, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_TRACE_GROUP_SIZE, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID){
#if NWB_REFLECTION_OPTICAL_FAST
    const uint candidates = g_NwbReflectionCounters.Load(NWB_REFLECTION_COUNTER_CANDIDATES);
    const uint rayCount = min(candidates, min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity));
    const uint groupCountX = min((rayCount + NWB_REFLECTION_TRACE_GROUP_SIZE - 1u) / NWB_REFLECTION_TRACE_GROUP_SIZE, NWB_REFLECTION_MAX_GROUP_COUNT_X);
    const uint rayIndex = (groupId.y * groupCountX + groupId.x) * NWB_REFLECTION_TRACE_GROUP_SIZE + groupThreadId.x;
    bool deferToOptical = false;
    uint packedPixel = 0u;
    if(rayIndex < rayCount){
        packedPixel = g_NwbReflectionQueue.Load(rayIndex * NWB_REFLECTION_QUEUE_ENTRY_BYTES);
        const uint pixelCount = g_NwbReflectionParameters.dimensionsAndRoute.x * g_NwbReflectionParameters.dimensionsAndRoute.y;
        const bool glass = packedPixel >= pixelCount;
        const uint linearPixel = glass ? packedPixel - pixelCount : packedPixel;
        const uint2 pixel = uint2(linearPixel % g_NwbReflectionParameters.dimensionsAndRoute.x, linearPixel / g_NwbReflectionParameters.dimensionsAndRoute.x);
        NwbReflectionSurface surface;
        const bool surfaceValid = nwbReflectionLoadSurface(pixel, glass, surface);
        if(surfaceValid && any(surface.weight > half3(0.0h)) && surface.bias < g_NwbReflectionParameters.maxRayDistance){
            const float3 traceOrigin = surface.position + surface.normal * surface.bias;
            const float minDistance = surface.bias;
            const float maxDistance = g_NwbReflectionParameters.maxRayDistance;
            const uint instanceMaterialHeapSlot = g_NwbReflectionMaterialContext.sceneSlots.z;
            const uint opticalSceneHeapSlot = g_NwbReflectionMaterialContext.materialSlots.y;
            const uint opticalInstanceCount = g_NwbReflectionMaterialContext.materialSlots.z;
            const uint maxQueries = min(g_NwbReflectionParameters.maxOpticalQueries, NWB_OPTICAL_MAX_QUERIES);
            if(
                maxQueries == 0u || !all(isfinite(traceOrigin)) || !all(isfinite(surface.direction)) || dot(surface.direction, surface.direction) <= 0.0
                || !isfinite(minDistance) || !isfinite(maxDistance) || minDistance < 0.0 || maxDistance <= minDistance
                || opticalSceneHeapSlot == NWB_BINDLESS_HEAP_INVALID_SLOT
            ){
                deferToOptical = true;
            }
            else{
                const float3 direction = normalize(surface.direction);
                const float directionLengthSquared = dot(direction, direction);
                const uint headerFlags = nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load(NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET);
                const uint transparentCount = nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load(NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET);
                const float3 boundsMin = asfloat(nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET));
                const float3 boundsMax = asfloat(nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET));
                if(
                    !all(isfinite(direction)) || !isfinite(directionLengthSquared) || directionLengthSquared <= 0.0
                    || (headerFlags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID) == 0u
                    || !all(isfinite(boundsMin)) || !all(isfinite(boundsMax)) || any(boundsMin > boundsMax)
                ){
                    deferToOptical = true;
                }
                else{
                    const bool originOutsideBounds = transparentCount == 0u || any(traceOrigin < boundsMin) || any(traceOrigin > boundsMax);
                    const bool exterior = g_NwbReflectionParameters.diagnosticsEnabled != 0u && (transparentCount == 0u || nwbOpticalSegmentOutsideBounds(traceOrigin, direction, maxDistance, boundsMin, boundsMax));
                    const uint requiredQueries = originOutsideBounds ? 1u : 2u;
                    if(maxQueries < requiredQueries){
                        deferToOptical = true;
                    }
                    else{
                        bool originInAir = originOutsideBounds;
                        if(!originInAir){
                            const float tolerance = NWB_OPTICS_POSITION_TOLERANCE_SCALE * max(nwbOpticsPositionUncertainty(boundsMin), nwbOpticsPositionUncertainty(boundsMax));
                            const float escapePad = max(tolerance * 4.0, 1e-3);
                            const float3 toMin = traceOrigin - boundsMin;
                            const float3 toMax = boundsMax - traceOrigin;
                            float bestDist = toMin.x;
                            float3 escapeDir = float3(-1.0, 0.0, 0.0);
                            if(toMax.x < bestDist){ bestDist = toMax.x; escapeDir = float3(1.0, 0.0, 0.0); }
                            if(toMin.y < bestDist){ bestDist = toMin.y; escapeDir = float3(0.0, -1.0, 0.0); }
                            if(toMax.y < bestDist){ bestDist = toMax.y; escapeDir = float3(0.0, 1.0, 0.0); }
                            if(toMin.z < bestDist){ bestDist = toMin.z; escapeDir = float3(0.0, 0.0, -1.0); }
                            if(toMax.z < bestDist){ bestDist = toMax.z; escapeDir = float3(0.0, 0.0, 1.0); }
                            float escapeDist = max(bestDist, 0.0) + escapePad;
                            if(glass){
                                const float3 cameraPos = g_NwbReflectionView.cameraPosition.xyz;
                                if(all(isfinite(cameraPos)) && (any(cameraPos < boundsMin) || any(cameraPos > boundsMax))){
                                    const float3 toCam = cameraPos - traceOrigin;
                                    const float camDist = length(toCam);
                                    if(isfinite(camDist) && camDist > 0.0){
                                        escapeDir = toCam / camDist;
                                        escapeDist = camDist;
                                    }
                                }
                            }
                            const float3 escapeEnd = traceOrigin + escapeDir * escapeDist;
                            if(isfinite(escapeDist) && escapeDist > 0.0 && all(isfinite(escapeEnd)) && (any(escapeEnd < boundsMin) || any(escapeEnd > boundsMax))){
                                RayDesc escapeRay;
                                escapeRay.Origin = traceOrigin;
                                escapeRay.Direction = escapeDir;
                                escapeRay.TMin = 0.0;
                                escapeRay.TMax = escapeDist;
                                RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> escapeQuery;
                                escapeQuery.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, NWB_RT_OPTICAL_TRANSPARENT_INSTANCE_MASK, escapeRay);
                                while(escapeQuery.Proceed()){}
                                originInAir = escapeQuery.CommittedStatus() == COMMITTED_NOTHING;
                            }
                        }
                        if(!originInAir){
                            deferToOptical = true;
                        }
                        else{
                            RayDesc ray;
                            ray.Origin = traceOrigin;
                            ray.Direction = direction;
                            ray.TMin = minDistance;
                            ray.TMax = maxDistance;
                            RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
                            query.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RAY_TRACING_ALL_INSTANCE_MASK, ray);
                            while(query.Proceed()){}
                            if(query.CommittedStatus() != COMMITTED_TRIANGLE_HIT){
                                const half3 radiance = nwbReflectionEnvironment(direction);
                                nwbReflectionStore(pixel, glass, radiance * surface.weight, half(NWB_REFLECTION_SOURCE_FALLBACK));
                                nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_RAYS, 1u);
                                if(g_NwbReflectionParameters.diagnosticsEnabled != 0u)
                                    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_EXTERIOR_ELIGIBLE_RAYS, exterior ? 1u : 0u);
                                nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS, requiredQueries);
                            }
                            else{
                                const uint hitInstance = query.CommittedInstanceID();
                                if(hitInstance >= opticalInstanceCount){
                                    deferToOptical = true;
                                }
                                else{
                                    const uint opticalOffset = NWB_RT_OPTICAL_SCENE_HEADER_BYTES + hitInstance * NWB_RT_OPTICAL_INSTANCE_BYTES;
                                    const uint4 opticalRecord = nwbHeapRawBufferUniform(opticalSceneHeapSlot).Load4(opticalOffset);
                                    if((opticalRecord.w & NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT) != 0u){
                                        deferToOptical = true;
                                    }
                                    else{
                                        const NwbRayTraceSurfaceHit hit = nwbRayTraceReconstructSurfaceHit(
                                            instanceMaterialHeapSlot,
                                            traceOrigin,
                                            direction,
                                            query.CommittedRayT(),
                                            hitInstance,
                                            query.CommittedPrimitiveIndex(),
                                            query.CommittedTriangleBarycentrics(),
                                            query.CommittedWorldToObject3x4()
                                        );
                                        const float segmentDistance = length(hit.position - traceOrigin);
                                        if(
                                            !isfinite(hit.distance) || !all(isfinite(hit.position)) || !all(isfinite(hit.geometricNormal))
                                            || dot(hit.geometricNormal, hit.geometricNormal) == 0.0 || hit.distance <= 0.0
                                            || !isfinite(segmentDistance) || segmentDistance <= 0.0 || segmentDistance > maxDistance
                                        ){
                                            deferToOptical = true;
                                        }
                                        else{
                                            half3 radiance = nwbRayTraceSurfaceRadiance(hit, -direction);
                                            const half rangeWeight = half(1.0h) - half(smoothstep(g_NwbReflectionParameters.distanceFadeStart, g_NwbReflectionParameters.maxRayDistance, segmentDistance));
                                            radiance = lerp(nwbReflectionEnvironment(direction), radiance, rangeWeight);
                                            nwbReflectionStore(pixel, glass, radiance * surface.weight, half(NWB_REFLECTION_SOURCE_HARDWARE));
                                            nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_RAYS, 1u);
                                            if(g_NwbReflectionParameters.diagnosticsEnabled != 0u)
                                                nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_EXTERIOR_ELIGIBLE_RAYS, exterior ? 1u : 0u);
                                            nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS, requiredQueries);
                                            nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_HITS, 1u);
                                            nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, NWB_REFLECTION_COUNTER_SUBTRACT_ONE);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if(groupThreadId.x == 0u){
        g_NwbOpticalDeferMaskLow = 0u;
        g_NwbOpticalDeferMaskHigh = 0u;
        g_NwbOpticalDeferBaseIndex = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if(deferToOptical){
        const uint laneBit = 1u << (groupThreadId.x & 31u);
        uint previousMask;
        if(groupThreadId.x < 32u)
            InterlockedOr(g_NwbOpticalDeferMaskLow, laneBit, previousMask);
        else
            InterlockedOr(g_NwbOpticalDeferMaskHigh, laneBit, previousMask);
    }
    GroupMemoryBarrierWithGroupSync();
    if(groupThreadId.x == 0u){
        const uint totalDeferred = countbits(g_NwbOpticalDeferMaskLow) + countbits(g_NwbOpticalDeferMaskHigh);
        if(totalDeferred != 0u)
            g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_OPTICAL_CANDIDATES, totalDeferred, g_NwbOpticalDeferBaseIndex);
    }
    GroupMemoryBarrierWithGroupSync();
    if(deferToOptical){
        const uint laneBit = 1u << (groupThreadId.x & 31u);
        const uint belowMask = laneBit - 1u;
        const uint localOffset = groupThreadId.x < 32u
            ? countbits(g_NwbOpticalDeferMaskLow & belowMask)
            : countbits(g_NwbOpticalDeferMaskLow) + countbits(g_NwbOpticalDeferMaskHigh & belowMask);
        const uint dstIndex = g_NwbOpticalDeferBaseIndex + localOffset;
        if(dstIndex < g_NwbReflectionParameters.queueCapacity)
            g_NwbReflectionQueue.Store((g_NwbReflectionParameters.queueCapacity + dstIndex) * NWB_REFLECTION_QUEUE_ENTRY_BYTES, packedPixel);
    }
    return;
#else
    const uint candidates =
#if NWB_REFLECTION_OPTICAL_TRANSPORT == 1 && !NWB_RT_CSG_ENABLED
        g_NwbReflectionCounters.Load(NWB_REFLECTION_COUNTER_OPTICAL_CANDIDATES);
#else
        g_NwbReflectionCounters.Load(NWB_REFLECTION_COUNTER_CANDIDATES);
#endif
    const uint rayCount = min(candidates, min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity));
#if NWB_RT_CSG_ENABLED
    const uint rayOffset = g_NwbReflectionPush.rayOffset;
    if(rayOffset >= rayCount)
        return;
    const uint localRayIndex = groupId.x * NWB_REFLECTION_TRACE_GROUP_SIZE + groupThreadId.x;
    if(localRayIndex >= min(rayCount - rayOffset, NWB_REFLECTION_TRACE_SLICE_RAYS))
        return;
    const uint rayIndex = rayOffset + localRayIndex;
#else
    const uint groupCountX = min((rayCount + NWB_REFLECTION_TRACE_GROUP_SIZE - 1u) / NWB_REFLECTION_TRACE_GROUP_SIZE, NWB_REFLECTION_MAX_GROUP_COUNT_X);
    const uint rayIndex = (groupId.y * groupCountX + groupId.x) * NWB_REFLECTION_TRACE_GROUP_SIZE + groupThreadId.x;
    if(rayIndex >= rayCount)
        return;
#endif
    const uint queueIndex =
#if NWB_REFLECTION_OPTICAL_TRANSPORT == 1 && !NWB_RT_CSG_ENABLED
        g_NwbReflectionParameters.queueCapacity + rayIndex;
#else
        rayIndex;
#endif
    const uint packedPixel = g_NwbReflectionQueue.Load(queueIndex * NWB_REFLECTION_QUEUE_ENTRY_BYTES);
    const uint pixelCount = g_NwbReflectionParameters.dimensionsAndRoute.x * g_NwbReflectionParameters.dimensionsAndRoute.y;
    const bool glass = packedPixel >= pixelCount;
    const uint linearPixel = glass ? packedPixel - pixelCount : packedPixel;
    const uint2 pixel = uint2(linearPixel % g_NwbReflectionParameters.dimensionsAndRoute.x, linearPixel / g_NwbReflectionParameters.dimensionsAndRoute.x);
    NwbReflectionSurface surface;
    bool surfaceValid;
    if(glass)
        surfaceValid = nwbReflectionLoadSurface(pixel, true, surface);
    else
        surfaceValid = nwbReflectionLoadSurface(pixel, false, surface);
    if(!surfaceValid || !any(surface.weight > half3(0.0h)) || surface.bias >= g_NwbReflectionParameters.maxRayDistance)
        return;
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_RAYS, 1u);
#if NWB_REFLECTION_OPTICAL_TRANSPORT
    const NwbOpticalTraceConfig config = {
        g_NwbReflectionMaterialContext.sceneSlots.z,
        g_NwbReflectionMaterialContext.materialSlots.y,
        g_NwbReflectionMaterialContext.materialSlots.z,
        g_NwbReflectionParameters.maxOpticalQueries,
        surface.bias,
        g_NwbReflectionParameters.maxRayDistance,
    };
    const float3 traceOrigin = surface.position + surface.normal * surface.bias;
    const bool exterior = nwbOpticalSegmentProvenExterior(config.opticalSceneHeapSlot, traceOrigin, surface.direction, config.minDistance, config.maxDistance, config.maxQueries);
    if(g_NwbReflectionParameters.diagnosticsEnabled != 0u)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_EXTERIOR_ELIGIBLE_RAYS, exterior ? 1u : 0u);
    bool useExteriorFastPath = exterior;
#if NWB_RT_CSG_ENABLED
    if(useExteriorFastPath){
        const uint activeCount = nwbHeapRawBufferUniform(NWB_RT_CSG_CONTEXT_SLOT).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_COUNT_OFFSET);
        useExteriorFastPath = activeCount + 1u <= config.maxQueries;
    }
#endif
    NwbOpticalTraceResult result = (NwbOpticalTraceResult)0;
    if(useExteriorFastPath){
        const NwbRayTraceSurfaceHit hit = nwbRayTraceClosestSurfaceHit(config.instanceMaterialHeapSlot, traceOrigin, surface.direction, config.minDistance, config.maxDistance);
#if NWB_RT_CSG_ENABLED
        result.queryBudgetUnits = hit.queryBudgetUnits;
        result.physicalQueries = hit.physicalQueries;
        if(hit.failure != 0u){
            result.surfaceHit = true;
            result.transparentPath = (hit.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT) != 0u;
            result.termination = (hit.failure & NWB_CSG_RAY_FAILURE_AMBIGUOUS_NORMAL) != 0u ? NWB_OPTICAL_TERMINATION_AMBIGUOUS : NWB_OPTICAL_TERMINATION_UNSUPPORTED;
        }
        else
#else
        result.queryBudgetUnits = 1u;
#endif
        if(!hit.hit){
            result.radiance = nwbOpticalEnvironmentRadiance(surface.direction);
            result.termination = NWB_OPTICAL_TERMINATION_MISS_OUTSIDE;
        }
        else{
            result.surfaceHit = true;
            if(!isfinite(hit.distance) || !all(isfinite(hit.position)) || !all(isfinite(hit.geometricNormal)) || dot(hit.geometricNormal, hit.geometricNormal) == 0.0 || hit.distance <= 0.0)
                result.termination = NWB_OPTICAL_TERMINATION_AMBIGUOUS;
            else{
                const float segmentDistance = length(hit.position - traceOrigin);
                if(!isfinite(segmentDistance) || segmentDistance <= 0.0)
                    result.termination = NWB_OPTICAL_TERMINATION_AMBIGUOUS;
                else if(segmentDistance > config.maxDistance)
                    result.termination = NWB_OPTICAL_TERMINATION_RANGE;
                else{
                    result.distance = segmentDistance;
                    result.radiance = nwbOpticalSurfaceRadiance(hit, -surface.direction);
                    result.termination = NWB_OPTICAL_TERMINATION_OPAQUE;
                }
            }
        }
    }
    else
        result = nwbOpticalTrace(config, traceOrigin, surface.direction);
    nwbReflectionRecordOpticalDiagnostics(result);
    half3 radiance = result.radiance;
    if(!result.transparentPath && result.termination == NWB_OPTICAL_TERMINATION_OPAQUE){
        const half rangeWeight = half(1.0h) - half(smoothstep(g_NwbReflectionParameters.distanceFadeStart, g_NwbReflectionParameters.maxRayDistance, result.distance));
        radiance = lerp(nwbReflectionEnvironment(surface.direction), radiance, rangeWeight);
    }
    nwbReflectionStore(pixel, glass, radiance * surface.weight, half(result.surfaceHit ? NWB_REFLECTION_SOURCE_HARDWARE : NWB_REFLECTION_SOURCE_FALLBACK));
    if(result.surfaceHit){
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_HITS, 1u);
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, NWB_REFLECTION_COUNTER_SUBTRACT_ONE);
    }
#else
    const NwbRayTraceSurfaceHit hit = nwbRayTraceClosestSurfaceHit(g_NwbReflectionMaterialContext.sceneSlots.z, surface.position + surface.normal * surface.bias, surface.direction, surface.bias, g_NwbReflectionParameters.maxRayDistance);
#if NWB_RT_CSG_ENABLED
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS, hit.queryBudgetUnits);
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_PHYSICAL_QUERIES, hit.physicalQueries);
#else
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS, 1u);
#endif
    half3 radiance = nwbReflectionEnvironment(surface.direction);
    if(hit.hit
#if NWB_RT_CSG_ENABLED
        && hit.failure == 0u
#endif
    ){
        const half rangeWeight = half(1.0h) - half(smoothstep(g_NwbReflectionParameters.distanceFadeStart, g_NwbReflectionParameters.maxRayDistance, hit.distance));
        radiance = lerp(radiance, nwbRayTraceSurfaceRadiance(hit, -surface.direction), rangeWeight);
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_HITS, 1u);
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, NWB_REFLECTION_COUNTER_SUBTRACT_ONE);
    }
#if NWB_RT_CSG_ENABLED
    if(hit.failure != 0u)
        radiance = half3(0.0h);
#endif
    nwbReflectionStore(pixel, glass, radiance * surface.weight, half(hit.hit ? NWB_REFLECTION_SOURCE_HARDWARE : NWB_REFLECTION_SOURCE_FALLBACK));
#endif
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

