// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"
#include "renderer_frame_timing_task.h"

#include <core/common/log.h>
#include <core/task/gpu/frame_timing_begin_task.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_standalone{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StandaloneDeclarationContext{
    GpuRendererState& state;
    Core::GpuTimingFrameTransaction& frameTimingTransaction;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuTaskId GpuRendererState::declareStandalone(
    Core::GpuTaskGraph& graph,
    Core::GpuTimingFrameTransaction& frameTimingTransaction
){
    if(!m_pending || !m_pending->m_prepared || !m_readyToDeclare)
        return {};
    Core::GpuTaskSchedulingHint beginScheduling;
    beginScheduling.cost = Core::GpuTaskCostHint::Tiny;
    beginScheduling.forceSubmissionBoundary = true;
    beginScheduling.allowPacketMerge = false;
    const Core::GpuTaskId begin = graph.addTask<Core::FrameTimingBeginGraphTask>(
        Core::GpuTaskDesc().setIdentity(Name("ui.frame_timing_begin")).setMarkerLabel("UI Frame Timing Begin")
            .setScheduling(beginScheduling)
            .setTimingMetadata({ 0u, 0u, Core::GpuTaskTimingPolicy::PacketOnly }),
        Core::FrameTimingBeginGraphTask::Payload{
            .frameTimingTransaction = &frameTimingTransaction,
            .device = &m_graphics.getDevice(),
            .scopeDefinition = m_frameTimingScopePrepared ? GpuRendererTimingScope::s_Frame : Core::GpuTimingScopeDefinition{},
        }
    );
    if(!begin.valid() || !graph.setNormalExecutionPrelude(begin))
        return {};
    Core::GpuTaskGraphOutputLayer layer;
    if(!declare(graph, layer) || !layer.color.valid())
        return {};
    const GpuFrame frame = m_pending;
    const Core::GpuExternalCompletionId acquired = graph.importExternalCompletion(
        Core::GpuExternalCompletionDesc().setIdentity(Name("ui.acquired_ready")).setMarkerLabel("UI Acquired Image")
            .setToken(frame->m_acquired.backBuffer.availabilityCompletion)
    );
    if(!acquired.valid())
        return {};
    const Core::GpuGraphResourceId backBuffer = graph.importTexture(
        frame->m_acquired.backBuffer.texture,
        Core::GpuGraphResourceDesc().setIdentity(Name("ui.backbuffer")).setMarkerLabel("UI Acquired Backbuffer").setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(frame->m_acquired.backBuffer.nativeInitialState)
            .setExternalFinalState(Core::ResourceStates::Present)
            .setInitialAvailabilityCompletion(acquired)
    );
    const Core::GpuGraphPipelineId pipeline = graph.importGraphicsPipeline(
        frame->m_outputPipeline, Core::GpuGraphPipelineDesc().setIdentity(Name("ui.output_pipeline")).setMarkerLabel("UI Output Pipeline")
            .setType(Core::GpuGraphPipelineType::Graphics)
    );
    if(!backBuffer.valid() || !pipeline.valid())
        return {};
    const Core::GpuTaskResourceUse uses[]{
        { layer.color, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read },
        { backBuffer, {}, Core::ResourceStates::RenderTarget, Core::GpuTaskResourceAccess::Write },
    };
    const Core::GpuTaskResourceVersionUse consume{ layer.colorVersion, Core::GpuTaskResourceVersionRole::Consume };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.avoidQueueCrossing = true;
    const Core::GpuTaskId output = graph.addTask<GpuOutputTask>(
        Core::GpuTaskDesc().setIdentity(Name("ui.output")).setMarkerLabel("UI Standalone Output").setScheduling(scheduling)
            .setDependencies(&layer.readyTask, 1u).setResourceUses(uses, 2u).setResourceVersionUses(&consume, 1u)
            .setTimingMetadata({ 0u, m_width ^ (m_height << 16u), Core::GpuTaskTimingPolicy::Task }),
        GpuOutputTask::Payload{ frame, backBuffer, layer.color, pipeline, frame->m_acquired, frame->m_outputPipeline, frame->m_presentationMode }
    );
    if(!output.valid())
        return {};
    Core::GpuTaskId timingEndDependency = output;
    if(m_presentationContributor && m_presentationContributor->hasTaskGraphPresentationWork()){
        timingEndDependency = m_presentationContributor->declareTaskGraphPresentation(
            graph, frame->m_acquired, backBuffer, output
        );
        if(!timingEndDependency.valid())
            return {};
    }
    Core::GpuTaskSchedulingHint timingEndScheduling;
    timingEndScheduling.cost = Core::GpuTaskCostHint::Tiny;
    timingEndScheduling.overlapPreferred = false;
    timingEndScheduling.avoidQueueCrossing = true;
    timingEndScheduling.forceSubmissionBoundary = true;
    timingEndScheduling.allowPacketMerge = false;
    const Core::GpuTaskId terminal = graph.addTask<GpuFrameTimingEndTask>(
        Core::GpuTaskDesc().setIdentity(Name("ui.frame_timing_end")).setMarkerLabel("UI Frame Timing End")
            .setScheduling(timingEndScheduling).setDependencies(&timingEndDependency, 1u)
            .setTimingMetadata({ 0u, 0u, Core::GpuTaskTimingPolicy::PacketOnly }),
        GpuFrameTimingEndTask::Payload{ &frameTimingTransaction }
    );
    if(!terminal.valid() || !graph.declarePresentEndpoint({ terminal, backBuffer }))
        return {};
    return terminal;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRenderer::renderStandalone(const Core::AcquiredPresentationFrame& frame){
    if(!GpuRendererState::matchesAcquired(m_state->m_graphics.acquiredPresentationFrame(), frame))
        return false;
    // Only the exact accepted acquired image is already produced. A rejected claimed scene graph owns its recovery tail.
    if(GpuRendererState::matchesAcquired(m_state->m_lastAcceptedAcquired, frame))
        return true;
    if(!m_state->m_pending)
        return false;
    const Core::AcquiredPresentationFrame& prepared = m_state->m_pending->m_acquired;
    if(m_state->m_claimed && GpuRendererState::matchesAcquired(prepared, frame))
        return true;
    if(!m_state->prepare(frame))
        return false;
    if(!m_state->m_pending->m_prepared || !m_state->m_readyToDeclare)
        return false;
    m_state->m_presentationContributor = nullptr;
    if(Core::IGpuTaskGraphPresentationContributor* const contributor = m_state->m_graphics.taskGraphPresentationContributor()){
        if(contributor->prepareTaskGraphPresentation(frame))
            m_state->m_presentationContributor = contributor;
        else if(m_state->m_graphics.isDeviceRecreationRequested())
            return false;
        else
            NWB_LOGGER_WARNING(NWB_TEXT("GpuRenderer: presentation contributor preparation failed; presenting UI output without its contribution"));
    }
    const Core::GpuPhysicalQueueId queue = m_state->m_graphics.getDevice().getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(!queue.valid())
        return false;
    const GpuFrame pending = m_state->m_pending;
    Core::GpuTimingFrameTransaction frameTimingTransaction(m_state->m_graphics.gpuTiming());
    __hidden_ui_gpu_standalone::StandaloneDeclarationContext declarationContext{ *m_state, frameTimingTransaction };
    Core::QueueSubmissionToken submission;
    const bool submitted = m_state->m_graphics.submitStandaloneTaskGraph(
        &declarationContext,
        [](void* context, Core::GpuTaskGraph& graph){
            auto& declaration = *static_cast<__hidden_ui_gpu_standalone::StandaloneDeclarationContext*>(context);
            return declaration.state.declareStandalone(graph, declaration.frameTimingTransaction);
        },
        submission,
        queue,
        &m_state->m_graphics.gpuTiming(),
        &frameTimingTransaction
    );
    m_state->m_presentationContributor = nullptr;
    if(pending->m_finalConsumer.valid())
        acceptTaskGraphOutputLayer(pending->m_snapshot.generation(), pending->m_finalConsumer);
    if(!submitted || !submission.valid()){
        // Retain immutable inputs and accepted-prefix tokens; a later acquired frame can retry after completion.
        m_state->m_claimed = false;
        m_state->m_declaredGraph = nullptr;
        return false;
    }
    return pending->m_finalConsumer.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

