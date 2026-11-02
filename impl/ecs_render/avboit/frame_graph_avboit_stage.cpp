// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_avboit_stage.h"

#include <impl/ecs_render/avboit/accumulation_record_builder.h>
#include <impl/ecs_render/avboit/avboit_pass_upload_helper.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/avboit/clear_chain_builder.h>
#include <impl/ecs_render/avboit/compute_effect_chain_builder.h>
#include <impl/ecs_render/avboit/extinction_record_builder.h>
#include <impl/ecs_render/avboit/generated_geometry_reuse.h>
#include <impl/ecs_render/avboit/occupancy_record_builder.h>
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/avboit/task_graph_refraction_capture.h>
#include <impl/ecs_render/csg/frame_graph_transparent_csg_tasks.h>
#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>
#include <impl/ecs_render/csg/transparent_csg_interval_builder.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> FrameGraphAvboitStage::declare(Core::Alloc::ScratchArena& scratchArena, const FrameGraphAvboitStageInputs& inputs, const FrameGraphAvboitStageTiming& timing, ObjectGeometryCacheGraph& objectGeometry){
    using namespace RendererTaskGraphDetail;

    AvboitPreGraphTask::Payload avboitPrePayload{ m_arena, m_avboitSystem, inputs.targets, timing.preTimingTicket, timing.transparentCsgIntervalsTiming, };
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload avboitCsgReceiverSpanPayload{ m_arena, m_materialSystem, m_csgSystem, inputs.targets, timing.preTimingTicket, timing.transparentCsgIntervalsTiming, };
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload avboitCsgIntervalCombinePayload{ m_arena, m_materialSystem, m_csgSystem, inputs.targets, timing.preTimingTicket, timing.transparentCsgIntervalsTiming, };
    avboitPrePayload.hasTransparentRenderers = inputs.hasTransparentRenderers;
    avboitPrePayload.frameBindings = inputs.frameBindings;
    avboitPrePayload.csgResources = inputs.csgResources;
    avboitCsgReceiverSpanPayload.frameBindings = inputs.frameBindings;
    avboitCsgReceiverSpanPayload.csgResources = inputs.csgResources;
    avboitCsgIntervalCombinePayload.frameBindings = inputs.frameBindings;
    avboitCsgIntervalCombinePayload.csgResources = inputs.csgResources;

    // Freeze the transparent CSG interval producer before AVBOIT recording; snapshot covers receiver work only.
    TransparentCsgIntervalBuilder transparentCsgIntervalBuilder(m_graph, m_materialSystem, m_csgSystem, m_avboitSystem);
    const auto transparentCsgIntervalResult = transparentCsgIntervalBuilder.declare(
        scratchArena,
        TransparentCsgIntervalProducerInputs{
            .targets = inputs.targets,
            .csgFrameState = inputs.csgFrameState,
            .frameBindings = inputs.frameBindings,
            .csgResources = inputs.csgResources,
            .meshViewState = inputs.meshViewState,
            .materialInstances = inputs.resources.materialInstances,
            .materialTyped = inputs.resources.materialTyped,
            .csgReceiverRanges = inputs.resources.csgReceiverRanges,
            .csgCutters = inputs.resources.csgCutters,
            .csgClipContextSlots = inputs.resources.csgClipContextSlots,
            .csgIntervalSampleState = inputs.resources.csgIntervalSampleState,
            .csgIntervalId = inputs.resources.csgIntervalId,
            .csgReceiverEventCount = inputs.resources.csgReceiverEventCount,
            .csgPeelSubresources = inputs.resources.csgPeelSubresources,
            .csgReceiverEventCountSubresources = inputs.resources.csgReceiverEventCountSubresources,
            .prefixTask = inputs.prefixTask,
            .hasTransparentRenderers = inputs.hasTransparentRenderers,
        },
        avboitPrePayload,
        avboitCsgReceiverSpanPayload,
        avboitCsgIntervalCombinePayload,
        timing.transparentCsgIntervalClearTimingState
    );
    if(!transparentCsgIntervalResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval producer"));
        return MakeUnexpected(Failure{});
    }
    FrameGraphTransparentCsgTasks transparentCsgTasks(m_graph, m_avboitSystem);
    const auto transparentCsgTaskResult = transparentCsgTasks.declare(
        scratchArena,
        FrameGraphTransparentCsgTaskInputs{
            .albedo = inputs.resources.albedo,
            .normal = inputs.resources.normal,
            .worldPosition = inputs.resources.worldPosition,
            .specularRoughness = inputs.resources.specularRoughness,
            .depth = inputs.resources.depth,
            .meshView = inputs.resources.meshView,
            .materialInstances = inputs.resources.materialInstances,
            .materialTyped = inputs.resources.materialTyped,
            .csgReceiverRanges = inputs.resources.csgReceiverRanges,
            .csgCutters = inputs.resources.csgCutters,
            .csgClipContextSlots = inputs.resources.csgClipContextSlots,
            .csgIntervalSampleState = inputs.resources.csgIntervalSampleState,
            .csgCapBackNormal = inputs.resources.csgCapBackNormal,
            .csgIntervalDepth = inputs.resources.csgIntervalDepth,
            .csgIntervalId = inputs.resources.csgIntervalId,
            .csgReceiverEventData = inputs.resources.csgReceiverEventData,
            .csgReceiverEventCount = inputs.resources.csgReceiverEventCount,
            .csgReceiverSpanData = inputs.resources.csgReceiverSpanData,
            .csgReceiverSpanCount = inputs.resources.csgReceiverSpanCount,
            .csgRemovedIntervalDepth = inputs.resources.csgRemovedIntervalDepth,
            .csgRemovedIntervalCapNormal = inputs.resources.csgRemovedIntervalCapNormal,
            .csgRemovedIntervalData = inputs.resources.csgRemovedIntervalData,
            .csgRemovedIntervalCount = inputs.resources.csgRemovedIntervalCount,
            .currentBindlessSlots = inputs.resources.currentBindlessSlots,
            .avboitMaterialDomain = inputs.resources.avboitMaterialDomain,
            .avboitCsgDomain = inputs.resources.avboitCsgDomain,
            .csgPeelSubresources = inputs.resources.csgPeelSubresources,
            .csgReceiverEventDataSubresources = inputs.resources.csgReceiverEventDataSubresources,
            .csgReceiverEventCountSubresources = inputs.resources.csgReceiverEventCountSubresources,
            .csgReceiverSpanDataSubresources = inputs.resources.csgReceiverSpanDataSubresources,
            .csgReceiverSpanCountSubresources = inputs.resources.csgReceiverSpanCountSubresources,
            .csgRemovedIntervalSubresources = inputs.resources.csgRemovedIntervalSubresources,
            .csgRemovedIntervalCountSubresources = inputs.resources.csgRemovedIntervalCountSubresources,
            .timingTicket = &timing.preTimingTicket,
            .transparentCsgUploadTask = transparentCsgIntervalResult->uploadTask,
            .transparentCsgMaterialGeometrySet = transparentCsgIntervalResult->materialGeometrySet,
            .transparentCsgMaterialSampledTextureSet = transparentCsgIntervalResult->materialSampledTextureSet,
        },
        objectGeometry,
        avboitPrePayload,
        avboitCsgReceiverSpanPayload,
        avboitCsgIntervalCombinePayload
    );
    if(!transparentCsgTaskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph tasks"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuTaskId avboitIntervalCompletionTask = transparentCsgTaskResult->intervalCompletionTask;
    const b8 avboitIntervalOutputsGraphOwned = transparentCsgTaskResult->intervalOutputsGraphOwned;
    AvboitOccupancyGraphTask::Payload avboitOccupancyPayload{ m_arena, m_avboitSystem, inputs.targets, timing.preTimingTicket };
    AvboitOccupancyComputeEmulationGraphTask::Payload avboitOccupancyComputeEmulationPayload{ m_arena, m_graphics, m_materialSystem, inputs.targets, timing.preTimingTicket, timing.occupancyComputeEmulationTiming };
    avboitOccupancyPayload.frameBindings = inputs.frameBindings;
    avboitOccupancyComputeEmulationPayload.frameBindings = inputs.frameBindings;
    avboitOccupancyPayload.hasTransparentRenderers = inputs.hasTransparentRenderers;
    avboitOccupancyPayload.csgResources = inputs.csgResources;
    avboitOccupancyComputeEmulationPayload.csgResources = inputs.csgResources;

    AvboitGeneratedGeometryReuse generatedGeometry(scratchArena);
    const auto refractionCaptureTaskResult = DeclareAvboitRefractionCapture(
        scratchArena,
        m_graph, m_arena, m_materialSystem, m_csgSystem,
        inputs.csgFrameState, inputs.csgResources, inputs.frameBindings, inputs.meshViewState, avboitIntervalCompletionTask,
        inputs.refractionCaptureEnabled,
        inputs.targets, objectGeometry, generatedGeometry
    );
    if(!refractionCaptureTaskResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId refractionCaptureTask = *refractionCaptureTaskResult;
    FrameGraphAvboitPassUploadChain occupancyUploadChain(m_graph, m_materialSystem, m_csgSystem);
    const auto occupancyUploadChainResult = occupancyUploadChain.declare(
        scratchArena,
        FrameGraphAvboitPassPhaseDesc{
            .framebuffer = *inputs.targets.avboit.lowFramebuffer,
            .pass = MaterialPipelinePass::AvboitOccupancy,
            .phase = AvboitGeometryPhase::Occupancy,
        },
        FrameGraphAvboitPassUploadInputs{
            .targets = inputs.targets,
            .csgFrameState = inputs.csgFrameState,
            .frameBindings = inputs.frameBindings,
            .csgResources = inputs.csgResources,
            .meshViewState = inputs.meshViewState,
            .materialInstances = inputs.resources.materialInstances,
            .materialTyped = inputs.resources.materialTyped,
            .csgReceiverRanges = inputs.resources.csgReceiverRanges,
            .csgCutters = inputs.resources.csgCutters,
            .csgClipContextSlots = inputs.resources.csgClipContextSlots,
            .csgIntervalSampleState = Core::GpuGraphResourceId{},
            .uploadTask = refractionCaptureTask,
            .intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned,
            .hasTransparentRenderers = inputs.hasTransparentRenderers,
        },
        avboitOccupancyPayload,
        avboitOccupancyComputeEmulationPayload,
        generatedGeometry
    );
    if(!occupancyUploadChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy upload chain"));
        return MakeUnexpected(Failure{});
    }

    AvboitClearChainBuilder avboitClearChainBuilder(m_graph, m_avboitSystem);
    const auto avboitClearChainResult = avboitClearChainBuilder.declare(
        AvboitClearChainInputs{
            .lowRaster = inputs.resources.avboitLowRaster,
            .accumColor = inputs.resources.avboitAccumColor,
            .accumExtinction = inputs.resources.avboitAccumExtinction,
            .foregroundColor = inputs.resources.avboitForegroundColor,
            .foregroundExtinction = inputs.resources.avboitForegroundExtinction,
            .transmittance = inputs.resources.avboitTransmittance,
            .coverage = inputs.resources.avboitCoverage,
            .depthWarp = inputs.resources.avboitDepthWarp,
            .control = inputs.resources.avboitControl,
            .extinction = inputs.resources.avboitExtinction,
            .extinctionOverflow = inputs.resources.avboitExtinctionOverflow,
            .uploadTask = occupancyUploadChainResult->uploadTask,
            .clearTargets = inputs.clearTargets,
        },
        timing.clearTimingState
    );
    if(!avboitClearChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT clear chain"));
        return MakeUnexpected(Failure{});
    }

    AvboitOccupancyRecordInputs avboitOccupancyRecordInputs{ inputs.targets, timing.preTimingTicket, timing.occupancyComputeEmulationTiming };
    avboitOccupancyRecordInputs.applyResources(inputs.resources, avboitIntervalOutputsGraphOwned);
    avboitOccupancyRecordInputs.applyUploadResult(*occupancyUploadChainResult);
    avboitOccupancyRecordInputs.avboitLowRaster = inputs.resources.avboitLowRaster;
    avboitOccupancyRecordInputs.avboitCoverage = inputs.resources.avboitCoverage;
    avboitOccupancyRecordInputs.clearTask = avboitClearChainResult->clearTask;
    AvboitOccupancyRecordBuilder avboitOccupancyRecordBuilder(m_graph, m_graphics, m_materialSystem, m_avboitSystem);
    if(!avboitOccupancyRecordBuilder.declare(scratchArena, inputs.frameBindings, objectGeometry, avboitOccupancyRecordInputs, avboitOccupancyPayload, avboitOccupancyComputeEmulationPayload)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
        return MakeUnexpected(Failure{});
    }
    if(occupancyUploadChainResult->producesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return MakeUnexpected(Failure{});
    }

    AvboitComputeEffectChainBuilder avboitComputeEffectChainBuilder(m_graph, m_avboitSystem);
    const auto avboitDepthWarpStageResult = avboitComputeEffectChainBuilder.declareDepthWarp(
        AvboitDepthWarpStageInputs{
            .targets = inputs.targets.avboit,
            .coverage = inputs.resources.avboitCoverage,
            .depthWarp = inputs.resources.avboitDepthWarp,
            .control = inputs.resources.avboitControl,
            .currentBindlessSlots = inputs.resources.currentBindlessSlots,
            .occupancyTask = m_avboitSystem.taskGraphStage().m_occupancyTask,
            .depthWarpTimingTicket = timing.depthWarpTimingTicket,
            .timingFeedback = m_timingFeedback,
            .hasTransparentRenderers = inputs.hasTransparentRenderers,
        }
    );
    if(!avboitDepthWarpStageResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT inputs.resources.depth-warp graph task"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuTaskId avboitDepthWarpCompletionTask = avboitDepthWarpStageResult->completionTask;

    if(inputs.hasTransparentRenderers){
        if(!declareExtinction(scratchArena, inputs, timing, avboitDepthWarpCompletionTask, avboitIntervalOutputsGraphOwned, objectGeometry, generatedGeometry, avboitComputeEffectChainBuilder))
            return MakeUnexpected(Failure{});
        if(!declareAccumulation(scratchArena, inputs, timing, avboitIntervalOutputsGraphOwned, objectGeometry, generatedGeometry))
            return MakeUnexpected(Failure{});
        return m_avboitSystem.taskGraphStage().m_accumulationFinalizeTask;
    }
    return m_avboitSystem.taskGraphStage().m_occupancyTask;
}


b8 FrameGraphAvboitStage::declareExtinction(
    Core::Alloc::ScratchArena& scratchArena,
    const FrameGraphAvboitStageInputs& inputs,
    const FrameGraphAvboitStageTiming& timing,
    const Core::GpuTaskId avboitDepthWarpCompletionTask,
    const b8 avboitIntervalOutputsGraphOwned,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitGeneratedGeometryReuse& generatedGeometry,
    AvboitComputeEffectChainBuilder& avboitComputeEffectChainBuilder
){
    using namespace RendererTaskGraphDetail;

    AvboitExtinctionGraphTask::Payload avboitExtinctionPayload{ m_arena, m_avboitSystem, inputs.targets, timing.extinctionTimingTicket };
    AvboitExtinctionComputeEmulationGraphTask::Payload avboitExtinctionComputeEmulationPayload{ m_arena, m_graphics, m_materialSystem, inputs.targets, timing.extinctionTimingTicket, timing.extinctionComputeEmulationTiming };
    avboitExtinctionPayload.frameBindings = inputs.frameBindings;
    avboitExtinctionComputeEmulationPayload.frameBindings = inputs.frameBindings;
    avboitExtinctionPayload.hasTransparentRenderers = inputs.hasTransparentRenderers;
    avboitExtinctionPayload.csgResources = inputs.csgResources;
    avboitExtinctionComputeEmulationPayload.csgResources = inputs.csgResources;

    FrameGraphAvboitPassUploadChain extinctionUploadChain(m_graph, m_materialSystem, m_csgSystem);
    const auto extinctionUploadChainResult = extinctionUploadChain.declare(
        scratchArena,
        FrameGraphAvboitPassPhaseDesc{
            .framebuffer = *inputs.targets.avboit.lowFramebuffer,
            .pass = MaterialPipelinePass::AvboitExtinction,
            .phase = AvboitGeometryPhase::Extinction,
        },
        FrameGraphAvboitPassUploadInputs{
            .targets = inputs.targets,
            .csgFrameState = inputs.csgFrameState,
            .frameBindings = inputs.frameBindings,
            .csgResources = inputs.csgResources,
            .meshViewState = inputs.meshViewState,
            .materialInstances = inputs.resources.materialInstances,
            .materialTyped = inputs.resources.materialTyped,
            .csgReceiverRanges = inputs.resources.csgReceiverRanges,
            .csgCutters = inputs.resources.csgCutters,
            .csgClipContextSlots = inputs.resources.csgClipContextSlots,
            .csgIntervalSampleState = inputs.resources.csgIntervalSampleState,
            .uploadTask = avboitDepthWarpCompletionTask,
            .intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned,
            .hasTransparentRenderers = inputs.hasTransparentRenderers,
        },
        avboitExtinctionPayload,
        avboitExtinctionComputeEmulationPayload,
        generatedGeometry
    );
    if(!extinctionUploadChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction upload chain"));
        return false;
    }

    AvboitExtinctionRecordInputs avboitExtinctionRecordInputs{ inputs.targets, timing.extinctionTimingTicket, timing.extinctionComputeEmulationTiming };
    avboitExtinctionRecordInputs.applyResources(inputs.resources, avboitIntervalOutputsGraphOwned);
    avboitExtinctionRecordInputs.applyUploadResult(*extinctionUploadChainResult);
    avboitExtinctionRecordInputs.avboitLowRaster = inputs.resources.avboitLowRaster;
    avboitExtinctionRecordInputs.avboitDepthWarp = inputs.resources.avboitDepthWarp;
    avboitExtinctionRecordInputs.avboitExtinction = inputs.resources.avboitExtinction;
    avboitExtinctionRecordInputs.avboitControl = inputs.resources.avboitControl;
    avboitExtinctionRecordInputs.avboitExtinctionOverflow = inputs.resources.avboitExtinctionOverflow;
    avboitExtinctionRecordInputs.depthWarpCompletionTask = avboitDepthWarpCompletionTask;
    AvboitExtinctionRecordBuilder avboitExtinctionRecordBuilder(m_graph, m_graphics, m_materialSystem, m_avboitSystem);
    if(!avboitExtinctionRecordBuilder.declare(scratchArena, inputs.frameBindings, objectGeometry, avboitExtinctionRecordInputs, avboitExtinctionPayload, avboitExtinctionComputeEmulationPayload)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
        return false;
    }
    if(extinctionUploadChainResult->producesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return false;
    }

    if(!avboitComputeEffectChainBuilder.declareIntegration(
        AvboitIntegrationStageInputs{
            .targets = inputs.targets.avboit,
            .extinction = inputs.resources.avboitExtinction,
            .control = inputs.resources.avboitControl,
            .extinctionOverflow = inputs.resources.avboitExtinctionOverflow,
            .transmittance = inputs.resources.avboitTransmittance,
            .currentBindlessSlots = inputs.resources.currentBindlessSlots,
            .extinctionTask = m_avboitSystem.taskGraphStage().m_extinctionTask,
            .integrationTimingTicket = timing.integrationTimingTicket,
            .timingFeedback = m_timingFeedback,
        }
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return false;
    }
    return true;
}

b8 FrameGraphAvboitStage::declareAccumulation(
    Core::Alloc::ScratchArena& scratchArena,
    const FrameGraphAvboitStageInputs& inputs,
    const FrameGraphAvboitStageTiming& timing,
    const b8 avboitIntervalOutputsGraphOwned,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitGeneratedGeometryReuse& generatedGeometry
){
    using namespace RendererTaskGraphDetail;

    AvboitAccumulationGraphTask::Payload avboitAccumulationPayload{ m_arena, m_avboitSystem, inputs.targets, timing.accumulationTimingTicket };
    AvboitAccumulationComputeEmulationGraphTask::Payload avboitAccumulationComputeEmulationPayload{ m_arena, m_graphics, m_materialSystem, inputs.targets, timing.accumulationTimingTicket, timing.accumulationComputeEmulationTiming };
    avboitAccumulationPayload.frameBindings = inputs.frameBindings;
    avboitAccumulationComputeEmulationPayload.frameBindings = inputs.frameBindings;
    avboitAccumulationPayload.hasTransparentRenderers = inputs.hasTransparentRenderers;
    avboitAccumulationPayload.csgResources = inputs.csgResources;
    avboitAccumulationComputeEmulationPayload.csgResources = inputs.csgResources;

    FrameGraphAvboitPassUploadChain accumulationUploadChain(m_graph, m_materialSystem, m_csgSystem);
    const auto accumulationUploadChainResult = accumulationUploadChain.declare(
        scratchArena,
        FrameGraphAvboitPassPhaseDesc{
            .framebuffer = *inputs.targets.avboit.accumulationFramebuffer,
            .pass = MaterialPipelinePass::AvboitAccumulate,
            .phase = AvboitGeometryPhase::Accumulation,
        },
        FrameGraphAvboitPassUploadInputs{
            .targets = inputs.targets,
            .csgFrameState = inputs.csgFrameState,
            .frameBindings = inputs.frameBindings,
            .csgResources = inputs.csgResources,
            .meshViewState = inputs.meshViewState,
            .materialInstances = inputs.resources.materialInstances,
            .materialTyped = inputs.resources.materialTyped,
            .csgReceiverRanges = inputs.resources.csgReceiverRanges,
            .csgCutters = inputs.resources.csgCutters,
            .csgClipContextSlots = inputs.resources.csgClipContextSlots,
            .csgIntervalSampleState = inputs.resources.csgIntervalSampleState,
            .uploadTask = m_avboitSystem.taskGraphStage().m_integrationTask,
            .intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned,
            .hasTransparentRenderers = true,
        },
        avboitAccumulationPayload,
        avboitAccumulationComputeEmulationPayload,
        generatedGeometry
    );
    if(!accumulationUploadChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation upload chain"));
        return false;
    }

    AvboitAccumulationRecordInputs avboitAccumulationRecordInputs{ inputs.targets, timing.accumulationTimingTicket, timing.accumulationComputeEmulationTiming };
    avboitAccumulationRecordInputs.applyResources(inputs.resources, avboitIntervalOutputsGraphOwned);
    avboitAccumulationRecordInputs.applyUploadResult(*accumulationUploadChainResult);
    avboitAccumulationRecordInputs.refractionDepth = inputs.resources.refractionDepth;
    avboitAccumulationRecordInputs.avboitDepthWarp = inputs.resources.avboitDepthWarp;
    avboitAccumulationRecordInputs.avboitTransmittance = inputs.resources.avboitTransmittance;
    avboitAccumulationRecordInputs.avboitControl = inputs.resources.avboitControl;
    avboitAccumulationRecordInputs.avboitForegroundColor = inputs.resources.avboitForegroundColor;
    avboitAccumulationRecordInputs.avboitForegroundExtinction = inputs.resources.avboitForegroundExtinction;
    avboitAccumulationRecordInputs.avboitAccumColor = inputs.resources.avboitAccumColor;
    avboitAccumulationRecordInputs.avboitAccumExtinction = inputs.resources.avboitAccumExtinction;
    avboitAccumulationRecordInputs.integrationTask = m_avboitSystem.taskGraphStage().m_integrationTask;
    AvboitAccumulationRecordBuilder avboitAccumulationRecordBuilder(m_graph, m_graphics, m_materialSystem, m_avboitSystem);
    if(!avboitAccumulationRecordBuilder.declare(scratchArena, inputs.frameBindings, objectGeometry, avboitAccumulationRecordInputs, avboitAccumulationPayload, avboitAccumulationComputeEmulationPayload)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
        return false;
    }
    if(accumulationUploadChainResult->producesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

