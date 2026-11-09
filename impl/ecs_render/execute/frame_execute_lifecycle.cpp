// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_lifecycle.h"
#include "frame_execute_recovery.h"
#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <core/task/gpu/scheduler.h>
#include "frame_execute_timing_state.h"
#include <impl/ecs_render/kernel/timing_names.h>
#include "frame_execute_state_lifecycles.h"
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include "frame_execute_present_tails.h"
#include <impl/ecs_render/execute/frame_execute_recovery.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::PrepareShadowPrepareTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::ShadowPrepareStateLifecycleContext* const context = static_cast<ShadowPrepareStateLifecycleContext*>(rawContext);
    if(!context || !finalState)
        return false;
    const b8 candidateBuilt = context->renderer->m_shadowPreparePersistentState.buildMergedBufferSubset(*finalState, context->buffers, context->scratchArena, *context->stateCandidate);
    const b8 candidatePresent = context->stateCandidate->valid() && !context->stateCandidate->empty();
    context->statePrepared = candidateBuilt && (candidatePresent || !context->stateCandidateRequired);
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::AcceptShadowPrepareTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token){
    FrameExecuteLifecycle::ShadowPrepareStateLifecycleContext* const context = static_cast<ShadowPrepareStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    if(!context->stateCandidate->valid() || (context->stateCandidate->empty() && context->stateCandidateRequired)){
        context->stateReady = false;
        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
        return false;
    }

    context->stateReady = renderer.m_shadowPreparePersistentState.commit(*context->stateCandidate);
    if(!context->stateReady){
        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
        return false;
    }
    renderer.m_raytracingSystem.confirmPreparedSceneTlasBuild();
    renderer.m_raytracingSystem.confirmPreparedMeshBlasBuilds();
    renderer.m_raytracingSystem.confirmPreparedMeshSwBvhBuilds();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::PrepareShadowVisibilityTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext* const context = static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext);
    if(!context || context->returnTextures.empty() || context->scratchTextures.empty() || context->scratchBuffers.empty() || !finalState)
        return false;

    // An inactive route retains its live allocation and accepted native state until that route runs again.
    const b8 scratchStateReady = context->renderer->m_shadowComputePersistentState.buildMergedResourceSubset(
        *finalState,
        context->scratchTextures,
        context->scratchBuffers,
        context->scratchArena,
        *context->scratchStateCandidate
    );
    const b8 returnStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(*finalState, context->returnTextures, {}, context->scratchArena, *context->returnStateCandidate);
    context->statePrepared = returnStateReady && scratchStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::AcceptShadowVisibilityTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token){
    FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext* const context = static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    const b8 returnStateReady = context->renderer->m_shadowVisibilityReturnState.commit(*context->returnStateCandidate);
    const b8 scratchStateReady = context->renderer->m_shadowComputePersistentState.commit(*context->scratchStateCandidate);
    context->stateReady = returnStateReady && scratchStateReady;
    context->renderer->m_raytracingSystem.finalizeSoftShadowTemporalHistory(*context->targets);
    if(context->stateReady && context->lightSpacePrepared)
        context->renderer->m_raytracingSystem.acceptLightSpaceShadowCapture(context->lightSpaceCaptureTicket, *context->lightSpacePrepared);
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::PrepareSoftwareCausticsTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::SoftwareCausticsStateLifecycleContext* const context = static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || context->irradianceTextures.empty() || context->scratchTextures.empty() || !finalState)
        return false;

    const b8 returnStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(*finalState, context->irradianceTextures, {}, context->scratchArena, *context->returnStateCandidate);
    const b8 scratchStateReady = context->renderer->m_causticsComputePersistentState.buildFilteredResourceSubset(*finalState, context->scratchTextures, {}, context->scratchArena, *context->scratchStateCandidate);
    context->statePrepared = returnStateReady && scratchStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::AcceptSoftwareCausticsTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteLifecycle::SoftwareCausticsStateLifecycleContext* const context = static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    const b8 returnStateReady = context->renderer->m_causticIrradianceReturnState.commit(*context->returnStateCandidate);
    const b8 scratchStateReady = context->renderer->m_causticsComputePersistentState.commit(*context->scratchStateCandidate);
    context->stateReady = returnStateReady && scratchStateReady;
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::PrepareSurfelGiTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
    if(!context || context->returnTextures.empty() || context->counterBuffers.empty() || context->computeTextures.empty() || context->computeBuffers.empty() || !finalState)
        return false;

    const b8 returnStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(*finalState, context->returnTextures, {}, context->scratchArena, *context->returnStateCandidate);
    const b8 counterStateReady = context->renderer->m_surfelGiCounterPersistentState.buildFilteredResourceSubset(*finalState, {}, context->counterBuffers, context->scratchArena, *context->counterStateCandidate);
    const b8 computeStateReady =
        context->renderer->m_surfelGiComputePersistentState.buildFilteredResourceSubset(
            *finalState,
            context->computeTextures,
            context->computeBuffers,
            context->scratchArena,
            *context->computeStateCandidate
        )
    ;
    context->statePrepared = returnStateReady && counterStateReady && computeStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::AcceptSurfelGiTask(void* const rawContext, const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteLifecycle::SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    const b8 returnStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->returnStateCandidate);
    const b8 counterStateReady = renderer.m_surfelGiCounterPersistentState.commit(*context->counterStateCandidate);
    const b8 computeStateReady = renderer.m_surfelGiComputePersistentState.commit(*context->computeStateCandidate);
    context->stateReady = returnStateReady && counterStateReady && computeStateReady;
    if(context->stateReady)
        renderer.m_raytracingSystem.confirmRayTraceContextReadSubmission(token);
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::PrepareHardwareCausticsTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::HardwareCausticsStateLifecycleContext* const context = static_cast<HardwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || context->accumulatorTextures.empty() || !finalState)
        return false;

    const b8 accumulatorStateReady =
        context->renderer->m_hardwareCausticAccumulatorPersistentState.buildFilteredResourceSubset(*finalState, context->accumulatorTextures, {}, context->scratchArena, *context->accumulatorStateCandidate)
    ;
    context->statePrepared = accumulatorStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::AcceptHardwareCausticsTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteLifecycle::HardwareCausticsStateLifecycleContext* const context = static_cast<HardwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    const b8 accumulatorStateReady = context->renderer->m_hardwareCausticAccumulatorPersistentState.commit(*context->accumulatorStateCandidate);
    context->stateReady = accumulatorStateReady;
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::PrepareDeferredLightingTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::DeferredLightingStateLifecycleContext* const context = static_cast<DeferredLightingStateLifecycleContext*>(rawContext);
    if(!context || context->shadowReturnTextures.empty() || context->causticReturnTextures.empty() || context->surfelReturnTextures.empty() || !finalState)
        return false;

    b8 shadowStateReady = true;
    b8 causticStateReady = true;
    b8 surfelStateReady = true;
    if(!context->usesLaggedHistory){
        shadowStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(*finalState, context->shadowReturnTextures, {}, context->scratchArena, *context->shadowReturnStateCandidate);
        causticStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
            *finalState,
            context->causticReturnTextures,
            {},
            context->scratchArena,
            *context->causticReturnStateCandidate
        );
        surfelStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(*finalState, context->surfelReturnTextures, {}, context->scratchArena, *context->surfelReturnStateCandidate);
    }
    context->statePrepared = shadowStateReady && causticStateReady && surfelStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteLifecycle::AcceptDeferredLightingTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token){
    FrameExecuteLifecycle::DeferredLightingStateLifecycleContext* const context = static_cast<DeferredLightingStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    if(context->usesLaggedHistory)
        context->targets->laggedLightingHistory.slotsUploaded = true;
    b8 shadowStateReady = true;
    b8 causticStateReady = true;
    b8 surfelStateReady = true;
    if(!context->usesLaggedHistory){
        shadowStateReady = renderer.m_shadowVisibilityReturnState.commit(*context->shadowReturnStateCandidate);
        causticStateReady = renderer.m_causticIrradianceReturnState.commit(*context->causticReturnStateCandidate);
        surfelStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->surfelReturnStateCandidate);
    }
    context->stateReady = shadowStateReady && causticStateReady && surfelStateReady;
    if(!context->stateReady)
        return false;
    if(context->usesLaggedHistory)
        renderer.reportLaggedLightingTransition(LaggedLightingReport::ActiveHistoryAccepted, context->targets->laggedLightingHistory.generation);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FrameExecuteCpuRestore::RestorePrefixCpuState(RendererMeshSystem& meshSystem, RendererDeferredSystem& deferredSystem)noexcept{
    // Rejected recording invalidates upload mirrors.
    meshSystem.invalidateMeshViewBufferUploadMirror();
    deferredSystem.invalidateSceneLightingUploadMirrors();
}

void FrameExecuteCpuRestore::RestorePostGbufferPacketCpuState(
    const Snapshot& snapshot,
    const b8 restoreBindlessSlots,
    RendererMeshSystem& meshSystem,
    RendererDeferredSystem& deferredSystem,
    RendererRayTracingState& rayTracingState,
    RendererAvboitSystem& avboitSystem,
    DeferredFrameTargets& targets
)noexcept{
    if(restoreBindlessSlots)
        targets.bindless.slotsUploaded = snapshot.deferredBindlessSlotsUploaded;
    RestorePrefixCpuState(meshSystem, deferredSystem);
    rayTracingState.restoreShadowPacketCpuState(snapshot.rayTracing);
    rayTracingState.restoreCausticPacketCpuState(snapshot.rayTracing);
    rayTracingState.restoreSurfelGiPacketCpuState(snapshot.rayTracing);
    avboitSystem.restoreTargetClearState(snapshot.avboitTargetsNeedClear);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameExecuteRecovery::discardUnacceptedGraphPackets(){
    RendererFramePipeline& renderer = m_renderer;
    return renderer.m_deferredLightingSubmissionTransaction.discardUnaccepted(renderer.m_deferredLightingTaskGraph, renderer.m_deferredLightingCompiledGraph, renderer.m_deferredLightingRecordedGraph.recordingAttemptGeneration());
}

b8 FrameExecuteRecovery::executeFrameRecoveryTask(){
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
    b8 retireTiming = m_frameTimingTransaction.needsRetirement();
    if(retireTiming && !m_frameTimingTransaction.prepareForRecovery()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frame timing recovery preparation failed; continuing resource/frontier recovery"));
        m_frameTimingTransaction.discard();
        retireTiming = false;
    }
    renderer.m_deferredFrameRecoveryArmed = true;
    renderer.m_deferredFrameRecoveryRetiresTiming = retireTiming;
    const Core::GpuTaskScheduler& scheduler = renderer.m_graphics.gpuTasks();
    const b8 recoveryAccepted = scheduler.executeAcceptedFrontierTask(
        renderer.m_deferredLightingCompiledGraph,
        renderer.m_deferredFrameRecoveryTask,
        m_scratchArena,
        renderer.m_deferredLightingTaskGraph,
        renderer.m_deferredLightingRecordedGraph,
        renderer.m_deferredLightingSubmissionTransaction,
        &renderer.m_graphics.gpuTiming()
    );
    if(!recoveryAccepted){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery record/submission was rejected"));
        return false;
    }
    return true;
}

b8 FrameExecuteRecovery::recoverPendingFrameSubmission(){
    return (m_renderer.m_deferredLightingSubmissionTransaction.hasAcceptedPackets() || m_frameTimingTransaction.needsRetirement()) ? executeFrameRecoveryTask() : true;
}

b8 FrameExecuteRecovery::recoverPendingFrameThenDiscardUnaccepted(){
    const b8 recovered = recoverPendingFrameSubmission();
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
        .rebindableTimingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(DeferredGraphicsPrefixTimingSlot::DeferredClear)],
        .scope = RendererGpuTimingScope::s_DeferredClear,
    }
    , opaqueCsgIntervalClearTimingState{
        .graphics = &graphics,
        .timing = &opaqueCsgIntervalClearTiming,
        .rebindableTimingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(DeferredGraphicsPrefixTimingSlot::Gbuffer)],
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
    const Span<const Core::GpuTaskId> graphicsPrefixTimingTasks,
    const usize graphicsPrefixUniquePacketCount,
    const RendererAvboitTaskGraphValidation& avboitValidation,
    const b8 hardwareShadowSupported,
    const Span<Core::GpuTaskGraphTaskTimingTicket> tickets
){
    if(graphicsPrefixTimingTasks.size() != s_GraphicsPrefixTimingTaskCount)
        return MakeUnexpected(Failure{});
    usize ticketCount = 0u;
    const auto appendTicket = [&](const Core::GpuTaskId task, Core::GpuTimingSubmissionTicket& timingTicket)noexcept -> b8 {
        if(!task.valid() || ticketCount >= tickets.size())
            return false;
        tickets[ticketCount++] = Core::GpuTaskGraphTaskTimingTicket{ .task = task, .timingTicket = &timingTicket, };
        return true;
    };
    b8 ready = appendTicket(renderer.m_shadowPrepareGraph.m_deferredShadowPrepareTask, shadowPrepareTimingTicket);
    for(usize prefixTaskIndex = 0u; ready && prefixTaskIndex < s_GraphicsPrefixTimingTaskCount; ++prefixTaskIndex){
        const Core::GpuTaskId task = graphicsPrefixTimingTasks[prefixTaskIndex];
        b8 packetAlreadyTimed = false;
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
        && appendTicket(renderer.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask, shadowVisibilityTimingTicket)
        && appendTicket(hardwareShadowSupported ? renderer.m_causticsGraph.m_deferredHardwareCausticsTask : renderer.m_causticsGraph.m_deferredSoftwareCausticsTask, hardwareShadowSupported ? hardwareCausticsTimingTicket : softwareCausticsTimingTicket)
        && appendTicket(renderer.m_surfelGiGraph.m_deferredSurfelGiTask, surfelGiTimingTicket)
    ;
    RendererAvboitTaskGraphTimingTickets avboitTimingTickets{
        .m_pre = avboitPreTimingTicket,
        .m_depthWarp = avboitDepthWarpTimingTicket,
        .m_extinction = avboitExtinctionTimingTicket,
        .m_integration = avboitIntegrationTimingTicket,
        .m_accumulation = avboitAccumulationTimingTicket,
    };
    ready = ready
        && renderer.m_avboitSystem.appendTaskGraphTimingTickets(avboitValidation, avboitTimingTickets, tickets, ticketCount)
        && appendTicket(renderer.m_deferredLightingTask, deferredLightingTimingTicket)
        && appendTicket(renderer.m_deferredCompositeTask, deferredCompositeTimingTicket)
        && appendTicket(renderer.m_deferredPresentTask, deferredPresentTimingTicket)
    ;
    if(!ready)
        return MakeUnexpected(Failure{});
    return ticketCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameExecuteStateLifecycleSet::FrameExecuteStateLifecycleSet(
    RendererFramePipeline& renderer,
    DeferredFrameTargets& targets,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources,
    const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources,
    const b8 hardwareShadowSupported,
    const b8 usesLaggedHistory
)
    : shadowPrepareStateScratchArena(s_TaskGraphArena)
    , meshAccelerationStateBuffers(shadowPrepareStateScratchArena)
    , shadowPrepareLiveStateBuffers(shadowPrepareStateScratchArena)
    , shadowPrepareAcceptedStateCandidate(renderer.m_shadowPreparePersistentState)
    , normalExecutionScratchArena(s_TaskGraphArena)
    , shadowVisibilityReturnTextures{ targets.shadowVisibility, }
    , lightSpaceShadowResources(renderer.m_raytracingSystem.lightSpaceShadowSnapshot())
    , shadowComputeScratchTextures{
        lightSpaceShadowResources.depth,
        targets.shadowCoarseTransmittance,
        targets.shadowSoftHalfA,
        targets.shadowSoftHalfB,
        targets.shadowSoftGeometry,
        targets.shadowSoftGeometryPrev,
        targets.shadowHistA,
        targets.shadowHistB,
        targets.shadowMomentsA,
        targets.shadowMomentsB,
        targets.transparentSoftHalf,
        targets.transparentHistA,
        targets.transparentHistB,
        targets.transparentMomentsA,
        targets.transparentMomentsB,
    }
    , shadowComputeScratchBuffers{
        lightSpaceShadowResources.counts,
        lightSpaceShadowResources.events,
        lightSpaceShadowResources.views,
        lightSpaceShadowResources.drawArguments,
        lightSpaceShadowResources.csgContext,
        lightSpaceShadowResources.csgOpaqueDepth,
        rayTracingShadowResources.swShadowEdgeCounterBuffer,
        rayTracingShadowResources.swShadowEdgeListBuffer,
        rayTracingShadowResources.swShadowIndirectArgsBuffer,
        rayTracingGraphResources.hardwareTransparentCrossingsBuffer,
        rayTracingGraphResources.hardwareTransparentOverflowListBuffer,
        rayTracingGraphResources.hardwareTransparentOverflowArgsBuffer,
    }
    , shadowVisibilityReturnStateCandidate(renderer.m_shadowVisibilityReturnState)
    , shadowComputeScratchStateCandidate(renderer.m_shadowComputePersistentState)
    , causticsComputeScratchTextures{ targets.causticAccumulator, targets.causticHistory, targets.causticResolveHalf, targets.causticResolveGeometry, }
    , causticIrradianceTextures{ targets.causticIrradiance, }
    , causticReturnStateCandidate(renderer.m_causticIrradianceReturnState)
    , causticsScratchStateCandidate(renderer.m_causticsComputePersistentState)
    , surfelIrradianceReturnTextures{ targets.surfelIrradiance, }
    , surfelGiCounterBuffers{ rayTracingSurfelResources.counterBuffer, }
    , surfelGiComputeScratchTextures{ targets.surfelIrradianceHalf, }
    , surfelGiComputeScratchBuffers{
        rayTracingSurfelResources.poolBuffer,
        rayTracingSurfelResources.cellHeadBuffer,
        rayTracingSurfelResources.traceIndirectArgsBuffer,
        rayTracingSurfelResources.freeListBuffer,
        rayTracingSurfelResources.poolSnapshotBuffer,
        rayTracingSurfelResources.cellHeadSnapshotBuffer,
    }
    , surfelIrradianceReturnStateCandidate(renderer.m_surfelIrradianceReturnState)
    , surfelGiCounterStateCandidate(renderer.m_surfelGiCounterPersistentState)
    , surfelGiComputeStateCandidate(renderer.m_surfelGiComputePersistentState)
    , hardwareCausticAccumulatorTextures{ targets.causticAccumulator, }
    , hardwareCausticAccumulatorStateCandidate(renderer.m_hardwareCausticAccumulatorPersistentState)
    , deferredLightingShadowReturnStateCandidate(renderer.m_shadowVisibilityReturnState)
    , deferredLightingCausticReturnStateCandidate(renderer.m_causticIrradianceReturnState)
    , deferredLightingSurfelReturnStateCandidate(renderer.m_surfelIrradianceReturnState)
    , shadowPrepareStateLifecycle{
        .renderer = MakeNotNull(&renderer),
        .scratchArena = shadowPrepareStateScratchArena,
        .stateCandidate = MakeNotNull(&shadowPrepareAcceptedStateCandidate),
    }
    , shadowVisibilityStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .targets = MakeNotNull(&targets),
        .returnStateCandidate = MakeNotNull(&shadowVisibilityReturnStateCandidate),
        .scratchStateCandidate = MakeNotNull(&shadowComputeScratchStateCandidate),
        .returnTextures = shadowVisibilityReturnTextures,
        .scratchTextures = shadowComputeScratchTextures,
        .scratchBuffers = shadowComputeScratchBuffers,
        .lightSpacePrepared = &renderer.m_shadowPrepareGraph.m_shadowPreparationOutcome.ready,
        .lightSpaceCaptureTicket = lightSpaceShadowResources.captureTicket,
    }
    , softwareCausticsStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .returnStateCandidate = MakeNotNull(&causticReturnStateCandidate),
        .scratchStateCandidate = MakeNotNull(&causticsScratchStateCandidate),
        .irradianceTextures = causticIrradianceTextures,
        .scratchTextures = causticsComputeScratchTextures,
    }
    , surfelGiStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .returnStateCandidate = MakeNotNull(&surfelIrradianceReturnStateCandidate),
        .counterStateCandidate = MakeNotNull(&surfelGiCounterStateCandidate),
        .computeStateCandidate = MakeNotNull(&surfelGiComputeStateCandidate),
        .returnTextures = surfelIrradianceReturnTextures,
        .counterBuffers = surfelGiCounterBuffers,
        .computeTextures = surfelGiComputeScratchTextures,
        .computeBuffers = surfelGiComputeScratchBuffers,
    }
    , hardwareCausticsStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .accumulatorStateCandidate = MakeNotNull(&hardwareCausticAccumulatorStateCandidate),
        .accumulatorTextures = hardwareCausticAccumulatorTextures,
    }
    , deferredLightingStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .targets = MakeNotNull(&targets),
        .shadowReturnStateCandidate = MakeNotNull(&deferredLightingShadowReturnStateCandidate),
        .causticReturnStateCandidate = MakeNotNull(&deferredLightingCausticReturnStateCandidate),
        .surfelReturnStateCandidate = MakeNotNull(&deferredLightingSurfelReturnStateCandidate),
        .shadowReturnTextures = shadowVisibilityReturnTextures,
        .causticReturnTextures = causticIrradianceTextures,
        .surfelReturnTextures = surfelIrradianceReturnTextures,
        .usesLaggedHistory = usesLaggedHistory,
    }
{
    collectShadowPrepareStateBuffers(renderer, rayTracingShadowResources, rayTracingGraphResources);
    bindCallbacks(renderer, hardwareShadowSupported);
}


void FrameExecuteStateLifecycleSet::collectShadowPrepareStateBuffers(RendererFramePipeline& renderer, const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources, const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources){
    renderer.m_meshSystem.collectRetainedAccelerationStateBuffers(meshAccelerationStateBuffers);
    const auto& acceptedTraceGeometry = renderer.m_raytracingSystem.acceptedShadowTraceGeometryBuffers();
    const auto& preparedTraceGeometry = renderer.m_raytracingSystem.preparedShadowTraceGeometryBuffers();
    const usize shadowPrepareFixedCapacity = rayTracingGraphResources.csgTraceContextBuffer ? s_ShadowPrepareCsgStateBufferCapacity : s_ShadowPrepareFixedStateBufferCapacity;
    shadowPrepareLiveStateBuffers.reserve(AddSize(AddSize(acceptedTraceGeometry.size(), preparedTraceGeometry.size()), AddSize(meshAccelerationStateBuffers.size(), shadowPrepareFixedCapacity)));
    const auto appendStateBuffer = [this](const Core::BufferHandle& buffer)noexcept{
        if(buffer)
            shadowPrepareLiveStateBuffers.push_back(buffer);
    };
    if(rayTracingShadowResources.sceneTlasBackingBuffer)
        appendStateBuffer(rayTracingShadowResources.sceneTlasBackingBuffer);
    b8 stateCandidateRequired = static_cast<b8>(rayTracingShadowResources.sceneTlas) || renderer.m_raytracingSystem.preparedMeshSwBvhBuildsReady() || renderer.m_raytracingSystem.shadowVisibilitySoftwareResourcesPreflighted();
    // Keep accepted trace-geometry state with the handoff, including invisible streams.
    for(const Core::BufferHandle& acceptedBuffer : acceptedTraceGeometry){
        appendStateBuffer(acceptedBuffer);
        stateCandidateRequired = true;
    }
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        appendStateBuffer(preparedBuffer.buffer);
        stateCandidateRequired = true;
    }
    for(const Core::BufferHandle& buffer : meshAccelerationStateBuffers){
        if(buffer)
            stateCandidateRequired = true;
        appendStateBuffer(buffer);
    }
    appendStateBuffer(rayTracingShadowResources.bvhSortKeysBuffer);
    appendStateBuffer(rayTracingShadowResources.bvhSortPayloadBuffer);
    appendStateBuffer(rayTracingShadowResources.bvhVisitCounterBuffer);
    if(rayTracingGraphResources.csgTraceContextBuffer){
        appendStateBuffer(rayTracingGraphResources.csgTraceContextBuffer);
        stateCandidateRequired = true;
    }

    shadowPrepareStateLifecycle.buffers = shadowPrepareLiveStateBuffers;
    shadowPrepareStateLifecycle.stateCandidateRequired = stateCandidateRequired;
}

void FrameExecuteStateLifecycleSet::bindCallbacks(const RendererFramePipeline& renderer, const b8 hardwareShadowSupported)noexcept{
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_shadowPrepareGraph.m_deferredShadowPrepareTask, .context = &shadowPrepareStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareShadowPrepareTask, };
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask, .context = &shadowVisibilityStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareShadowVisibilityTask, };
    if(!hardwareShadowSupported)
        recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_causticsGraph.m_deferredSoftwareCausticsTask, .context = &softwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareSoftwareCausticsTask, };
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_surfelGiGraph.m_deferredSurfelGiTask, .context = &surfelGiStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareSurfelGiTask, };
    if(hardwareShadowSupported)
        recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_causticsGraph.m_deferredHardwareCausticsTask, .context = &hardwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareHardwareCausticsTask, };
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_deferredLightingTask, .context = &deferredLightingStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareDeferredLightingTask, };
    NWB_ASSERT(recordedCallbackCount <= recordedCallbacks.size());

    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_shadowPrepareGraph.m_deferredShadowPrepareTask, .context = &shadowPrepareStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptShadowPrepareTask, };
    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask, .context = &shadowVisibilityStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptShadowVisibilityTask, };
    if(!hardwareShadowSupported)
        acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_causticsGraph.m_deferredSoftwareCausticsTask, .context = &softwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptSoftwareCausticsTask, };
    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_surfelGiGraph.m_deferredSurfelGiTask, .context = &surfelGiStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptSurfelGiTask, };
    if(hardwareShadowSupported)
        acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_causticsGraph.m_deferredHardwareCausticsTask, .context = &hardwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptHardwareCausticsTask, };
    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_deferredLightingTask, .context = &deferredLightingStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptDeferredLightingTask, };
    NWB_ASSERT(acceptedCallbackCount <= acceptedCallbacks.size());
}


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
