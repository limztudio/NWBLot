// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hardware_transparent_collect.slangi"
#include "hardware_transparent_ray.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_HW_TRANSPARENT_GROUP_SIZE, NWB_HW_TRANSPARENT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint2 extent = nwbHwShadowTraceExtent();
    if(any(dispatchThreadId.xy >= extent))
        return;
    const uint pixel = dispatchThreadId.y * extent.x + dispatchThreadId.x;
    RayDesc ray;
    if(nwbHwShadowBuildRay(dispatchThreadId.xy, ray))
        nwbHwShadowCollect(ray, pixel, extent.x * extent.y);
    else
        g_NwbHardwareTransparentCrossings[pixel] = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

