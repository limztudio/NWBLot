// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_recovery.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/kernel/arena_names.h>

#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FrameExecuteRecovery::discardUnacceptedGraphPackets(){
    RendererFramePipeline& renderer = m_renderer;
    return renderer.m_deferredLightingSubmissionTransaction.discardUnaccepted(renderer.m_deferredLightingTaskGraph, renderer.m_deferredLightingCompiledGraph, renderer.m_deferredLightingRecordedGraph.recordingAttemptGeneration());
}

bool FrameExecuteRecovery::executeFrameRecoveryTask(){
    RendererFramePipeline& renderer = m_renderer;

    // Retire the scope after rejection; Graphics order needs no extra wait.
    if(renderer.m_graphics.getDevice().requiresRecreation()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery packet skipped because the graphics device requires recreation"));
        renderer.m_deferredFrameRecoveryArmed = false;
        renderer.m_deferredFrameRecoveryRetiresTiming = false;
        m_frameTimingTransaction.discard();
        return false;
    }
    if(!renderer.m_deferredLightingTaskGraphScheduled || !renderer.m_deferredFrameRecoveryTask.valid() || !m_compiledPlan.findTask(renderer.m_deferredFrameRecoveryTask).valid()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery task was unavailable"));
        renderer.m_deferredFrameRecoveryArmed = false;
        renderer.m_deferredFrameRecoveryRetiresTiming = false;
        m_frameTimingTransaction.discard();
        return false;
    }
    bool retireTiming = m_frameTimingTransaction.needsRetirement();
    if(retireTiming && !m_frameTimingTransaction.prepareForRecovery()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frame timing recovery preparation failed; continuing resource/frontier recovery"));
        m_frameTimingTransaction.discard();
        retireTiming = false;
    }
    renderer.m_deferredFrameRecoveryArmed = true;
    renderer.m_deferredFrameRecoveryRetiresTiming = retireTiming;
    Core::Alloc::ScratchArena recoveryScratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskScheduler& scheduler = renderer.m_graphics.gpuTasks();
    const bool recoveryAccepted = scheduler.executeAcceptedFrontierTask(
        renderer.m_deferredLightingTaskGraph,
        renderer.m_deferredLightingCompiledGraph,
        renderer.m_deferredLightingRecordedGraph,
        renderer.m_deferredFrameRecoveryTask,
        renderer.m_deferredLightingSubmissionTransaction,
        &renderer.m_graphics.gpuTiming(),
        recoveryScratchArena
    );
    if(!recoveryAccepted){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery record/submission was rejected"));
        return false;
    }
    return true;
}

bool FrameExecuteRecovery::recoverPendingFrameSubmission(){
    return (m_renderer.m_deferredLightingSubmissionTransaction.hasAcceptedPackets() || m_frameTimingTransaction.needsRetirement()) ? executeFrameRecoveryTask() : true;
}

bool FrameExecuteRecovery::recoverPendingFrameThenDiscardUnaccepted(){
    const bool recovered = recoverPendingFrameSubmission();
    // Record/submit while Declared; then reject remaining normal packets.
    return discardUnacceptedGraphPackets() && recovered;
}

void FrameExecuteRecovery::failFrameRenderRecovery(){
    RendererFramePipeline& renderer = m_renderer;
    if(renderer.m_frameRenderRecoveryFailed)
        return;
    renderer.m_frameRenderRecoveryFailed = true;
    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: cannot safely continue after an unresolved frame recovery submission; requesting device recreation"));
    // Defer recreation until accepted work is safe.
    renderer.m_graphics.requestDeviceRecreation();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

