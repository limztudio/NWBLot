// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hardware_transparent_continuation.slangi"
#include "hardware_transparent_ray.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_HW_TRANSPARENT_OVERFLOW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint index = dispatchThreadId.x;
    if(index >= g_NwbHardwareTransparentOverflowArgs[NWB_HW_TRANSPARENT_OVERFLOW_ARGS_COUNT])
        return;
    const uint2 extent = nwbHwShadowTraceExtent();
    const uint pixel = g_NwbHardwareTransparentOverflowList[index];
    const uint2 halfPixel = uint2(pixel % extent.x, pixel / extent.x);
    // Visibility visibility is a (0, 1) bounded factor; half math matches the finalized transmittance.
    half3 visibility = half3(1.0h);
    RayDesc ray;
    if(nwbHwShadowBuildRay(halfPixel, ray))
        visibility = nwbHwShadowEvaluateContinuation(ray);
    nwbHwShadowStoreVisibility(halfPixel, visibility);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

