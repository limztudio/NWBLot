// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_frame_topology_validator.h"

#include <impl/ecs_render/execute/frame_diagnostics.adhd.h>
#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/execute/frame_execute_timing_state.h>
#include <impl/ecs_render/execute/graphics_prefix_timing_resolver.h>
#include <impl/ecs_render/execute/opaque_emulation_merge_validator.h>
#include <impl/ecs_render/execute/surfel_caustics_merge_validator.h>
#include <impl/ecs_render/shadow/shadow_prepare_packet_validator.h>
#include <impl/ecs_render/shadow/shadow_visibility_merge_validator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeferredFrameTopologyValidator::validate(
    const Core::GpuCompiledGraph::ReadView& deferredCompiledPlan,
    const Core::GpuTaskGraph::DeclarationReadView& deferredTaskGraphView,
    FrameExecuteTimingState& timing,
    const Core::GpuPhysicalQueueId primaryGraphicsQueue,
    const b8 deferredGraphScheduled,
    const b8 captureLaggedLightingHistory,
    const b8 hardwareShadowSupported,
    const b8 hasOpaqueCsgFrameWork,
    const b8 hasTransparentRenderers,
    const b8 clearAvboitTargets,
    const b8 laggedAsyncLightingSchedule,
    const b8 laggedLightingHistoryWriterWaitPending,
    const b8 surfelCounterReadbackCompletionValid,
    DeferredFrameTopologyResult& outResult
)const noexcept{
    const auto taskIsCompiled = [&](const Core::GpuTaskId task)noexcept{
        return deferredCompiledPlan.findTask(task).valid();
    };
    ShadowPreparePacketValidator shadowPreparePacketValidator(m_pipeline);
    ShadowPreparePacketValidationResult shadowPreparePacket;
    shadowPreparePacketValidator.validate(deferredCompiledPlan, shadowPreparePacket);
    const b8 shadowPrepareSoftwareBvhBuildsMerged = shadowPreparePacket.softwareBvhBuildsMerged;
    const b8 shadowPrepareAccelStructFinalizeMerged = shadowPreparePacket.accelStructFinalizeMerged;
    const b8 deferredBindlessSlotsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.bindlessSlotsUploadMerged;
    const b8 rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.rayTraceMaterialContextSlotsUploadMerged;
    const b8 causticEmissionTargetsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.causticEmissionTargetsUploadMerged;
    const b8 surfelFrameConstantsUploadMergedIntoShadowPreparePacket = shadowPreparePacket.surfelFrameConstantsUploadMerged;
    const b8 shadowMaterialContextUploadsMergedIntoShadowPreparePacket = shadowPreparePacket.shadowMaterialContextUploadsMerged;
    const b8 sceneBvhUploadsMergedIntoShadowPreparePacket = shadowPreparePacket.sceneBvhUploadsMerged;
    // CSG callbacks keep an independent anchor across FrontierSafe splits.
    Array<Core::GpuTaskId, s_GraphicsPrefixTimingTaskCount> graphicsPrefixTimingTasks;
    GraphicsPrefixTimingResolver graphicsPrefixTimingResolver(m_pipeline);
    GraphicsPrefixTimingResolutionResult graphicsPrefixTimingResolution;
    graphicsPrefixTimingResolver.resolve(
        deferredCompiledPlan,
        timing.graphicsPrefixOwnedTimingTickets,
        timing.graphicsPrefixTimingTickets,
        graphicsPrefixTimingTasks,
        timing.asyncPrefixTimingSpansOnePacket,
        graphicsPrefixTimingResolution
    );
    const b8 graphicsPrefixTimingBindingsValid = graphicsPrefixTimingResolution.bindingsValid;
    const usize graphicsPrefixUniquePacketCount = graphicsPrefixTimingResolution.uniquePacketCount;
    ShadowVisibilityMergeValidator shadowVisibilityMergeValidator(m_pipeline);
    ShadowVisibilityMergeValidationResult shadowVisibilityMerge;
    shadowVisibilityMergeValidator.validate(deferredCompiledPlan, shadowVisibilityMerge);
    const b8 shadowVisibilityPreparedTasksMerged = shadowVisibilityMerge.preparedTasksMerged;
    const b8 shadowVisibilityAllLitClearMerged = shadowVisibilityMerge.allLitClearMerged;
    const b8 shadowVisibilityAdaptivePrimitivesMerged = shadowVisibilityMerge.adaptivePrimitivesMerged;
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask);
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixTask);
    OpaqueEmulationMergeValidator opaqueEmulationMergeValidator(m_pipeline);
    OpaqueEmulationMergeValidationResult opaqueEmulationMerge;
    opaqueEmulationMergeValidator.validate(deferredCompiledPlan, graphicsPrefixTimingTasks, graphicsPrefixTimingBindingsValid, hasOpaqueCsgFrameWork, opaqueEmulationMerge);
    const b8 graphicsPrefixPacketsAreGraphics = opaqueEmulationMerge.packetsAreGraphics;
    const b8 graphicsPrefixOpaqueComputeEmulationMerged = opaqueEmulationMerge.opaqueComputeEmulationMerged;
    const b8 graphicsPrefixOpaqueSharedComputeEmulationMerged = opaqueEmulationMerge.opaqueSharedComputeEmulationMerged;
    const b8 graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged = opaqueEmulationMerge.opaqueCsgReceiverComputeEmulationMerged;
    const b8 graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged = opaqueEmulationMerge.opaqueCsgIntervalSampleComputeEmulationMerged;
    const b8 graphicsPrefixCsgIntervalClearBundleMerged = opaqueEmulationMerge.csgIntervalClearBundleMerged;
    const Core::GpuPhysicalQueueInfo* const frameTimingBeginQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_deferredFrameTimingBeginTask);
    const Core::GpuSubmissionPacketId frameTimingBeginPacket = deferredCompiledPlan.packetForTask(m_pipeline.m_deferredFrameTimingBeginTask);
    const Core::GpuSubmissionPacketRange frameTimingEnvelope = deferredCompiledPlan.packetTimingEnvelopeRange();
    const Core::GpuPhysicalQueueInfo* const shadowPrepareQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask);
    const Core::GpuPhysicalQueueInfo* const softwareCausticsQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_causticsGraph.m_deferredSoftwareCausticsTask);
    const RendererAvboitTaskGraphValidation avboitValidation = m_pipeline.m_avboitSystem.validateTaskGraphStage(deferredCompiledPlan, clearAvboitTargets, hasTransparentRenderers);
    const Core::GpuTaskId causticsTask = hardwareShadowSupported ? m_pipeline.m_causticsGraph.m_deferredHardwareCausticsTask : m_pipeline.m_causticsGraph.m_deferredSoftwareCausticsTask;
    // History-selector uploads must share Lighting's acceptance boundary.
    const b8 laggedLightingHistorySlotsUploadMergedIntoLightingPacket = !m_pipeline.m_deferredLaggedLightingHistorySlotsUploadTask.valid() || deferredCompiledPlan.tasksSharePacket(m_pipeline.m_deferredLightingTask, m_pipeline.m_deferredLaggedLightingHistorySlotsUploadTask);
    // Resolve the terminal endpoint through the compiler, not renderer policy.
    const Core::GpuCompiledPresentEndpoint* const presentationEndpoint = deferredCompiledPlan.presentEndpoint();
    const Core::GpuTaskId terminalPresentationTask = presentationEndpoint ? presentationEndpoint->producer : Core::GpuTaskId{};
    const Core::GpuPhysicalQueueInfo* const deferredLightingQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_deferredLightingTask);
    const Core::GpuPhysicalQueueInfo* const deferredCompositeQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_deferredCompositeTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_deferredPresentTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentationOverlayQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_deferredPresentationOverlayTask);
    const Core::GpuPhysicalQueueInfo* const terminalPresentationQueue = presentationEndpoint ? deferredCompiledPlan.queueInfo(presentationEndpoint->queue) : nullptr;
    const Core::GpuPhysicalQueueInfo* const deferredLaggedLightingHistoryQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_deferredLaggedLightingHistoryTask);
    const Core::GpuPhysicalQueueInfo* const deferredFrameRecoveryQueue = deferredCompiledPlan.queueInfoForTask(m_pipeline.m_deferredFrameRecoveryTask);
    SurfelCausticsMergeValidator surfelCausticsMergeValidator(m_pipeline);
    SurfelCausticsMergeValidationResult surfelCausticsMerge;
    surfelCausticsMergeValidator.validate(deferredCompiledPlan, causticsTask, terminalPresentationTask, captureLaggedLightingHistory, surfelCausticsMerge);
    const Core::GpuPhysicalQueueInfo* const hardwareCausticsQueue = surfelCausticsMerge.hardwareCausticsQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiQueue = surfelCausticsMerge.surfelGiQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiPreparationQueue = surfelCausticsMerge.surfelGiPreparationQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiSnapshotCopyQueue = surfelCausticsMerge.surfelGiSnapshotCopyQueue;
    const Core::GpuPhysicalQueueInfo* const surfelGiCounterReadbackQueue = surfelCausticsMerge.surfelGiCounterReadbackQueue;
    const b8 surfelGiOutputClearMergedIntoGiPacket = surfelCausticsMerge.surfelGiOutputClearMergedIntoGiPacket;
    const b8 surfelGiPreparedPrefixMergedIntoGiPacket = surfelCausticsMerge.surfelGiPreparedPrefixMergedIntoGiPacket;
    const b8 surfelGiInitializationLifecycleMergedIntoPreparationPacket = surfelCausticsMerge.surfelGiInitializationLifecycleMergedIntoPreparationPacket;
    const b8 causticPhotonMergedIntoCausticsPacket = surfelCausticsMerge.causticPhotonMergedIntoCausticsPacket;
    const b8 causticGeometryMergedIntoCausticsPacket = surfelCausticsMerge.causticGeometryMergedIntoCausticsPacket;
    const b8 causticResolvePrepareMergedIntoCausticsPacket = surfelCausticsMerge.causticResolvePrepareMergedIntoCausticsPacket;
    const b8 causticResolveWaveletsMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveWaveletsMergedIntoCausticsPacket;
    const b8 causticResolveUpsampleMergedIntoCausticsPacket = surfelCausticsMerge.causticResolveUpsampleMergedIntoCausticsPacket;
    const b8 causticIrradianceClearMergedIntoCausticsPacket = surfelCausticsMerge.causticIrradianceClearMergedIntoCausticsPacket;
    const b8 causticAccumulatorBootstrapClearMergedIntoCausticsPacket = surfelCausticsMerge.causticAccumulatorBootstrapClearMergedIntoCausticsPacket;
    const b8 causticAccumulatorDecayMergedIntoCausticsPacket = surfelCausticsMerge.causticAccumulatorDecayMergedIntoCausticsPacket;
    const b8 surfelGiSnapshotCopyAndTimingPacketsAreDistinct = surfelCausticsMerge.surfelGiSnapshotCopyAndTimingPacketsAreDistinct;
    const b8 surfelCounterReadbackFollowsPresentation = surfelCausticsMerge.surfelCounterReadbackFollowsPresentation;
    const b8 laggedLightingHistoryFollowsPresentation = surfelCausticsMerge.laggedLightingHistoryFollowsPresentation;
    if(
        !deferredGraphScheduled
        || !m_pipeline.m_deferredLightingTaskGraphScheduled
        || !m_pipeline.m_deferredFrameTimingBeginTask.valid()
        || !taskIsCompiled(m_pipeline.m_deferredFrameTimingBeginTask)
        || !frameTimingBeginQueue
        || frameTimingBeginQueue->queueClass != Core::CommandQueue::Graphics
        || frameTimingBeginQueue->id != primaryGraphicsQueue
        || !frameTimingBeginPacket.valid()
        || frameTimingBeginPacket.index != 0u
        || !frameTimingEnvelope.valid()
        || frameTimingEnvelope.first != frameTimingBeginPacket
        || !m_pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask.valid()
        || !taskIsCompiled(m_pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask)
        || deferredCompiledPlan.tasksSharePacket(m_pipeline.m_deferredFrameTimingBeginTask, m_pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask)
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
        || !m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupTask.valid()
        || !m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixSceneShadingSetupTask.valid()
        || !m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixDeferredClearTask.valid()
        || !m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask.valid()
        || (hasOpaqueCsgFrameWork && (!m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgReceiverSpanTask.valid() || !m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask.valid() || !m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask.valid()))
        || !m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixTask.valid()
        || !taskIsCompiled(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupTask)
        || !taskIsCompiled(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixSceneShadingSetupTask)
        || !taskIsCompiled(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixDeferredClearTask)
        || !taskIsCompiled(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask)
        || (hasOpaqueCsgFrameWork && (
            !deferredCompiledPlan.findTask(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgReceiverSpanTask).valid()
            || !deferredCompiledPlan.findTask(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask).valid()
            || !deferredCompiledPlan.findTask(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask).valid()
        ))
        || !taskIsCompiled(m_pipeline.m_graphicsPrefixGraph.m_graphicsPrefixTask)
        || !graphicsPrefixTimingBindingsValid
        || !graphicsPrefixPacketsAreGraphics
        || !graphicsPrefixOpaqueComputeEmulationMerged
        || !graphicsPrefixOpaqueSharedComputeEmulationMerged
        || !graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged
        || !graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged
        || !graphicsPrefixCsgIntervalClearBundleMerged
        || !graphicsPrefixQueue
        || graphicsPrefixQueue->queueClass != Core::CommandQueue::Graphics
        || !m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask.valid()
        || !taskIsCompiled(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask)
        || !shadowVisibilityPreparedTasksMerged
        || !shadowVisibilityAllLitClearMerged
        || !shadowVisibilityAdaptivePrimitivesMerged
        || !shadowVisibilityQueue
        || (!hardwareShadowSupported && (!m_pipeline.m_causticsGraph.m_deferredSoftwareCausticsTask.valid() || !taskIsCompiled(m_pipeline.m_causticsGraph.m_deferredSoftwareCausticsTask) || !softwareCausticsQueue))
        || !m_pipeline.m_causticsGraph.m_deferredCausticIrradianceClearTask.valid()
        || !m_pipeline.m_causticsGraph.m_deferredCausticPhotonTask.valid()
        || !causticPhotonMergedIntoCausticsPacket
        || !m_pipeline.m_causticsGraph.m_deferredCausticGeometryTask.valid()
        || !causticGeometryMergedIntoCausticsPacket
        || !m_pipeline.m_causticsGraph.m_deferredCausticResolvePrepareTask.valid()
        || !causticResolvePrepareMergedIntoCausticsPacket
        || !causticResolveWaveletsMergedIntoCausticsPacket
        || !m_pipeline.m_causticsGraph.m_deferredCausticResolveUpsampleTask.valid()
        || !causticResolveUpsampleMergedIntoCausticsPacket
        || !causticIrradianceClearMergedIntoCausticsPacket
        || !causticAccumulatorBootstrapClearMergedIntoCausticsPacket
        || !causticAccumulatorDecayMergedIntoCausticsPacket
        || !m_pipeline.m_surfelGiGraph.m_deferredSurfelGiTask.valid()
        || !m_pipeline.m_surfelGiGraph.m_deferredSurfelGiIrradianceClearTask.valid()
        || !surfelGiOutputClearMergedIntoGiPacket
        || !surfelGiPreparedPrefixMergedIntoGiPacket
        || !surfelGiInitializationLifecycleMergedIntoPreparationPacket
        || !taskIsCompiled(m_pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
        || !surfelGiQueue
        || (m_pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask.valid() && (
            !m_pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask.valid()
            || !taskIsCompiled(m_pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask)
            || !surfelGiPreparationQueue
            || !taskIsCompiled(m_pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask)
            || !surfelGiSnapshotCopyQueue
            || (static_cast<u8>(surfelGiSnapshotCopyQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || (m_pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask.valid() && (
            !taskIsCompiled(m_pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask)
            || !surfelGiCounterReadbackQueue
            || (static_cast<u8>(surfelGiCounterReadbackQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || !surfelCounterReadbackFollowsPresentation
        || (hardwareShadowSupported && (
            !m_pipeline.m_causticsGraph.m_deferredHardwareCausticsTask.valid()
            || !taskIsCompiled(m_pipeline.m_causticsGraph.m_deferredHardwareCausticsTask)
            || !hardwareCausticsQueue
            || (static_cast<u8>(hardwareCausticsQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Compute)) == 0u
        ))
        || !avboitValidation.valid()
        || !m_pipeline.m_deferredLightingTask.valid()
        || !m_pipeline.m_deferredCompositeTask.valid()
        || !m_pipeline.m_deferredPresentTask.valid()
        || !m_pipeline.m_deferredFrameTimingEndTask.valid()
        || (m_pipeline.m_deferredPresentationOverlayRequired != m_pipeline.m_deferredPresentationOverlayTask.valid())
        || !m_pipeline.m_deferredFrameRecoveryTask.valid()
        || (m_pipeline.m_deferredSurfelGiCounterReadbackCompletion.valid() && !surfelCounterReadbackCompletionValid)
        || (captureLaggedLightingHistory && (
            !m_pipeline.m_deferredLaggedLightingHistoryTask.valid()
            || !taskIsCompiled(m_pipeline.m_deferredLaggedLightingHistoryTask)
            || !deferredLaggedLightingHistoryQueue
            || (static_cast<u8>(deferredLaggedLightingHistoryQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || !laggedLightingHistoryFollowsPresentation
        || (laggedAsyncLightingSchedule && !m_pipeline.m_deferredLightingHistoryReadReadyCompletion.valid())
        || (laggedLightingHistoryWriterWaitPending && !m_pipeline.m_deferredLightingHistoryWriterDrainCompletion.valid())
        || !taskIsCompiled(m_pipeline.m_deferredLightingTask)
        || !laggedLightingHistorySlotsUploadMergedIntoLightingPacket
        || !taskIsCompiled(m_pipeline.m_deferredCompositeTask)
        || !taskIsCompiled(m_pipeline.m_deferredPresentTask)
        || !taskIsCompiled(m_pipeline.m_deferredFrameTimingEndTask)
        || (m_pipeline.m_deferredPresentationOverlayRequired && !taskIsCompiled(m_pipeline.m_deferredPresentationOverlayTask))
        || !presentationEndpoint
        || !presentationEndpoint->valid()
        || presentationEndpoint->producer != m_pipeline.m_deferredFrameTimingEndTask
        || !deferredTaskGraphView.validResource(presentationEndpoint->backBuffer)
        || !taskIsCompiled(m_pipeline.m_deferredFrameRecoveryTask)
        || !deferredLightingQueue
        || !deferredCompositeQueue
        || !deferredPresentQueue
        || (m_pipeline.m_deferredPresentationOverlayRequired && !deferredPresentationOverlayQueue)
        || !terminalPresentationQueue
        || !deferredFrameRecoveryQueue
        // Backbuffer writers need an acquired-image contract on the primary queue.
        || !primaryGraphicsQueue.valid()
        || presentationEndpoint->queue != primaryGraphicsQueue
        || deferredPresentQueue->id != primaryGraphicsQueue
        || (m_pipeline.m_deferredPresentationOverlayRequired && deferredPresentationOverlayQueue->id != primaryGraphicsQueue)
        || terminalPresentationQueue->id != primaryGraphicsQueue
        || (laggedAsyncLightingSchedule && !surfelGiSnapshotCopyAndTimingPacketsAreDistinct)
        || (static_cast<u8>(deferredLightingQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Compute)) == 0u
        || (static_cast<u8>(deferredCompositeQueue->capabilities) & static_cast<u8>(Core::GpuQueueCapability::Compute)) == 0u
        || deferredPresentQueue->queueClass != Core::CommandQueue::Graphics
        || (m_pipeline.m_deferredPresentationOverlayRequired && deferredPresentationOverlayQueue->queueClass != Core::CommandQueue::Graphics)
        || terminalPresentationQueue->queueClass != Core::CommandQueue::Graphics
        || deferredFrameRecoveryQueue->queueClass != Core::CommandQueue::Graphics
    ){
        DeferredFrameDiagnostics{}.logRejectedDeferredGraphTopology(
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueTask).index,
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueFirstWaveletTask).index,
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueResolveTask).index,
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTraceTask).index,
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTemporalMergeTask).index,
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentFirstWaveletTask).index,
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask).index,
            shadowVisibilityPreparedTasksMerged,
            shadowVisibilityAllLitClearMerged,
            shadowVisibilityAdaptivePrimitivesMerged,
            surfelGiOutputClearMergedIntoGiPacket,
            surfelGiPreparedPrefixMergedIntoGiPacket,
            surfelGiInitializationLifecycleMergedIntoPreparationPacket,
            causticPhotonMergedIntoCausticsPacket,
            causticGeometryMergedIntoCausticsPacket,
            static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueTask)),
            static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTraceTask)),
            static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTemporalMergeTask)),
            static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentFirstWaveletTask)),
            static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask)),
            static_cast<u32>(deferredCompiledPlan.packetizationDecisionForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityDiagnosticsTask)),
            deferredCompiledPlan.packetForTask(m_pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityDiagnosticsTask).index,
            m_pipeline.m_deferredLightingTaskGraphDeclared,
            deferredGraphScheduled
        );
        return false;
    }
    outResult.graphicsPrefixTimingTasks = graphicsPrefixTimingTasks;
    outResult.graphicsPrefixUniquePacketCount = graphicsPrefixUniquePacketCount;
    outResult.avboitValidation = avboitValidation;
    outResult.terminalPresentationTask = terminalPresentationTask;
    outResult.surfelGiCounterReadbackQueue = surfelGiCounterReadbackQueue;
    outResult.laggedLightingHistoryQueue = deferredLaggedLightingHistoryQueue;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

