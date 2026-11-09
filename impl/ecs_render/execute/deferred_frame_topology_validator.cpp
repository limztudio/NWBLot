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
#include "graphics_prefix_timing_resolver.h"
#include "opaque_emulation_merge_validator.h"
#include "surfel_caustics_merge_validator.h"


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


void GraphicsPrefixTimingResolver::resolve(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    const Span<Core::GpuTimingSubmissionTicket* const> ownedTickets,
    const Span<Core::GpuTimingSubmissionTicket*> timingTickets,
    const Span<Core::GpuTaskId> timingTasks,
    b8& asyncSpansOnePacket,
    GraphicsPrefixTimingResolutionResult& outResult
)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = GraphicsPrefixTimingResolutionResult{};
    // CSG callbacks keep an independent anchor across FrontierSafe splits.
    const Array<Core::GpuTaskId, s_GraphicsPrefixTimingTaskCount> resolvedTasks = {
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixSceneShadingSetupTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixDeferredClearTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgReceiverSpanTask.valid() ? pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgReceiverSpanTask : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask.valid() ? pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask.valid() ? pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask,
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixTask,
    };
    if(ownedTickets.size() != s_GraphicsPrefixTimingTaskCount || timingTickets.size() != s_GraphicsPrefixTimingTaskCount || timingTasks.size() != s_GraphicsPrefixTimingTaskCount)
        return;
    for(usize taskIndex = 0u; taskIndex < s_GraphicsPrefixTimingTaskCount; ++taskIndex)
        timingTasks[taskIndex] = resolvedTasks[taskIndex];
    for(usize prefixTaskIndex = 0u; prefixTaskIndex < s_GraphicsPrefixTimingTaskCount; ++prefixTaskIndex){
        const Core::GpuTaskId task = timingTasks[prefixTaskIndex];
        if(!compiledPlan.findTask(task).valid() || (prefixTaskIndex != 0u && !compiledPlan.taskPrecedesOrSharesPacket(timingTasks[prefixTaskIndex - 1u], task))){
            outResult.bindingsValid = false;
            break;
        }
        b8 sharesPacketWithEarlierTask = false;
        for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
            if(!compiledPlan.tasksSharePacket(task, timingTasks[earlierTaskIndex]))
                continue;
            timingTickets[prefixTaskIndex] = timingTickets[earlierTaskIndex];
            sharesPacketWithEarlierTask = true;
            break;
        }
        if(!sharesPacketWithEarlierTask){
            timingTickets[prefixTaskIndex] = ownedTickets[prefixTaskIndex];
            ++outResult.uniquePacketCount;
        }
    }
    // Measures are submission-local; skip the scope across a frontier.
    asyncSpansOnePacket = outResult.bindingsValid && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueEmulationMergeValidator::validate(const Core::GpuCompiledGraph::ReadView& compiledPlan, const Span<const Core::GpuTaskId> timingTasks, b8 timingBindingsValid, b8 hasOpaqueCsgFrameWork, OpaqueEmulationMergeValidationResult& outResult)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = OpaqueEmulationMergeValidationResult{};
    const auto taskIsCompiled = [&](const Core::GpuTaskId task)noexcept{
        return compiledPlan.findTask(task).valid();
    };
    const Core::GpuPhysicalQueueInfo* const opaqueComputeEmulationQueue = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask.valid() ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask) : nullptr;
    const Core::GpuPhysicalQueueInfo* const opaqueSharedComputeEmulationQueue =
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount != 0u
        && pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u].valid()
            ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u])
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const opaqueCsgReceiverComputeEmulationQueue =
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
            ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const opaqueCsgIntervalSampleComputeEmulationQueue =
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid()
            ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask)
            : nullptr
    ;
    outResult.packetsAreGraphics = timingBindingsValid;
    for(usize prefixTaskIndex = 0u; outResult.packetsAreGraphics && prefixTaskIndex < timingTasks.size(); ++prefixTaskIndex){
        const Core::GpuPhysicalQueueInfo* const queue = compiledPlan.queueInfoForTask(timingTasks[prefixTaskIndex]);
        outResult.packetsAreGraphics = queue && queue->queueClass == Core::CommandQueue::Graphics;
    }
    // The producer shares G-buffer's packet; its boundary stays packet-local.
    outResult.opaqueComputeEmulationMerged =
        !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask.valid()
        || (
            taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask)
            && opaqueComputeEmulationQueue
            && opaqueComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // Shared outputs need exact packet order; keep the G-buffer prelude first.
    outResult.opaqueSharedComputeEmulationMerged = [&]()noexcept{
        const usize phaseCount = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(!IsSupportedSharedComputeEmulationPhaseCount(phaseCount))
            return false;
        if(
            !opaqueSharedComputeEmulationQueue
            || opaqueSharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || !compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u])
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex];
            const Core::GpuPhysicalQueueInfo* const queue = compiledPlan.queueInfoForTask(task);
            if(!task.valid() || !queue || queue->queueClass != Core::CommandQueue::Graphics || !compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask, task))
                return false;
        }
        for(usize phaseIndex = phaseCount; phaseIndex < pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks.size(); ++phaseIndex){
            if(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        return
            compiledPlan.tasksFormContiguousPacketSequence(Span<const Core::GpuTaskId>(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks.data(), phaseCount))
            && compiledPlan.taskPrecedesInSamePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u])
        ;
    }();
    // Declared producers must share G-buffer's packet for the same handoff.
    outResult.opaqueCsgReceiverComputeEmulationMerged =
        !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
        || (
            taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask)
            && opaqueCsgReceiverComputeEmulationQueue
            && opaqueCsgReceiverComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // The producer/raster pair must stay contiguous in one packet.
    outResult.opaqueCsgIntervalSampleComputeEmulationMerged = [&]()noexcept{
        if(!pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid())
            return true;
        if(
            !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask.valid()
            || !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask.valid()
            || !opaqueCsgIntervalSampleComputeEmulationQueue
            || opaqueCsgIntervalSampleComputeEmulationQueue->queueClass
                != Core::CommandQueue::Graphics
            || !compiledPlan.taskPrecedesOrSharesPacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask)
            || !compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask)
        )
            return false;
        const Array<Core::GpuTaskId, 2> sequence = { pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask, };
        return compiledPlan.tasksFormContiguousPacketSequence(sequence);
    }();
    // Keep the clear with G-buffer's ticket; a split would rebind ownership.
    const Core::GpuPhysicalQueueInfo* const csgIntervalClearQueue = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask.valid() ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask) : nullptr;
    outResult.csgIntervalClearBundleMerged = !hasOpaqueCsgFrameWork
        ? (!pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask.valid() && !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask.valid())
        : (
            pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask.valid()
            && pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask.valid()
            && taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask)
            && taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask)
            && csgIntervalClearQueue
            && csgIntervalClearQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SurfelCausticsMergeValidator::validate(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    Core::GpuTaskId causticsTask,
    Core::GpuTaskId terminalPresentationTask,
    b8 captureLaggedLightingHistory,
    SurfelCausticsMergeValidationResult& outResult
)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = SurfelCausticsMergeValidationResult{};
    outResult.hardwareCausticsQueue = compiledPlan.queueInfoForTask(pipeline.m_causticsGraph.m_deferredHardwareCausticsTask);
    outResult.surfelGiQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiTask);
    outResult.surfelGiPreparationQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask);
    outResult.surfelGiSnapshotCopyQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask);
    outResult.surfelGiCounterReadbackQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask);
    // Keep the clear in GI's packet; a split would escape its endpoint.
    outResult.surfelGiOutputClearMergedIntoGiPacket = pipeline.m_surfelGiGraph.m_deferredSurfelGiIrradianceClearTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiIrradianceClearTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask);
    // Every GI callback must share the semantic packet.
    outResult.surfelGiPreparedPrefixMergedIntoGiPacket =
        (
            !pipeline.m_surfelGiGraph.m_deferredSurfelGiAgeFreeTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiCellHeadClearTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiHashBuildTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiSpawnTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceBuildArgsTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiResolveTask.valid()
        )
        || (
            pipeline.m_surfelGiGraph.m_deferredSurfelGiAgeFreeTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiCellHeadClearTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiHashBuildTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiSpawnTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceBuildArgsTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiResolveTask.valid()
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiAgeFreeTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiCellHeadClearTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiHashBuildTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiSpawnTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceBuildArgsTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiResolveTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
        )
    ;
    // The lifecycle tail must share the first clear's packet.
    outResult.surfelGiInitializationLifecycleMergedIntoPreparationPacket =
        !pipeline.m_surfelGiGraph.m_deferredSurfelGiInitializationLifecycleTask.valid()
        || (pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiInitializationLifecycleTask))
    ;
    // Keep the clear with the producer; caustic callbacks stay one submission.
    outResult.causticPhotonMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticPhotonTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticPhotonTask, causticsTask);
    outResult.causticGeometryMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticGeometryTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticGeometryTask, causticsTask);
    outResult.causticResolvePrepareMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticResolvePrepareTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticResolvePrepareTask, causticsTask);
    outResult.causticResolveWaveletsMergedIntoCausticsPacket = true;
    for(const Core::GpuTaskId waveletTask : pipeline.m_causticsGraph.m_deferredCausticResolveWaveletTasks){
        if(!waveletTask.valid() || !compiledPlan.tasksSharePacket(waveletTask, causticsTask)){
            outResult.causticResolveWaveletsMergedIntoCausticsPacket = false;
            break;
        }
    }
    outResult.causticResolveUpsampleMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticResolveUpsampleTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticResolveUpsampleTask, causticsTask);
    outResult.causticIrradianceClearMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticIrradianceClearTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticIrradianceClearTask, causticsTask);
    // Keep the bootstrap clear in the producer packet; no hidden writers.
    outResult.causticAccumulatorBootstrapClearMergedIntoCausticsPacket = !pipeline.m_causticsGraph.m_deferredCausticAccumulatorBootstrapClearTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticAccumulatorBootstrapClearTask, causticsTask);
    // Keep decay in the packet; a split would break the UAV dependency.
    outResult.causticAccumulatorDecayMergedIntoCausticsPacket = !pipeline.m_causticsGraph.m_deferredCausticAccumulatorDecayTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticAccumulatorDecayTask, causticsTask);
    outResult.surfelGiSnapshotCopyAndTimingPacketsAreDistinct = !pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask.valid() || !compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask);
    outResult.surfelCounterReadbackFollowsPresentation = !pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask.valid()
        || (compiledPlan.taskPrecedesOrSharesPacket(terminalPresentationTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask) && !compiledPlan.tasksSharePacket(terminalPresentationTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask))
    ;
    outResult.laggedLightingHistoryFollowsPresentation = !captureLaggedLightingHistory
        || (compiledPlan.taskPrecedesOrSharesPacket(terminalPresentationTask, pipeline.m_deferredLaggedLightingHistoryTask) && !compiledPlan.tasksSharePacket(terminalPresentationTask, pipeline.m_deferredLaggedLightingHistoryTask))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
