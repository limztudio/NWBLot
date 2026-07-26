// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stage-3 BUILD INDIRECT ARGS (compacted transparent resolve). One thread: clamp the classify pass's append count to
// the list capacity, write the DispatchIndirectArguments{groupsX,1,1} for the indirect trace (groupsX =
// ceil(count / traceGroupSize); 0 when no edges -> a valid zero-workgroup indirect dispatch), and stash the clamped
// count for the indirect trace's tail guard. No trace, no G-buffer -- just the compaction counter + args buffers.


#include "sw_binding_slots.h"
#include "sw_shadow_heap_resources.slangi"

// The append counter and indirect arguments are both heap-selected writable uint buffers.
#define g_NwbSwShadowEdgeCounter NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.edgeCounterStorageSlot)
#define g_NwbSwShadowIndirectArgs NwbHeapShadowWritableUintBuffer(g_NwbSwShadowPush.indirectArgsStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    if(dispatchThreadID.x != 0u || dispatchThreadID.y != 0u)
        return;
    const uint groupSize = max(g_NwbSwShadowPush.traceGroupSize, 1u);
    uint count = g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_APPEND];
    count = min(count, g_NwbSwShadowPush.edgeCapacity);
    const uint groups = (count + groupSize - 1u) / groupSize;
    g_NwbSwShadowIndirectArgs[NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_X] = groups;
    g_NwbSwShadowIndirectArgs[NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_Y] = 1u;
    g_NwbSwShadowIndirectArgs[NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_Z] = 1u;
    g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_TRACE] = count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

