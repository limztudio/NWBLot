// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_standalone{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PresentationTailTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::None, true };
    struct Payload{};

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commands,
        const Core::GpuTaskRecordContext& context){
        static_cast<void>(payload);
        static_cast<void>(context);
        return !commands.commandRecordingFailed();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuTaskId GpuRendererState::declareStandalone(Core::GpuTaskGraph& graph){
    Core::GpuTaskGraphOutputLayer layer;
    if(!declare(graph, layer) || !layer.color.valid() || !m_pending)
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
        GpuOutputTask::Payload{ frame, backBuffer, layer.color, frame->m_acquired, frame->m_outputPipeline, frame->m_presentationMode }
    );
    if(!output.valid())
        return {};
    Core::GpuTaskId terminal = output;
    if(m_presentationContributor && m_presentationContributor->hasTaskGraphPresentationWork()){
        const Core::GpuTaskId contribution = m_presentationContributor->declareTaskGraphPresentation(
            graph, frame->m_acquired, backBuffer, output
        );
        if(!contribution.valid())
            return {};
        Core::GpuTaskSchedulingHint terminalScheduling;
        terminalScheduling.cost = Core::GpuTaskCostHint::Tiny;
        terminalScheduling.overlapPreferred = false;
        terminalScheduling.avoidQueueCrossing = true;
        terminalScheduling.forceSubmissionBoundary = true;
        terminalScheduling.allowPacketMerge = false;
        terminal = graph.addTask<__hidden_ui_gpu_standalone::PresentationTailTask>(
            Core::GpuTaskDesc().setIdentity(Name("ui.presentation_tail")).setMarkerLabel("UI Presentation Tail")
                .setScheduling(terminalScheduling).setDependencies(&contribution, 1u),
            __hidden_ui_gpu_standalone::PresentationTailTask::Payload{}
        );
    }
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
    Core::QueueSubmissionToken submission;
    const bool submitted = m_state->m_graphics.submitStandaloneTaskGraph(
        m_state.get(),
        [](void* context, Core::GpuTaskGraph& graph){
            return static_cast<GpuRendererState*>(context)->declareStandalone(graph);
        },
        submission,
        queue,
        &m_state->m_graphics.gpuTiming()
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

