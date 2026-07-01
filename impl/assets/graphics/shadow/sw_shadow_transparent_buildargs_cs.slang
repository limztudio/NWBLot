// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-3 BUILD INDIRECT ARGS (compacted transparent resolve). One thread: clamp the classify pass's append count to
// the list capacity, write the DispatchIndirectArguments{groupsX,1,1} for the indirect trace (groupsX =
// ceil(count / traceGroupSize); 0 when no edges -> a valid zero-workgroup indirect dispatch), and stash the clamped
// count for the indirect trace's tail guard. No trace, no G-buffer -- just the compaction counter + args buffers.


#include "sw_binding_slots.h"

struct NwbSwShadowTransparentBuildArgsPushConstants{
    // Threads per group of the indirect trace pass (= NWB_SW_SHADOW_TRACE_GROUP): the count is divided by this to size
    // the indirect dispatch.
    uint traceGroupSize;
    // Element capacity of the edge-list buffer in RECORDS: the append count is clamped to this.
    uint edgeCapacity;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowTransparentBuildArgsPushConstants> g_NwbSwShadowPush;

// The append counter (read [0], write the clamped count into [1]) + the indirect dispatch-args buffer (3 u32).
[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeCounter;

[[vk::binding(NWB_SW_SHADOW_BINDING_INDIRECT_ARGS, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowIndirectArgs;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    if(dispatchThreadID.x != 0u || dispatchThreadID.y != 0u)
        return;
    const uint groupSize = max(g_NwbSwShadowPush.traceGroupSize, 1u);
    uint count = g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_APPEND];
    count = min(count, g_NwbSwShadowPush.edgeCapacity);
    const uint groups = (count + groupSize - 1u) / groupSize;
    g_NwbSwShadowIndirectArgs[0] = groups;
    g_NwbSwShadowIndirectArgs[1] = 1u;
    g_NwbSwShadowIndirectArgs[2] = 1u;
    g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_TRACE] = count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
