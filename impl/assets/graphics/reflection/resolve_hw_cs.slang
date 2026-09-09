// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"
#include "../raytrace/material_context_slots.slangi"

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
    if(!nwbReflectionLoadSurface(pixel, glass, surface) || surface.bias >= g_NwbReflectionParameters.maxRayDistance)
        return;
    uint previousCount;
    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_HARDWARE_RAYS, 1u, previousCount);
    const NwbRayTraceSurfaceHit hit = nwbRayTraceClosestSurfaceHit(
        g_NwbReflectionMaterialContext.sceneSlots.z,
        surface.position + surface.normal * surface.bias,
        surface.direction,
        surface.bias,
        g_NwbReflectionParameters.maxRayDistance
    );
    if(!hit.hit)
        return;
    const float3 hitRadiance = nwbRayTraceSurfaceRadiance(hit, -surface.direction);
    const float rangeWeight = 1.0 - smoothstep(g_NwbReflectionParameters.distanceFadeStart, g_NwbReflectionParameters.maxRayDistance, hit.distance);
    const float3 radiance = lerp(nwbReflectionEnvironment(surface.direction), hitRadiance, rangeWeight);
    nwbReflectionStore(pixel, glass, radiance * surface.weight, 3.0);
    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_HARDWARE_HITS, 1u, previousCount);
    g_NwbReflectionCounters.InterlockedAdd(NWB_REFLECTION_COUNTER_FALLBACK_PIXELS, 0xffffffffu, previousCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

