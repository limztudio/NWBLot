// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_present_tails.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/execute/frame_execute_history_copy.h>
#include <impl/ecs_render/execute/frame_execute_recovery.h>
#include <impl/ecs_render/execute/frame_execute_surfel_readback.h>
#include <impl/ecs_render/kernel/arena_names.h>

#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FrameExecutePresentTails::executeSurfelCounterReadback(const Core::GpuPhysicalQueueInfo* const readbackQueue, const Core::BufferHandle* const counterBuffers, const usize counterBufferCount){
    RendererFramePipeline& renderer = m_renderer;
    if(!renderer.m_deferredSurfelGiCounterReadbackTask.valid())
        return true;

    // No normal-frame consumer; record only after Present.
    const bool readbackTailAvailable =
        m_finalPresentationSubmissionToken.valid()
        && renderer.m_deferredLightingTaskGraphScheduled
        && m_compiledPlan.findTask(renderer.m_deferredSurfelGiCounterReadbackTask).valid()
        && readbackQueue
        && (static_cast<u8>(readbackQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) != 0u
    ;
    if(!readbackTailAvailable){
        renderer.m_deferredLightingSubmissionTransaction.rejectTask(renderer.m_deferredLightingTaskGraph, renderer.m_deferredLightingCompiledGraph, renderer.m_deferredSurfelGiCounterReadbackTask, renderer.m_deferredLightingRecordedGraph.recordingAttemptGeneration());
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback tail was unavailable"));
    }
    else{
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
        Core::GpuPersistentResourceStateCache::Candidate readbackCounterStateCandidate(renderer.m_surfelGiCounterPersistentState);
        // Keep the candidate private until Transfer accepts.
        FrameExecuteSurfelReadback::Context readbackContext{ .scratchArena = scratchArena, .renderer = MakeNotNull(&renderer), .candidate = MakeNotNull(&readbackCounterStateCandidate), .buffers = counterBuffers, .bufferCount = counterBufferCount, };
        const Core::GpuTaskGraphTaskRecordedCallback readbackRecordedCallback{ .task = renderer.m_deferredSurfelGiCounterReadbackTask, .context = &readbackContext, .invoke = FrameExecuteSurfelReadback::PrepareReadbackFinalState, };
        const Core::GpuTaskGraphTaskAcceptedCallback readbackAcceptedCallback{ .task = renderer.m_deferredSurfelGiCounterReadbackTask, .context = &readbackContext, .invoke = FrameExecuteSurfelReadback::AcceptReadbackFinalState, };
        const Core::GpuTaskScheduler& scheduler = renderer.m_graphics.gpuTasks();
        const bool readbackAccepted = scheduler.executeTask(
            renderer.m_deferredLightingTaskGraph,
            renderer.m_deferredLightingCompiledGraph,
            renderer.m_deferredLightingRecordedGraph,
            renderer.m_deferredSurfelGiCounterReadbackTask,
            &readbackRecordedCallback,
            renderer.m_deferredLightingSubmissionTransaction,
            &renderer.m_graphics.gpuTiming(),
            scratchArena,
            nullptr,
            &readbackAcceptedCallback
        );
        const Core::QueueSubmissionToken readbackSubmissionToken = renderer.m_deferredLightingSubmissionTransaction.taskToken(m_compiledPlan, renderer.m_deferredSurfelGiCounterReadbackTask);
        if(!readbackSubmissionToken.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback record/submission was rejected"));
        }
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

void FrameExecutePresentTails::executeLaggedLightingHistoryCapture(DeferredFrameTargets& targets, const Core::GpuPhysicalQueueInfo* const historyQueue, const bool requestsCapture, const bool captureDeclared, const bool laggedAsyncLightingSchedule){
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
            Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
            Core::GpuPersistentResourceStateCache::Candidate shadowHistoryReturnStateCandidate(renderer.m_shadowVisibilityReturnState);
            Core::GpuPersistentResourceStateCache::Candidate causticHistoryReturnStateCandidate(renderer.m_causticIrradianceReturnState);
            Core::GpuPersistentResourceStateCache::Candidate surfelHistoryReturnStateCandidate(renderer.m_surfelIrradianceReturnState);
            FrameExecuteHistoryCopy::Context historyCopyAcceptance{
                .scratchArena = scratchArena,
                .renderer = MakeNotNull(&renderer),
                .targets = MakeNotNull(&targets),
                .shadowStateCandidate = MakeNotNull(&shadowHistoryReturnStateCandidate),
                .causticStateCandidate = MakeNotNull(&causticHistoryReturnStateCandidate),
                .surfelStateCandidate = MakeNotNull(&surfelHistoryReturnStateCandidate),
            };
            const Core::GpuTaskGraphTaskRecordedCallback historyCopyRecordedCallback{ .task = renderer.m_deferredLaggedLightingHistoryTask, .context = &historyCopyAcceptance, .invoke = FrameExecuteHistoryCopy::PrepareHistoryCopyFinalState, };
            const Core::GpuTaskGraphTaskAcceptedCallback historyCopyAcceptedCallback{ .task = renderer.m_deferredLaggedLightingHistoryTask, .context = &historyCopyAcceptance, .invoke = FrameExecuteHistoryCopy::AcceptHistoryCopyFinalState, };
            const Core::GpuTaskScheduler& scheduler = renderer.m_graphics.gpuTasks();
            const bool historyCopyAccepted = scheduler.executeTask(
                renderer.m_deferredLightingTaskGraph,
                renderer.m_deferredLightingCompiledGraph,
                renderer.m_deferredLightingRecordedGraph,
                renderer.m_deferredLaggedLightingHistoryTask,
                &historyCopyRecordedCallback,
                renderer.m_deferredLightingSubmissionTransaction,
                &renderer.m_graphics.gpuTiming(),
                scratchArena,
                nullptr,
                &historyCopyAcceptedCallback
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
                if(!laggedAsyncLightingSchedule){
                    renderer.reportLaggedLightingTransition(RendererFramePipeline::LaggedLightingReport::BootstrapAccepted, targets.laggedLightingHistory.generation);
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

