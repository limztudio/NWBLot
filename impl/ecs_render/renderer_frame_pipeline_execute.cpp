// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

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


// Capacities, not a runtime packet-count assumption.
inline constexpr usize s_DeferredStateLifecycleCallbackCapacity = 6u;
inline constexpr usize s_DeferredTimingTicketCapacity = 15u + s_AvboitTaskGraphTimingTicketCapacity;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererFramePipeline::render(Core::Framebuffer* framebuffer){
    m_frameGraphSourceFrameIndex = m_graphics.getFrameIndex();

    // Preserve the accepted frontier; artifacts below reset for the next frame.
    if(m_deferredLightingTaskGraphValid){
        Core::Alloc::ScratchArena queueAssignmentTelemetryScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
        const Core::GpuCompiledGraph::ReadView compiledPlan(m_deferredLightingCompiledGraph);
        if(!m_deferredLightingTaskGraphQueueAssignmentTelemetry.update(
            declarations,
            m_deferredLightingTaskGraphQueueAssignments,
            compiledPlan,
            m_deferredLightingSubmissionTransaction,
            queueAssignmentTelemetryScratchArena
        ))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred queue-assignment history refresh failed before graph reset"));
    }

    resetFrameTaskState();

    if(!framebuffer)
        return;

    const Core::AcquiredPresentationFrame presentationFrame = m_graphics.acquiredPresentationFrame();
    const Core::FramebufferDesc& presentationFramebufferDesc = framebuffer->getDescription();
    if(
        !presentationFrame.valid()
        || presentationFrame.framebuffer.get() != framebuffer
        || presentationFramebufferDesc.colorAttachments.size() != 1u
        || presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()
    ){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: render target did not match the acquired presentation frame; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return;
    }

    if(!m_frameTargets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_frameTargets;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    NWB_ASSERT(m_shadowPreparationOutcome.resourcesValid);
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
    Core::GpuDescriptorHeap::PendingRecordingLease descriptorHeapPendingRecordingLease =
        device.getDescriptorHeap().acquirePendingRecordingLease()
    ;
    if(!descriptorHeapPendingRecordingLease.valid()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: descriptor heap pending-recording lease was unavailable; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return;
    }
    // Scheduling queries the graph-visible transport, not the legacy lane.
    const Core::GpuPhysicalQueueId primaryGraphicsQueue =
        device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const bool laggedAsyncLightingRequested = m_frameLaggedAsyncLightingEnabled && dedicatedAsyncCompute;
    const bool laggedLightingHistoryResourcesReady = deferredTargets.laggedLightingHistory.valid();
    if(laggedAsyncLightingRequested && !laggedLightingHistoryResourcesReady){
        NWB_ASSERT(laggedLightingHistoryResourcesReady);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: lagged async lighting requires validated history targets"));
        return;
    }

    const u64 laggedLightingHistoryTargetGeneration = deferredTargets.laggedLightingHistory.generation;
    const Core::QueueSubmissionToken laggedLightingHistorySubmissionToken =
        m_laggedLightingHistorySubmissionToken
    ;
    const auto laggedLightingHistoryTokenPending = [&device, laggedLightingHistoryTargetGeneration](
        const Core::QueueSubmissionToken& token,
        const u64 tokenTargetGeneration
    ){
        return token.valid()
        && tokenTargetGeneration == laggedLightingHistoryTargetGeneration
        && token.hasPhysicalQueueIdentity()
        && device.matchesPhysicalQueueIdentity(
            token.queue,
            token.physicalQueueIndex,
            token.deviceGeneration
        )
        && device.queueGetCompletedInstance(
            Core::GpuPhysicalQueueId{
                token.physicalQueueIndex,
                token.deviceGeneration,
            }
        ) < token.value
    ;
    };
    const bool laggedLightingHistorySubmissionPending = laggedLightingHistoryTokenPending(
        laggedLightingHistorySubmissionToken,
        m_laggedLightingHistoryGeneration
    );

    // Keep only an incomplete accepted tail as the writer drain.
    if(laggedLightingHistorySubmissionPending){
        m_laggedLightingHistoryWriterDrainToken = laggedLightingHistorySubmissionToken;
        m_laggedLightingHistoryWriterDrainGeneration = m_laggedLightingHistoryGeneration;
    }
    else if(
        m_laggedLightingHistoryWriterDrainToken.valid()
        && !laggedLightingHistoryTokenPending(
            m_laggedLightingHistoryWriterDrainToken,
            m_laggedLightingHistoryWriterDrainGeneration
        )
    )
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
    const Core::QueueSubmissionToken priorLaggedLightingHistoryReadReadyToken =
        m_laggedLightingHistorySubmissionToken
    ;
    const Core::QueueSubmissionToken priorLaggedLightingHistoryWriterDrainToken =
        m_laggedLightingHistoryWriterDrainToken
    ;
    const bool laggedLightingHistoryWriterWaitPending = priorLaggedLightingHistoryWriterDrainToken.valid();
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    const bool laggedAsyncLightingSchedule =
        laggedAsyncLightingRequested
        && laggedLightingHistoryResourcesReady
        && priorLaggedLightingHistoryReadReadyToken.valid()
    ;
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
    m_raytracingSystem.retireCompletedAdaptiveShadowStatisticsReadback();

    // Preserve mirrors so rejected recordings retry exactly.
    const RayTracingFrameCpuStateSnapshot rayTracingCpuState = m_rayTracingState.captureFrameCpuState();
    const bool avboitTargetsNeedClear = m_avboitSystem.captureTargetClearState();
    const bool deferredBindlessSlotsUploaded = deferredTargets.bindless.slotsUploaded;
    const RayTracingShadowPreparationResourceSnapshot rayTracingShadowResources =
        m_raytracingSystem.snapshotShadowPreparationResources()
    ;
    const RayTracingSurfelPersistentResourceSnapshot rayTracingSurfelResources =
        m_raytracingSystem.snapshotSurfelPersistentResources()
    ;
    const auto restorePrefixCpuState = [&](){
        // Rejected recording invalidates upload mirrors.
        m_meshSystem.invalidateMeshViewBufferUploadMirror();
        m_deferredSystem.invalidateSceneLightingUploadMirrors();
    };

    const auto restoreShadowCpuState = [&](){
        m_rayTracingState.restoreShadowPacketCpuState(rayTracingCpuState);
    };

    const auto restoreCausticsCpuState = [&](){
        m_rayTracingState.restoreCausticPacketCpuState(rayTracingCpuState);
    };
    const auto restoreSurfelGiCpuState = [&](){
        m_rayTracingState.restoreSurfelGiPacketCpuState(rayTracingCpuState);
    };
    const auto restoreAvboitCpuState = [&](){
        m_avboitSystem.restoreTargetClearState(avboitTargetsNeedClear);
    };
    const auto restorePostGbufferEffectsCpuState = [&](){
        restoreCausticsCpuState();
        restoreSurfelGiCpuState();
        restoreAvboitCpuState();
    };
    const auto restorePostGbufferPacketCpuState = [&](const bool restoreBindlessSlots){
        if(restoreBindlessSlots)
            deferredTargets.bindless.slotsUploaded = deferredBindlessSlotsUploaded;
        restorePrefixCpuState();
        restoreShadowCpuState();
        restorePostGbufferEffectsCpuState();
    };
    // Retain this token even if Surfel GI consumes the diagnostic.
    const Core::QueueSubmissionToken surfelCounterReadbackCompletionToken = rayTracingCpuState.surfelCountReadbackSubmissionToken;

    // Prefix stages start separate; shared packets rebind to one ticket.
    Core::GpuTimingSubmissionTicket shadowPrepareTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixMeshViewSetupTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixSceneShadingSetupTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixDeferredClearTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixGbufferTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixCsgReceiverSpanTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixCsgIntervalCombineTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixCsgIntervalSampleTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket graphicsPrefixNormalizeTimingTicket(m_graphics.gpuTiming());
    constexpr usize graphicsPrefixTimingTicketCount = static_cast<usize>(
        ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::kCount
    );
    Core::GpuTimingSubmissionTicket* graphicsPrefixTimingTickets[graphicsPrefixTimingTicketCount] = {
        &graphicsPrefixMeshViewSetupTimingTicket,
        &graphicsPrefixSceneShadingSetupTimingTicket,
        &graphicsPrefixDeferredClearTimingTicket,
        &graphicsPrefixGbufferTimingTicket,
        &graphicsPrefixCsgReceiverSpanTimingTicket,
        &graphicsPrefixCsgIntervalCombineTimingTicket,
        &graphicsPrefixCsgIntervalSampleTimingTicket,
        &graphicsPrefixNormalizeTimingTicket,
    };
    Core::GpuTimingSubmissionTicket* const graphicsPrefixOwnedTimingTickets[graphicsPrefixTimingTicketCount] = {
        &graphicsPrefixMeshViewSetupTimingTicket,
        &graphicsPrefixSceneShadingSetupTimingTicket,
        &graphicsPrefixDeferredClearTimingTicket,
        &graphicsPrefixGbufferTimingTicket,
        &graphicsPrefixCsgReceiverSpanTimingTicket,
        &graphicsPrefixCsgIntervalCombineTimingTicket,
        &graphicsPrefixCsgIntervalSampleTimingTicket,
        &graphicsPrefixNormalizeTimingTicket,
    };
    // AsyncPrefix is valid only when the endpoints share one submission.
    bool asyncPrefixTimingSpansOnePacket = true;
    Optional<Core::GpuTimingMeasure> asyncPrefixTiming;
    Optional<Core::GpuTimingMeasure> deferredClearTiming;
    GraphClearTimingRecordState deferredClearTimingState{
        .graphics = &m_graphics,
        .timing = &deferredClearTiming,
        .rebindableTimingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(
            ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::DeferredClear
        )],
        .scope = RendererGpuTimingScope::s_DeferredClear,
    };
    Optional<Core::GpuTimingMeasure> opaqueCsgIntervalClearTiming;
    GraphClearTimingRecordState opaqueCsgIntervalClearTimingState{
        .graphics = &m_graphics,
        .timing = &opaqueCsgIntervalClearTiming,
        .rebindableTimingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(
            ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::Gbuffer
        )],
        .scope = RendererGpuTimingScope::s_CsgIntervalClear,
    };
    // Keep this measurement alive like CSG intervals.
    Optional<Core::GpuTimingMeasure> opaqueRegularSharedComputeEmulationTiming;
    // Interval-sample compute/raster may span two callbacks.
    Optional<Core::GpuTimingMeasure> opaqueCsgIntervalSampleComputeEmulationTiming;
    Core::GpuTimingSubmissionTicket shadowVisibilityTimingTicket(m_graphics.gpuTiming());
    // The terminal fold still closes the Shadow Visibility range.
    Optional<Core::GpuTimingMeasure> shadowVisibilityAsyncTiming;
    Optional<Core::GpuTimingMeasure> shadowVisibilityTiming;
    Optional<Core::GpuTimingMeasure> opaqueSoftResolveTiming;
    Optional<Core::GpuTimingMeasure> transparentSoftResolveTiming;
    bool shadowVisibilityOpaqueProduced = false;
    bool shadowVisibilityTransparentTraceProduced = false;
    u32 shadowVisibilityOpaqueFrameIndex = 0u;
    Core::GpuTimingSubmissionTicket softwareCausticsTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket surfelGiTimingTicket(m_graphics.gpuTiming());
    // Age/free opens it; the GI callback closes it.
    Optional<Core::GpuTimingMeasure> surfelGiAsyncTiming;
    Core::GpuTimingSubmissionTicket hardwareCausticsTimingTicket(m_graphics.gpuTiming());
    // Decay opens it; the photon producer closes it.
    Optional<Core::GpuTimingMeasure> causticPhotonTiming;
    // Downsample opens it; wavelet resolve closes it.
    Optional<Core::GpuTimingMeasure> causticResolveTiming;
    const bool clearAvboitTargets = m_avboitSystem.shouldClearTargets(hasTransparentRenderers);
    Core::GpuTimingSubmissionTicket avboitPreTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitDepthWarpTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitExtinctionTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitIntegrationTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitAccumulationTimingTicket(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> avboitClearTiming;
    GraphClearTimingRecordState avboitClearTimingState{
        .graphics = &m_graphics,
        .timing = &avboitClearTiming,
        .timingTicket = &avboitPreTimingTicket,
        .scope = RendererGpuTimingScope::s_AvboitClear,
    };
    Optional<Core::GpuTimingMeasure> transparentCsgIntervalClearTiming;
    GraphClearTimingRecordState transparentCsgIntervalClearTimingState{
        .graphics = &m_graphics,
        .timing = &transparentCsgIntervalClearTiming,
        .timingTicket = &avboitPreTimingTicket,
        .scope = RendererGpuTimingScope::s_CsgIntervalClear,
    };
    // Pre opens it; Combine closes it.
    Optional<Core::GpuTimingMeasure> transparentCsgIntervalsTiming;
    // Producer opens it; raster consumer closes it.
    Optional<Core::GpuTimingMeasure> avboitOccupancyComputeEmulationTiming;
    // Producer opens it; raster consumer closes it.
    Optional<Core::GpuTimingMeasure> avboitExtinctionComputeEmulationTiming;
    // Producer opens it; raster consumer closes it.
    Optional<Core::GpuTimingMeasure> avboitAccumulationComputeEmulationTiming;
    Core::GpuTimingSubmissionTicket deferredLightingTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket deferredCompositeTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket deferredPresentTimingTicket(m_graphics.gpuTiming());
    // Publish the endpoint only after Present accepts.
    Core::GpuTimingFrameTransaction frameTimingTransaction(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncFinalTiming;
    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(
        deferredTargets.framebuffer->getFramebufferInfo()
    );
    buildDeferredLightingTaskGraph(
        frameGraphFeatures,
        deferredTargets,
        csgFrameState,
        clearAvboitTargets,
        hasTransparentRenderers,
        hasOpaqueCsgFrameWork,
        meshViewAspectRatio,
        presentationFrame,
        frameTimingTransaction,
        asyncPrefixTiming,
        deferredClearTiming,
        deferredClearTimingState,
        opaqueCsgIntervalClearTimingState,
        opaqueRegularSharedComputeEmulationTiming,
        opaqueCsgIntervalSampleComputeEmulationTiming,
        shadowPrepareTimingTicket,
        graphicsPrefixTimingTickets,
        &asyncPrefixTimingSpansOnePacket,
        asyncFinalTiming,
        avboitPreTimingTicket,
        avboitClearTimingState,
        transparentCsgIntervalClearTimingState,
        transparentCsgIntervalsTiming,
        avboitOccupancyComputeEmulationTiming,
        avboitExtinctionComputeEmulationTiming,
        avboitAccumulationComputeEmulationTiming,
        avboitDepthWarpTimingTicket,
        avboitExtinctionTimingTicket,
        avboitIntegrationTimingTicket,
        avboitAccumulationTimingTicket,
        shadowVisibilityTimingTicket,
        shadowVisibilityAsyncTiming,
        shadowVisibilityTiming,
        opaqueSoftResolveTiming,
        transparentSoftResolveTiming,
        shadowVisibilityOpaqueProduced,
        shadowVisibilityTransparentTraceProduced,
        shadowVisibilityOpaqueFrameIndex,
        softwareCausticsTimingTicket,
        surfelGiTimingTicket,
        surfelGiAsyncTiming,
        hardwareCausticsTimingTicket,
        causticPhotonTiming,
        causticResolveTiming,
        deferredLightingTimingTicket,
        deferredCompositeTimingTicket,
        deferredPresentTimingTicket,
        surfelCounterReadbackCompletionToken,
        priorLaggedLightingHistoryReadReadyToken,
        priorLaggedLightingHistoryWriterDrainToken,
        requestsLaggedLightingHistoryCapture
    );
    if(requestsLaggedLightingHistoryCapture && !m_deferredLightingTaskGraphValid){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph build with optional lagged lighting-history capture failed; retrying without the tail"));
        buildDeferredLightingTaskGraph(
            frameGraphFeatures,
            deferredTargets,
            csgFrameState,
            clearAvboitTargets,
            hasTransparentRenderers,
            hasOpaqueCsgFrameWork,
            meshViewAspectRatio,
            presentationFrame,
            frameTimingTransaction,
            asyncPrefixTiming,
            deferredClearTiming,
            deferredClearTimingState,
            opaqueCsgIntervalClearTimingState,
            opaqueRegularSharedComputeEmulationTiming,
            opaqueCsgIntervalSampleComputeEmulationTiming,
            shadowPrepareTimingTicket,
            graphicsPrefixTimingTickets,
            &asyncPrefixTimingSpansOnePacket,
            asyncFinalTiming,
            avboitPreTimingTicket,
            avboitClearTimingState,
            transparentCsgIntervalClearTimingState,
            transparentCsgIntervalsTiming,
            avboitOccupancyComputeEmulationTiming,
            avboitExtinctionComputeEmulationTiming,
            avboitAccumulationComputeEmulationTiming,
            avboitDepthWarpTimingTicket,
            avboitExtinctionTimingTicket,
            avboitIntegrationTimingTicket,
            avboitAccumulationTimingTicket,
            shadowVisibilityTimingTicket,
            shadowVisibilityAsyncTiming,
            shadowVisibilityTiming,
            opaqueSoftResolveTiming,
            transparentSoftResolveTiming,
            shadowVisibilityOpaqueProduced,
            shadowVisibilityTransparentTraceProduced,
            shadowVisibilityOpaqueFrameIndex,
            softwareCausticsTimingTicket,
            surfelGiTimingTicket,
            surfelGiAsyncTiming,
            hardwareCausticsTimingTicket,
            causticPhotonTiming,
            causticResolveTiming,
            deferredLightingTimingTicket,
            deferredCompositeTimingTicket,
            deferredPresentTimingTicket,
            surfelCounterReadbackCompletionToken,
            priorLaggedLightingHistoryReadReadyToken,
            priorLaggedLightingHistoryWriterDrainToken,
            false
        );
    }
    const Core::GpuTaskGraph::DeclarationReadView deferredTaskGraphView(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView deferredCompiledPlan(m_deferredLightingCompiledGraph);
    const bool captureLaggedLightingHistory = m_deferredLaggedLightingHistoryTask.valid();
    const auto taskIsCompiled = [&](const Core::GpuTaskId task){
        return deferredCompiledPlan.findTask(task).valid();
    };
    // Software clears belong to the Shadow Preparation packet; a split would omit them.
    const bool shadowPrepareSoftwareBvhBuildsMerged =
        !m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid()
            ? !m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
            : (
                m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
                && deferredCompiledPlan.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_deferredShadowPrepareSoftwareBvhBuildFirstTask
                )
                && deferredCompiledPlan.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_deferredShadowPrepareSoftwareBvhBuildLastTask
                )
            )
    ;
    // The hybrid tail keeps the old acceptance boundary in this packet.
    const bool shadowPrepareHybridSoftwareTailMerged =
        !m_deferredShadowPrepareHybridSoftwareTailTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_deferredShadowPrepareHybridSoftwareTailTask
        )
    ;
    // Frozen transitions must share the build's submission for an atomic handoff.
    const bool shadowPrepareAccelStructFinalizeMerged =
        !m_deferredShadowPrepareAccelStructFinalizeTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_deferredShadowPrepareAccelStructFinalizeTask
        )
    ;
    // Selector uploads must stay in the Shadow Preparation packet.
    const bool deferredBindlessSlotsUploadMergedIntoShadowPreparePacket =
        !m_deferredBindlessSlotsUploadTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_deferredBindlessSlotsUploadTask
        )
    ;
    // Keep the upload in the first packet so it becomes the handoff.
    const bool rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket =
        !m_rayTraceMaterialContextSlotsUploadTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_rayTraceMaterialContextSlotsUploadTask
        )
    ;
    // Nonempty caustic payloads must live in the Shadow Preparation packet.
    const bool causticEmissionTargetsUploadMergedIntoShadowPreparePacket =
        !m_causticEmissionTargetsUploadTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_causticEmissionTargetsUploadTask
        )
    ;
    // Surfel constants must share the Shadow Preparation packet.
    const bool surfelFrameConstantsUploadMergedIntoShadowPreparePacket =
        !m_surfelFrameConstantsUploadTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_surfelFrameConstantsUploadTask
        )
    ;
    // This triple must stay in the first accepted packet.
    const bool shadowMaterialContextUploadsMergedIntoShadowPreparePacket =
        (!m_shadowInstanceMaterialUploadTask.valid()
            || deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowPrepareTask,
                m_shadowInstanceMaterialUploadTask
            ))
        && (!m_shadowInstanceUploadTask.valid()
            || deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowPrepareTask,
                m_shadowInstanceUploadTask
            ))
        && (!m_shadowMaterialTypedUploadTask.valid()
            || deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowPrepareTask,
                m_shadowMaterialTypedUploadTask
            ))
    ;
    // Keep the pair in Shadow Preparation as the only producer.
    const bool sceneBvhUploadsMergedIntoShadowPreparePacket =
        m_sceneBvhNodesUploadTask.valid() == m_sceneBvhInstancesUploadTask.valid()
        && (!m_sceneBvhNodesUploadTask.valid()
            || (
                deferredCompiledPlan.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_sceneBvhNodesUploadTask
                )
                && deferredCompiledPlan.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_sceneBvhInstancesUploadTask
                )
            ))
    ;
    // CSG callbacks keep an independent anchor across FrontierSafe splits.
    const Core::GpuTaskId graphicsPrefixTimingTasks[graphicsPrefixTimingTicketCount] = {
        m_graphicsPrefixMeshViewSetupTask,
        m_graphicsPrefixSceneShadingSetupTask,
        m_graphicsPrefixDeferredClearTask,
        m_graphicsPrefixGbufferTask,
        m_graphicsPrefixCsgReceiverSpanTask.valid()
            ? m_graphicsPrefixCsgReceiverSpanTask
            : m_graphicsPrefixGbufferTask,
        m_graphicsPrefixCsgIntervalCombineTask.valid()
            ? m_graphicsPrefixCsgIntervalCombineTask
            : m_graphicsPrefixGbufferTask,
        m_graphicsPrefixCsgIntervalSampleTask.valid()
            ? m_graphicsPrefixCsgIntervalSampleTask
            : m_graphicsPrefixGbufferTask,
        m_graphicsPrefixTask,
    };
    bool graphicsPrefixTimingBindingsValid = true;
    usize graphicsPrefixUniquePacketCount = 0u;
    for(usize prefixTaskIndex = 0u; prefixTaskIndex < graphicsPrefixTimingTicketCount; ++prefixTaskIndex){
        const Core::GpuTaskId task = graphicsPrefixTimingTasks[prefixTaskIndex];
        if(
            !deferredCompiledPlan.findTask(task).valid()
            || (
                prefixTaskIndex != 0u
                && !deferredCompiledPlan.taskPrecedesOrSharesPacket(
                    graphicsPrefixTimingTasks[prefixTaskIndex - 1u],
                    task
                )
            )
        ){
            graphicsPrefixTimingBindingsValid = false;
            break;
        }
        bool sharesPacketWithEarlierTask = false;
        for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
            if(!deferredCompiledPlan.tasksSharePacket(
                task,
                graphicsPrefixTimingTasks[earlierTaskIndex]
            ))
                continue;
            graphicsPrefixTimingTickets[prefixTaskIndex] = graphicsPrefixTimingTickets[earlierTaskIndex];
            sharesPacketWithEarlierTask = true;
            break;
        }
        if(!sharesPacketWithEarlierTask){
            graphicsPrefixTimingTickets[prefixTaskIndex] = graphicsPrefixOwnedTimingTickets[prefixTaskIndex];
            ++graphicsPrefixUniquePacketCount;
        }
    }
    // Measures are submission-local; skip the scope across a frontier.
    asyncPrefixTimingSpansOnePacket = graphicsPrefixTimingBindingsValid
        && deferredCompiledPlan.tasksSharePacket(
            m_graphicsPrefixMeshViewSetupTask,
            m_graphicsPrefixTask
        )
    ;
    const bool shadowVisibilityPreparedTasksMerged =
        !m_deferredShadowVisibilityOpaqueTask.valid()
        || (
            deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityOpaqueTask
            )
            && m_deferredShadowVisibilityOpaqueFirstWaveletTask.valid()
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityOpaqueFirstWaveletTask
            )
            && m_deferredShadowVisibilityOpaqueResolveTask.valid()
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityOpaqueResolveTask
            )
            && m_deferredShadowVisibilityTransparentTraceTask.valid()
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityTransparentTraceTask
            )
            && (
                !m_deferredShadowVisibilityTransparentTemporalMergeTask.valid()
                || deferredCompiledPlan.tasksSharePacket(
                    m_deferredShadowVisibilityTask,
                    m_deferredShadowVisibilityTransparentTemporalMergeTask
                )
            )
            && m_deferredShadowVisibilityTransparentFirstWaveletTask.valid()
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityTransparentFirstWaveletTask
            )
        )
    ;
    // Keep the all-lit clear in its packet; split-soft frames keep native clear.
    const bool shadowVisibilityAllLitClearMerged = m_deferredShadowVisibilityOpaqueTask.valid()
        ? !m_deferredShadowVisibilityAllLitClearTask.valid()
        : m_deferredShadowVisibilityAllLitClearTask.valid()
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAllLitClearTask
            )
    ;
    // Keep the original acceptance endpoint; a split would leak state early.
    const bool shadowVisibilityAdaptivePrimitivesMerged =
        (!m_deferredShadowVisibilityAdaptiveStatsClearTask.valid()
            || deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAdaptiveStatsClearTask
            ))
        && (!m_deferredShadowVisibilityAdaptiveCounterClearTask.valid()
            || deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAdaptiveCounterClearTask
            ))
        && (!m_deferredShadowVisibilityAdaptiveStatsReadbackTask.valid()
            || deferredCompiledPlan.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAdaptiveStatsReadbackTask
            ))
    ;
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredShadowVisibilityTask);
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixQueue =
        deferredCompiledPlan.queueInfoForTask(m_graphicsPrefixTask);
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueComputeEmulationQueue =
        m_graphicsPrefixOpaqueComputeEmulationTask.valid()
            ? deferredCompiledPlan.queueInfoForTask(m_graphicsPrefixOpaqueComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueSharedComputeEmulationQueue =
        m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount != 0u
        && m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u].valid()
            ? deferredCompiledPlan.queueInfoForTask(
                m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueCsgReceiverComputeEmulationQueue =
        m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
            ? deferredCompiledPlan.queueInfoForTask(m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationQueue =
        m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid()
            ? deferredCompiledPlan.queueInfoForTask(
                m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask
            )
            : nullptr
    ;
    bool graphicsPrefixPacketsAreGraphics = graphicsPrefixTimingBindingsValid;
    for(usize prefixTaskIndex = 0u;
        graphicsPrefixPacketsAreGraphics && prefixTaskIndex < graphicsPrefixTimingTicketCount;
        ++prefixTaskIndex
    ){
        const Core::GpuPhysicalQueueInfo* const queue =
            deferredCompiledPlan.queueInfoForTask(graphicsPrefixTimingTasks[prefixTaskIndex]);
        graphicsPrefixPacketsAreGraphics = queue && queue->queueClass == Core::CommandQueue::Graphics;
    }
    // The producer shares G-buffer's packet; its boundary stays packet-local.
    const bool graphicsPrefixOpaqueComputeEmulationMerged =
        !m_graphicsPrefixOpaqueComputeEmulationTask.valid()
        || (
            taskIsCompiled(m_graphicsPrefixOpaqueComputeEmulationTask)
            && deferredCompiledPlan.tasksSharePacket(
                m_graphicsPrefixOpaqueComputeEmulationTask,
                m_graphicsPrefixGbufferTask
            )
            && graphicsPrefixOpaqueComputeEmulationQueue
            && graphicsPrefixOpaqueComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // Shared outputs need exact packet order; keep the G-buffer prelude first.
    const bool graphicsPrefixOpaqueSharedComputeEmulationMerged = [&](){
        const usize phaseCount = m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : m_graphicsPrefixOpaqueSharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(!ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount))
            return false;
        if(
            !graphicsPrefixOpaqueSharedComputeEmulationQueue
            || graphicsPrefixOpaqueSharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || !deferredCompiledPlan.tasksSharePacket(
                m_graphicsPrefixGbufferTask,
                m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex];
            const Core::GpuPhysicalQueueInfo* const queue = deferredCompiledPlan.queueInfoForTask(task);
            if(
                !task.valid()
                || !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !deferredCompiledPlan.tasksSharePacket(
                    m_graphicsPrefixGbufferTask,
                    task
                )
            )
                return false;
        }
        for(usize phaseIndex = phaseCount;
            phaseIndex < LengthOf(m_graphicsPrefixOpaqueSharedComputeEmulationTasks);
            ++phaseIndex
        ){
            if(m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        return deferredCompiledPlan.tasksFormContiguousPacketSequence(
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks,
            phaseCount
        ) && deferredCompiledPlan.taskPrecedesInSamePacket(
            m_graphicsPrefixGbufferTask,
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u]
        );
    }();
    // Declared producers must share G-buffer's packet for the same handoff.
    const bool graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged =
        !m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
        || (
            taskIsCompiled(m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            && deferredCompiledPlan.tasksSharePacket(
                m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask,
                m_graphicsPrefixGbufferTask
            )
            && graphicsPrefixOpaqueCsgReceiverComputeEmulationQueue
            && graphicsPrefixOpaqueCsgReceiverComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // The producer/raster pair must stay contiguous in one packet.
    const bool graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged = [&](){
        if(!m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid())
            return true;
        if(
            !m_graphicsPrefixCsgIntervalCombineTask.valid()
            || !m_graphicsPrefixCsgIntervalSampleTask.valid()
            || !graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationQueue
            || graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationQueue->queueClass
                != Core::CommandQueue::Graphics
            || !deferredCompiledPlan.taskPrecedesOrSharesPacket(
                m_graphicsPrefixCsgIntervalCombineTask,
                m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask
            )
            || !deferredCompiledPlan.tasksSharePacket(
                m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask,
                m_graphicsPrefixCsgIntervalSampleTask
            )
        )
            return false;
        const Core::GpuTaskId sequence[] = {
            m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask,
            m_graphicsPrefixCsgIntervalSampleTask,
        };
        return deferredCompiledPlan.tasksFormContiguousPacketSequence(
            sequence,
            LengthOf(sequence)
        );
    }();
    // Keep the clear with G-buffer's ticket; a split would rebind ownership.
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixCsgIntervalClearQueue =
        m_graphicsPrefixCsgIntervalClearTask.valid()
            ? deferredCompiledPlan.queueInfoForTask(m_graphicsPrefixCsgIntervalClearTask)
            : nullptr
    ;
    const bool graphicsPrefixCsgIntervalClearBundleMerged = !hasOpaqueCsgFrameWork
        ? (!m_graphicsPrefixCsgIntervalClearFirstTask.valid() && !m_graphicsPrefixCsgIntervalClearTask.valid())
        : (
            m_graphicsPrefixCsgIntervalClearFirstTask.valid()
            && m_graphicsPrefixCsgIntervalClearTask.valid()
            && taskIsCompiled(m_graphicsPrefixCsgIntervalClearFirstTask)
            && taskIsCompiled(m_graphicsPrefixCsgIntervalClearTask)
            && deferredCompiledPlan.tasksSharePacket(
                m_graphicsPrefixCsgIntervalClearFirstTask,
                m_graphicsPrefixCsgIntervalClearTask
            )
            && deferredCompiledPlan.tasksSharePacket(
                m_graphicsPrefixCsgIntervalClearTask,
                m_graphicsPrefixGbufferTask
            )
            && graphicsPrefixCsgIntervalClearQueue
            && graphicsPrefixCsgIntervalClearQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    const Core::GpuPhysicalQueueInfo* const shadowPrepareQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredShadowPrepareTask);
    const Core::GpuPhysicalQueueInfo* const softwareCausticsQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredSoftwareCausticsTask);
    const bool shadowVisibilityRunsOnCompute = shadowVisibilityQueue
        && shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute
    ;
    const bool softwareCausticsRunsOnCompute = !hardwareShadowSupported
        && softwareCausticsQueue
        && softwareCausticsQueue->queueClass == Core::CommandQueue::Compute
    ;
    const RendererAvboitTaskGraphValidation avboitValidation = m_avboitSystem.validateTaskGraphStage(
        deferredCompiledPlan,
        clearAvboitTargets,
        hasTransparentRenderers
    );
    const Core::GpuTaskId causticsTask = hardwareShadowSupported
        ? m_deferredHardwareCausticsTask
        : m_deferredSoftwareCausticsTask
    ;
    // History-selector uploads must share Lighting's acceptance boundary.
    const bool laggedLightingHistorySlotsUploadMergedIntoLightingPacket =
        !m_deferredLaggedLightingHistorySlotsUploadTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredLightingTask,
            m_deferredLaggedLightingHistorySlotsUploadTask
        )
    ;
    // Resolve the terminal endpoint through the compiler, not renderer policy.
    const Core::GpuCompiledPresentEndpoint* const presentationEndpoint =
        deferredCompiledPlan.presentEndpoint();
    const Core::GpuTaskId terminalPresentationTask = presentationEndpoint
        ? presentationEndpoint->producer
        : Core::GpuTaskId{}
    ;
    const Core::GpuPhysicalQueueInfo* const deferredLightingQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredLightingTask);
    const Core::GpuPhysicalQueueInfo* const deferredCompositeQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredCompositeTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredPresentTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentationOverlayQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredPresentationOverlayTask);
    const Core::GpuPhysicalQueueInfo* const terminalPresentationQueue = presentationEndpoint
        ? deferredCompiledPlan.queueInfo(presentationEndpoint->queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const deferredLaggedLightingHistoryQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredLaggedLightingHistoryTask);
    const Core::GpuPhysicalQueueInfo* const deferredFrameRecoveryQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredFrameRecoveryTask);
    const Core::GpuPhysicalQueueInfo* const hardwareCausticsQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredHardwareCausticsTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredSurfelGiTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiPreparationQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredSurfelGiPreparationTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiSnapshotCopyQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredSurfelGiSnapshotCopyTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiCounterReadbackQueue =
        deferredCompiledPlan.queueInfoForTask(m_deferredSurfelGiCounterReadbackTask);
    // Keep the clear in GI's packet; a split would escape its endpoint.
    const bool surfelGiOutputClearMergedIntoGiPacket =
        m_deferredSurfelGiIrradianceClearTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredSurfelGiIrradianceClearTask,
            m_deferredSurfelGiTask
        )
    ;
    // Every GI callback must share the semantic packet.
    const bool surfelGiPreparedPrefixMergedIntoGiPacket =
        (
            !m_deferredSurfelGiAgeFreeTask.valid()
            && !m_deferredSurfelGiCellHeadClearTask.valid()
            && !m_deferredSurfelGiHashBuildTask.valid()
            && !m_deferredSurfelGiSpawnTask.valid()
            && !m_deferredSurfelGiTraceBuildArgsTask.valid()
            && !m_deferredSurfelGiTraceTask.valid()
            && !m_deferredSurfelGiResolveTask.valid()
        )
        || (
            m_deferredSurfelGiAgeFreeTask.valid()
            && m_deferredSurfelGiCellHeadClearTask.valid()
            && m_deferredSurfelGiHashBuildTask.valid()
            && m_deferredSurfelGiSpawnTask.valid()
            && m_deferredSurfelGiTraceBuildArgsTask.valid()
            && m_deferredSurfelGiTraceTask.valid()
            && m_deferredSurfelGiResolveTask.valid()
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiAgeFreeTask,
                m_deferredSurfelGiTask
            )
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiCellHeadClearTask,
                m_deferredSurfelGiTask
            )
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiHashBuildTask,
                m_deferredSurfelGiTask
            )
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiSpawnTask,
                m_deferredSurfelGiTask
            )
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiTraceBuildArgsTask,
                m_deferredSurfelGiTask
            )
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiTraceTask,
                m_deferredSurfelGiTask
            )
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiResolveTask,
                m_deferredSurfelGiTask
            )
        )
    ;
    // The lifecycle tail must share the first clear's packet.
    const bool surfelGiInitializationLifecycleMergedIntoPreparationPacket =
        !m_deferredSurfelGiInitializationLifecycleTask.valid()
        || (
            m_deferredSurfelGiPreparationTask.valid()
            && deferredCompiledPlan.tasksSharePacket(
                m_deferredSurfelGiPreparationTask,
                m_deferredSurfelGiInitializationLifecycleTask
            )
        )
    ;
    // Keep the clear with the producer; caustic callbacks stay one submission.
    const bool causticPhotonMergedIntoCausticsPacket =
        m_deferredCausticPhotonTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticPhotonTask,
            causticsTask
        )
    ;
    const bool causticGeometryMergedIntoCausticsPacket =
        m_deferredCausticGeometryTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticGeometryTask,
            causticsTask
        )
    ;
    const bool causticResolvePrepareMergedIntoCausticsPacket =
        m_deferredCausticResolvePrepareTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticResolvePrepareTask,
            causticsTask
        )
    ;
    const bool causticResolveWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveWaveletTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticResolveWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveSecondWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveSecondWaveletTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticResolveSecondWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveThirdWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveThirdWaveletTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticResolveThirdWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveFourthWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveFourthWaveletTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticResolveFourthWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveFifthWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveFifthWaveletTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticResolveFifthWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveUpsampleMergedIntoCausticsPacket =
        m_deferredCausticResolveUpsampleTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticResolveUpsampleTask,
            causticsTask
        )
    ;
    const bool causticIrradianceClearMergedIntoCausticsPacket =
        m_deferredCausticIrradianceClearTask.valid()
        && deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticIrradianceClearTask,
            causticsTask
        )
    ;
    // CPU reset commits only after the shared packet accepts.
    const bool causticAccumulatorNonTemporalClearMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorNonTemporalClearTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticAccumulatorNonTemporalClearTask,
            causticsTask
        )
    ;
    // Keep the bootstrap clear in the producer packet; no hidden writers.
    const bool causticAccumulatorBootstrapClearMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorBootstrapClearTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticAccumulatorBootstrapClearTask,
            causticsTask
        )
    ;
    // Keep decay in the packet; a split would break the UAV dependency.
    const bool causticAccumulatorDecayMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorDecayTask.valid()
        || deferredCompiledPlan.tasksSharePacket(
            m_deferredCausticAccumulatorDecayTask,
            causticsTask
        )
    ;
    // Keep Snapshot Copy and the GI endpoint separate with distinct boundaries.
    const bool surfelGiSnapshotCopyAndTimingPacketsAreDistinct =
        !m_deferredSurfelGiSnapshotCopyTask.valid()
        || !deferredCompiledPlan.tasksSharePacket(
            m_deferredSurfelGiSnapshotCopyTask,
            m_deferredSurfelGiTask
        )
    ;
    const bool surfelCounterReadbackFollowsPresentation = !m_deferredSurfelGiCounterReadbackTask.valid()
        || (
            deferredCompiledPlan.taskPrecedesOrSharesPacket(
                terminalPresentationTask,
                m_deferredSurfelGiCounterReadbackTask
            )
            && !deferredCompiledPlan.tasksSharePacket(
                terminalPresentationTask,
                m_deferredSurfelGiCounterReadbackTask
            )
        )
    ;
    const bool laggedLightingHistoryFollowsPresentation = !captureLaggedLightingHistory
        || (
            deferredCompiledPlan.taskPrecedesOrSharesPacket(
                terminalPresentationTask,
                m_deferredLaggedLightingHistoryTask
            )
            && !deferredCompiledPlan.tasksSharePacket(
                terminalPresentationTask,
                m_deferredLaggedLightingHistoryTask
            )
        )
    ;
    const auto discardGraphicsPrefixTimingTickets = [&graphicsPrefixOwnedTimingTickets](){
        for(Core::GpuTimingSubmissionTicket* const timingTicket : graphicsPrefixOwnedTimingTickets)
            timingTicket->discard();
    };
    if(
        !m_deferredLightingTaskGraphValid
        || !m_deferredShadowPrepareTask.valid()
        || !taskIsCompiled(m_deferredShadowPrepareTask)
        || !shadowPrepareSoftwareBvhBuildsMerged
        || !shadowPrepareHybridSoftwareTailMerged
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
        || (hasOpaqueCsgFrameWork && (
            !m_graphicsPrefixCsgReceiverSpanTask.valid()
            || !m_graphicsPrefixCsgIntervalCombineTask.valid()
            || !m_graphicsPrefixCsgIntervalSampleTask.valid()
        ))
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
        || (!hardwareShadowSupported && (
            !m_deferredSoftwareCausticsTask.valid()
            || !taskIsCompiled(m_deferredSoftwareCausticsTask)
            || !softwareCausticsQueue
        ))
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
            || (static_cast<u8>(surfelGiSnapshotCopyQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || (m_deferredSurfelGiCounterReadbackTask.valid() && (
            !taskIsCompiled(m_deferredSurfelGiCounterReadbackTask)
            || !surfelGiCounterReadbackQueue
            || (static_cast<u8>(surfelGiCounterReadbackQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || !surfelCounterReadbackFollowsPresentation
        || (hardwareShadowSupported && (
            !m_deferredHardwareCausticsTask.valid()
            || !taskIsCompiled(m_deferredHardwareCausticsTask)
            || !hardwareCausticsQueue
            || hardwareCausticsQueue->queueClass != Core::CommandQueue::Graphics
        ))
        || !avboitValidation.valid()
        || !m_deferredLightingTask.valid()
        || !m_deferredCompositeTask.valid()
        || !m_deferredPresentTask.valid()
        || !m_deferredFrameTimingEndTask.valid()
        || (m_deferredPresentationOverlayRequired != m_deferredPresentationOverlayTask.valid())
        || !m_deferredFrameRecoveryTask.valid()
        || (m_deferredSurfelGiCounterReadbackCompletion.valid()
            && !surfelCounterReadbackCompletionToken.valid())
        || (captureLaggedLightingHistory && (
            !m_deferredLaggedLightingHistoryTask.valid()
            || !taskIsCompiled(m_deferredLaggedLightingHistoryTask)
            || !deferredLaggedLightingHistoryQueue
            || (static_cast<u8>(deferredLaggedLightingHistoryQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
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
        || (m_deferredPresentationOverlayRequired
            && deferredPresentationOverlayQueue->id != primaryGraphicsQueue)
        || terminalPresentationQueue->id != primaryGraphicsQueue
        || !surfelGiSnapshotCopyAndTimingPacketsAreDistinct
        || (laggedAsyncLightingSchedule && deferredLightingQueue->queueClass != Core::CommandQueue::Compute)
        || (laggedAsyncLightingSchedule && deferredCompositeQueue->queueClass != Core::CommandQueue::Graphics)
        || deferredPresentQueue->queueClass != Core::CommandQueue::Graphics
        || (m_deferredPresentationOverlayRequired
            && deferredPresentationOverlayQueue->queueClass != Core::CommandQueue::Graphics)
        || terminalPresentationQueue->queueClass != Core::CommandQueue::Graphics
        || deferredFrameRecoveryQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compiled deferred graph topology was unavailable"));
        deferredPresentTimingTicket.discard();
        deferredCompositeTimingTicket.discard();
        deferredLightingTimingTicket.discard();
        avboitPreTimingTicket.discard();
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        shadowPrepareTimingTicket.discard();
        discardGraphicsPrefixTimingTickets();
        // No packet accepted yet; restore the CPU-only classification.
        m_rayTracingState.restorePreparedLightingCpuState(rayTracingCpuState);
        return;
    }
    const bool deferredLightingRunsOnCompute = deferredLightingQueue->queueClass == Core::CommandQueue::Compute;
    const auto discardTimingTickets = [
        &shadowPrepareTimingTicket,
        &discardGraphicsPrefixTimingTickets,
        &shadowVisibilityTimingTicket,
        &hardwareCausticsTimingTicket,
        &softwareCausticsTimingTicket,
        &surfelGiTimingTicket,
        &avboitPreTimingTicket,
        &avboitDepthWarpTimingTicket,
        &avboitExtinctionTimingTicket,
        &avboitIntegrationTimingTicket,
        &avboitAccumulationTimingTicket,
        &deferredLightingTimingTicket,
        &deferredCompositeTimingTicket,
        &deferredPresentTimingTicket
    ](){
        shadowPrepareTimingTicket.discard();
        discardGraphicsPrefixTimingTickets();
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
    };
    const auto discardUnacceptedGraphPackets = [&]() -> bool {
        return m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            m_deferredLightingRecordedGraph.recordingAttemptGeneration()
        );
    };
    const auto discardRenderPackets = [&](){
        DiscardGpuTimingMeasure(&asyncPrefixTiming);
        DiscardGpuTimingMeasure(&deferredClearTiming);
        DiscardGpuTimingMeasure(&asyncFinalTiming);
        DiscardGpuTimingMeasure(&causticPhotonTiming);
        DiscardGpuTimingMeasure(&causticResolveTiming);
        DiscardGpuTimingMeasure(&transparentCsgIntervalsTiming);
        DiscardGpuTimingMeasure(&avboitOccupancyComputeEmulationTiming);
        DiscardGpuTimingMeasure(&avboitExtinctionComputeEmulationTiming);
        DiscardGpuTimingMeasure(&avboitAccumulationComputeEmulationTiming);
        DiscardGpuTimingMeasure(&opaqueRegularSharedComputeEmulationTiming);
        DiscardGpuTimingMeasure(&opaqueCsgIntervalSampleComputeEmulationTiming);
        frameTimingTransaction.discard();
        discardTimingTickets();
        if(!discardUnacceptedGraphPackets()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred graph cancellation overlapped active native work; requesting device recreation"));
            m_graphics.requestDeviceRecreation();
        }
        const bool shadowPrepareAccepted = taskIsCompiled(m_deferredShadowPrepareTask)
            && m_deferredLightingSubmissionTransaction.taskToken(
                deferredCompiledPlan,
                m_deferredShadowPrepareTask
            ).valid()
        ;
        restorePostGbufferPacketCpuState(!shadowPrepareAccepted);
        m_raytracingSystem.discardSoftShadowTemporalHistory();
    };

    // Prep/prefix stay serial; later upload packets may use workers.
    const Core::GpuNativePacketRecorder deferredRecorder(device, m_graphics.gpuTiming());
    Core::Alloc::ScratchArena shadowPrepareStateScratchArena(RendererArenaScope::s_TaskGraphArena);
    ECSRenderDetail::MeshRetainedAccelerationStateBufferVector meshAccelerationStateBuffers{ shadowPrepareStateScratchArena };
    m_meshSystem.collectRetainedAccelerationStateBuffers(meshAccelerationStateBuffers);
    const auto& acceptedTraceGeometry = m_raytracingSystem.acceptedShadowTraceGeometryBuffers();
    const auto& preparedTraceGeometry = m_raytracingSystem.preparedShadowTraceGeometryBuffers();
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> shadowPrepareLiveStateBuffers{ shadowPrepareStateScratchArena };
    shadowPrepareLiveStateBuffers.reserve(AddSize(
        AddSize(acceptedTraceGeometry.size(), preparedTraceGeometry.size()),
        AddSize(meshAccelerationStateBuffers.size(), 4u)
    ));
    const auto appendShadowPrepareStateBuffer = [&](const Core::BufferHandle& buffer){
        if(buffer)
            shadowPrepareLiveStateBuffers.push_back(buffer);
    };
    if(rayTracingShadowResources.sceneTlasBackingBuffer)
        appendShadowPrepareStateBuffer(rayTracingShadowResources.sceneTlasBackingBuffer);
    bool shadowPrepareStateCandidateRequired = static_cast<bool>(rayTracingShadowResources.sceneTlas)
        || m_raytracingSystem.preparedMeshSwBvhBuildsReady()
        || m_raytracingSystem.shadowVisibilitySoftwareResourcesPreflighted()
    ;
    // Keep accepted trace-geometry state with the handoff, including invisible streams.
    for(const Core::BufferHandle& acceptedBuffer : acceptedTraceGeometry){
        appendShadowPrepareStateBuffer(acceptedBuffer);
        shadowPrepareStateCandidateRequired = true;
    }
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        appendShadowPrepareStateBuffer(preparedBuffer.buffer);
        shadowPrepareStateCandidateRequired = true;
    }
    for(const Core::BufferHandle& buffer : meshAccelerationStateBuffers){
        if(buffer)
            shadowPrepareStateCandidateRequired = true;
        appendShadowPrepareStateBuffer(buffer);
    }
    appendShadowPrepareStateBuffer(rayTracingShadowResources.bvhSortKeysBuffer);
    appendShadowPrepareStateBuffer(rayTracingShadowResources.bvhSortPayloadBuffer);
    appendShadowPrepareStateBuffer(rayTracingShadowResources.bvhVisitCounterBuffer);

    // Build the sparse candidate after recording; commit on Shadow Preparation accept.
    Core::GpuPersistentResourceStateCache::Candidate shadowPrepareAcceptedStateCandidate(m_shadowPreparePersistentState);
    m_avboitSystem.markFrameTargetUsage(hasTransparentRenderers);

    const auto submitFrameRecoveryPacket = [&]() -> bool {
        // Retire the scope after rejection; Graphics order needs no extra wait.
        if(device.requiresRecreation()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery packet skipped because the graphics device requires recreation"));
            m_deferredFrameRecoveryArmed = false;
            m_deferredFrameRecoveryRetiresTiming = false;
            frameTimingTransaction.discard();
            return false;
        }
        if(
            !m_deferredLightingTaskGraphValid
            || !m_deferredFrameRecoveryTask.valid()
            || !deferredCompiledPlan.findTask(m_deferredFrameRecoveryTask).valid()
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery task was unavailable"));
            m_deferredFrameRecoveryArmed = false;
            m_deferredFrameRecoveryRetiresTiming = false;
            frameTimingTransaction.discard();
            return false;
        }
        bool retireTiming = frameTimingTransaction.needsRetirement();
        if(retireTiming && !frameTimingTransaction.prepareForRecovery()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frame timing recovery preparation failed; continuing resource/frontier recovery"));
            frameTimingTransaction.discard();
            retireTiming = false;
        }
        m_deferredFrameRecoveryArmed = true;
        m_deferredFrameRecoveryRetiresTiming = retireTiming;
        Core::Alloc::ScratchArena recoveryScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskScheduler& submitter = m_graphics.gpuTasks();
        const bool recoveryAccepted = submitter.recordAndSubmitAcceptedFrontierTask(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            deferredRecorder,
            m_deferredLightingRecordedGraph,
            m_deferredFrameRecoveryTask,
            m_deferredLightingSubmissionTransaction,
            recoveryScratchArena
        );
        if(!recoveryAccepted){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery record/submission was rejected"));
            return false;
        }
        return true;
    };
    const auto recoverPendingFrameSubmission = [&]() -> bool {
        return (m_deferredLightingSubmissionTransaction.hasAcceptedPackets() || frameTimingTransaction.needsRetirement())
            ? submitFrameRecoveryPacket()
            : true
        ;
    };
    const auto recoverPendingFrameThenDiscardUnaccepted = [&]() -> bool {
        const bool recovered = recoverPendingFrameSubmission();
        // Record/submit while Declared; then reject remaining normal packets.
        return discardUnacceptedGraphPackets() && recovered;
    };
    const auto failFrameRenderRecovery = [&](){
        if(m_frameRenderRecoveryFailed)
            return;
        m_frameRenderRecoveryFailed = true;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: cannot safely continue after an unresolved frame recovery submission; requesting device recreation"));
        // Defer recreation until accepted work is safe.
        m_graphics.requestDeviceRecreation();
    };

    struct ShadowPrepareStateLifecycleContext{
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        RendererFramePipeline* renderer = nullptr;
        Core::Alloc::ScratchArena& scratchArena;
        Core::GpuPersistentResourceStateCache::Candidate* stateCandidate = nullptr;
        const Core::BufferHandle* buffers = nullptr;
        usize bufferCount = 0u;
        bool stateCandidateRequired = false;
        bool statePrepared = false;
        bool stateReady = true;
    } shadowPrepareStateLifecycle{
        .frameTimingTransaction = &frameTimingTransaction,
        .renderer = this,
        .scratchArena = shadowPrepareStateScratchArena,
        .stateCandidate = &shadowPrepareAcceptedStateCandidate,
        .buffers = shadowPrepareLiveStateBuffers.data(),
        .bufferCount = shadowPrepareLiveStateBuffers.size(),
        .stateCandidateRequired = shadowPrepareStateCandidateRequired,
    };
    const auto prepareShadowPrepareTask = [](
        void* const rawContext,
        const Core::CommandListResourceStateHandoff* const finalState
    ) -> bool {
        ShadowPrepareStateLifecycleContext* const context =
            static_cast<ShadowPrepareStateLifecycleContext*>(rawContext)
        ;
        if(!context || !context->renderer || !context->stateCandidate || !finalState)
            return false;
        if(context->bufferCount != 0u && !context->buffers)
            return false;

        const bool candidateBuilt = context->renderer->m_shadowPreparePersistentState.buildMergedBufferSubset(
            *context->stateCandidate,
            *finalState,
            context->buffers,
            context->bufferCount,
            context->scratchArena
        );
        const bool candidatePresent = context->stateCandidate->valid() && !context->stateCandidate->empty();
        context->statePrepared = candidateBuilt && (candidatePresent || !context->stateCandidateRequired);
        return context->statePrepared;
    };
    const auto acceptShadowPrepareTask = [](
        void* const rawContext,
        const Core::QueueSubmissionToken& token
    ) -> bool {
        ShadowPrepareStateLifecycleContext* const context =
            static_cast<ShadowPrepareStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->frameTimingTransaction
            || !context->renderer
            || !context->stateCandidate
            || !context->statePrepared
        )
            return false;

        if(!context->frameTimingTransaction->confirmBeginSubmission(token)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm accepted frame timing prefix; quarantining timing without rejecting native work"));
            context->frameTimingTransaction->discard();
        }

        RendererFramePipeline& renderer = *context->renderer;
        if(
            !context->stateCandidate->valid()
            || (context->stateCandidate->empty() && context->stateCandidateRequired)
        ){
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
        renderer.m_raytracingSystem.confirmAcceptedShadowPrepareAccelStructStateHandoffs();
        renderer.m_raytracingSystem.confirmPreparedMeshSwBvhBuilds();
        return true;
    };

    Core::Alloc::ScratchArena normalExecutionScratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::TextureHandle shadowVisibilityReturnTextures[] = {
        deferredTargets.shadowVisibility,
    };
    const Core::TextureHandle shadowComputeScratchTextures[] = {
        deferredTargets.shadowCoarseTransmittance,
        deferredTargets.shadowSoftHalfA,
        deferredTargets.shadowSoftHalfB,
        deferredTargets.shadowSoftGeometry,
        deferredTargets.shadowSoftGeometryPrev,
        deferredTargets.shadowHistA,
        deferredTargets.shadowHistB,
        deferredTargets.shadowMomentsA,
        deferredTargets.shadowMomentsB,
        deferredTargets.transparentSoftHalf,
        deferredTargets.transparentHistA,
        deferredTargets.transparentHistB,
        deferredTargets.transparentMomentsA,
        deferredTargets.transparentMomentsB,
    };
    const Core::BufferHandle shadowComputeScratchBuffers[] = {
        rayTracingShadowResources.swShadowEdgeStatsBuffer,
        rayTracingShadowResources.swShadowEdgeStatsReadback,
        rayTracingShadowResources.swShadowEdgeCounterBuffer,
        rayTracingShadowResources.swShadowEdgeListBuffer,
        rayTracingShadowResources.swShadowIndirectArgsBuffer,
    };
    Core::GpuPersistentResourceStateCache::Candidate shadowVisibilityReturnStateCandidate(m_shadowVisibilityReturnState);
    Core::GpuPersistentResourceStateCache::Candidate shadowComputeScratchStateCandidate(m_shadowComputePersistentState);
    struct ShadowVisibilityStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* returnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* scratchStateCandidate = nullptr;
        const Core::TextureHandle* returnTextures = nullptr;
        const Core::TextureHandle* scratchTextures = nullptr;
        const Core::BufferHandle* scratchBuffers = nullptr;
        usize returnTextureCount = 0u;
        usize scratchTextureCount = 0u;
        usize scratchBufferCount = 0u;
        bool runsOnCompute = false;
        bool statePrepared = false;
        bool stateReady = false;
    } shadowVisibilityStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = this,
        .targets = &deferredTargets,
        .returnStateCandidate = &shadowVisibilityReturnStateCandidate,
        .scratchStateCandidate = &shadowComputeScratchStateCandidate,
        .returnTextures = shadowVisibilityReturnTextures,
        .scratchTextures = shadowComputeScratchTextures,
        .scratchBuffers = shadowComputeScratchBuffers,
        .returnTextureCount = LengthOf(shadowVisibilityReturnTextures),
        .scratchTextureCount = LengthOf(shadowComputeScratchTextures),
        .scratchBufferCount = LengthOf(shadowComputeScratchBuffers),
        .runsOnCompute = shadowVisibilityRunsOnCompute,
    };
    const auto prepareShadowVisibilityTask = [](
        void* const rawContext,
        const Core::CommandListResourceStateHandoff* const finalState
    ) -> bool {
        ShadowVisibilityStateLifecycleContext* const context =
            static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->returnStateCandidate
            || !context->scratchStateCandidate
            || !context->returnTextures
            || !context->scratchTextures
            || !context->scratchBuffers
            || !finalState
        )
            return false;

        const bool scratchStateReady = context->renderer->m_shadowComputePersistentState.buildFilteredResourceSubset(
            *context->scratchStateCandidate,
            *finalState,
            context->scratchTextures,
            context->scratchTextureCount,
            context->scratchBuffers,
            context->scratchBufferCount,
            context->scratchArena
        );
        bool returnStateReady = true;
        if(context->runsOnCompute){
            returnStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(
                *context->returnStateCandidate,
                *finalState,
                context->returnTextures,
                context->returnTextureCount,
                nullptr,
                0u,
                context->scratchArena
            );
        }
        context->statePrepared = returnStateReady && scratchStateReady;
        return context->statePrepared;
    };
    const auto acceptShadowVisibilityTask = [](
        void* const rawContext,
        const Core::QueueSubmissionToken& token
    ) -> bool {
        static_cast<void>(token);
        ShadowVisibilityStateLifecycleContext* const context =
            static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->targets
            || !context->returnStateCandidate
            || !context->scratchStateCandidate
            || !context->statePrepared
        )
            return false;

        bool returnStateReady = true;
        if(context->runsOnCompute)
            returnStateReady = context->renderer->m_shadowVisibilityReturnState.commit(*context->returnStateCandidate);
        const bool scratchStateReady =
            context->renderer->m_shadowComputePersistentState.commit(*context->scratchStateCandidate)
        ;
        context->stateReady = returnStateReady && scratchStateReady;
        context->renderer->m_raytracingSystem.finalizeSoftShadowTemporalHistory(*context->targets);
        return context->stateReady;
    };

    const Core::TextureHandle causticsComputeScratchTextures[] = {
        deferredTargets.causticAccumulator,
        deferredTargets.causticHistory,
        deferredTargets.causticResolveHalf,
        deferredTargets.causticResolveGeometry,
    };
    const Core::TextureHandle causticIrradianceTextures[] = {
        deferredTargets.causticIrradiance,
    };
    Core::GpuPersistentResourceStateCache::Candidate causticReturnStateCandidate(m_causticIrradianceReturnState);
    Core::GpuPersistentResourceStateCache::Candidate causticsScratchStateCandidate(m_causticsComputePersistentState);
    struct SoftwareCausticsStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* returnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* scratchStateCandidate = nullptr;
        const Core::TextureHandle* irradianceTextures = nullptr;
        const Core::TextureHandle* scratchTextures = nullptr;
        usize irradianceTextureCount = 0u;
        usize scratchTextureCount = 0u;
        bool runsOnCompute = false;
        bool statePrepared = false;
        bool stateReady = false;
    } softwareCausticsStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = this,
        .returnStateCandidate = &causticReturnStateCandidate,
        .scratchStateCandidate = &causticsScratchStateCandidate,
        .irradianceTextures = causticIrradianceTextures,
        .scratchTextures = causticsComputeScratchTextures,
        .irradianceTextureCount = LengthOf(causticIrradianceTextures),
        .scratchTextureCount = LengthOf(causticsComputeScratchTextures),
        .runsOnCompute = softwareCausticsRunsOnCompute,
    };
    const auto prepareSoftwareCausticsTask = [](
        void* const rawContext,
        const Core::CommandListResourceStateHandoff* const finalState
    ) -> bool {
        SoftwareCausticsStateLifecycleContext* const context =
            static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->returnStateCandidate
            || !context->scratchStateCandidate
            || !context->irradianceTextures
            || !context->scratchTextures
            || !finalState
        )
            return false;

        bool returnStateReady = true;
        if(context->runsOnCompute){
            returnStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
                *context->returnStateCandidate,
                *finalState,
                context->irradianceTextures,
                context->irradianceTextureCount,
                nullptr,
                0u,
                context->scratchArena
            );
        }
        const bool scratchStateReady = context->renderer->m_causticsComputePersistentState.buildFilteredResourceSubset(
            *context->scratchStateCandidate,
            *finalState,
            context->scratchTextures,
            context->scratchTextureCount,
            nullptr,
            0u,
            context->scratchArena
        );
        context->statePrepared = returnStateReady && scratchStateReady;
        return context->statePrepared;
    };
    const auto acceptSoftwareCausticsTask = [](
        void* const rawContext,
        const Core::QueueSubmissionToken& token
    ) -> bool {
        static_cast<void>(token);
        SoftwareCausticsStateLifecycleContext* const context =
            static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->returnStateCandidate
            || !context->scratchStateCandidate
            || !context->statePrepared
        )
            return false;

        bool returnStateReady = true;
        if(context->runsOnCompute)
            returnStateReady = context->renderer->m_causticIrradianceReturnState.commit(*context->returnStateCandidate);
        const bool scratchStateReady =
            context->renderer->m_causticsComputePersistentState.commit(*context->scratchStateCandidate)
        ;
        context->stateReady = returnStateReady && scratchStateReady;
        return context->stateReady;
    };

    const Core::TextureHandle surfelIrradianceReturnTextures[] = {
        deferredTargets.surfelIrradiance,
    };
    const Core::BufferHandle surfelGiCounterBuffers[] = {
        rayTracingSurfelResources.counterBuffer,
    };
    const Core::TextureHandle surfelGiComputeScratchTextures[] = {
        deferredTargets.surfelIrradianceHalf,
    };
    const Core::BufferHandle surfelGiComputeScratchBuffers[] = {
        rayTracingSurfelResources.poolBuffer,
        rayTracingSurfelResources.cellHeadBuffer,
        rayTracingSurfelResources.traceIndirectArgsBuffer,
        rayTracingSurfelResources.freeListBuffer,
        rayTracingSurfelResources.poolSnapshotBuffer,
        rayTracingSurfelResources.cellHeadSnapshotBuffer,
    };
    Core::GpuPersistentResourceStateCache::Candidate surfelIrradianceReturnStateCandidate(m_surfelIrradianceReturnState);
    Core::GpuPersistentResourceStateCache::Candidate surfelGiCounterStateCandidate(m_surfelGiCounterPersistentState);
    Core::GpuPersistentResourceStateCache::Candidate surfelGiComputeStateCandidate(m_surfelGiComputePersistentState);
    struct SurfelGiStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* returnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* counterStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* computeStateCandidate = nullptr;
        const Core::TextureHandle* returnTextures = nullptr;
        const Core::BufferHandle* counterBuffers = nullptr;
        const Core::TextureHandle* computeTextures = nullptr;
        const Core::BufferHandle* computeBuffers = nullptr;
        usize returnTextureCount = 0u;
        usize counterBufferCount = 0u;
        usize computeTextureCount = 0u;
        usize computeBufferCount = 0u;
        bool statePrepared = false;
        bool stateReady = false;
    } surfelGiStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = this,
        .returnStateCandidate = &surfelIrradianceReturnStateCandidate,
        .counterStateCandidate = &surfelGiCounterStateCandidate,
        .computeStateCandidate = &surfelGiComputeStateCandidate,
        .returnTextures = surfelIrradianceReturnTextures,
        .counterBuffers = surfelGiCounterBuffers,
        .computeTextures = surfelGiComputeScratchTextures,
        .computeBuffers = surfelGiComputeScratchBuffers,
        .returnTextureCount = LengthOf(surfelIrradianceReturnTextures),
        .counterBufferCount = LengthOf(surfelGiCounterBuffers),
        .computeTextureCount = LengthOf(surfelGiComputeScratchTextures),
        .computeBufferCount = LengthOf(surfelGiComputeScratchBuffers),
    };
    const auto prepareSurfelGiTask = [](
        void* const rawContext,
        const Core::CommandListResourceStateHandoff* const finalState
    ) -> bool {
        SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
        if(
            !context
            || !context->renderer
            || !context->returnStateCandidate
            || !context->counterStateCandidate
            || !context->computeStateCandidate
            || !context->returnTextures
            || !context->counterBuffers
            || !context->computeTextures
            || !context->computeBuffers
            || !finalState
        )
            return false;

        const bool returnStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(
            *context->returnStateCandidate,
            *finalState,
            context->returnTextures,
            context->returnTextureCount,
            nullptr,
            0u,
            context->scratchArena
        );
        const bool counterStateReady = context->renderer->m_surfelGiCounterPersistentState.buildFilteredResourceSubset(
            *context->counterStateCandidate,
            *finalState,
            nullptr,
            0u,
            context->counterBuffers,
            context->counterBufferCount,
            context->scratchArena
        );
        const bool computeStateReady =
            context->renderer->m_surfelGiComputePersistentState.buildFilteredResourceSubset(
                *context->computeStateCandidate,
                *finalState,
                context->computeTextures,
                context->computeTextureCount,
                context->computeBuffers,
                context->computeBufferCount,
                context->scratchArena
            )
        ;
        context->statePrepared = returnStateReady && counterStateReady && computeStateReady;
        return context->statePrepared;
    };
    const auto acceptSurfelGiTask = [](
        void* const rawContext,
        const Core::QueueSubmissionToken& token
    ) -> bool {
        static_cast<void>(token);
        SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
        if(
            !context
            || !context->renderer
            || !context->returnStateCandidate
            || !context->counterStateCandidate
            || !context->computeStateCandidate
            || !context->statePrepared
        )
            return false;

        RendererFramePipeline& renderer = *context->renderer;
        const bool returnStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->returnStateCandidate);
        const bool counterStateReady = renderer.m_surfelGiCounterPersistentState.commit(*context->counterStateCandidate);
        const bool computeStateReady = renderer.m_surfelGiComputePersistentState.commit(*context->computeStateCandidate);
        context->stateReady = returnStateReady && counterStateReady && computeStateReady;
        return context->stateReady;
    };

    const Core::TextureHandle hardwareCausticAccumulatorTextures[] = {
        deferredTargets.causticAccumulator,
    };
    Core::GpuPersistentResourceStateCache::Candidate hardwareCausticAccumulatorStateCandidate(m_hardwareCausticAccumulatorPersistentState);
    struct HardwareCausticsStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* accumulatorStateCandidate = nullptr;
        const Core::TextureHandle* accumulatorTextures = nullptr;
        usize accumulatorTextureCount = 0u;
        bool statePrepared = false;
        bool stateReady = false;
    } hardwareCausticsStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = this,
        .accumulatorStateCandidate = &hardwareCausticAccumulatorStateCandidate,
        .accumulatorTextures = hardwareCausticAccumulatorTextures,
        .accumulatorTextureCount = LengthOf(hardwareCausticAccumulatorTextures),
    };
    const auto prepareHardwareCausticsTask = [](
        void* const rawContext,
        const Core::CommandListResourceStateHandoff* const finalState
    ) -> bool {
        HardwareCausticsStateLifecycleContext* const context =
            static_cast<HardwareCausticsStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->accumulatorStateCandidate
            || !context->accumulatorTextures
            || !finalState
        )
            return false;

        const bool accumulatorStateReady =
            context->renderer->m_hardwareCausticAccumulatorPersistentState.buildFilteredResourceSubset(
                *context->accumulatorStateCandidate,
                *finalState,
                context->accumulatorTextures,
                context->accumulatorTextureCount,
                nullptr,
                0u,
                context->scratchArena
            )
        ;
        context->statePrepared = accumulatorStateReady;
        return context->statePrepared;
    };
    const auto acceptHardwareCausticsTask = [](
        void* const rawContext,
        const Core::QueueSubmissionToken& token
    ) -> bool {
        static_cast<void>(token);
        HardwareCausticsStateLifecycleContext* const context =
            static_cast<HardwareCausticsStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->accumulatorStateCandidate
            || !context->statePrepared
        )
            return false;

        const bool accumulatorStateReady =
            context->renderer->m_hardwareCausticAccumulatorPersistentState.commit(*context->accumulatorStateCandidate)
        ;
        context->stateReady = accumulatorStateReady;
        return context->stateReady;
    };

    const Core::TextureHandle deferredLightingShadowReturnTextures[] = {
        deferredTargets.shadowVisibility,
    };
    const Core::TextureHandle deferredLightingCausticReturnTextures[] = {
        deferredTargets.causticIrradiance,
    };
    const Core::TextureHandle deferredLightingSurfelReturnTextures[] = {
        deferredTargets.surfelIrradiance,
    };
    Core::GpuPersistentResourceStateCache::Candidate deferredLightingShadowReturnStateCandidate(m_shadowVisibilityReturnState);
    Core::GpuPersistentResourceStateCache::Candidate deferredLightingCausticReturnStateCandidate(m_causticIrradianceReturnState);
    Core::GpuPersistentResourceStateCache::Candidate deferredLightingSurfelReturnStateCandidate(m_surfelIrradianceReturnState);
    struct DeferredLightingStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* shadowReturnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* causticReturnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* surfelReturnStateCandidate = nullptr;
        const Core::TextureHandle* shadowReturnTextures = nullptr;
        const Core::TextureHandle* causticReturnTextures = nullptr;
        const Core::TextureHandle* surfelReturnTextures = nullptr;
        usize shadowReturnTextureCount = 0u;
        usize causticReturnTextureCount = 0u;
        usize surfelReturnTextureCount = 0u;
        bool runsOnCompute = false;
        bool usesLaggedHistory = false;
        bool statePrepared = false;
        bool stateReady = false;
    } deferredLightingStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = this,
        .targets = &deferredTargets,
        .shadowReturnStateCandidate = &deferredLightingShadowReturnStateCandidate,
        .causticReturnStateCandidate = &deferredLightingCausticReturnStateCandidate,
        .surfelReturnStateCandidate = &deferredLightingSurfelReturnStateCandidate,
        .shadowReturnTextures = deferredLightingShadowReturnTextures,
        .causticReturnTextures = deferredLightingCausticReturnTextures,
        .surfelReturnTextures = deferredLightingSurfelReturnTextures,
        .shadowReturnTextureCount = LengthOf(deferredLightingShadowReturnTextures),
        .causticReturnTextureCount = LengthOf(deferredLightingCausticReturnTextures),
        .surfelReturnTextureCount = LengthOf(deferredLightingSurfelReturnTextures),
        .runsOnCompute = deferredLightingRunsOnCompute,
        .usesLaggedHistory = laggedAsyncLightingSchedule,
    };
    const auto prepareDeferredLightingTask = [](
        void* const rawContext,
        const Core::CommandListResourceStateHandoff* const finalState
    ) -> bool {
        DeferredLightingStateLifecycleContext* const context =
            static_cast<DeferredLightingStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->shadowReturnStateCandidate
            || !context->causticReturnStateCandidate
            || !context->surfelReturnStateCandidate
            || !context->shadowReturnTextures
            || !context->causticReturnTextures
            || !context->surfelReturnTextures
            || !finalState
        )
            return false;

        bool shadowStateReady = true;
        bool causticStateReady = true;
        bool surfelStateReady = true;
        if(context->runsOnCompute && !context->usesLaggedHistory){
            shadowStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(
                *context->shadowReturnStateCandidate,
                *finalState,
                context->shadowReturnTextures,
                context->shadowReturnTextureCount,
                nullptr,
                0u,
                context->scratchArena
            );
            causticStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
                *context->causticReturnStateCandidate,
                *finalState,
                context->causticReturnTextures,
                context->causticReturnTextureCount,
                nullptr,
                0u,
                context->scratchArena
            );
            surfelStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(
                *context->surfelReturnStateCandidate,
                *finalState,
                context->surfelReturnTextures,
                context->surfelReturnTextureCount,
                nullptr,
                0u,
                context->scratchArena
            );
        }
        context->statePrepared = shadowStateReady && causticStateReady && surfelStateReady;
        return context->statePrepared;
    };
    const auto acceptDeferredLightingTask = [](
        void* const rawContext,
        const Core::QueueSubmissionToken& token
    ) -> bool {
        static_cast<void>(token);
        DeferredLightingStateLifecycleContext* const context =
            static_cast<DeferredLightingStateLifecycleContext*>(rawContext)
        ;
        if(
            !context
            || !context->renderer
            || !context->targets
            || !context->shadowReturnStateCandidate
            || !context->causticReturnStateCandidate
            || !context->surfelReturnStateCandidate
            || !context->statePrepared
        )
            return false;

        RendererFramePipeline& renderer = *context->renderer;
        if(context->usesLaggedHistory)
            context->targets->laggedLightingHistory.slotsUploaded = true;
        bool shadowStateReady = true;
        bool causticStateReady = true;
        bool surfelStateReady = true;
        if(context->runsOnCompute && !context->usesLaggedHistory){
            shadowStateReady = renderer.m_shadowVisibilityReturnState.commit(*context->shadowReturnStateCandidate);
            causticStateReady = renderer.m_causticIrradianceReturnState.commit(*context->causticReturnStateCandidate);
            surfelStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->surfelReturnStateCandidate);
        }
        context->stateReady = shadowStateReady && causticStateReady && surfelStateReady;
        if(!context->stateReady)
            return false;
        if(context->usesLaggedHistory){
            renderer.reportLaggedLightingTransition(
                LaggedLightingReport::ActiveHistoryAccepted,
                context->targets->laggedLightingHistory.generation
            );
        }
        return true;
    };

    Core::GpuTaskGraphTaskRecordedCallback normalRecordedCallbacks[
        RendererFramePipelineExecuteDetail::s_DeferredStateLifecycleCallbackCapacity
    ] = {};
    usize normalRecordedCallbackCount = 0u;
    normalRecordedCallbacks[normalRecordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{
        .task = m_deferredShadowPrepareTask,
        .context = &shadowPrepareStateLifecycle,
        .invoke = prepareShadowPrepareTask,
    };
    normalRecordedCallbacks[normalRecordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{
        .task = m_deferredShadowVisibilityTask,
        .context = &shadowVisibilityStateLifecycle,
        .invoke = prepareShadowVisibilityTask,
    };
    if(!hardwareShadowSupported){
        normalRecordedCallbacks[normalRecordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{
            .task = m_deferredSoftwareCausticsTask,
            .context = &softwareCausticsStateLifecycle,
            .invoke = prepareSoftwareCausticsTask,
        };
    }
    normalRecordedCallbacks[normalRecordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{
        .task = m_deferredSurfelGiTask,
        .context = &surfelGiStateLifecycle,
        .invoke = prepareSurfelGiTask,
    };
    if(hardwareShadowSupported){
        normalRecordedCallbacks[normalRecordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{
            .task = m_deferredHardwareCausticsTask,
            .context = &hardwareCausticsStateLifecycle,
            .invoke = prepareHardwareCausticsTask,
        };
    }
    normalRecordedCallbacks[normalRecordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{
        .task = m_deferredLightingTask,
        .context = &deferredLightingStateLifecycle,
        .invoke = prepareDeferredLightingTask,
    };
    NWB_ASSERT(normalRecordedCallbackCount <= LengthOf(normalRecordedCallbacks));

    Core::GpuTaskGraphTaskAcceptedCallback normalAcceptedCallbacks[
        RendererFramePipelineExecuteDetail::s_DeferredStateLifecycleCallbackCapacity
    ] = {};
    usize normalAcceptedCallbackCount = 0u;
    normalAcceptedCallbacks[normalAcceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{
        .task = m_deferredShadowPrepareTask,
        .context = &shadowPrepareStateLifecycle,
        .invoke = acceptShadowPrepareTask,
    };
    normalAcceptedCallbacks[normalAcceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{
        .task = m_deferredShadowVisibilityTask,
        .context = &shadowVisibilityStateLifecycle,
        .invoke = acceptShadowVisibilityTask,
    };
    if(!hardwareShadowSupported){
        normalAcceptedCallbacks[normalAcceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{
            .task = m_deferredSoftwareCausticsTask,
            .context = &softwareCausticsStateLifecycle,
            .invoke = acceptSoftwareCausticsTask,
        };
    }
    normalAcceptedCallbacks[normalAcceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{
        .task = m_deferredSurfelGiTask,
        .context = &surfelGiStateLifecycle,
        .invoke = acceptSurfelGiTask,
    };
    if(hardwareShadowSupported){
        normalAcceptedCallbacks[normalAcceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{
            .task = m_deferredHardwareCausticsTask,
            .context = &hardwareCausticsStateLifecycle,
            .invoke = acceptHardwareCausticsTask,
        };
    }
    normalAcceptedCallbacks[normalAcceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{
        .task = m_deferredLightingTask,
        .context = &deferredLightingStateLifecycle,
        .invoke = acceptDeferredLightingTask,
    };
    NWB_ASSERT(normalAcceptedCallbackCount <= LengthOf(normalAcceptedCallbacks));

    Core::GpuTaskGraphTaskTimingTicket normalTimingTickets[
        RendererFramePipelineExecuteDetail::s_DeferredTimingTicketCapacity
    ] = {};
    usize normalTimingTicketCount = 0u;
    const auto appendNormalTimingTicket = [&](
        const Core::GpuTaskId task,
        Core::GpuTimingSubmissionTicket& timingTicket
    ) -> bool {
        if(!task.valid() || normalTimingTicketCount >= LengthOf(normalTimingTickets))
            return false;
        normalTimingTickets[normalTimingTicketCount++] = Core::GpuTaskGraphTaskTimingTicket{
            .task = task,
            .timingTicket = &timingTicket,
        };
        return true;
    };
    bool normalTimingTicketsReady = appendNormalTimingTicket(
        m_deferredShadowPrepareTask,
        shadowPrepareTimingTicket
    );
    for(usize prefixTaskIndex = 0u;
        normalTimingTicketsReady && prefixTaskIndex < graphicsPrefixTimingTicketCount;
        ++prefixTaskIndex
    ){
        const Core::GpuTaskId task = graphicsPrefixTimingTasks[prefixTaskIndex];
        bool packetAlreadyTimed = false;
        for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
            if(deferredCompiledPlan.tasksSharePacket(task, graphicsPrefixTimingTasks[earlierTaskIndex])){
                packetAlreadyTimed = true;
                break;
            }
        }
        if(packetAlreadyTimed)
            continue;
        if(!deferredCompiledPlan.findTask(task).valid() || !graphicsPrefixTimingTickets[prefixTaskIndex]){
            normalTimingTicketsReady = false;
            break;
        }
        normalTimingTicketsReady = appendNormalTimingTicket(task, *graphicsPrefixTimingTickets[prefixTaskIndex]);
    }
    normalTimingTicketsReady = normalTimingTicketsReady
        && normalTimingTicketCount == 1u + graphicsPrefixUniquePacketCount
        && appendNormalTimingTicket(m_deferredShadowVisibilityTask, shadowVisibilityTimingTicket)
        && appendNormalTimingTicket(
            hardwareShadowSupported ? m_deferredHardwareCausticsTask : m_deferredSoftwareCausticsTask,
            hardwareShadowSupported ? hardwareCausticsTimingTicket : softwareCausticsTimingTicket
        )
        && appendNormalTimingTicket(m_deferredSurfelGiTask, surfelGiTimingTicket)
    ;
    RendererAvboitTaskGraphTimingTickets avboitTimingTickets{
        .m_pre = avboitPreTimingTicket,
        .m_depthWarp = avboitDepthWarpTimingTicket,
        .m_extinction = avboitExtinctionTimingTicket,
        .m_integration = avboitIntegrationTimingTicket,
        .m_accumulation = avboitAccumulationTimingTicket,
    };
    normalTimingTicketsReady = normalTimingTicketsReady
        && m_avboitSystem.appendTaskGraphTimingTickets(
            avboitValidation,
            avboitTimingTickets,
            normalTimingTickets,
            LengthOf(normalTimingTickets),
            normalTimingTicketCount
        )
        && appendNormalTimingTicket(m_deferredLightingTask, deferredLightingTimingTicket)
        && appendNormalTimingTicket(m_deferredCompositeTask, deferredCompositeTimingTicket)
        && appendNormalTimingTicket(m_deferredPresentTask, deferredPresentTimingTicket)
    ;
    if(!normalTimingTicketsReady){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to bind normal deferred graph timing"));
        discardRenderPackets();
        return;
    }

    // Empty hooks keep the compatibility present() path.
    const Core::QueueSubmissionPreSubmitHook framePresentationSignal = m_graphics.claimFramePresentationSignal();
    const Core::GpuTaskGraphTaskSubmissionHook terminalPresentationSubmissionHooks[] = {
        Core::GpuTaskGraphTaskSubmissionHook{
            .task = terminalPresentationTask,
            .hook = framePresentationSignal,
        },
    };
    Core::GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = terminalPresentationTask;
    normalExecution.taskRecordedCallbacks = normalRecordedCallbacks;
    normalExecution.taskRecordedCallbackCount = normalRecordedCallbackCount;
    normalExecution.readyFrontierScheduler = &m_world.taskScheduler();
    normalExecution.taskTimingTickets = normalTimingTickets;
    normalExecution.taskTimingTicketCount = normalTimingTicketCount;
    normalExecution.taskAcceptedCallbacks = normalAcceptedCallbacks;
    normalExecution.taskAcceptedCallbackCount = normalAcceptedCallbackCount;
    normalExecution.taskSubmissionHooks = framePresentationSignal.valid()
        ? terminalPresentationSubmissionHooks
        : nullptr
    ;
    normalExecution.taskSubmissionHookCount = framePresentationSignal.valid()
        ? LengthOf(terminalPresentationSubmissionHooks)
        : 0u
    ;

    const Core::GpuTaskScheduler& normalSubmitter = m_graphics.gpuTasks();
    const bool normalGraphAccepted = normalSubmitter.submit(
        m_deferredLightingTaskGraph,
        m_deferredLightingCompiledGraph,
        deferredRecorder,
        m_deferredLightingRecordedGraph,
        normalExecution,
        m_deferredLightingSubmissionTransaction,
        normalExecutionScratchArena
    );

    const Core::QueueSubmissionToken shadowPrepareSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            m_deferredShadowPrepareTask
        )
    ;
    const Core::QueueSubmissionToken graphicsPrefixSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            m_graphicsPrefixTask
        )
    ;
    const Core::QueueSubmissionToken shadowVisibilitySubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            m_deferredShadowVisibilityTask
        )
    ;
    const Core::GpuTaskId selectedCausticsTask = hardwareShadowSupported
        ? m_deferredHardwareCausticsTask
        : m_deferredSoftwareCausticsTask
    ;
    const Core::QueueSubmissionToken selectedCausticsSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            selectedCausticsTask
        )
    ;
    const Core::QueueSubmissionToken surfelGiSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            m_deferredSurfelGiTask
        )
    ;
    const Core::QueueSubmissionToken avboitPreSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            avboitValidation.stage().firstTask
        )
    ;
    const Core::QueueSubmissionToken avboitCompletionSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            avboitValidation.stage().completionTask
        )
    ;
    const Core::QueueSubmissionToken deferredLightingSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            m_deferredLightingTask
        )
    ;
    const Core::QueueSubmissionToken deferredCompositeSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            m_deferredCompositeTask
        )
    ;
    const Core::QueueSubmissionToken deferredPresentSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            m_deferredPresentTask
        )
    ;
    const Core::QueueSubmissionToken finalPresentationSubmissionToken =
        m_deferredLightingSubmissionTransaction.taskToken(
            deferredCompiledPlan,
            terminalPresentationTask
        )
    ;

    bool presentationSignalReady = true;
    if(framePresentationSignal.valid()){
        if(finalPresentationSubmissionToken.valid()){
            presentationSignalReady = m_graphics.confirmFramePresentationSignal(
                framePresentationSignal,
                finalPresentationSubmissionToken
            );
            if(!presentationSignalReady && !m_graphics.cancelFramePresentationSignal(framePresentationSignal))
                m_graphics.requestDeviceRecreation();
        }
        else{
            presentationSignalReady = false;
            if(!m_graphics.cancelFramePresentationSignal(framePresentationSignal))
                m_graphics.requestDeviceRecreation();
        }
    }

    const bool selectedCausticsStateReady = hardwareShadowSupported
        ? hardwareCausticsStateLifecycle.stateReady
        : softwareCausticsStateLifecycle.stateReady
    ;
    const bool normalSemanticTokensReady =
        shadowPrepareSubmissionToken.valid()
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
    const bool normalStateReady =
        shadowPrepareStateLifecycle.stateReady
        && shadowVisibilityStateLifecycle.stateReady
        && selectedCausticsStateReady
        && surfelGiStateLifecycle.stateReady
        && deferredLightingStateLifecycle.stateReady
    ;
    const bool acceptedStateLost =
        (shadowPrepareSubmissionToken.valid() && !shadowPrepareStateLifecycle.stateReady)
        || (shadowVisibilitySubmissionToken.valid() && !shadowVisibilityStateLifecycle.stateReady)
        || (selectedCausticsSubmissionToken.valid() && !selectedCausticsStateReady)
        || (surfelGiSubmissionToken.valid() && !surfelGiStateLifecycle.stateReady)
        || (deferredLightingSubmissionToken.valid() && !deferredLightingStateLifecycle.stateReady)
    ;

    bool frameTimingEndReady = true;
    if(finalPresentationSubmissionToken.valid()){
        frameTimingEndReady = frameTimingTransaction.confirmEndSubmission(finalPresentationSubmissionToken, true);
        if(!frameTimingEndReady){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm frame critical-path timing"));
            frameTimingTransaction.discard();
        }
    }
    if(
        !normalGraphAccepted
        || !normalSemanticTokensReady
        || !normalStateReady
        || !presentationSignalReady
    ){
        const bool hadAcceptedPackets = m_deferredLightingSubmissionTransaction.hasAcceptedPackets();
        bool recovered = true;
        if(!hadAcceptedPackets){
            discardRenderPackets();
        }
        else{
            recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();

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
            failFrameRenderRecovery();
        return;
    }

    if(!dedicatedAsyncCompute && m_frameLaggedAsyncLightingEnabled){
        reportLaggedLightingTransition(
            LaggedLightingReport::NoDedicatedAsyncCompute,
            deferredTargets.laggedLightingHistory.generation
        );
    }
    else if(m_laggedLightingCurrentFrameAcceptancePending){
        reportLaggedLightingTransition(
            LaggedLightingReport::CurrentFrameAccepted,
            deferredTargets.laggedLightingHistory.generation
        );
        m_laggedLightingCurrentFrameAcceptancePending = false;
    }

    if(m_deferredSurfelGiCounterReadbackTask.valid()){
        // No normal-frame consumer; record only after Present.
        const bool readbackTailAvailable =
            finalPresentationSubmissionToken.valid()
            && m_deferredLightingTaskGraphValid
            && deferredCompiledPlan.findTask(m_deferredSurfelGiCounterReadbackTask).valid()
            && surfelGiCounterReadbackQueue
            && (static_cast<u8>(surfelGiCounterReadbackQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) != 0u
        ;
        if(!readbackTailAvailable){
            m_deferredLightingSubmissionTransaction.rejectTask(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredSurfelGiCounterReadbackTask,
                m_deferredLightingRecordedGraph.recordingAttemptGeneration()
            );
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback tail was unavailable"));
        }
        else{
            Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
            Core::GpuPersistentResourceStateCache::Candidate readbackCounterStateCandidate(m_surfelGiCounterPersistentState);
            const Core::BufferHandle readbackCounterBuffers[] = {
                rayTracingSurfelResources.counterBuffer,
            };
            // Keep the candidate private until Transfer accepts.
            struct SurfelCounterReadbackContext{
                Core::Alloc::ScratchArena& scratchArena;
                RendererFramePipeline* renderer = nullptr;
                Core::GpuPersistentResourceStateCache::Candidate* candidate = nullptr;
                const Core::BufferHandle* buffers = nullptr;
                usize bufferCount = 0u;
                bool finalStateReady = false;
                bool acceptedStateReady = false;
            } readbackContext{
                .scratchArena = scratchArena,
                .renderer = this,
                .candidate = &readbackCounterStateCandidate,
                .buffers = readbackCounterBuffers,
                .bufferCount = LengthOf(readbackCounterBuffers),
            };
            const auto prepareReadbackFinalState = [](
                void* const rawContext,
                const Core::CommandListResourceStateHandoff* const finalState
            ) -> bool {
                SurfelCounterReadbackContext* const context =
                    static_cast<SurfelCounterReadbackContext*>(rawContext)
                ;
                if(!context || !context->renderer || !context->candidate || !context->buffers)
                    return false;
                context->finalStateReady = finalState
                    && context->renderer->m_surfelGiCounterPersistentState.buildFilteredBufferSubset(
                        *context->candidate,
                        *finalState,
                        context->buffers,
                        context->bufferCount,
                        context->scratchArena
                    )
                ;
                return context->finalStateReady;
            };
            const Core::GpuTaskGraphTaskRecordedCallback readbackRecordedCallback{
                .task = m_deferredSurfelGiCounterReadbackTask,
                .context = &readbackContext,
                .invoke = prepareReadbackFinalState,
            };
            const auto acceptReadbackFinalState = [](
                void* const rawContext,
                const Core::QueueSubmissionToken& token
            ) -> bool {
                static_cast<void>(token);
                SurfelCounterReadbackContext* const context =
                    static_cast<SurfelCounterReadbackContext*>(rawContext)
                ;
                if(!context || !context->renderer || !context->candidate || !context->finalStateReady)
                    return false;
                context->acceptedStateReady = context->renderer->m_surfelGiCounterPersistentState.commit(
                    *context->candidate
                );
                if(context->acceptedStateReady)
                    context->renderer->m_raytracingSystem.confirmSurfelCountReadbackSubmission(token);
                return context->acceptedStateReady;
            };
            const Core::GpuTaskGraphTaskAcceptedCallback readbackAcceptedCallback{
                .task = m_deferredSurfelGiCounterReadbackTask,
                .context = &readbackContext,
                .invoke = acceptReadbackFinalState,
            };
            const Core::GpuNativePacketRecorder recorder(device, m_graphics.gpuTiming());
            const Core::GpuTaskScheduler& submitter = m_graphics.gpuTasks();
            const bool readbackAccepted = submitter.recordAndSubmitTask(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                recorder,
                m_deferredLightingRecordedGraph,
                m_deferredSurfelGiCounterReadbackTask,
                &readbackRecordedCallback,
                m_deferredLightingSubmissionTransaction,
                scratchArena,
                nullptr,
                &readbackAcceptedCallback
            );
            const Core::QueueSubmissionToken readbackSubmissionToken =
                m_deferredLightingSubmissionTransaction.taskToken(
                    deferredCompiledPlan,
                    m_deferredSurfelGiCounterReadbackTask
                )
            ;
            if(!readbackSubmissionToken.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback record/submission was rejected"));
            }
            else if(!readbackAccepted || !readbackContext.acceptedStateReady){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: accepted surfel counter-readback tail lost its retained state"));
                failFrameRenderRecovery();
                return;
            }
            else{
                // Publish only after retained state commits.
                NWB_ASSERT(m_raytracingSystem.surfelCountReadbackSubmissionMatches(readbackSubmissionToken));
            }
        }
    }

    if(requestsLaggedLightingHistoryCapture && !captureLaggedLightingHistory){
        // Present already completed; force next frame through bootstrap.
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting"));
        invalidateLaggedLightingHistorySubmission();
    }
    else if(captureLaggedLightingHistory){
        // The history copy depends on Present; publication needs accepted presentation.
        if(
            !finalPresentationSubmissionToken.valid()
            || !m_deferredLightingTaskGraphValid
            || !m_deferredLaggedLightingHistoryTask.valid()
            || !taskIsCompiled(m_deferredLaggedLightingHistoryTask)
            || !deferredLaggedLightingHistoryQueue
            || (static_cast<u8>(deferredLaggedLightingHistoryQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ){
            if(m_deferredLightingTaskGraphValid){
                if(!m_deferredLightingSubmissionTransaction.discardUnaccepted(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph.recordingAttemptGeneration()
                ))
                    m_graphics.requestDeviceRecreation();
            }
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting"));
            invalidateLaggedLightingHistorySubmission();
        }
        else{
            Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
            Core::GpuPersistentResourceStateCache::Candidate shadowHistoryReturnStateCandidate(m_shadowVisibilityReturnState);
            Core::GpuPersistentResourceStateCache::Candidate causticHistoryReturnStateCandidate(m_causticIrradianceReturnState);
            Core::GpuPersistentResourceStateCache::Candidate surfelHistoryReturnStateCandidate(m_surfelIrradianceReturnState);
            struct HistoryCopyAcceptanceContext{
                Core::Alloc::ScratchArena& scratchArena;
                RendererFramePipeline* renderer = nullptr;
                DeferredFrameTargets* targets = nullptr;
                Core::GpuPersistentResourceStateCache::Candidate* shadowStateCandidate = nullptr;
                Core::GpuPersistentResourceStateCache::Candidate* causticStateCandidate = nullptr;
                Core::GpuPersistentResourceStateCache::Candidate* surfelStateCandidate = nullptr;
                bool finalStateReady = false;
                bool acceptedStateReady = false;
            } historyCopyAcceptance{
                .scratchArena = scratchArena,
                .renderer = this,
                .targets = &deferredTargets,
                .shadowStateCandidate = &shadowHistoryReturnStateCandidate,
                .causticStateCandidate = &causticHistoryReturnStateCandidate,
                .surfelStateCandidate = &surfelHistoryReturnStateCandidate,
            };
            const auto prepareHistoryCopyFinalState = [](
                void* const rawContext,
                const Core::CommandListResourceStateHandoff* const finalState
            ) -> bool {
                HistoryCopyAcceptanceContext* const context =
                    static_cast<HistoryCopyAcceptanceContext*>(rawContext)
                ;
                if(
                    !context
                    || !context->renderer
                    || !context->targets
                    || !context->shadowStateCandidate
                    || !context->causticStateCandidate
                    || !context->surfelStateCandidate
                    || !finalState
                )
                    return false;

                const bool shadowStateReady =
                    context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(
                        *context->shadowStateCandidate,
                        *finalState,
                        &context->targets->shadowVisibility,
                        1u,
                        nullptr,
                        0u,
                        context->scratchArena
                    )
                ;
                const bool causticStateReady =
                    context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
                        *context->causticStateCandidate,
                        *finalState,
                        &context->targets->causticIrradiance,
                        1u,
                        nullptr,
                        0u,
                        context->scratchArena
                    )
                ;
                const bool surfelStateReady =
                    context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(
                        *context->surfelStateCandidate,
                        *finalState,
                        &context->targets->surfelIrradiance,
                        1u,
                        nullptr,
                        0u,
                        context->scratchArena
                    )
                ;
                context->finalStateReady = shadowStateReady && causticStateReady && surfelStateReady;
                return context->finalStateReady;
            };
            const Core::GpuTaskGraphTaskRecordedCallback historyCopyRecordedCallback{
                .task = m_deferredLaggedLightingHistoryTask,
                .context = &historyCopyAcceptance,
                .invoke = prepareHistoryCopyFinalState,
            };
            const auto acceptHistoryCopyFinalState = [](
                void* const rawContext,
                const Core::QueueSubmissionToken& token
            ) -> bool {
                static_cast<void>(token);
                HistoryCopyAcceptanceContext* const context =
                    static_cast<HistoryCopyAcceptanceContext*>(rawContext)
                ;
                if(
                    !context
                    || !context->renderer
                    || !context->shadowStateCandidate
                    || !context->causticStateCandidate
                    || !context->surfelStateCandidate
                    || !context->finalStateReady
                )
                    return false;

                const bool shadowStateReady = context->renderer->m_shadowVisibilityReturnState.commit(
                    *context->shadowStateCandidate
                );
                const bool causticStateReady = context->renderer->m_causticIrradianceReturnState.commit(
                    *context->causticStateCandidate
                );
                const bool surfelStateReady = context->renderer->m_surfelIrradianceReturnState.commit(
                    *context->surfelStateCandidate
                );
                context->acceptedStateReady = shadowStateReady && causticStateReady && surfelStateReady;
                return context->acceptedStateReady;
            };
            const Core::GpuTaskGraphTaskAcceptedCallback historyCopyAcceptedCallback{
                .task = m_deferredLaggedLightingHistoryTask,
                .context = &historyCopyAcceptance,
                .invoke = acceptHistoryCopyFinalState,
            };
            const Core::GpuNativePacketRecorder recorder(device, m_graphics.gpuTiming());
            const Core::GpuTaskScheduler& submitter = m_graphics.gpuTasks();
            const bool historyCopyAccepted = submitter.recordAndSubmitTask(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                recorder,
                m_deferredLightingRecordedGraph,
                m_deferredLaggedLightingHistoryTask,
                &historyCopyRecordedCallback,
                m_deferredLightingSubmissionTransaction,
                scratchArena,
                nullptr,
                &historyCopyAcceptedCallback
            );
            const Core::QueueSubmissionToken historyCopySubmissionToken =
                m_deferredLightingSubmissionTransaction.taskToken(
                    deferredCompiledPlan,
                    m_deferredLaggedLightingHistoryTask
                )
            ;
            if(historyCopySubmissionToken.valid() && (!historyCopyAccepted || !historyCopyAcceptance.acceptedStateReady)){
                if(!submitFrameRecoveryPacket())
                    failFrameRenderRecovery();
                // The accepted copy cannot be replayed.
                failFrameRenderRecovery();
                return;
            }
            if(!historyCopyAccepted || !historyCopySubmissionToken.valid()){
                if(!m_deferredLightingSubmissionTransaction.discardUnaccepted(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph.recordingAttemptGeneration()
                ))
                    m_graphics.requestDeviceRecreation();
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned lagged lighting-history capture record/submission was rejected; reverting to current-frame lighting"));
                invalidateLaggedLightingHistorySubmission();
            }
            else{
                // The accepted hook publishes this token; keep the assertion here.
                NWB_ASSERT(
                    m_laggedLightingHistorySubmissionToken.queue == historyCopySubmissionToken.queue
                    && m_laggedLightingHistorySubmissionToken.value == historyCopySubmissionToken.value
                );
                if(!laggedAsyncLightingSchedule){
                    reportLaggedLightingTransition(
                        LaggedLightingReport::BootstrapAccepted,
                        deferredTargets.laggedLightingHistory.generation
                    );
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

