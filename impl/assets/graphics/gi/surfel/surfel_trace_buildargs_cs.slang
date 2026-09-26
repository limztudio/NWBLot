// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Indirect trace args: {ceil(BUMP_TOP / divisor), 1, 1} visits every live surfel over `divisor` frames (shared SW/HW).
// After spawn, before trace; BUMP_TOP = 0 dispatches a no-op.


#include "surfel_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSurfelBuildArgsConstants g_NwbHeapSurfelConstantBuffers[g_NwbSurfelHeapPushConstants.constantsHeapSlot]
#define g_NwbSurfelBuildArgsCounter g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.counterHeapSlot]
#define g_NwbSurfelBuildArgsOutput g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.traceIndirectArgsHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(
    NWB_SURFEL_TRACE_BUILDARGS_GROUP_SIZE_X,
    NWB_SURFEL_TRACE_BUILDARGS_GROUP_SIZE_Y,
    NWB_SURFEL_TRACE_BUILDARGS_GROUP_SIZE_Z
)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    if(dispatchThreadID.x != 0u)
        return;
    const uint bumpTop = g_NwbSurfelBuildArgsCounter[NWB_SURFEL_COUNTER_BUMP_TOP];
    const uint divisor = max(uint(g_NwbSurfelBuildArgsConstants.hashPoolFrameDivisor.w), 1u);
    // ceil(BUMP_TOP / divisor). groups <= BUMP_TOP <= poolCapacity; indirect dispatch is NOT CPU-validated against the
    // device limit, so poolCapacity MUST stay <= maxComputeWorkGroupCount[0] (Vulkan guarantees >= 65535; default pool is
    // 16384). Raising NWB_SURFEL_POOL_CAPACITY past that floor would need a clamp here.
    const uint groups = (bumpTop + divisor - 1u) / divisor;
    g_NwbSurfelBuildArgsOutput[NWB_SURFEL_TRACE_INDIRECT_ARGS_GROUP_COUNT_X] = groups;
    g_NwbSurfelBuildArgsOutput[NWB_SURFEL_TRACE_INDIRECT_ARGS_GROUP_COUNT_Y] = 1u;
    g_NwbSurfelBuildArgsOutput[NWB_SURFEL_TRACE_INDIRECT_ARGS_GROUP_COUNT_Z] = 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

