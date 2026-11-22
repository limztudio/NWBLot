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
#if NWB_REFLECTION_OPTICAL_TRANSPORT
#include "../raytrace/optical_bounds.slangi"
#include "../raytrace/optical_transport.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_REFLECTION_OPTICAL_TRANSPORT


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
    const uint candidates = g_NwbReflectionCounters.Load(NWB_REFLECTION_COUNTER_CANDIDATES);
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
    const uint packedPixel = g_NwbReflectionQueue.Load(rayIndex * NWB_REFLECTION_QUEUE_ENTRY_BYTES);
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

