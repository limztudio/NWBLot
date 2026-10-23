// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/execute/frame_execute_cpu_restore.h>
#include <impl/ecs_render/execute/frame_execute_history_copy.h>
#include <impl/ecs_render/execute/frame_execute_lifecycle.h>
#include <impl/ecs_render/execute/frame_execute_present_tails.h>
#include <impl/ecs_render/execute/frame_execute_recovery.h>
#include <impl/ecs_render/execute/frame_execute_state_lifecycles.h>
#include <impl/ecs_render/execute/frame_execute_timing_state.h>
#include <impl/ecs_render/execute/frame_execute_surfel_readback.h>
#include <impl/ecs_render/execute/graphics_prefix_timing_resolver.h>
#include <impl/ecs_render/execute/opaque_emulation_merge_validator.h>
#include <impl/ecs_render/execute/shadow_prepare_packet_validator.h>
#include <impl/ecs_render/execute/shadow_visibility_merge_validator.h>
#include <impl/ecs_render/execute/surfel_caustics_merge_validator.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/shared/renderer_scene_private.h>
#include <impl/ecs_scene/components.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererFramePipelineExecuteDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Capacity, not a runtime packet-count assumption.
inline constexpr usize s_DeferredTimingTicketCapacity = 15u + s_AvboitTaskGraphTimingTicketCapacity;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererFramePipeline::render(Core::Framebuffer* framebuffer){
    m_frameGraphSourceFrameIndex = m_graphics.getFrameIndex();

    // Preserve the accepted frontier; artifacts below reset for the next frame.
    if(m_deferredLightingTaskGraphScheduled){
        Core::Alloc::ScratchArena queueAssignmentTelemetryScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
        const Core::GpuCompiledGraph::ReadView compiledPlan(m_deferredLightingCompiledGraph);
        if(!m_deferredLightingTaskGraphQueueAssignmentTelemetry.update(declarations, m_deferredLightingTaskGraphQueueAssignments, compiledPlan, m_deferredLightingSubmissionTransaction, queueAssignmentTelemetryScratchArena))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred queue-assignment history refresh failed before graph reset"));
    }

    resetFrameTaskState();

    if(!framebuffer)
        return;

    const Core::AcquiredPresentationFrame presentationFrame = m_graphics.acquiredPresentationFrame();
    const Core::FramebufferDesc& presentationFramebufferDesc = framebuffer->getDescription();
    if(!presentationFrame.valid() || presentationFrame.framebuffer.get() != framebuffer || presentationFramebufferDesc.colorAttachments.size() != 1u || presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: render target did not match the acquired presentation frame; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return;
    }

    if(!m_frameTargets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_frameTargets;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    if(!m_shadowPreparationOutcome.resourcesValid){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow-visibility resource preflight was unavailable"));
        return;
    }

    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    const bool hasTransparentRenderers = m_preparedHasTransparentRenderers;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto& device = m_graphics.getDevice();
    if(m_graphics.isDeviceRecreationRequested() || device.requiresRecreation()){
        if(device.requiresRecreation())
            m_graphics.requestDeviceRecreation();
        return;
    }
    if(m_frameRenderRecoveryFailed){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame render recovery failed; rendering is suspended until resources are recreated"));
        return;
    }
    Core::GpuDescriptorHeap::PendingRecordingLease descriptorHeapPendingRecordingLease = device.getDescriptorHeap().acquirePendingRecordingLease();
    if(!descriptorHeapPendingRecordingLease.valid()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: descriptor heap pending-recording lease was unavailable; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return;
    }
    // Use the device's primary physical Graphics queue for scheduling.
    const Core::GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = computeFamilyIndex != Limit<u32>::s_Max && computeFamilyIndex != graphicsFamilyIndex;
    const bool laggedAsyncLightingRequested = m_frameLaggedAsyncLightingEnabled && dedicatedAsyncCompute;
    const bool laggedLightingHistoryResourcesReady = deferredTargets.laggedLightingHistory.valid();
    if(laggedAsyncLightingRequested && !laggedLightingHistoryResourcesReady){
        NWB_ASSERT(laggedLightingHistoryResourcesReady);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: lagged async lighting requires validated history targets"));
        return;
    }

    const u64 laggedLightingHistoryTargetGeneration = deferredTargets.laggedLightingHistory.generation;
    const Core::QueueSubmissionToken laggedLightingHistorySubmissionToken = m_laggedLightingHistorySubmissionToken;
    const auto laggedLightingHistoryTokenPending = [&device, laggedLightingHistoryTargetGeneration](const Core::QueueSubmissionToken& token, const u64 tokenTargetGeneration){
        if(!token.valid() || tokenTargetGeneration != laggedLightingHistoryTargetGeneration || !token.hasPhysicalQueueIdentity() || !device.matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration))
            return false;
        // A failed query keeps the writer pending.
        const Expected<u64> completedValue = device.queueGetCompletedInstance(Core::GpuPhysicalQueueId{ .index = token.physicalQueueIndex, .deviceGeneration = token.deviceGeneration, });
        return !completedValue || *completedValue < token.value;
    };
    const bool laggedLightingHistorySubmissionPending = laggedLightingHistoryTokenPending(laggedLightingHistorySubmissionToken, m_laggedLightingHistoryGeneration);

    // Keep only an incomplete accepted tail as the writer drain.
    if(laggedLightingHistorySubmissionPending){
        m_laggedLightingHistoryWriterDrainToken = laggedLightingHistorySubmissionToken;
        m_laggedLightingHistoryWriterDrainGeneration = m_laggedLightingHistoryGeneration;
    }
    else if(m_laggedLightingHistoryWriterDrainToken.valid() && !laggedLightingHistoryTokenPending(m_laggedLightingHistoryWriterDrainToken, m_laggedLightingHistoryWriterDrainGeneration))
        invalidateLaggedLightingHistoryWriterDrain();

    if(laggedAsyncLightingRequested){
        if(m_laggedLightingHistoryGeneration != laggedLightingHistoryTargetGeneration){
            // Generations block recycled slots after resize.
            invalidateLaggedLightingHistorySubmission();
            m_laggedLightingHistoryGeneration = laggedLightingHistoryTargetGeneration;
        }
    }
    else{
        resetLaggedLightingHistoryReadTracking();
    }
    // Snapshot the prior tail before declaration resets the token.
    const Core::QueueSubmissionToken priorLaggedLightingHistoryReadReadyToken = m_laggedLightingHistorySubmissionToken;
    const Core::QueueSubmissionToken priorLaggedLightingHistoryWriterDrainToken = m_laggedLightingHistoryWriterDrainToken;
    const bool laggedLightingHistoryWriterWaitPending = priorLaggedLightingHistoryWriterDrainToken.valid();
    const bool hardwareShadowSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && m_graphics.queryFeatureSupport(Core::Feature::RayQuery);
    const bool laggedAsyncLightingSchedule = laggedAsyncLightingRequested && laggedLightingHistoryResourcesReady && priorLaggedLightingHistoryReadReadyToken.valid();
    // The history tail is optional; its failure must not break the frame.
    const bool requestsLaggedLightingHistoryCapture = laggedAsyncLightingRequested;
    m_shadowPreparationOutcome.ready = false;
    // Compile graphs first; the prefix records its five tasks natively.
    const ECSRenderDetail::RendererFrameGraphFeatures frameGraphFeatures{
        .frameLaggedAsyncLightingEnabled = m_frameLaggedAsyncLightingEnabled,
        .laggedLightingHistoryReady = laggedLightingHistoryResourcesReady,
        .laggedLightingHistoryReadReady = priorLaggedLightingHistoryReadReadyToken.valid(),
        .laggedLightingHistoryWriterWaitPending = laggedLightingHistoryWriterWaitPending,
        .hasTransparentRenderers = hasTransparentRenderers,
        .hardwareCaustics = hardwareShadowSupported,
    };
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Preserve mirrors so rejected recordings retry exactly.
    const RayTracingFrameCpuStateSnapshot rayTracingCpuState = m_rayTracingState.captureFrameCpuState();
    const bool avboitTargetsNeedClear = m_avboitSystem.captureTargetClearState();
    const bool deferredBindlessSlotsUploaded = deferredTargets.bindless.slotsUploaded;
    const RayTracingShadowPreparationResourceSnapshot rayTracingShadowResources = m_raytracingSystem.snapshotShadowPreparationResources();
    const RayTracingDeferredGraphResourceSnapshot rayTracingGraphResources = m_raytracingSystem.snapshotDeferredGraphResources();
    const RayTracingSurfelPersistentResourceSnapshot rayTracingSurfelResources = m_raytracingSystem.snapshotSurfelPersistentResources();
    const FrameExecuteCpuRestore::Snapshot executeCpuRestoreSnapshot{ .rayTracing = rayTracingCpuState, .avboitTargetsNeedClear = avboitTargetsNeedClear, .deferredBindlessSlotsUploaded = deferredBindlessSlotsUploaded, };
    const auto restorePrefixCpuState = [&]()noexcept{
        FrameExecuteCpuRestore::RestorePrefixCpuState(m_meshSystem, m_deferredSystem);
    };

    const auto restoreShadowCpuState = [&]()noexcept{
        FrameExecuteCpuRestore::RestoreShadowCpuState(m_rayTracingState, rayTracingCpuState);
    };

    const auto restoreCausticsCpuState = [&]()noexcept{
        FrameExecuteCpuRestore::RestoreCausticsCpuState(m_rayTracingState, rayTracingCpuState);
    };
    const auto restoreSurfelGiCpuState = [&]()noexcept{
        FrameExecuteCpuRestore::RestoreSurfelGiCpuState(m_rayTracingState, rayTracingCpuState);
    };
    const auto restoreAvboitCpuState = [&]()noexcept{
        FrameExecuteCpuRestore::RestoreAvboitCpuState(m_avboitSystem, avboitTargetsNeedClear);
    };
    const auto restorePostGbufferPacketCpuState = [&](const bool restoreBindlessSlots)noexcept{
        FrameExecuteCpuRestore::RestorePostGbufferPacketCpuState(m_meshSystem, m_deferredSystem, m_rayTracingState, m_avboitSystem, executeCpuRestoreSnapshot, restoreBindlessSlots, deferredTargets);
    };
    // Retain this token even if Surfel GI consumes the diagnostic.
    const Core::QueueSubmissionToken surfelCounterReadbackCompletionToken = rayTracingCpuState.surfelCountReadbackSubmissionToken;

    FrameExecuteTimingState timing(m_graphics);
    bool shadowVisibilityOpaqueProduced = false;
    bool shadowVisibilityTransparentTraceProduced = false;
    u32 shadowVisibilityOpaqueFrameIndex = 0u;
    const bool clearAvboitTargets = m_avboitSystem.shouldClearTargets(hasTransparentRenderers);
    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(deferredTargets.framebuffer->getFramebufferInfo());
    Core::GpuGraphSubmissionTransaction::PayloadLifetimeScope graphPayloadLifetime(m_deferredLightingSubmissionTransaction, m_deferredLightingTaskGraph, m_deferredLightingCompiledGraph);
    // Graph build, its retry, and scheduling share one scratch arena.
    Core::Alloc::ScratchArena deferredGraphScratchArena(RendererArenaScope::s_TaskGraphArena);
    const auto buildDeferredGraph = [&](const bool captureLightingHistory){
        buildDeferredLightingTaskGraph(
            deferredGraphScratchArena,
            frameGraphFeatures,
            csgFrameState,
            clearAvboitTargets,
            hasTransparentRenderers,
            hasOpaqueCsgFrameWork,
            meshViewAspectRatio,
            presentationFrame,
            timing.asyncPrefixTimingSpansOnePacket,
            surfelCounterReadbackCompletionToken,
            priorLaggedLightingHistoryReadReadyToken,
            priorLaggedLightingHistoryWriterDrainToken,
            captureLightingHistory,
            deferredTargets,
            timing.frameTimingTransaction,
            timing.asyncPrefixTiming,
            timing.deferredClearTiming,
            timing.deferredClearTimingState,
            timing.opaqueCsgIntervalClearTimingState,
            timing.opaqueRegularSharedComputeEmulationTiming,
            timing.opaqueCsgIntervalSampleComputeEmulationTiming,
            timing.shadowPrepareTimingTicket,
            timing.graphicsPrefixTimingTickets,
            timing.asyncFinalTiming,
            timing.avboitPreTimingTicket,
            timing.avboitClearTimingState,
            timing.transparentCsgIntervalClearTimingState,
            timing.transparentCsgIntervalsTiming,
            timing.avboitOccupancyComputeEmulationTiming,
            timing.avboitExtinctionComputeEmulationTiming,
            timing.avboitAccumulationComputeEmulationTiming,
            timing.avboitDepthWarpTimingTicket,
            timing.avboitExtinctionTimingTicket,
            timing.avboitIntegrationTimingTicket,
            timing.avboitAccumulationTimingTicket,
            timing.shadowVisibilityTimingTicket,
            timing.shadowVisibilityAsyncTiming,
            timing.shadowVisibilityTiming,
            timing.opaqueSoftResolveTiming,
            timing.transparentSoftResolveTiming,
            shadowVisibilityOpaqueProduced,
            shadowVisibilityTransparentTraceProduced,
            shadowVisibilityOpaqueFrameIndex,
            timing.softwareCausticsTimingTicket,
            timing.surfelGiTimingTicket,
            timing.surfelGiAsyncTiming,
            timing.hardwareCausticsTimingTicket,
            timing.causticPhotonTiming,
            timing.causticResolveTiming,
            timing.deferredLightingTimingTicket,
            timing.deferredCompositeTimingTicket,
            timing.deferredPresentTimingTicket
        );
    };
    buildDeferredGraph(requestsLaggedLightingHistoryCapture);
    if(requestsLaggedLightingHistoryCapture && !m_deferredLightingTaskGraphDeclared){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph build with optional lagged lighting-history capture failed; retrying without the tail"));
        buildDeferredGraph(false);
    }
    const bool deferredGraphScheduled = scheduleDeferredLightingTaskGraphForExecution(deferredGraphScratchArena);
    const Core::GpuTaskGraph::DeclarationReadView deferredTaskGraphView(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView deferredCompiledPlan(m_deferredLightingCompiledGraph);
    const bool captureLaggedLightingHistory = m_deferredLaggedLightingHistoryTask.valid();
    const auto taskIsCompiled = [&](const Core::GpuTaskId task)noexcept{
        return deferredCompiledPlan.findTask(task).valid();
    };
    ShadowPreparePacketValidator shadowPreparePacketValidator(MakeNotNull(this));
    ShadowPreparePacketValidationResult shadowPreparePacket;
    shadowPreparePacketValidator.validate(deferredCompiledPlan, shadowPreparePacket);
    const bool shadowPrepareSoftwareBvhBuildsMerged = shadowPreparePacket.softwareBvhBuildsMerged;
    const bool shadowPrepareAccelStructFinalizeMerged = shadowPreparePacket.accelStructFinalizeMerged;
    const bool deferredBindlessSlotsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.bindlessSlotsUploadMerged;
    const bool rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.rayTraceMaterialContextSlotsUploadMerged;
    const bool causticEmissionTargetsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.causticEmissionTargetsUploadMerged;
    const bool surfelFrameConstantsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.surfelFrameConstantsUploadMerged;
    const bool shadowMaterialContextUploadsMergedIntoShadowPreparePacket = shadowPreparePacket.shadowMaterialContextUploadsMerged;
    const bool sceneBvhUploadsMergedIntoShadowPreparePacket = shadowPreparePacket.sceneBvhUploadsMerged;
    // CSG callbacks keep an independent anchor across FrontierSafe splits.
    static_assert(FrameExecuteTimingState::s_GraphicsPrefixTimingTicketCount == GraphicsPrefixTimingResolver::s_TimingTaskCount);
    Core::GpuTaskId graphicsPrefixTimingTasks[GraphicsPrefixTimingResolver::s_TimingTaskCount];
    GraphicsPrefixTimingResolver graphicsPrefixTimingResolver(MakeNotNull(this));
    GraphicsPrefixTimingResolutionResult graphicsPrefixTimingResolution;
    graphicsPrefixTimingResolver.resolve(deferredCompiledPlan, timing.graphicsPrefixOwnedTimingTickets, FrameExecuteTimingState::s_GraphicsPrefixTimingTicketCount, timing.graphicsPrefixTimingTickets, graphicsPrefixTimingTasks, timing.asyncPrefixTimingSpansOnePacket, graphicsPrefixTimingResolution);
    const bool graphicsPrefixTimingBindingsValid = graphicsPrefixTimingResolution.bindingsValid;
    const usize graphicsPrefixUniquePacketCount = graphicsPrefixTimingResolution.uniquePacketCount;
    ShadowVisibilityMergeValidator shadowVisibilityMergeValidator(MakeNotNull(this));
    ShadowVisibilityMergeValidationResult shadowVisibilityMerge;
    shadowVisibilityMergeValidator.validate(deferredCompiledPlan, shadowVisibilityMerge);
    const bool shadowVisibilityPreparedTasksMerged = shadowVisibilityMerge.preparedTasksMerged;
    const bool shadowVisibilityAllLitClearMerged = shadowVisibilityMerge.allLitClearMerged;
    const bool shadowVisibilityAdaptivePrimitivesMerged = shadowVisibilityMerge.adaptivePrimitivesMerged;
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = deferredCompiledPlan.queueInfoForTask(m_deferredShadowVisibilityTask);
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixQueue = deferredCompiledPlan.queueInfoForTask(m_graphicsPrefixTask);
    OpaqueEmulationMergeValidator opaqueEmulationMergeValidator(MakeNotNull(this));
    OpaqueEmulationMergeValidationResult opaqueEmulationMerge;
    opaqueEmulationMergeValidator.validate(deferredCompiledPlan, graphicsPrefixTimingTasks, FrameExecuteTimingState::s_GraphicsPrefixTimingTicketCount, graphicsPrefixTimingBindingsValid, hasOpaqueCsgFrameWork, opaqueEmulationMerge);
    const bool graphicsPrefixPacketsAreGraphics = opaqueEmulationMerge.packetsAreGraphics;
    const bool graphicsPrefixOpaqueComputeEmulationMerged = opaqueEmulationMerge.opaqueComputeEmulationMerged;
    const bool graphicsPrefixOpaqueSharedComputeEmulationMerged = opaqueEmulationMerge.opaqueSharedComputeEmulationMerged;
    const bool graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged = opaqueEmulationMerge.opaqueCsgReceiverComputeEmulationMerged;
    const bool graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged = opaqueEmulationMerge.opaqueCsgIntervalSampleComputeEmulationMerged;
    const bool graphicsPrefixCsgIntervalClearBundleMerged = opaqueEmulationMerge.csgIntervalClearBundleMerged;
    const Core::GpuPhysicalQueueInfo* const frameTimingBeginQueue = deferredCompiledPlan.queueInfoForTask(m_deferredFrameTimingBeginTask);
    const Core::GpuSubmissionPacketId frameTimingBeginPacket = deferredCompiledPlan.packetForTask(m_deferredFrameTimingBeginTask);
    const Core::GpuSubmissionPacketRange frameTimingEnvelope = deferredCompiledPlan.packetTimingEnvelopeRange();
    const Core::GpuPhysicalQueueInfo* const shadowPrepareQueue = deferredCompiledPlan.queueInfoForTask(m_deferredShadowPrepareTask);
    const Core::GpuPhysicalQueueInfo* const softwareCausticsQueue = deferredCompiledPlan.queueInfoForTask(m_deferredSoftwareCausticsTask);
    const RendererAvboitTaskGraphValidation avboitValidation = m_avboitSystem.validateTaskGraphStage(deferredCompiledPlan, clearAvboitTargets, hasTransparentRenderers);
    const Core::GpuTaskId causticsTask = hardwareShadowSupported ? m_deferredHardwareCausticsTask : m_deferredSoftwareCausticsTask;
    // History-selector uploads must share Lighting's acceptance boundary.
    const bool laggedLightingHistorySlotsUploadMergedIntoLightingPacket = !m_deferredLaggedLightingHistorySlotsUploadTask.valid() || deferredCompiledPlan.tasksSharePacket(m_deferredLightingTask, m_deferredLaggedLightingHistorySlotsUploadTask);
    // Resolve the terminal endpoint through the compiler, not renderer policy.
    const Core::GpuCompiledPresentEndpoint* const presentationEndpoint = deferredCompiledPlan.presentEndpoint();
    const Core::GpuTaskId terminalPresentationTask = presentationEndpoint ? presentationEndpoint->producer : Core::GpuTaskId{};
    const Core::GpuPhysicalQueueInfo* const deferredLightingQueue = deferredCompiledPlan.queueInfoForTask(m_deferredLightingTask);
    const Core::GpuPhysicalQueueInfo* const deferredCompositeQueue = deferredCompiledPlan.queueInfoForTask(m_deferredCompositeTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentQueue = deferredCompiledPlan.queueInfoForTask(m_deferredPresentTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentationOverlayQueue = deferredCompiledPlan.queueInfoForTask(m_deferredPresentationOverlayTask);
    const Core::GpuPhysicalQueueInfo* const terminalPresentationQueue = presentationEndpoint ? deferredCompiledPlan.queueInfo(presentationEndpoint->queue) : nullptr;
    const Core::GpuPhysicalQueueInfo* const deferredLaggedLightingHistoryQueue = deferredCompiledPlan.queueInfoForTask(m_deferredLaggedLightingHistoryTask);
    const Core::GpuPhysicalQueueInfo* const deferredFrameRecoveryQueue = deferredCompiledPlan.queueInfoForTask(m_deferredFrameRecoveryTask);
    SurfelCausticsMergeValidator surfelCausticsMergeValidator(MakeNotNull(this));
    SurfelCausticsMergeValidationResult surfelCausticsMerge;
    surfelCausticsMergeValidator.validate(deferredCompiledPlan, causticsTask, terminalPresentationTask, captureLaggedLightingHistory, surfelCausticsMerge);
    const Core::GpuPhysicalQueueInfo* const hardwareCausticsQueue = surfelCausticsMerge.hardwareCausticsQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiQueue = surfelCausticsMerge.surfelGiQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiPreparationQueue = surfelCausticsMerge.surfelGiPreparationQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiSnapshotCopyQueue = surfelCausticsMerge.surfelGiSnapshotCopyQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiCounterReadbackQueue = surfelCausticsMerge.surfelGiCounterReadbackQueue;
    const bool surfelGiOutputClearMergedIntoGiPacket = surfelCausticsMerge.surfelGiOutputClearMergedIntoGiPacket;
    const bool surfelGiPreparedPrefixMergedIntoGiPacket = surfelCausticsMerge.surfelGiPreparedPrefixMergedIntoGiPacket;
    const bool surfelGiInitializationLifecycleMergedIntoPreparationPacket = surfelCausticsMerge.surfelGiInitializationLifecycleMergedIntoPreparationPacket;
    const bool causticPhotonMergedIntoCausticsPacket = surfelCausticsMerge.causticPhotonMergedIntoCausticsPacket;
    const bool causticGeometryMergedIntoCausticsPacket = surfelCausticsMerge.causticGeometryMergedIntoCausticsPacket;
    const bool causticResolvePrepareMergedIntoCausticsPacket = surfelCausticsMerge.causticResolvePrepareMergedIntoCausticsPacket;
    const bool causticResolveWaveletMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveWaveletMergedIntoCausticsPacket;
    const bool causticResolveSecondWaveletMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveSecondWaveletMergedIntoCausticsPacket;
    const bool causticResolveThirdWaveletMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveThirdWaveletMergedIntoCausticsPacket;
    const bool causticResolveFourthWaveletMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveFourthWaveletMergedIntoCausticsPacket;
    const bool causticResolveFifthWaveletMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveFifthWaveletMergedIntoCausticsPacket;
    const bool causticResolveUpsampleMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveUpsampleMergedIntoCausticsPacket;
    const bool causticIrradianceClearMergedIntoCausticsPacket = surfelCausticsMerge.causticIrradianceClearMergedIntoCausticsPacket;
    const bool causticAccumulatorNonTemporalClearMergedIntoCausticsPacket = surfelCausticsMerge.causticAccumulatorNonTemporalClearMergedIntoCausticsPacket;
    const bool causticAccumulatorBootstrapClearMergedIntoCausticsPacket = surfelCausticsMerge.causticAccumulatorBootstrapClearMergedIntoCausticsPacket;
    const bool causticAccumulatorDecayMergedIntoCausticsPacket = surfelCausticsMerge.causticAccumulatorDecayMergedIntoCausticsPacket;
    const bool surfelGiSnapshotCopyAndTimingPacketsAreDistinct = surfelCausticsMerge.surfelGiSnapshotCopyAndTimingPacketsAreDistinct;
    const bool surfelCounterReadbackFollowsPresentation = surfelCausticsMerge.surfelCounterReadbackFollowsPresentation;
    const bool laggedLightingHistoryFollowsPresentation = surfelCausticsMerge.laggedLightingHistoryFollowsPresentation;
    if(
        !deferredGraphScheduled
        || !m_deferredLightingTaskGraphScheduled
        || !m_deferredFrameTimingBeginTask.valid()
        || !taskIsCompiled(m_deferredFrameTimingBeginTask)
        || !frameTimingBeginQueue
        || frameTimingBeginQueue->queueClass != Core::CommandQueue::Graphics
        || frameTimingBeginQueue->id != primaryGraphicsQueue
        || !frameTimingBeginPacket.valid()
        || frameTimingBeginPacket.index != 0u
        || !frameTimingEnvelope.valid()
        || frameTimingEnvelope.first != frameTimingBeginPacket
        || !m_deferredShadowPrepareTask.valid()
        || !taskIsCompiled(m_deferredShadowPrepareTask)
        || deferredCompiledPlan.tasksSharePacket(m_deferredFrameTimingBeginTask, m_deferredShadowPrepareTask)
        || !shadowPrepareSoftwareBvhBuildsMerged
        || !shadowPreparePacket.softwareSceneRefitMerged
        || !shadowPrepareAccelStructFinalizeMerged
        || !deferredBindlessSlotsUploadMergedIntoShadowPreparePacket
        || !rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket
        || !causticEmissionTargetsUploadMergedIntoShadowPreparePacket
        || !surfelFrameConstantsUploadMergedIntoShadowPreparePacket
        || !shadowMaterialContextUploadsMergedIntoShadowPreparePacket
        || !sceneBvhUploadsMergedIntoShadowPreparePacket
        || !shadowPrepareQueue
        || shadowPrepareQueue->queueClass != Core::CommandQueue::Graphics
        || !m_graphicsPrefixMeshViewSetupTask.valid()
        || !m_graphicsPrefixSceneShadingSetupTask.valid()
        || !m_graphicsPrefixDeferredClearTask.valid()
        || !m_graphicsPrefixGbufferTask.valid()
        || (hasOpaqueCsgFrameWork && (!m_graphicsPrefixCsgReceiverSpanTask.valid() || !m_graphicsPrefixCsgIntervalCombineTask.valid() || !m_graphicsPrefixCsgIntervalSampleTask.valid()))
        || !m_graphicsPrefixTask.valid()
        || !taskIsCompiled(m_graphicsPrefixMeshViewSetupTask)
        || !taskIsCompiled(m_graphicsPrefixSceneShadingSetupTask)
        || !taskIsCompiled(m_graphicsPrefixDeferredClearTask)
        || !taskIsCompiled(m_graphicsPrefixGbufferTask)
        || (hasOpaqueCsgFrameWork && (
            !deferredCompiledPlan.findTask(m_graphicsPrefixCsgReceiverSpanTask).valid()
            || !deferredCompiledPlan.findTask(m_graphicsPrefixCsgIntervalCombineTask).valid()
            || !deferredCompiledPlan.findTask(m_graphicsPrefixCsgIntervalSampleTask).valid()
        ))
        || !taskIsCompiled(m_graphicsPrefixTask)
        || !graphicsPrefixTimingBindingsValid
        || !graphicsPrefixPacketsAreGraphics
        || !graphicsPrefixOpaqueComputeEmulationMerged
        || !graphicsPrefixOpaqueSharedComputeEmulationMerged
        || !graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged
        || !graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged
        || !graphicsPrefixCsgIntervalClearBundleMerged
        || !graphicsPrefixQueue
        || graphicsPrefixQueue->queueClass != Core::CommandQueue::Graphics
        || !m_deferredShadowVisibilityTask.valid()
        || !taskIsCompiled(m_deferredShadowVisibilityTask)
        || !shadowVisibilityPreparedTasksMerged
        || !shadowVisibilityAllLitClearMerged
        || !shadowVisibilityAdaptivePrimitivesMerged
        || !shadowVisibilityQueue
        || (!hardwareShadowSupported && (!m_deferredSoftwareCausticsTask.valid() || !taskIsCompiled(m_deferredSoftwareCausticsTask) || !softwareCausticsQueue))
        || !m_deferredCausticIrradianceClearTask.valid()
        || !m_deferredCausticPhotonTask.valid()
        || !causticPhotonMergedIntoCausticsPacket
        || !m_deferredCausticGeometryTask.valid()
        || !causticGeometryMergedIntoCausticsPacket
        || !m_deferredCausticResolvePrepareTask.valid()
        || !causticResolvePrepareMergedIntoCausticsPacket
        || !m_deferredCausticResolveWaveletTask.valid()
        || !causticResolveWaveletMergedIntoCausticsPacket
        || !m_deferredCausticResolveSecondWaveletTask.valid()
        || !causticResolveSecondWaveletMergedIntoCausticsPacket
        || !m_deferredCausticResolveThirdWaveletTask.valid()
        || !causticResolveThirdWaveletMergedIntoCausticsPacket
        || !m_deferredCausticResolveFourthWaveletTask.valid()
        || !causticResolveFourthWaveletMergedIntoCausticsPacket
        || !m_deferredCausticResolveFifthWaveletTask.valid()
        || !causticResolveFifthWaveletMergedIntoCausticsPacket
        || !m_deferredCausticResolveUpsampleTask.valid()
        || !causticResolveUpsampleMergedIntoCausticsPacket
        || !causticIrradianceClearMergedIntoCausticsPacket
        || !causticAccumulatorNonTemporalClearMergedIntoCausticsPacket
        || !causticAccumulatorBootstrapClearMergedIntoCausticsPacket
        || !causticAccumulatorDecayMergedIntoCausticsPacket
        || !m_deferredSurfelGiTask.valid()
        || !m_deferredSurfelGiIrradianceClearTask.valid()
        || !surfelGiOutputClearMergedIntoGiPacket
        || !surfelGiPreparedPrefixMergedIntoGiPacket
        || !surfelGiInitializationLifecycleMergedIntoPreparationPacket
        || !taskIsCompiled(m_deferredSurfelGiTask)
        || !surfelGiQueue
        || (m_deferredSurfelGiSnapshotCopyTask.valid() && (
            !m_deferredSurfelGiPreparationTask.valid()
            || !taskIsCompiled(m_deferredSurfelGiPreparationTask)
            || !surfelGiPreparationQueue
            || !taskIsCompiled(m_deferredSurfelGiSnapshotCopyTask)
            || !surfelGiSnapshotCopyQueue
            || (static_cast<u8>(surfelGiSnapshotCopyQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || (m_deferredSurfelGiCounterReadbackTask.valid() && (
            !taskIsCompiled(m_deferredSurfelGiCounterReadbackTask)
            || !surfelGiCounterReadbackQueue
            || (static_cast<u8>(surfelGiCounterReadbackQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || !surfelCounterReadbackFollowsPresentation
        || (hardwareShadowSupported && (
            !m_deferredHardwareCausticsTask.valid()
            || !taskIsCompiled(m_deferredHardwareCausticsTask)
            || !hardwareCausticsQueue
            || (static_cast<u8>(hardwareCausticsQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Compute)) == 0u
        ))
        || !avboitValidation.valid()
        || !m_deferredLightingTask.valid()
        || !m_deferredCompositeTask.valid()
        || !m_deferredPresentTask.valid()
        || !m_deferredFrameTimingEndTask.valid()
        || (m_deferredPresentationOverlayRequired != m_deferredPresentationOverlayTask.valid())
        || !m_deferredFrameRecoveryTask.valid()
        || (m_deferredSurfelGiCounterReadbackCompletion.valid() && !surfelCounterReadbackCompletionToken.valid())
        || (captureLaggedLightingHistory && (
            !m_deferredLaggedLightingHistoryTask.valid()
            || !taskIsCompiled(m_deferredLaggedLightingHistoryTask)
            || !deferredLaggedLightingHistoryQueue
            || (static_cast<u8>(deferredLaggedLightingHistoryQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || !laggedLightingHistoryFollowsPresentation
        || (laggedAsyncLightingSchedule && !m_deferredLightingHistoryReadReadyCompletion.valid())
        || (laggedLightingHistoryWriterWaitPending && !m_deferredLightingHistoryWriterDrainCompletion.valid())
        || !taskIsCompiled(m_deferredLightingTask)
        || !laggedLightingHistorySlotsUploadMergedIntoLightingPacket
        || !taskIsCompiled(m_deferredCompositeTask)
        || !taskIsCompiled(m_deferredPresentTask)
        || !taskIsCompiled(m_deferredFrameTimingEndTask)
        || (m_deferredPresentationOverlayRequired && !taskIsCompiled(m_deferredPresentationOverlayTask))
        || !presentationEndpoint
        || !presentationEndpoint->valid()
        || presentationEndpoint->producer != m_deferredFrameTimingEndTask
        || !deferredTaskGraphView.validResource(presentationEndpoint->backBuffer)
        || !taskIsCompiled(m_deferredFrameRecoveryTask)
        || !deferredLightingQueue
        || !deferredCompositeQueue
        || !deferredPresentQueue
        || (m_deferredPresentationOverlayRequired && !deferredPresentationOverlayQueue)
        || !terminalPresentationQueue
        || !deferredFrameRecoveryQueue
        // Backbuffer writers need an acquired-image contract on the primary queue.
        || !primaryGraphicsQueue.valid()
        || presentationEndpoint->queue != primaryGraphicsQueue
        || deferredPresentQueue->id != primaryGraphicsQueue
        || (m_deferredPresentationOverlayRequired && deferredPresentationOverlayQueue->id != primaryGraphicsQueue)
        || terminalPresentationQueue->id != primaryGraphicsQueue
        || (laggedAsyncLightingSchedule && !surfelGiSnapshotCopyAndTimingPacketsAreDistinct)
        || (static_cast<u8>(deferredLightingQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Compute)) == 0u
        || (static_cast<u8>(deferredCompositeQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Compute)) == 0u
        || deferredPresentQueue->queueClass != Core::CommandQueue::Graphics
        || (m_deferredPresentationOverlayRequired && deferredPresentationOverlayQueue->queueClass != Core::CommandQueue::Graphics)
        || terminalPresentationQueue->queueClass != Core::CommandQueue::Graphics
        || deferredFrameRecoveryQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: rejected graph shadow packets opaque={} opaqueWavelet={} opaqueResolve={} transparent={} temporal={} wavelet={} terminal={}")
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityOpaqueTask).index
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityOpaqueFirstWaveletTask).index
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityOpaqueResolveTask).index
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityTransparentTraceTask).index
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityTransparentTemporalMergeTask).index
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityTransparentFirstWaveletTask).index
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityTask).index
        );
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: rejected graph lighting merges shadow={} allLit={} adaptive={} surfelClear={} surfelPrefix={} surfelInit={} causticPhoton={} causticGeometry={}")
            , shadowVisibilityPreparedTasksMerged, shadowVisibilityAllLitClearMerged, shadowVisibilityAdaptivePrimitivesMerged
            , surfelGiOutputClearMergedIntoGiPacket, surfelGiPreparedPrefixMergedIntoGiPacket
            , surfelGiInitializationLifecycleMergedIntoPreparationPacket, causticPhotonMergedIntoCausticsPacket
            , causticGeometryMergedIntoCausticsPacket
        );
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: rejected graph shadow merge decisions opaque={} transparent={} temporal={} wavelet={} terminal={} diagnostic={} diagnosticPacket={}")
            , static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_deferredShadowVisibilityOpaqueTask))
            , static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_deferredShadowVisibilityTransparentTraceTask))
            , static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_deferredShadowVisibilityTransparentTemporalMergeTask))
            , static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_deferredShadowVisibilityTransparentFirstWaveletTask))
            , static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_deferredShadowVisibilityTask))
            , static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_deferredShadowVisibilityDiagnosticsTask))
            , deferredCompiledPlan.packetForTask(m_deferredShadowVisibilityDiagnosticsTask).index
        );
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compiled deferred graph topology was unavailable (declared={} scheduled={})"), m_deferredLightingTaskGraphDeclared, deferredGraphScheduled);
        timing.discardTickets();
        // No packet accepted yet; restore the CPU-only classification.
        m_rayTracingState.restorePreparedLightingCpuState(rayTracingCpuState);
        return;
    }
    FrameExecuteRecovery recovery(*this, timing.frameTimingTransaction, deferredCompiledPlan);
    const auto discardRenderPackets = [&](){
        timing.discardAll();
        if(!recovery.discardUnacceptedGraphPackets()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred graph cancellation overlapped active native work; requesting device recreation"));
            m_graphics.requestDeviceRecreation();
        }
        const bool shadowPrepareAccepted = taskIsCompiled(m_deferredShadowPrepareTask) && m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredShadowPrepareTask).valid();
        restorePostGbufferPacketCpuState(!shadowPrepareAccepted);
        m_raytracingSystem.discardSoftShadowTemporalHistory();
    };

    FrameExecuteStateLifecycleSet lifecycles(*this, deferredTargets, rayTracingShadowResources, rayTracingGraphResources, rayTracingSurfelResources, hardwareShadowSupported, laggedAsyncLightingSchedule);
    m_avboitSystem.markFrameTargetUsage(hasTransparentRenderers);

    Core::GpuTaskGraphTaskTimingTicket normalTimingTickets[RendererFramePipelineExecuteDetail::s_DeferredTimingTicketCapacity] = {};
    const auto normalTimingTicketCount = timing.bindNormalTimingTickets(*this, deferredCompiledPlan, graphicsPrefixTimingTasks, graphicsPrefixUniquePacketCount, avboitValidation, hardwareShadowSupported, LengthOf(normalTimingTickets), normalTimingTickets);
    if(!normalTimingTicketCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to bind normal deferred graph timing"));
        discardRenderPackets();
        return;
    }

    // Empty hooks use direct presentation for standalone passes.
    const Core::QueueSubmissionPreSubmitHook framePresentationSignal = m_graphics.claimFramePresentationSignal();
    const Core::GpuTaskGraphTaskSubmissionHook terminalPresentationSubmissionHooks[] = { Core::GpuTaskGraphTaskSubmissionHook{ .task = terminalPresentationTask, .hook = framePresentationSignal, }, };
    Core::GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = terminalPresentationTask;
    normalExecution.taskRecordedCallbacks = lifecycles.recordedCallbacks;
    normalExecution.taskRecordedCallbackCount = lifecycles.recordedCallbackCount;
    normalExecution.readyFrontierScheduler = &m_world.taskScheduler();
    normalExecution.taskTimingTickets = normalTimingTickets;
    normalExecution.taskTimingTicketCount = *normalTimingTicketCount;
    normalExecution.taskAcceptedCallbacks = lifecycles.acceptedCallbacks;
    normalExecution.taskAcceptedCallbackCount = lifecycles.acceptedCallbackCount;
    normalExecution.taskSubmissionHooks = framePresentationSignal.valid() ? terminalPresentationSubmissionHooks : nullptr;
    normalExecution.taskSubmissionHookCount = framePresentationSignal.valid() ? LengthOf(terminalPresentationSubmissionHooks) : 0u;

    const Core::GpuTaskScheduler& normalScheduler = m_graphics.gpuTasks();
    const bool normalGraphAccepted = normalScheduler.executeGraph(
        normalExecution,
        lifecycles.normalExecutionScratchArena,
        m_deferredLightingTaskGraph,
        m_deferredLightingCompiledGraph,
        m_deferredLightingRecordedGraph,
        m_deferredLightingSubmissionTransaction,
        &m_graphics.gpuTiming()
    );

    const Core::QueueSubmissionToken frameTimingBeginSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredFrameTimingBeginTask);
    const Core::QueueSubmissionToken shadowPrepareSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredShadowPrepareTask);
    const Core::QueueSubmissionToken graphicsPrefixSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_graphicsPrefixTask);
    const Core::QueueSubmissionToken shadowVisibilitySubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredShadowVisibilityTask);
    const Core::GpuTaskId selectedCausticsTask = hardwareShadowSupported ? m_deferredHardwareCausticsTask : m_deferredSoftwareCausticsTask;
    const Core::QueueSubmissionToken selectedCausticsSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, selectedCausticsTask);
    const Core::QueueSubmissionToken surfelGiSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredSurfelGiTask);
    const Core::QueueSubmissionToken avboitPreSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, avboitValidation.stage().firstTask);
    const Core::QueueSubmissionToken avboitCompletionSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, avboitValidation.stage().completionTask);
    const Core::QueueSubmissionToken deferredLightingSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredLightingTask);
    const Core::QueueSubmissionToken deferredCompositeSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredCompositeTask);
    const Core::QueueSubmissionToken deferredPresentSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredPresentTask);
    const Core::QueueSubmissionToken finalPresentationSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, terminalPresentationTask);

    bool presentationSignalReady = true;
    if(framePresentationSignal.valid()){
        if(finalPresentationSubmissionToken.valid()){
            presentationSignalReady = m_graphics.confirmFramePresentationSignal(framePresentationSignal, finalPresentationSubmissionToken);
            if(!presentationSignalReady && !m_graphics.cancelFramePresentationSignal(framePresentationSignal))
                m_graphics.requestDeviceRecreation();
        }
        else{
            presentationSignalReady = false;
            if(!m_graphics.cancelFramePresentationSignal(framePresentationSignal))
                m_graphics.requestDeviceRecreation();
        }
    }

    const bool selectedCausticsStateReady = lifecycles.selectedCausticsStateReady(hardwareShadowSupported);
    const bool normalSemanticTokensReady =
        frameTimingBeginSubmissionToken.valid()
        && frameTimingBeginSubmissionToken.matchesPhysicalQueue(primaryGraphicsQueue.index, primaryGraphicsQueue.deviceGeneration)
        && shadowPrepareSubmissionToken.valid()
        && graphicsPrefixSubmissionToken.valid()
        && shadowVisibilitySubmissionToken.valid()
        && selectedCausticsSubmissionToken.valid()
        && surfelGiSubmissionToken.valid()
        && avboitPreSubmissionToken.valid()
        && avboitCompletionSubmissionToken.valid()
        && deferredLightingSubmissionToken.valid()
        && deferredCompositeSubmissionToken.valid()
        && deferredPresentSubmissionToken.valid()
        && finalPresentationSubmissionToken.valid()
    ;
    const bool normalStateReady = lifecycles.shadowPrepareStateLifecycle.stateReady && lifecycles.shadowVisibilityStateLifecycle.stateReady && selectedCausticsStateReady && lifecycles.surfelGiStateLifecycle.stateReady && lifecycles.deferredLightingStateLifecycle.stateReady;
    const bool acceptedStateLost =
        (shadowPrepareSubmissionToken.valid() && !lifecycles.shadowPrepareStateLifecycle.stateReady)
        || (shadowVisibilitySubmissionToken.valid() && !lifecycles.shadowVisibilityStateLifecycle.stateReady)
        || (selectedCausticsSubmissionToken.valid() && !selectedCausticsStateReady)
        || (surfelGiSubmissionToken.valid() && !lifecycles.surfelGiStateLifecycle.stateReady)
        || (deferredLightingSubmissionToken.valid() && !lifecycles.deferredLightingStateLifecycle.stateReady)
    ;

    bool frameTimingEndReady = true;
    if(finalPresentationSubmissionToken.valid()){
        frameTimingEndReady = timing.frameTimingTransaction.confirmEndSubmission(finalPresentationSubmissionToken, true);
        if(!frameTimingEndReady){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm frame critical-path timing"));
            timing.frameTimingTransaction.discard();
        }
    }
    if(!normalGraphAccepted || !normalSemanticTokensReady || !normalStateReady || !presentationSignalReady){
        const bool hadAcceptedPackets = m_deferredLightingSubmissionTransaction.hasAcceptedPackets();
        bool recovered = true;
        if(!hadAcceptedPackets){
            discardRenderPackets();
        }
        else{
            recovered = recovery.recoverPendingFrameThenDiscardUnaccepted();
            timing.discardTickets();

            if(!shadowPrepareSubmissionToken.valid()){
                restorePostGbufferPacketCpuState(true);
            }
            else{
                if(!graphicsPrefixSubmissionToken.valid())
                    restorePrefixCpuState();
                if(!shadowVisibilitySubmissionToken.valid()){
                    restoreShadowCpuState();
                    m_raytracingSystem.discardSoftShadowTemporalHistory();
                }
                if(!selectedCausticsSubmissionToken.valid())
                    restoreCausticsCpuState();
                if(!surfelGiSubmissionToken.valid())
                    restoreSurfelGiCpuState();
                if(!avboitPreSubmissionToken.valid())
                    restoreAvboitCpuState();
            }
        }

        if(deferredPresentSubmissionToken.valid() && !finalPresentationSubmissionToken.valid()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: acquired back buffer was written before presentation suffix rejection; requesting recreation"));
            m_graphics.requestDeviceRecreation();
        }
        if(!presentationSignalReady && finalPresentationSubmissionToken.valid()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: terminal graph presentation signal confirmation failed"));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned normal deferred execution was rejected"));
        }
        if(!recovered || acceptedStateLost || (!presentationSignalReady && finalPresentationSubmissionToken.valid()))
            recovery.failFrameRenderRecovery();
        return;
    }

    if(!dedicatedAsyncCompute && m_frameLaggedAsyncLightingEnabled){
        reportLaggedLightingTransition(LaggedLightingReport::NoDedicatedAsyncCompute, deferredTargets.laggedLightingHistory.generation);
    }
    else if(m_laggedLightingCurrentFrameAcceptancePending){
        reportLaggedLightingTransition(LaggedLightingReport::CurrentFrameAccepted, deferredTargets.laggedLightingHistory.generation);
        m_laggedLightingCurrentFrameAcceptancePending = false;
    }

    FrameExecutePresentTails presentTails(*this, recovery, deferredCompiledPlan, finalPresentationSubmissionToken);
    if(!presentTails.executeSurfelCounterReadback(surfelGiCounterReadbackQueue, lifecycles.surfelGiCounterBuffers, LengthOf(lifecycles.surfelGiCounterBuffers)))
        return;
    presentTails.executeLaggedLightingHistoryCapture(deferredTargets, deferredLaggedLightingHistoryQueue, requestsLaggedLightingHistoryCapture, captureLaggedLightingHistory, laggedAsyncLightingSchedule);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

