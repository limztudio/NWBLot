// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint candidates = g_NwbReflectionCounters.Load(NWB_REFLECTION_COUNTER_CANDIDATES);
    const uint rayCount = min(candidates, min(g_NwbReflectionParameters.maxHardwareRays, g_NwbReflectionParameters.queueCapacity));
    const uint groupCount = (rayCount + NWB_REFLECTION_TRACE_GROUP_SIZE - 1u) / NWB_REFLECTION_TRACE_GROUP_SIZE;
    const uint groupCountX = min(groupCount, NWB_REFLECTION_MAX_GROUP_COUNT_X);
    const uint groupCountY = (groupCount + max(groupCountX, 1u) - 1u) / max(groupCountX, 1u);
    g_NwbHeapStorageBuffers[g_NwbReflectionParameters.argsSlot].Store3(0u, uint3(groupCountX, groupCountY, 1u));
    if(nwbReflectionFeedbackWriting()){
        const uint tilesX = (g_NwbReflectionParameters.dimensionsAndRoute.x + NWB_REFLECTION_FEEDBACK_GROUP_SIZE - 1u)
            / NWB_REFLECTION_FEEDBACK_GROUP_SIZE;
        const uint tilesY = (g_NwbReflectionParameters.dimensionsAndRoute.y + NWB_REFLECTION_FEEDBACK_GROUP_SIZE - 1u)
            / NWB_REFLECTION_FEEDBACK_GROUP_SIZE;
        // Classification wrote the disjoint entry range. This header store is the complete writer's publication
        // boundary; the owning CPU publishes only accepted work and does not recycle the previous accepted bank.
        g_NwbHeapStorageBuffers[g_NwbReflectionParameters.feedbackWriteSlot].Store4(0u, uint4(
            g_NwbReflectionCounters.Load(NWB_REFLECTION_COUNTER_POTENTIAL_RECEIVERS),
            g_NwbReflectionParameters.feedbackProbeIndex, tilesX * tilesY, NWB_REFLECTION_FEEDBACK_HEADER_VALID
        ));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

