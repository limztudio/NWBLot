// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"
#include "../raytrace/material_context_slots.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma warning(push)
#pragma warning(disable: 39001)
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbRayTraceMaterialContextSlots, Std140DataLayout> g_NwbHeapReflectionMaterialContexts[];
#pragma warning(pop)
#define g_NwbReflectionMaterialContext g_NwbHeapReflectionMaterialContexts[g_NwbReflectionParameters.materialContextSlot]

uint nwbMeshFrameMaterialTypedHeapSlot(){ return g_NwbReflectionMaterialContext.sceneSlots.w; }
uint nwbMeshFrameInstanceHeapSlot(){ return g_NwbReflectionMaterialContext.materialSlots.x; }
uint nwbMeshFrameViewHeapSlot(){ return g_NwbReflectionParameters.viewSlot; }
#include "../bindless/bindless_heap_mesh.slangi"

#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbReflectionResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbReflectionResources.avboitSlots.w
#include "../raytrace/surface_hit.slangi"
#include "../raytrace/surface_lighting.slangi"
#if NWB_REFLECTION_OPTICAL_TRANSPORT
#include "../raytrace/optical_transport.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_REFLECTION_OPTICAL_TRANSPORT

float3 nwbOpticalSurfaceRadiance(const NwbRayTraceSurfaceHit hit, const float3 observerDirection){
    return nwbRayTraceSurfaceRadiance(hit, observerDirection);
}

float3 nwbOpticalEnvironmentRadiance(const float3 direction){
    return nwbReflectionEnvironment(direction);
}

void nwbReflectionRecordOpticalDiagnostics(const NwbOpticalTraceResult result){
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_QUERIES, result.queryCount);
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_BOOTSTRAP_EVENTS, result.bootstrapEventCount);
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_TRANSPARENT_PATHS, result.transparentPath ? 1u : 0u);
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_TIR_EVENTS, result.tirCount);
    if(result.termination == NWB_OPTICAL_TERMINATION_UNSUPPORTED)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_UNSUPPORTED_PATHS, 1u);
    else if(result.termination == NWB_OPTICAL_TERMINATION_MEDIUM_LIMIT)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_MEDIUM_OVERFLOW_PATHS, 1u);
    else if(result.termination == NWB_OPTICAL_TERMINATION_AMBIGUOUS)
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_AMBIGUOUS_PATHS, 1u);
    else if(
        result.termination == NWB_OPTICAL_TERMINATION_QUERY_LIMIT || result.termination == NWB_OPTICAL_TERMINATION_EVENT_LIMIT
        || result.termination == NWB_OPTICAL_TERMINATION_RANGE
    )
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_LIMITED_PATHS, 1u);
}


#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_TRACE_GROUP_SIZE, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID){
    const uint candidates = g_NwbReflectionCounters.Load(NWB_REFLECTION_COUNTER_CANDIDATES);
    const uint rayCount = min(candidates, min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity));
    const uint groupCountX = min((rayCount + NWB_REFLECTION_TRACE_GROUP_SIZE - 1u) / NWB_REFLECTION_TRACE_GROUP_SIZE, NWB_REFLECTION_MAX_GROUP_COUNT_X);
    const uint rayIndex = (groupId.y * groupCountX + groupId.x) * NWB_REFLECTION_TRACE_GROUP_SIZE + groupThreadId.x;
    if(rayIndex >= rayCount)
        return;
    const uint packedPixel = g_NwbReflectionQueue.Load(rayIndex * 4u);
    const uint pixelCount = g_NwbReflectionParameters.width * g_NwbReflectionParameters.height;
    const bool glass = packedPixel >= pixelCount;
    const uint linearPixel = glass ? packedPixel - pixelCount : packedPixel;
    const uint2 pixel = uint2(linearPixel % g_NwbReflectionParameters.width, linearPixel / g_NwbReflectionParameters.width);
    NwbReflectionSurface surface;
    if(
        !nwbReflectionLoadSurface(pixel, glass, surface) || !any(surface.weight > 0.0)
        || surface.bias >= g_NwbReflectionParameters.maxRayDistance
    )
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
    const NwbOpticalTraceResult result = nwbOpticalTrace(
        config, surface.position + surface.normal * surface.bias, surface.direction
    );
    nwbReflectionRecordOpticalDiagnostics(result);
    float3 radiance = result.radiance;
    if(!result.transparentPath && result.termination == NWB_OPTICAL_TERMINATION_OPAQUE){
        const float rangeWeight = 1.0 - smoothstep(
            g_NwbReflectionParameters.distanceFadeStart, g_NwbReflectionParameters.maxRayDistance, result.distance
        );
        radiance = lerp(nwbReflectionEnvironment(surface.direction), radiance, rangeWeight);
    }
    // Optical continuation supplies its accumulated radiance, including ordinary exterior misses. A capped or
    // ambiguous in-medium path discards unresolved energy; blending sky here would leak light through glass.
    nwbReflectionStore(pixel, glass, radiance * surface.weight, result.surfaceHit ? 3.0 : 1.0);
    if(result.surfaceHit){
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_HITS, 1u);
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, 0xffffffffu);
    }
#else
    // A complete scene with no transparent instances uses this separately compiled kernel. Its private storage
    // contains no media or candidate-event arrays, so opaque-only reflection retains the simple query path.
    nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_QUERIES, 1u);
    const NwbRayTraceSurfaceHit hit = nwbRayTraceClosestSurfaceHit(
        g_NwbReflectionMaterialContext.sceneSlots.z, surface.position + surface.normal * surface.bias,
        surface.direction, surface.bias, g_NwbReflectionParameters.maxRayDistance
    );
    float3 radiance = nwbReflectionEnvironment(surface.direction);
    if(hit.hit){
        const float rangeWeight = 1.0 - smoothstep(
            g_NwbReflectionParameters.distanceFadeStart, g_NwbReflectionParameters.maxRayDistance, hit.distance
        );
        radiance = lerp(radiance, nwbRayTraceSurfaceRadiance(hit, -surface.direction), rangeWeight);
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_HARDWARE_HITS, 1u);
        nwbReflectionRecordDiagnostic(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, 0xffffffffu);
    }
    nwbReflectionStore(pixel, glass, radiance * surface.weight, hit.hit ? 3.0 : 1.0);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

