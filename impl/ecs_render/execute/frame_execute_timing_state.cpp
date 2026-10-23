// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_timing_state.h"

#include <impl/ecs_render/kernel/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameExecuteTimingState::FrameExecuteTimingState(Core::GraphicsRuntime& graphics)noexcept
    : shadowPrepareTimingTicket(graphics.gpuTiming())
    , graphicsPrefixMeshViewSetupTimingTicket(graphics.gpuTiming())
    , graphicsPrefixSceneShadingSetupTimingTicket(graphics.gpuTiming())
    , graphicsPrefixDeferredClearTimingTicket(graphics.gpuTiming())
    , graphicsPrefixGbufferTimingTicket(graphics.gpuTiming())
    , graphicsPrefixCsgReceiverSpanTimingTicket(graphics.gpuTiming())
    , graphicsPrefixCsgIntervalCombineTimingTicket(graphics.gpuTiming())
    , graphicsPrefixCsgIntervalSampleTimingTicket(graphics.gpuTiming())
    , graphicsPrefixNormalizeTimingTicket(graphics.gpuTiming())
    , graphicsPrefixTimingTickets{
        &graphicsPrefixMeshViewSetupTimingTicket,
        &graphicsPrefixSceneShadingSetupTimingTicket,
        &graphicsPrefixDeferredClearTimingTicket,
        &graphicsPrefixGbufferTimingTicket,
        &graphicsPrefixCsgReceiverSpanTimingTicket,
        &graphicsPrefixCsgIntervalCombineTimingTicket,
        &graphicsPrefixCsgIntervalSampleTimingTicket,
        &graphicsPrefixNormalizeTimingTicket,
    }
    , graphicsPrefixOwnedTimingTickets{
        &graphicsPrefixMeshViewSetupTimingTicket,
        &graphicsPrefixSceneShadingSetupTimingTicket,
        &graphicsPrefixDeferredClearTimingTicket,
        &graphicsPrefixGbufferTimingTicket,
        &graphicsPrefixCsgReceiverSpanTimingTicket,
        &graphicsPrefixCsgIntervalCombineTimingTicket,
        &graphicsPrefixCsgIntervalSampleTimingTicket,
        &graphicsPrefixNormalizeTimingTicket,
    }
    , deferredClearTimingState{
        .graphics = &graphics,
        .timing = &deferredClearTiming,
        .rebindableTimingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::DeferredClear)],
        .scope = RendererGpuTimingScope::s_DeferredClear,
    }
    , opaqueCsgIntervalClearTimingState{
        .graphics = &graphics,
        .timing = &opaqueCsgIntervalClearTiming,
        .rebindableTimingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::Gbuffer)],
        .scope = RendererGpuTimingScope::s_CsgIntervalClear,
    }
    , shadowVisibilityTimingTicket(graphics.gpuTiming())
    , softwareCausticsTimingTicket(graphics.gpuTiming())
    , surfelGiTimingTicket(graphics.gpuTiming())
    , hardwareCausticsTimingTicket(graphics.gpuTiming())
    , avboitPreTimingTicket(graphics.gpuTiming())
    , avboitDepthWarpTimingTicket(graphics.gpuTiming())
    , avboitExtinctionTimingTicket(graphics.gpuTiming())
    , avboitIntegrationTimingTicket(graphics.gpuTiming())
    , avboitAccumulationTimingTicket(graphics.gpuTiming())
    , avboitClearTimingState{ .graphics = &graphics, .timing = &avboitClearTiming, .timingTicket = &avboitPreTimingTicket, .scope = RendererGpuTimingScope::s_AvboitClear, }
    , transparentCsgIntervalClearTimingState{ .graphics = &graphics, .timing = &transparentCsgIntervalClearTiming, .timingTicket = &avboitPreTimingTicket, .scope = RendererGpuTimingScope::s_CsgIntervalClear, }
    , deferredLightingTimingTicket(graphics.gpuTiming())
    , deferredCompositeTimingTicket(graphics.gpuTiming())
    , deferredPresentTimingTicket(graphics.gpuTiming())
    , frameTimingTransaction(graphics.gpuTiming())
{}


void FrameExecuteTimingState::discardGraphicsPrefixTickets(){
    for(Core::GpuTimingSubmissionTicket* const timingTicket : graphicsPrefixOwnedTimingTickets)
        timingTicket->discard();
}

void FrameExecuteTimingState::discardTickets(){
    shadowPrepareTimingTicket.discard();
    discardGraphicsPrefixTickets();
    shadowVisibilityTimingTicket.discard();
    hardwareCausticsTimingTicket.discard();
    softwareCausticsTimingTicket.discard();
    surfelGiTimingTicket.discard();
    avboitPreTimingTicket.discard();
    avboitDepthWarpTimingTicket.discard();
    avboitExtinctionTimingTicket.discard();
    avboitIntegrationTimingTicket.discard();
    avboitAccumulationTimingTicket.discard();
    deferredLightingTimingTicket.discard();
    deferredCompositeTimingTicket.discard();
    deferredPresentTimingTicket.discard();
}

void FrameExecuteTimingState::discardAll(){
    Core::DiscardGpuTimingMeasure(&asyncPrefixTiming);
    Core::DiscardGpuTimingMeasure(&deferredClearTiming);
    Core::DiscardGpuTimingMeasure(&asyncFinalTiming);
    Core::DiscardGpuTimingMeasure(&causticPhotonTiming);
    Core::DiscardGpuTimingMeasure(&causticResolveTiming);
    Core::DiscardGpuTimingMeasure(&transparentCsgIntervalsTiming);
    Core::DiscardGpuTimingMeasure(&avboitOccupancyComputeEmulationTiming);
    Core::DiscardGpuTimingMeasure(&avboitExtinctionComputeEmulationTiming);
    Core::DiscardGpuTimingMeasure(&avboitAccumulationComputeEmulationTiming);
    Core::DiscardGpuTimingMeasure(&opaqueRegularSharedComputeEmulationTiming);
    Core::DiscardGpuTimingMeasure(&opaqueCsgIntervalSampleComputeEmulationTiming);
    frameTimingTransaction.discard();
    discardTickets();
}

Expected<usize> FrameExecuteTimingState::bindNormalTimingTickets(
    const RendererFramePipeline& renderer,
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    const Core::GpuTaskId (&graphicsPrefixTimingTasks)[s_GraphicsPrefixTimingTicketCount],
    const usize graphicsPrefixUniquePacketCount,
    const RendererAvboitTaskGraphValidation& avboitValidation,
    const bool hardwareShadowSupported,
    const usize ticketCapacity,
    Core::GpuTaskGraphTaskTimingTicket* const tickets
){
    usize ticketCount = 0u;
    const auto appendTicket = [&](const Core::GpuTaskId task, Core::GpuTimingSubmissionTicket& timingTicket)noexcept -> bool {
        if(!task.valid() || ticketCount >= ticketCapacity)
            return false;
        tickets[ticketCount++] = Core::GpuTaskGraphTaskTimingTicket{ .task = task, .timingTicket = &timingTicket, };
        return true;
    };
    bool ready = appendTicket(renderer.m_deferredShadowPrepareTask, shadowPrepareTimingTicket);
    for(usize prefixTaskIndex = 0u; ready && prefixTaskIndex < s_GraphicsPrefixTimingTicketCount; ++prefixTaskIndex){
        const Core::GpuTaskId task = graphicsPrefixTimingTasks[prefixTaskIndex];
        bool packetAlreadyTimed = false;
        for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
            if(compiledPlan.tasksSharePacket(task, graphicsPrefixTimingTasks[earlierTaskIndex])){
                packetAlreadyTimed = true;
                break;
            }
        }
        if(packetAlreadyTimed)
            continue;
        if(!compiledPlan.findTask(task).valid() || !graphicsPrefixTimingTickets[prefixTaskIndex]){
            ready = false;
            break;
        }
        ready = appendTicket(task, *graphicsPrefixTimingTickets[prefixTaskIndex]);
    }
    ready = ready
        && ticketCount == 1u + graphicsPrefixUniquePacketCount
        && appendTicket(renderer.m_deferredShadowVisibilityTask, shadowVisibilityTimingTicket)
        && appendTicket(hardwareShadowSupported ? renderer.m_deferredHardwareCausticsTask : renderer.m_deferredSoftwareCausticsTask, hardwareShadowSupported ? hardwareCausticsTimingTicket : softwareCausticsTimingTicket)
        && appendTicket(renderer.m_deferredSurfelGiTask, surfelGiTimingTicket)
    ;
    RendererAvboitTaskGraphTimingTickets avboitTimingTickets{
        .m_pre = avboitPreTimingTicket,
        .m_depthWarp = avboitDepthWarpTimingTicket,
        .m_extinction = avboitExtinctionTimingTicket,
        .m_integration = avboitIntegrationTimingTicket,
        .m_accumulation = avboitAccumulationTimingTicket,
    };
    ready = ready
        && renderer.m_avboitSystem.appendTaskGraphTimingTickets(avboitValidation, ticketCapacity, avboitTimingTickets, tickets, ticketCount)
        && appendTicket(renderer.m_deferredLightingTask, deferredLightingTimingTicket)
        && appendTicket(renderer.m_deferredCompositeTask, deferredCompositeTimingTicket)
        && appendTicket(renderer.m_deferredPresentTask, deferredPresentTimingTicket)
    ;
    if(!ready)
        return MakeUnexpected(Failure{});
    return ticketCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

