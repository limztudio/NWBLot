// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics_prefix_timing_resolver.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GraphicsPrefixTimingResolver::resolve(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    const Span<Core::GpuTimingSubmissionTicket* const> ownedTickets,
    const Span<Core::GpuTimingSubmissionTicket*> timingTickets,
    const Span<Core::GpuTaskId> timingTasks,
    b8& asyncSpansOnePacket,
    GraphicsPrefixTimingResolutionResult& outResult
)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = GraphicsPrefixTimingResolutionResult{};
    // CSG callbacks keep an independent anchor across FrontierSafe splits.
    const Array<Core::GpuTaskId, s_GraphicsPrefixTimingTaskCount> resolvedTasks = {
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixSceneShadingSetupTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixDeferredClearTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgReceiverSpanTask.valid() ? pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgReceiverSpanTask : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask.valid() ? pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask.valid() ? pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixTask,
    };
    if(ownedTickets.size() != s_GraphicsPrefixTimingTaskCount || timingTickets.size() != s_GraphicsPrefixTimingTaskCount || timingTasks.size() != s_GraphicsPrefixTimingTaskCount)
        return;
    for(usize taskIndex = 0u; taskIndex < s_GraphicsPrefixTimingTaskCount; ++taskIndex)
        timingTasks[taskIndex] = resolvedTasks[taskIndex];
    for(usize prefixTaskIndex = 0u; prefixTaskIndex < s_GraphicsPrefixTimingTaskCount; ++prefixTaskIndex){
        const Core::GpuTaskId task = timingTasks[prefixTaskIndex];
        if(!compiledPlan.findTask(task).valid() || (prefixTaskIndex != 0u && !compiledPlan.taskPrecedesOrSharesPacket(timingTasks[prefixTaskIndex - 1u], task))){
            outResult.bindingsValid = false;
            break;
        }
        b8 sharesPacketWithEarlierTask = false;
        for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
            if(!compiledPlan.tasksSharePacket(task, timingTasks[earlierTaskIndex]))
                continue;
            timingTickets[prefixTaskIndex] = timingTickets[earlierTaskIndex];
            sharesPacketWithEarlierTask = true;
            break;
        }
        if(!sharesPacketWithEarlierTask){
            timingTickets[prefixTaskIndex] = ownedTickets[prefixTaskIndex];
            ++outResult.uniquePacketCount;
        }
    }
    // Measures are submission-local; skip the scope across a frontier.
    asyncSpansOnePacket = outResult.bindingsValid && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

