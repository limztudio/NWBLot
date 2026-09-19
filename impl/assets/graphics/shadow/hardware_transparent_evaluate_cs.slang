// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hardware_transparent_evaluate.slangi"
#include "hardware_transparent_ray.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_HW_TRANSPARENT_GROUP_SIZE, NWB_HW_TRANSPARENT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint2 extent = nwbHwShadowTraceExtent();
    if(any(dispatchThreadId.xy >= extent))
        return;
    const uint pixel = dispatchThreadId.y * extent.x + dispatchThreadId.x;
    const uint count = g_NwbHardwareTransparentCrossings[pixel];
    if(count > NWB_HW_TRANSPARENT_CROSSING_CAPACITY)
        return;
    float3 visibility = float3(1.0);
    RayDesc ray;
    if(count != 0u && nwbHwShadowBuildRay(dispatchThreadId.xy, ray))
        visibility = nwbHwShadowEvaluateGathered(ray, pixel, extent.x * extent.y);
    nwbHwShadowStoreVisibility(dispatchThreadId.xy, visibility);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

