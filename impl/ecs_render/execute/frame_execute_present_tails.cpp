// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_present_tails.h"

#include <impl/ecs_render/execute/frame_execute_recovery.h>
#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Lagged-lighting history-copy tail acceptance (shadow/caustic/surfel returns).
// Context and recorded/accepted callbacks filter the three return states and commit them together on acceptance.
namespace FrameExecuteHistoryCopy{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Context{
    Core::Alloc::ScratchArena& scratchArena;
    Core::GpuPersistentResourceStateCache& shadowVisibilityReturnState;
    Core::GpuPersistentResourceStateCache& causticIrradianceReturnState;
    Core::GpuPersistentResourceStateCache& surfelIrradianceReturnState;
    NotNull<DeferredFrameTargets*> targets;
    NotNull<Core::GpuPersistentResourceStateCache::Candidate*> shadowStateCandidate;
    NotNull<Core::GpuPersistentResourceStateCache::Candidate*> causticStateCandidate;
    NotNull<Core::GpuPersistentResourceStateCache::Candidate*> surfelStateCandidate;
    b8 finalStateReady = false;
    b8 acceptedStateReady = false;
};

[[nodiscard]] inline b8 PrepareHistoryCopyFinalState(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteHistoryCopy::Context* const context = static_cast<Context*>(rawContext);
    if(!context || !finalState)
        return false;

    const b8 shadowStateReady = context->shadowVisibilityReturnState.buildFilteredResourceSubset(*finalState, Span<const Core::TextureHandle>(&context->targets->shadowVisibility, 1u), {}, context->scratchArena, *context->shadowStateCandidate);
    const b8 causticStateReady = context->causticIrradianceReturnState.buildFilteredResourceSubset(*finalState, Span<const Core::TextureHandle>(&context->targets->causticIrradiance, 1u), {}, context->scratchArena, *context->causticStateCandidate);
    const b8 surfelStateReady = context->surfelIrradianceReturnState.buildFilteredResourceSubset(*finalState, Span<const Core::TextureHandle>(&context->targets->surfelIrradiance, 1u), {}, context->scratchArena, *context->surfelStateCandidate);
    context->finalStateReady = shadowStateReady && causticStateReady && surfelStateReady;
    return context->finalStateReady;
}

[[nodiscard]] inline b8 AcceptHistoryCopyFinalState(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteHistoryCopy::Context* const context = static_cast<Context*>(rawContext);
    if(!context || !context->finalStateReady)
        return false;

    const b8 shadowStateReady = context->shadowVisibilityReturnState.commit(*context->shadowStateCandidate);
    const b8 causticStateReady = context->causticIrradianceReturnState.commit(*context->causticStateCandidate);
    const b8 surfelStateReady = context->surfelIrradianceReturnState.commit(*context->surfelStateCandidate);
    context->acceptedStateReady = shadowStateReady && causticStateReady && surfelStateReady;
    return context->acceptedStateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

// Surfel-counter readback tail acceptance (Transfer queue).
// Context and recorded/accepted callbacks hold the counter candidate until Transfer accepts, then publish the token.
namespace FrameExecuteSurfelReadback{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Context{
    Core::Alloc::ScratchArena& scratchArena;
    Core::GpuPersistentResourceStateCache& surfelGiCounterPersistentState;
    RendererRayTracingSystem& raytracingSystem;
    NotNull<Core::GpuPersistentResourceStateCache::Candidate*> candidate;
    Span<const Core::BufferHandle> buffers{};
    b8 finalStateReady = false;
    b8 acceptedStateReady = false;
};

[[nodiscard]] inline b8 PrepareReadbackFinalState(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteSurfelReadback::Context* const context = static_cast<Context*>(rawContext);
    if(!context || context->buffers.empty())
        return false;
    context->finalStateReady = finalState && context->surfelGiCounterPersistentState.buildFilteredBufferSubset(*finalState, context->buffers, context->scratchArena, *context->candidate);
    return context->finalStateReady;
}

[[nodiscard]] inline b8 AcceptReadbackFinalState(void* const rawContext, const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteSurfelReadback::Context* const context = static_cast<Context*>(rawContext);
    if(!context || !context->finalStateReady)
        return false;
    context->acceptedStateReady = context->surfelGiCounterPersistentState.commit(*context->candidate);
    if(context->acceptedStateReady)
        context->raytracingSystem.confirmSurfelCountReadbackSubmission(token);
    return context->acceptedStateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecutePresentTails::executeSurfelCounterReadback(const Core::GpuPhysicalQueueInfo* const readbackQueue, const Span<const Core::BufferHandle> counterBuffers){
    RendererFramePipeline& renderer = m_renderer;
    if(!renderer.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask.valid())
        return true;

    // No normal-frame consumer; record only after Present.
    const b8 readbackTailAvailable =
        m_finalPresentationSubmissionToken.valid()
        && renderer.m_deferredLightingTaskGraphScheduled
        && m_compiledPlan.findTask(renderer.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask).valid()
        && readbackQueue
        && (static_cast<u8>(readbackQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) != 0u
    ;
    if(!readbackTailAvailable){
        renderer.m_deferredLightingSubmissionTransaction.rejectTask(
            renderer.m_deferredLightingTaskGraph,
            renderer.m_deferredLightingCompiledGraph,
            renderer.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask,
            renderer.m_deferredLightingRecordedGraph.recordingAttemptGeneration()
        );
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback tail was unavailable"));
    }
    else{
        Core::GpuPersistentResourceStateCache::Candidate readbackCounterStateCandidate(renderer.m_surfelGiCounterPersistentState);
        // Keep the candidate private until Transfer accepts.
        FrameExecuteSurfelReadback::Context readbackContext{
            .scratchArena = m_scratchArena,
            .surfelGiCounterPersistentState = renderer.m_surfelGiCounterPersistentState,
            .raytracingSystem = renderer.m_raytracingSystem,
            .candidate = MakeNotNull(&readbackCounterStateCandidate),
            .buffers = counterBuffers,
        };
        const Core::GpuTaskGraphTaskRecordedCallback readbackRecordedCallback{ .task = renderer.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask, .context = &readbackContext, .invoke = FrameExecuteSurfelReadback::PrepareReadbackFinalState, };
        const Core::GpuTaskGraphTaskAcceptedCallback readbackAcceptedCallback{ .task = renderer.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask, .context = &readbackContext, .invoke = FrameExecuteSurfelReadback::AcceptReadbackFinalState, };
        const Core::GpuTaskScheduler& scheduler = renderer.m_graphics.gpuTasks();
        const b8 readbackAccepted = scheduler.executeTask(
            renderer.m_deferredLightingCompiledGraph,
            renderer.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask,
            &readbackRecordedCallback,
            &readbackAcceptedCallback,
            m_scratchArena,
            renderer.m_deferredLightingTaskGraph,
            renderer.m_deferredLightingRecordedGraph,
            renderer.m_deferredLightingSubmissionTransaction,
            &renderer.m_graphics.gpuTiming(),
            nullptr
        );
        const Core::QueueSubmissionToken readbackSubmissionToken = renderer.m_deferredLightingSubmissionTransaction.taskToken(m_compiledPlan, renderer.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask);
        if(!readbackSubmissionToken.valid())
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback record/submission was rejected"));
        else if(!readbackAccepted || !readbackContext.acceptedStateReady){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: accepted surfel counter-readback tail lost its retained state"));
            m_recovery.failFrameRenderRecovery();
            return false;
        }
        else{
            // Publish only after retained state commits.
            NWB_ASSERT(renderer.m_raytracingSystem.surfelCountReadbackSubmissionMatches(readbackSubmissionToken));
        }
    }
    return true;
}

void FrameExecutePresentTails::executeLaggedLightingHistoryCapture(DeferredFrameTargets& targets, const Core::GpuPhysicalQueueInfo* const historyQueue, const b8 requestsCapture, const b8 captureDeclared, const b8 laggedAsyncLightingSchedule){
    RendererFramePipeline& renderer = m_renderer;
    if(requestsCapture && !captureDeclared){
        // Present already completed; force next frame through bootstrap.
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting"));
        renderer.invalidateLaggedLightingHistorySubmission();
    }
    else if(captureDeclared){
        // The history copy depends on Present; publication needs accepted presentation.
        if(
            !m_finalPresentationSubmissionToken.valid()
            || !renderer.m_deferredLightingTaskGraphScheduled
            || !renderer.m_deferredLaggedLightingHistoryTask.valid()
            || !m_compiledPlan.findTask(renderer.m_deferredLaggedLightingHistoryTask).valid()
            || !historyQueue
            || (static_cast<u8>(historyQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ){
            if(renderer.m_deferredLightingTaskGraphScheduled){
                if(!renderer.m_deferredLightingSubmissionTransaction.discardUnaccepted(renderer.m_deferredLightingTaskGraph, renderer.m_deferredLightingCompiledGraph, renderer.m_deferredLightingRecordedGraph.recordingAttemptGeneration()))
                    renderer.m_graphics.requestDeviceRecreation();
            }
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting"));
            renderer.invalidateLaggedLightingHistorySubmission();
        }
        else{
            Core::GpuPersistentResourceStateCache::Candidate shadowHistoryReturnStateCandidate(renderer.m_shadowVisibilityReturnState);
            Core::GpuPersistentResourceStateCache::Candidate causticHistoryReturnStateCandidate(renderer.m_causticIrradianceReturnState);
            Core::GpuPersistentResourceStateCache::Candidate surfelHistoryReturnStateCandidate(renderer.m_surfelIrradianceReturnState);
            FrameExecuteHistoryCopy::Context historyCopyAcceptance{
                .scratchArena = m_scratchArena,
                .shadowVisibilityReturnState = renderer.m_shadowVisibilityReturnState,
                .causticIrradianceReturnState = renderer.m_causticIrradianceReturnState,
                .surfelIrradianceReturnState = renderer.m_surfelIrradianceReturnState,
                .targets = MakeNotNull(&targets),
                .shadowStateCandidate = MakeNotNull(&shadowHistoryReturnStateCandidate),
                .causticStateCandidate = MakeNotNull(&causticHistoryReturnStateCandidate),
                .surfelStateCandidate = MakeNotNull(&surfelHistoryReturnStateCandidate),
            };
            const Core::GpuTaskGraphTaskRecordedCallback historyCopyRecordedCallback{ .task = renderer.m_deferredLaggedLightingHistoryTask, .context = &historyCopyAcceptance, .invoke = FrameExecuteHistoryCopy::PrepareHistoryCopyFinalState, };
            const Core::GpuTaskGraphTaskAcceptedCallback historyCopyAcceptedCallback{ .task = renderer.m_deferredLaggedLightingHistoryTask, .context = &historyCopyAcceptance, .invoke = FrameExecuteHistoryCopy::AcceptHistoryCopyFinalState, };
            const Core::GpuTaskScheduler& scheduler = renderer.m_graphics.gpuTasks();
            const b8 historyCopyAccepted = scheduler.executeTask(
                renderer.m_deferredLightingCompiledGraph,
                renderer.m_deferredLaggedLightingHistoryTask,
                &historyCopyRecordedCallback,
                &historyCopyAcceptedCallback,
                m_scratchArena,
                renderer.m_deferredLightingTaskGraph,
                renderer.m_deferredLightingRecordedGraph,
                renderer.m_deferredLightingSubmissionTransaction,
                &renderer.m_graphics.gpuTiming(),
                nullptr
            );
            const Core::QueueSubmissionToken historyCopySubmissionToken = renderer.m_deferredLightingSubmissionTransaction.taskToken(m_compiledPlan, renderer.m_deferredLaggedLightingHistoryTask);
            if(historyCopySubmissionToken.valid() && (!historyCopyAccepted || !historyCopyAcceptance.acceptedStateReady)){
                if(!m_recovery.executeFrameRecoveryTask())
                    m_recovery.failFrameRenderRecovery();
                // The accepted copy cannot be replayed.
                m_recovery.failFrameRenderRecovery();
                return;
            }
            if(!historyCopyAccepted || !historyCopySubmissionToken.valid()){
                if(!renderer.m_deferredLightingSubmissionTransaction.discardUnaccepted(renderer.m_deferredLightingTaskGraph, renderer.m_deferredLightingCompiledGraph, renderer.m_deferredLightingRecordedGraph.recordingAttemptGeneration()))
                    renderer.m_graphics.requestDeviceRecreation();
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned lagged lighting-history capture record/submission was rejected; reverting to current-frame lighting"));
                renderer.invalidateLaggedLightingHistorySubmission();
            }
            else{
                // The accepted hook publishes this token; keep the assertion here.
                NWB_ASSERT(renderer.m_laggedLightingHistorySubmissionToken.queue == historyCopySubmissionToken.queue && renderer.m_laggedLightingHistorySubmissionToken.value == historyCopySubmissionToken.value);
                if(!laggedAsyncLightingSchedule)
                    renderer.reportLaggedLightingTransition(LaggedLightingReport::BootstrapAccepted, targets.laggedLightingHistory.generation);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

