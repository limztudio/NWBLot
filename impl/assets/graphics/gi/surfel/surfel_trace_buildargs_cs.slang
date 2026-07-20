// Build the trace's DispatchIndirectArguments (U6): 1 thread reads the allocation high-water BUMP_TOP + the update
// divisor (both from the surfel CB) and writes {ceil(BUMP_TOP / divisor), 1, 1} -- the exact workgroup count to visit
// every LIVE surfel over `divisor` frames via the trace's round-robin, instead of the fixed ceil(poolCapacity/divisor).
// Slots >= BUMP_TOP are never allocated, so groups*divisor >= BUMP_TOP covers every live slot and the trace's existing
// `surfelIndex < poolCapacity && alive` guard drops the partial last group. Runs AFTER spawn (so BUMP_TOP includes this
// frame's new surfels) and BEFORE the trace; the SAME args buffer feeds both the SW and HW trace (identical round-robin).
// BUMP_TOP = 0 -> 0 groups -> a valid no-op indirect dispatch.


#include "surfel_binding_slots.h"
#include "surfel_constants.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_TRACE_BUILDARGS_BINDING_CONSTANTS, NWB_SURFEL_TRACE_BUILDARGS_SET)]]
ConstantBuffer<NwbSurfelConstants> g_NwbSurfelBuildArgsConstants;

[[vk::binding(NWB_SURFEL_TRACE_BUILDARGS_BINDING_COUNTER, NWB_SURFEL_TRACE_BUILDARGS_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelBuildArgsCounter;

[[vk::binding(NWB_SURFEL_TRACE_BUILDARGS_BINDING_ARGS, NWB_SURFEL_TRACE_BUILDARGS_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelBuildArgsOutput;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(1, 1, 1)]
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

