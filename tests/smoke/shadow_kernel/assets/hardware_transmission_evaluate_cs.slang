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
    const float3 continuation = nwbHwShadowEvaluateContinuation(ray);
    const float3 selected = overflow ? continuation : nwbHwShadowEvaluateGathered(ray, 0u, 1u);
    RWStructuredBuffer<uint, Std430DataLayout> output = NwbHeapShadowWritableUintBuffer(g_NwbHardwareTransparentPush.outputStorageSlot);
    output[0u] = asuint(selected.x);
    output[1u] = asuint(selected.y);
    output[2u] = asuint(selected.z);
    output[3u] = count;
    output[4u] = asuint(continuation.x);
    output[5u] = asuint(continuation.y);
    output[6u] = asuint(continuation.z);
    output[7u] = g_NwbHardwareTransparentOverflowArgs[NWB_HW_TRANSPARENT_OVERFLOW_ARGS_COUNT];
    output[8u] = g_NwbHardwareTransparentOverflowArgs[NWB_HW_TRANSPARENT_OVERFLOW_ARGS_GROUPS_X];
    output[9u] = g_NwbHardwareTransparentOverflowList[0u];
    output[10u] = overflow ? 1u : 0u;
    output[11u] = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

