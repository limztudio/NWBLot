// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1

#define NWB_OPTICAL_BOUNDS_OUTSIDE_ONLY 1
#include "hardware_transparent_collect.slangi"
#include "hardware_transparent_ray.slangi"
#include "../raytrace/optical_bounds.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbHwShadowRayOutsideTransparentBounds(const RayDesc ray){
    const uint opticalSceneSlot = g_NwbHardwareTransparentContext.materialSlots.y;
    if(opticalSceneSlot == NWB_BINDLESS_HEAP_INVALID_SLOT)
        return false;
    const uint headerFlags = nwbHeapRawBuffer(opticalSceneSlot).Load(NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET);
    if((headerFlags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID) == 0u)
        return false;
    const uint transparentCount = nwbHeapRawBuffer(opticalSceneSlot).Load(NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET);
    if(transparentCount == 0u)
        return true;
    const float3 boundsMin = asfloat(nwbHeapRawBuffer(opticalSceneSlot).Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET));
    const float3 boundsMax = asfloat(nwbHeapRawBuffer(opticalSceneSlot).Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET));
    const float directionLenSq = dot(ray.Direction, ray.Direction);
    if(!isfinite(directionLenSq) || directionLenSq <= 0.0)
        return false;
    return nwbOpticalSegmentOutsideBounds(ray.Origin, ray.Direction * rsqrt(directionLenSq), ray.TMax, boundsMin, boundsMax);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_HW_TRANSPARENT_GROUP_SIZE, NWB_HW_TRANSPARENT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint2 extent = nwbHwShadowTraceExtent();
    if(any(dispatchThreadId.xy >= extent))
        return;
    const uint pixel = dispatchThreadId.y * extent.x + dispatchThreadId.x;
    RayDesc ray;
    if(nwbHwShadowBuildRay(dispatchThreadId.xy, ray) && !nwbHwShadowRayOutsideTransparentBounds(ray))
        nwbHwShadowCollect(ray, pixel, extent.x * extent.y);
    else
        g_NwbHardwareTransparentCrossings[pixel] = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

