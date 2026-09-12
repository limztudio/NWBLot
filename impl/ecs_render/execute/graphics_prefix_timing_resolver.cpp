// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/graphics_prefix_timing_resolver.h>


#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsPrefixTimingResolver::GraphicsPrefixTimingResolver(NotNull<RendererFramePipeline*> pipeline)
    : m_pipeline(pipeline){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GraphicsPrefixTimingResolver::resolve(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    Core::GpuTimingSubmissionTicket** timingTickets,
    Core::GpuTimingSubmissionTicket* const* ownedTickets,
    usize ticketCount,
    Core::GpuTaskId* timingTasks,
    bool& asyncSpansOnePacket,
    GraphicsPrefixTimingResolutionResult& outResult
)const{
    RendererFramePipeline& pipeline = *m_pipeline;
    outResult = GraphicsPrefixTimingResolutionResult{};
    // CSG callbacks keep an independent anchor across FrontierSafe splits.
    Core::GpuTaskId resolvedTasks[s_TimingTaskCount] = {
        pipeline.m_graphicsPrefixMeshViewSetupTask,
        pipeline.m_graphicsPrefixSceneShadingSetupTask,
        pipeline.m_graphicsPrefixDeferredClearTask,
        pipeline.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixCsgReceiverSpanTask.valid()
            ? pipeline.m_graphicsPrefixCsgReceiverSpanTask
            : pipeline.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixCsgIntervalCombineTask.valid()
            ? pipeline.m_graphicsPrefixCsgIntervalCombineTask
            : pipeline.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixCsgIntervalSampleTask.valid()
            ? pipeline.m_graphicsPrefixCsgIntervalSampleTask
            : pipeline.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixTask,
    };
    static_assert(LengthOf(resolvedTasks) == s_TimingTaskCount);
    if(ticketCount != s_TimingTaskCount || !timingTasks)
        return;
    for(usize taskIndex = 0u; taskIndex < s_TimingTaskCount; ++taskIndex)
        timingTasks[taskIndex] = resolvedTasks[taskIndex];
    for(usize prefixTaskIndex = 0u; prefixTaskIndex < ticketCount; ++prefixTaskIndex){
        const Core::GpuTaskId task = timingTasks[prefixTaskIndex];
        if(
            !compiledPlan.findTask(task).valid()
            || (
                prefixTaskIndex != 0u
                && !compiledPlan.taskPrecedesOrSharesPacket(
                    timingTasks[prefixTaskIndex - 1u],
                    task
                )
            )
        ){
            outResult.bindingsValid = false;
            break;
        }
        bool sharesPacketWithEarlierTask = false;
        for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
            if(!compiledPlan.tasksSharePacket(
                task,
                timingTasks[earlierTaskIndex]
            ))
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
    asyncSpansOnePacket = outResult.bindingsValid
        && compiledPlan.tasksSharePacket(
            pipeline.m_graphicsPrefixMeshViewSetupTask,
            pipeline.m_graphicsPrefixTask
        )
    ;
    outResult.asyncSpansOnePacket = asyncSpansOnePacket;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
