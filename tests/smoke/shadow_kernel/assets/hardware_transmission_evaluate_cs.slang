// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_BINDLESS_TLAS 1


#include "shadow/hardware_transparent_continuation.slangi"
#include "hardware_transmission_test_ray.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(1, 1, 1)]
void main(){
    const RayDesc ray = nwbHardwareTransmissionTestRay();
    const uint count = g_NwbHardwareTransparentCrossings[0u];
    const bool overflow = count == NWB_HW_TRANSPARENT_OVERFLOW_COUNT;
    // Visibility visibility is a (0, 1) bounded factor transmitted through the kernel oracle (0.002 tolerance).
    const half3 continuation = nwbHwShadowEvaluateContinuation(ray);
    const half3 selected = overflow ? continuation : nwbHwShadowEvaluateGathered(ray, 0u, 1u);
    RWStructuredBuffer<uint, Std430DataLayout> output = NwbHeapShadowWritableUintBuffer(g_NwbHardwareTransparentPush.outputStorageSlot);
    output[0u] = asuint(float(selected.x));
    output[1u] = asuint(float(selected.y));
    output[2u] = asuint(float(selected.z));
    output[3u] = count;
    output[4u] = asuint(float(continuation.x));
    output[5u] = asuint(float(continuation.y));
    output[6u] = asuint(float(continuation.z));
    output[7u] = g_NwbHardwareTransparentOverflowArgs[NWB_HW_TRANSPARENT_OVERFLOW_ARGS_COUNT];
    output[8u] = g_NwbHardwareTransparentOverflowArgs[NWB_HW_TRANSPARENT_OVERFLOW_ARGS_GROUPS_X];
    output[9u] = g_NwbHardwareTransparentOverflowList[0u];
    output[10u] = overflow ? 1u : 0u;
    output[11u] = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

