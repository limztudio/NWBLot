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
#include <impl/ecs_render/avboit/phase_identities.adhd.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <core/graphics/backend_selection/backend.h>
#include <impl/ecs_render/avboit/compute_effect_record_inputs.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_timing_feedback.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include "refraction_capture_resources.h"
#include <impl/ecs_render/avboit/types.adhd.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/refraction/types.adhd.h>
#include "refraction_cap_builder.h"
#include <impl/ecs_render/avboit/avboit_frame_targets.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <global/basic_string.h>


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
        AvboitClearChainInputs(
            inputs.resources.avboitLowRaster,
            inputs.resources.avboitAccumColor,
            inputs.resources.avboitAccumExtinction,
            inputs.resources.avboitForegroundColor,
            inputs.resources.avboitForegroundExtinction,
            inputs.resources.avboitTransmittance,
            inputs.resources.avboitCoverage,
            inputs.resources.avboitDepthWarp,
            inputs.resources.avboitControl,
            inputs.resources.avboitExtinction,
            inputs.resources.avboitExtinctionOverflow,
            occupancyUploadChainResult->uploadTask,
            inputs.clearTargets
        ),
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
        });
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
        })){
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


Expected<AvboitClearChainResult> AvboitClearChainBuilder::declare(const AvboitClearChainInputs& inputs, GraphClearTimingRecordState& clearTimingState){
    AvboitClearChainResult result{};
    if(!inputs.uploadTask.valid())
        return MakeUnexpected(Failure{});

    // Keep native order: uploads, serial target clears, then occupancy; graph owns all CopyDest ops.
    Core::GpuTaskId avboitClearTask = inputs.uploadTask;
    if(inputs.clearTargets){
        if(
            !inputs.lowRaster.valid()
            || !inputs.accumColor.valid()
            || !inputs.accumExtinction.valid()
            || !inputs.foregroundColor.valid()
            || !inputs.foregroundExtinction.valid()
            || !inputs.transmittance.valid()
            || !inputs.coverage.valid()
            || !inputs.depthWarp.valid()
            || !inputs.control.valid()
            || !inputs.extinction.valid()
            || !inputs.extinctionOverflow.valid()
        )
            return MakeUnexpected(Failure{});

        Core::GpuTaskSchedulingHint avboitClearScheduling;
        avboitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        avboitClearScheduling.forceSubmissionBoundary = false;
        avboitClearScheduling.allowPacketMerge = true;
        avboitClearScheduling.mergeWithPrevious = true;
        // Clear chain and Occupancy share one AVBOIT Pre timing packet across consumer frontiers.
        avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto makeAvboitClearTaskDesc = [&avboitClearScheduling](const Name identity, const AStringView markerLabel, const Core::GpuTaskId& dependency)noexcept{
            Core::GpuTaskDesc clearDesc;
            clearDesc.setIdentity(identity).setMarkerLabel(markerLabel).setScheduling(avboitClearScheduling).setDependencies({ &dependency, 1u });
            return clearDesc;
        };
        const auto makeAvboitFloatClearDesc = [](const Core::GpuGraphResourceId destination, const Core::Color& value, const Core::GpuClearTextureTaskRecordHooks& recordHooks = {})noexcept{
            Core::GpuClearTextureTaskDesc clearDesc;
            clearDesc.destination = destination;
            clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
            clearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
            clearDesc.floatValue = value;
            clearDesc.recordHooks = recordHooks;
            return clearDesc;
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearBeginHooks{ .context = &clearTimingState, .beforeClear = &BeginGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
        const Core::GpuClearTextureTaskRecordHooks avboitClearEndHooks{ .context = &clearTimingState, .afterClear = &EndGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
        const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
        const auto avboitClearTaskResult = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(Name(s_AvboitClearLowRasterIdentity), "AVBOIT Clear Low Raster", inputs.uploadTask),
            makeAvboitFloatClearDesc(inputs.lowRaster, transparentBlack, avboitClearBeginHooks)
        );
        if(!avboitClearTaskResult){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult;
        m_avboitSystem.taskGraphStage().m_clearFirstTask = avboitClearTask;
        const auto avboitClearTaskResult2 = m_graph.addClearTextureTask(makeAvboitClearTaskDesc(Name(s_AvboitClearAccumColorIdentity), "AVBOIT Clear Accumulation Color", avboitClearTask), makeAvboitFloatClearDesc(inputs.accumColor, transparentBlack));
        if(!avboitClearTaskResult2){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult2;
        const auto avboitClearTaskResult3 = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(Name(s_AvboitClearAccumExtinctionIdentity), "AVBOIT Clear Accumulation Extinction", avboitClearTask),
            makeAvboitFloatClearDesc(inputs.accumExtinction, transparentBlack)
        );
        if(!avboitClearTaskResult3){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult3;
        const Array<Core::GpuGraphResourceId, 2u> foregroundClearTargets = { inputs.foregroundColor, inputs.foregroundExtinction };
        for(const auto target : foregroundClearTargets){
            const auto avboitClearTaskResult4 = m_graph.addClearTextureTask(
                makeAvboitClearTaskDesc(target == inputs.foregroundColor ? Name(s_AvboitClearForegroundColorIdentity) : Name(s_AvboitClearForegroundExtinctionIdentity), "AVBOIT Clear Foreground", avboitClearTask),
                makeAvboitFloatClearDesc(target, transparentBlack)
            );
            if(!avboitClearTaskResult4){
                avboitClearTask = {};
                return MakeUnexpected(Failure{});
            }
            avboitClearTask = *avboitClearTaskResult4;
        }
        const auto appendAvboitBufferClear = [&](const Name identity, const AStringView markerLabel, const Core::GpuGraphResourceId destination, const u32 value){
            const auto producedTask = m_graph.addClearBufferTask(makeAvboitClearTaskDesc(identity, markerLabel, avboitClearTask), Core::GpuClearBufferTaskDesc{ .destination = destination, .clearValue = value, });
            if(!producedTask){
                avboitClearTask = {};
                return false;
            }
            avboitClearTask = *producedTask;
            return true;
        };
        if(
            !appendAvboitBufferClear(Name(s_AvboitClearCoverageIdentity), "AVBOIT Clear Coverage", inputs.coverage, 0u)
            || !appendAvboitBufferClear(Name(s_AvboitClearDepthWarpIdentity), "AVBOIT Clear Depth Warp", inputs.depthWarp, 0u)
            || !appendAvboitBufferClear(Name(s_AvboitClearControlIdentity), "AVBOIT Clear Control", inputs.control, 0u)
            || !appendAvboitBufferClear(Name(s_AvboitClearExtinctionIdentity), "AVBOIT Clear Extinction", inputs.extinction, 0u)
            || !appendAvboitBufferClear(Name(s_AvboitClearExtinctionOverflowIdentity), "AVBOIT Clear Extinction Overflow", inputs.extinctionOverflow, NWB_AVBOIT_OVERFLOW_INVALID)
        )
            return MakeUnexpected(Failure{});
        const auto avboitClearTaskResult5 = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(Name(s_AvboitClearTransmittanceIdentity), "AVBOIT Clear Transmittance", avboitClearTask),
            makeAvboitFloatClearDesc(inputs.transmittance, Core::Color(1.f, 1.f, 1.f, 1.f), avboitClearEndHooks)
        );
        if(!avboitClearTaskResult5){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult5;
        m_avboitSystem.taskGraphStage().m_clearTask = avboitClearTask;
    }
    result.clearTask = avboitClearTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_compute_effect_chain_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::GpuTaskSchedulingHint ComputeEffectScheduling()noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    // Keep each effect in one recording packet while the compiler chooses its queue.
    RendererTaskGraphDetail::EnableSameFamilyComputeEffectRouting(scheduling);
    RendererTaskGraphDetail::EnableCrossFamilyComputeEffectRouting(scheduling);
    // Accepted samples use the queue class and exact transport chosen by this compile.
    scheduling.allowTimingFeedbackRouting = true;
    scheduling.allowCrossClassTimingFeedbackRouting = true;
    return scheduling;
}

[[nodiscard]] static Core::GpuTaskTimingMetadata AvboitComputeStageTimingMetadata(const AvboitFrameTargets& targets)noexcept{
    return Core::GpuTaskTimingMetadata{ .resolutionClass = ResolveAvboitTimingResolutionClass(targets.lowWidth, targets.lowHeight) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AvboitDepthWarpStageResult> AvboitComputeEffectChainBuilder::declareDepthWarp(const AvboitDepthWarpStageInputs& inputs){
    using namespace RendererTaskGraphDetail;
    AvboitDepthWarpStageResult result{};

    if(!inputs.occupancyTask.valid())
        return MakeUnexpected(Failure{});

    const Core::GpuTaskSchedulingHint avboitComputeScheduling = __hidden_avboit_compute_effect_chain_builder::ComputeEffectScheduling();
    const Core::GpuTaskTimingMetadata avboitComputeStageTiming = __hidden_avboit_compute_effect_chain_builder::AvboitComputeStageTimingMetadata(inputs.targets);
    Core::GpuTaskId completionTask = inputs.occupancyTask;
    if(inputs.hasTransparentRenderers){
        if(!inputs.coverage.valid() || !inputs.depthWarp.valid() || !inputs.control.valid())
            return MakeUnexpected(Failure{});
        if(!inputs.currentBindlessSlots.valid())
            return MakeUnexpected(Failure{});
        const Array<Core::GpuTaskResourceUse, 4> depthWarpResourceUses = {
            ReadUse(inputs.coverage, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(inputs.depthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(inputs.control, Core::ResourceStates::UnorderedAccess),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Array<Core::GpuTaskId, 1> preDependency = { inputs.occupancyTask };
        Core::GpuTaskDesc depthWarpDesc;
        depthWarpDesc
            .setIdentity(Name(RendererGpuPassText::s_AvboitDepthWarpName))
            .setMarkerLabel(RendererGpuPassText::s_AvboitDepthWarpLabel)
            .setScheduling(avboitComputeScheduling)
            .setTiming(avboitComputeStageTiming)
            .setDependencies(preDependency)
            .setResourceUses(depthWarpResourceUses)
        ;
        const auto depthWarpTaskResult = m_graph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{ .avboitSystem = m_avboitSystem, .targets = inputs.targets, .timingTicket = inputs.depthWarpTimingTicket, .timingFeedback = &inputs.timingFeedback, .timingScope = &RendererGpuTimingScope::s_AvboitDepthWarp, });
        if(!depthWarpTaskResult){
            m_avboitSystem.taskGraphStage().m_depthWarpTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT depth-warp graph task"));
            return MakeUnexpected(Failure{});
        }
        m_avboitSystem.taskGraphStage().m_depthWarpTask = *depthWarpTaskResult;
        completionTask = m_avboitSystem.taskGraphStage().m_depthWarpTask;
    }

    result.completionTask = completionTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitComputeEffectChainBuilder::declareIntegration(const AvboitIntegrationStageInputs& inputs){
    using namespace RendererTaskGraphDetail;

    if(!inputs.extinctionTask.valid())
        return false;
    if(!inputs.extinction.valid() || !inputs.control.valid() || !inputs.extinctionOverflow.valid())
        return false;
    if(!inputs.transmittance.valid() || !inputs.currentBindlessSlots.valid())
        return false;

    const Core::GpuTaskSchedulingHint avboitComputeScheduling = __hidden_avboit_compute_effect_chain_builder::ComputeEffectScheduling();
    const Core::GpuTaskTimingMetadata avboitComputeStageTiming = __hidden_avboit_compute_effect_chain_builder::AvboitComputeStageTimingMetadata(inputs.targets);
    // Integration retains packet affinity; the compiler lowers its queue and resource states.
    const Array<Core::GpuTaskResourceUse, 5> integrationResourceUses = {
        ReadUse(inputs.extinction),
        ReadUse(inputs.control),
        ReadUse(inputs.extinctionOverflow),
        ReadWriteUse(inputs.transmittance, Core::ResourceStates::UnorderedAccess),
        ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    const Array<Core::GpuTaskId, 1> integrationDependency = { inputs.extinctionTask };
    Core::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name(s_RenderIdentityRenderAvboitIntegration))
        .setMarkerLabel(RendererGpuPassText::s_AvboitIntegrationLabel)
        .setScheduling(avboitComputeScheduling)
        .setTiming(avboitComputeStageTiming)
        .setDependencies(integrationDependency)
        .setResourceUses(integrationResourceUses)
    ;
    const auto integrationTaskResult = m_graph.addTask<AvboitIntegrationGraphTask>(
        integrationDesc,
        AvboitIntegrationGraphTask::Payload{ .avboitSystem = m_avboitSystem, .targets = inputs.targets, .timingTicket = inputs.integrationTimingTicket, .timingFeedback = &inputs.timingFeedback, .timingScope = &RendererGpuTimingScope::s_AvboitIntegration, });
    if(!integrationTaskResult){
        m_avboitSystem.taskGraphStage().m_integrationTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return false;
    }
    m_avboitSystem.taskGraphStage().m_integrationTask = *integrationTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitOccupancyRecordBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitOccupancyRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitOccupancyGraphTask::Payload& occupancyPayload,
    RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask::Payload& computeEmulationPayload
){
    using namespace RendererTaskGraphDetail;
    if(!inputs.clearTask.valid() || !inputs.uploadTask.valid())
        return false;

    const Array<Core::GpuTaskResourceUse, 7u> baseResourceUses = {
        ReadUse(inputs.albedo),
        ReadUse(inputs.normal, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth),
        ReadUse(inputs.refractionInstance),
        ReadWriteUse(inputs.avboitLowRaster, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitCoverage, Core::ResourceStates::UnorderedAccess),
    };
    NWB_DEFINE_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.avboit.occupancy", "AVBOIT Occupancy");

    RendererAvboitTaskGraphStageState& stage = m_avboitSystem.taskGraphStage();
    return DeclareAvboitRasterRecordPhase<AvboitOccupancyComputeEmulationGraphTask, AvboitOccupancySharedComputeEmulationGraphTask, AvboitOccupancyGraphTask>(
        m_graph,
        m_graphics,
        m_materialSystem,
        scratchArena,
        frameBindings,
        objectGeometry,
        inputs,
        inputs.clearTask,
        baseResourceUses,
        inputs.preTimingTicket,
        inputs.occupancyComputeEmulationTiming,
        NWB_MAKE_AVBOIT_RASTER_RECORD_PHASE_DESC(
            "render.avboit.occupancy",
            "AVBOIT Occupancy",
            Name(s_RenderIdentityRenderAvboitPre),
            "AVBOIT Pre",
            s_AvboitOccupancyResourceBaseCount
        ),
        occupancyPayload,
        computeEmulationPayload,
        AvboitRasterRecordStageOutputs{
            .reusedGeometryProducer = stage.m_occupancyReusedGeometryProducer,
            .streamTask = stage.m_occupancyStreamTask,
            .computeEmulationTask = stage.m_occupancyComputeEmulationTask,
            .sharedComputeEmulationTasks = stage.m_occupancySharedComputeEmulationTasks,
            .sharedComputeEmulationTaskCount = stage.m_occupancySharedComputeEmulationTaskCount,
            .rasterTask = stage.m_occupancyTask,
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitExtinctionRecordBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitExtinctionRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitExtinctionGraphTask::Payload& extinctionPayload,
    RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask::Payload& computeEmulationPayload
){
    using namespace RendererTaskGraphDetail;
    if(!inputs.depthWarpCompletionTask.valid() || !inputs.uploadTask.valid())
        return false;

    const Array<Core::GpuTaskResourceUse, 10u> baseResourceUses = {
        ReadUse(inputs.albedo),
        ReadUse(inputs.refractionInstance),
        ReadUse(inputs.normal, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth),
        ReadWriteUse(inputs.avboitLowRaster, Core::ResourceStates::RenderTarget),
        ReadUse(inputs.avboitDepthWarp),
        ReadUse(inputs.avboitControl),
        ReadWriteUse(inputs.avboitExtinction, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(inputs.avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess),
    };
    NWB_DEFINE_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.avboit.extinction", "AVBOIT Extinction");

    RendererAvboitTaskGraphStageState& stage = m_avboitSystem.taskGraphStage();
    return DeclareAvboitRasterRecordPhase<AvboitExtinctionComputeEmulationGraphTask, AvboitExtinctionSharedComputeEmulationGraphTask, AvboitExtinctionGraphTask>(
        m_graph,
        m_graphics,
        m_materialSystem,
        scratchArena,
        frameBindings,
        objectGeometry,
        inputs,
        inputs.uploadTask,
        baseResourceUses,
        inputs.extinctionTimingTicket,
        inputs.extinctionComputeEmulationTiming,
        NWB_MAKE_AVBOIT_RASTER_RECORD_PHASE_DESC(
            "render.avboit.extinction",
            "AVBOIT Extinction",
            Name(RendererGpuPassText::s_AvboitExtinctionName),
            RendererGpuPassText::s_AvboitExtinctionLabel,
            s_AvboitResourceBaseCount
        ),
        extinctionPayload,
        computeEmulationPayload,
        AvboitRasterRecordStageOutputs{
            .reusedGeometryProducer = stage.m_extinctionReusedGeometryProducer,
            .streamTask = stage.m_extinctionStreamTask,
            .computeEmulationTask = stage.m_extinctionComputeEmulationTask,
            .sharedComputeEmulationTasks = stage.m_extinctionSharedComputeEmulationTasks,
            .sharedComputeEmulationTaskCount = stage.m_extinctionSharedComputeEmulationTaskCount,
            .rasterTask = stage.m_extinctionTask,
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitAccumulationRecordBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitAccumulationRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& accumulationPayload,
    RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& computeEmulationPayload
){
    using namespace RendererTaskGraphDetail;
    if(!inputs.integrationTask.valid() || !inputs.uploadTask.valid())
        return false;

    const Array<Core::GpuTaskResourceUse, 13u> baseResourceUses = {
        ReadUse(inputs.albedo),
        ReadUse(inputs.normal, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth, Core::ResourceStates::DepthRead),
        ReadUse(inputs.avboitTransmittance),
        ReadUse(inputs.avboitDepthWarp),
        ReadUse(inputs.avboitControl),
        ReadUse(inputs.refractionInstance),
        ReadUse(inputs.refractionDepth),
        ReadWriteUse(inputs.avboitForegroundColor, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitForegroundExtinction, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitAccumColor, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitAccumExtinction, Core::ResourceStates::RenderTarget),
    };
    NWB_DEFINE_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.avboit.accumulation", "AVBOIT Accumulation");

    RendererAvboitTaskGraphStageState& stage = m_avboitSystem.taskGraphStage();
    if(!DeclareAvboitRasterRecordPhase<AvboitAccumulationComputeEmulationGraphTask, AvboitAccumulationSharedComputeEmulationGraphTask, AvboitAccumulationGraphTask>(
        m_graph,
        m_graphics,
        m_materialSystem,
        scratchArena,
        frameBindings,
        objectGeometry,
        inputs,
        inputs.uploadTask,
        baseResourceUses,
        inputs.accumulationTimingTicket,
        inputs.accumulationComputeEmulationTiming,
        NWB_MAKE_AVBOIT_RASTER_RECORD_PHASE_DESC(
            "render.avboit.accumulation",
            "AVBOIT Accumulation",
            Name(s_RenderIdentityRenderAvboitAccumulation),
            "AVBOIT Accumulation",
            s_AvboitResourceBaseCount
        ),
        accumulationPayload,
        computeEmulationPayload,
        AvboitRasterRecordStageOutputs{
            .reusedGeometryProducer = stage.m_accumulationReusedGeometryProducer,
            .streamTask = stage.m_accumulationStreamTask,
            .computeEmulationTask = stage.m_accumulationComputeEmulationTask,
            .sharedComputeEmulationTasks = stage.m_accumulationSharedComputeEmulationTasks,
            .sharedComputeEmulationTaskCount = stage.m_accumulationSharedComputeEmulationTaskCount,
            .rasterTask = stage.m_accumulationTask,
        }))
        return false;

    const Array<Core::GpuTaskResourceUse, 5u> accumulationFinalizeResourceUses = {
        ReadUse(inputs.avboitAccumColor, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.avboitForegroundColor),
        ReadUse(inputs.avboitForegroundExtinction),
        ReadUse(inputs.avboitAccumExtinction, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint accumulationFinalizeScheduling;
    accumulationFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    accumulationFinalizeScheduling.forceSubmissionBoundary = false;
    accumulationFinalizeScheduling.allowPacketMerge = true;
    accumulationFinalizeScheduling.mergeWithPrevious = true;
    // Finalizer is Accumulation's tail; retain its timing/acceptance packet before Lighting.
    accumulationFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc accumulationFinalizeDesc;
    accumulationFinalizeDesc
        .setIdentity(Name(s_RenderIdentityRenderAvboitAccumulationFinalize))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setScheduling(accumulationFinalizeScheduling)
        .setDependencies({ &stage.m_accumulationTask, 1u })
        .setResourceUses(accumulationFinalizeResourceUses)
    ;
    const auto accumulationFinalizeTaskResult = m_graph.addTask<AvboitAccumulationFinalizeGraphTask>(accumulationFinalizeDesc, AvboitAccumulationFinalizeGraphTask::Payload{});
    if(!accumulationFinalizeTaskResult){
        stage.m_accumulationFinalizeTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation finalizer graph task"));
        return false;
    }
    stage.m_accumulationFinalizeTask = *accumulationFinalizeTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitPreGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    // A CSG upload without its frozen stream would detach clears and consumers.
    if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.hasTransparentRenderers && payload.transparentCsgStreamsUploaded){
        payload.avboitSystem.renderAvboitTransparentCsgIntervals(
            commandList,
            payload.targets,
            payload.transparentCsgSnapshot.receiverSurfaceDrawItemsView(),
            payload.transparentCsgSnapshot.csgFrameDataView(),
            payload.csgResources,
            payload.frameBindings,
            payload.transparentCsgSnapshot.instanceCount,
            payload.transparentCsgSnapshot.materialTypedByteCount,
            payload.transparentCsgMaterialGeometryStatesGraphOwned,
            payload.transparentCsgIntervalsTiming
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitOccupancyComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitOccupancySharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitSharedComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitOccupancyGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    return RecordAvboitRasterPassFromPayload(payload, commandList, &RendererAvboitSystem::renderAvboitOccupancyPass);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitDepthWarpGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    return RecordAvboitComputeEffect(payload, commandList, context, &RendererAvboitSystem::dispatchAvboitDepthWarp);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitExtinctionComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitExtinction, MaterialPipelinePass::AvboitExtinction, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitExtinctionSharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitExtinction, MaterialPipelinePass::AvboitExtinction, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitSharedComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitExtinctionGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    return RecordAvboitRasterPassFromPayload(payload, commandList, &RendererAvboitSystem::renderAvboitExtinctionPass);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitIntegrationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    return RecordAvboitComputeEffect(payload, commandList, context, &RendererAvboitSystem::dispatchAvboitIntegration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitAccumulationComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitAccumulate, MaterialPipelinePass::AvboitAccumulate, &AvboitFrameTargets::accumulationFramebuffer, };

    return RecordAvboitComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitAccumulationSharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitAccumulate, MaterialPipelinePass::AvboitAccumulate, &AvboitFrameTargets::accumulationFramebuffer, };

    return RecordAvboitSharedComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitAccumulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    return RecordAvboitRasterPassFromPayload(payload, commandList, &RendererAvboitSystem::renderAvboitAccumulatePass);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitAccumulationFinalizeGraphTask::Record([[maybe_unused]] const Payload& payload, [[maybe_unused]] Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context)noexcept{
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitRefractionCaptureResources::appendUpload(const Name identity, const Core::GpuGraphResourceId destination, const void* bytes, const usize byteSize, const usize alignment){
    auto& graph = m_graph;
    auto& dependency = m_dependency;
    if(!destination.valid() || !bytes || byteSize == 0u)
        return false;
    const auto blobResult = graph.copyUploadData(bytes, byteSize, alignment);
    if(!blobResult)
        return false;
    const Core::GpuUploadBlobId blob = *blobResult;
    Core::GpuTaskDesc desc = TaskDesc(identity, "Refraction Capture Upload", dependency);
    const auto producedTask = graph.addUploadBufferTask(desc, Core::GpuUploadBufferTaskDesc{ .source = blob, .destination = destination, .finalState = Core::ResourceStates::Common, });
    if(!producedTask){
        dependency = {};
        return false;
    }
    dependency = *producedTask;
    return true;
}


b8 AvboitRefractionCaptureResources::appendCsg(
    RendererCsgSystem& csgSystem,
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& csgUses
){
    auto& graph = m_graph;
    const auto importBuffer = [&](const Core::BufferHandle& buffer){ return FindOrImportBuffer(graph, buffer, "Refraction Capture Buffer"); };
    const auto importTexture = [&](const Core::TextureHandle& texture){ return FindOrImportTexture(graph, texture, "Refraction Capture Texture"); };
    csgUses.reserve(s_RefractionCaptureCsgUseCapacity);
    const auto clipContext = csgSystem.prepareCsgClipContextSlotData(targets, csgFrameData, csgResources, frameBindings);
    if(!clipContext)
        return false;
    const auto rangesResult = importBuffer(csgResources.receiverRanges);
    const auto cuttersResult = importBuffer(csgResources.cutters);
    const auto clipResult = importBuffer(csgResources.clipContextSlots);
    const auto intervalResult = importBuffer(csgResources.intervalSampleState);
    if(
        !intervalResult
        || !rangesResult
        || !appendUpload(Name(s_RenderIdentityRenderRefractionCaptureCsgRangesUpload), *rangesResult, csgFrameData.receiverRanges.data(), csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData))
        || !cuttersResult
        || !appendUpload(Name(s_RenderIdentityRenderRefractionCaptureCsgCuttersUpload), *cuttersResult, csgFrameData.cutters.data(), csgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData))
        || !clipResult
        || !appendUpload(Name(s_RenderIdentityRenderRefractionCaptureCsgContextUpload), *clipResult, &*clipContext, sizeof(*clipContext), alignof(CsgClipContextSlots))
    )
        return false;
    const Core::GpuGraphResourceId interval = *intervalResult;
    const Core::GpuGraphResourceId ranges = *rangesResult;
    const Core::GpuGraphResourceId cutters = *cuttersResult;
    const Core::GpuGraphResourceId clip = *clipResult;
    csgUses.push_back(ReadBufferUse(ranges, Core::BufferRange(0u, csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData))));
    csgUses.push_back(ReadBufferUse(cutters, Core::BufferRange(0u, csgFrameData.cutters.size() * sizeof(CsgCutterGpuData))));
    csgUses.push_back(ReadUse(clip, Core::ResourceStates::ConstantBuffer));
    csgUses.push_back(ReadUse(interval, Core::ResourceStates::ConstantBuffer));
    const Array<ReferenceWrapper<const Core::TextureHandle>, 4u> intervalTextures = { targets.csgRemovedIntervalDepth, targets.csgRemovedIntervalCapNormal, targets.csgRemovedIntervalData, targets.csgRemovedIntervalCount, };
    for(const Core::TextureHandle& texture : intervalTextures){
        const auto resourceResult = importTexture(texture);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        csgUses.push_back(ReadTextureUse(resource, Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_cap{


struct CaptureCapTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {Core::GpuQueueCapability::Graphics};


public:
    struct Payload{
        RendererCsgSystem& csgSystem;
        const DeferredFrameTargets& targets;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Core::Rect workRect;
        Vector<u32, Core::Alloc::GlobalArena> receiverIndices;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, RendererCsgSystem& csgSystemIn, const DeferredFrameTargets& targetsIn)noexcept
            : csgSystem(csgSystemIn)
            , targets(targetsIn)
            , receiverIndices(arena)
        {}
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.csgResources.refractionCapFillPipeline || !payload.csgResources.bindingValid() || !payload.targets.avboit.refractionFramebuffer || !payload.targets.bindless.slotsBufferDescriptor.valid() || payload.receiverIndices.empty())
            return false;
        commandList.endRenderPass();
        payload.csgSystem.renderRefractionIntervalCaps(commandList, payload.targets, payload.csgResources, payload.workRect, payload.receiverIndices);
        commandList.endRenderPass();
        return true;
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> AvboitRefractionCapBuilder::Declare(const AvboitRefractionCapInputs& inputs){
    using namespace __hidden_refraction_cap;
    auto& scratchArena = inputs.scratchArena;
    auto& graph = inputs.graph;
    auto& arena = inputs.arena;
    auto& csgSystem = inputs.csgSystem;
    const auto& targets = inputs.targets;
    const auto& csgResources = inputs.csgResources;
    const auto& csgFrameData = inputs.csgFrameData;
    const auto commonUses = inputs.commonUses;
    const auto csgUses = inputs.csgUses;
    const auto opaqueDepth = inputs.opaqueDepth;
    const auto depth = inputs.depth;
    const auto normalIor = inputs.normalIor;
    const auto tintCoverage = inputs.tintCoverage;
    const auto instance = inputs.instance;
    const auto specularRoughness = inputs.specularRoughness;
    const auto dependency = inputs.dependency;

    if(!csgResources.refractionCapFillPipeline)
        return MakeUnexpected(Failure{});
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(commonUses.size() + csgUses.size() + s_RefractionCaptureCapExtraUseCapacity);
    uses.assign(commonUses.begin(), commonUses.end());
    uses.insert(uses.end(), csgUses.begin(), csgUses.end());
    uses.push_back(ReadUse(opaqueDepth));
    uses.push_back(ReadWriteUse(depth, Core::ResourceStates::DepthWrite));
    uses.push_back(ReadWriteUse(normalIor, Core::ResourceStates::RenderTarget));
    uses.push_back(ReadWriteUse(tintCoverage, Core::ResourceStates::RenderTarget));
    uses.push_back(ReadWriteUse(instance, Core::ResourceStates::RenderTarget));
    uses.push_back(ReadWriteUse(specularRoughness, Core::ResourceStates::RenderTarget));
    Core::GpuTaskDesc desc = TaskDesc(Name(s_RenderIdentityRenderRefractionCaptureCaps), "Refraction Capture Caps", dependency);
    desc.setResourceUses(uses);
    if(inputs.sampledTextureSetUse)
        desc.setResourceSetUses({ inputs.sampledTextureSetUse, 1u });
    CaptureCapTask::Payload payload{arena, csgSystem, targets};
    payload.csgResources = csgResources;
    payload.workRect = ToCoreRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height));
    payload.receiverIndices.reserve(csgFrameData.receiverRanges.size());
    for(usize receiverIndex = 0u; receiverIndex < csgFrameData.receiverRanges.size(); ++receiverIndex){
        if(csgFrameData.receiverRanges[receiverIndex].cutterCount != 0u)
            payload.receiverIndices.push_back(static_cast<u32>(receiverIndex));
    }
    if(payload.receiverIndices.empty())
        return MakeUnexpected(Failure{});
    const auto dependencyResult = graph.addTask<CaptureCapTask>(desc, Move(payload));
    if(!dependencyResult)
        return MakeUnexpected(Failure{});
    return *dependencyResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportTexture(Core::GpuTaskGraph& graph, const Core::TextureHandle& texture){
    return FindOrImportTexture(graph, texture, "Refraction Capture Texture");
}

[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportBuffer(Core::GpuTaskGraph& graph, const Core::BufferHandle& buffer){
    return FindOrImportBuffer(graph, buffer, "Refraction Capture Buffer");
}

struct CaptureDrawTask{
    struct Payload{
        RendererMaterialSystem& materialSystem;
        AvboitFrameTargets avboitTargets;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena> drawItems;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        b8 csg = false;
        RenderPath::Enum renderPath = RenderPath::MeshShader;
        b8 generate = false;
        b8 conservativeGeometryScissor = false;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, RendererMaterialSystem& materialSystemIn)noexcept
            : materialSystem(materialSystemIn)
            , drawItems(arena)
        {}
    };

    [[nodiscard]] static constexpr Core::GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept{
        return RendererTaskGraphDetail::SharedComputeEmulationCommandRequirements(payload.generate);
    }

    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.avboitTargets.refractionFramebuffer || !payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount) || (payload.csg && !payload.csgResources.bindingValid()))
            return false;

        const Span<const MaterialPassDrawItem> drawItems(payload.drawItems);
        b8 resourcesReady = false;
        switch(payload.renderPath){
        case RenderPath::MeshShader:
            resourcesReady = payload.materialSystem.drawRecorder().meshMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        case RenderPath::VertexIndexed:
            resourcesReady = payload.materialSystem.drawRecorder().indexedMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        case RenderPath::ComputeEmulation:
            resourcesReady = payload.materialSystem.drawRecorder().computeMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        default:
            return false;
        }
        if(!resourcesReady || (payload.generate && payload.renderPath != RenderPath::ComputeEmulation))
            return false;

        commandList.endRenderPass();
        Core::ViewportState viewport;
        viewport.addViewportAndScissorRect(payload.avboitTargets.refractionFramebuffer->getFramebufferInfo().getViewport());
        const MaterialPassDrawContext drawContext{
            commandList,
            payload.generate ? nullptr : payload.avboitTargets.refractionFramebuffer.get(),
            &payload.avboitTargets,
            viewport,
            payload.csg ? &payload.csgResources : nullptr,
            payload.frameBindings,
            MaterialPipelinePass::AvboitRefractionCapture,
            true,
            true,
            true,
            payload.conservativeGeometryScissor
        };
        if(payload.generate)
            payload.materialSystem.drawRecorder().generateComputeMaterialPassDrawItems(drawContext, drawItems);
        else if(payload.renderPath == RenderPath::ComputeEmulation)
            payload.materialSystem.drawRecorder().renderComputeMaterialPassDrawItemsRasterOnly(drawContext, drawItems);
        else if(payload.renderPath == RenderPath::VertexIndexed)
            payload.materialSystem.drawRecorder().renderIndexedMaterialPassDrawItems(drawContext, drawItems);
        else
            payload.materialSystem.drawRecorder().renderMeshMaterialPassDrawItems(drawContext, drawItems);
        commandList.endRenderPass();
        return true;
    }
};

struct FinalizeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    struct Payload{};


public:
    [[nodiscard]] static b8 Record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> DeclareAvboitRefractionCapture(
    Core::Alloc::ScratchArena& scratchArena,
    Core::GpuTaskGraph& graph,
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem,
    const CsgFrameState& csgFrameState,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const MeshViewGpuData& meshViewState,
    Core::GpuTaskId dependency,
    const b8 enabled,
    DeferredFrameTargets& targets,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitGeneratedGeometryReuse& generatedGeometry
){
    using namespace __hidden_refraction_capture;
    generatedGeometry.reset();
    const auto depthResult = ImportTexture(graph, targets.avboit.refractionDepth);
    const auto normalIorResult = ImportTexture(graph, targets.avboit.refractionNormalIor);
    const auto tintCoverageResult = ImportTexture(graph, targets.avboit.refractionTintCoverage);
    const auto instanceResult = ImportTexture(graph, targets.avboit.refractionInstance);
    const auto specularRoughnessResult = ImportTexture(graph, targets.avboit.refractionSpecularRoughness);
    if(!depthResult || !normalIorResult || !tintCoverageResult || !instanceResult || !specularRoughnessResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId depth = *depthResult;
    const Core::GpuGraphResourceId normalIor = *normalIorResult;
    const Core::GpuGraphResourceId tintCoverage = *tintCoverageResult;
    const Core::GpuGraphResourceId instance = *instanceResult;
    const Core::GpuGraphResourceId specularRoughness = *specularRoughnessResult;

    const auto appendClear = [&](const Name identity, const Core::GpuGraphResourceId destination, const b8 isDepth, const Core::Color color = Core::Color(0.f, 0.f, 0.f, 0.f)){
        Core::GpuTaskDesc desc = TaskDesc(identity, "Refraction Capture Clear", dependency);
        Core::GpuClearTextureTaskDesc clear;
        clear.destination = destination;
        clear.subresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
        clear.valueType = isDepth ? Core::GpuClearTextureTaskValueType::DepthStencil : Core::GpuClearTextureTaskValueType::Float;
        clear.clearDepth = isDepth;
        clear.depthValue = 1.f;
        clear.floatValue = color;
        const auto producedTask = graph.addClearTextureTask(desc, clear);
        if(!producedTask){
            dependency = {};
            return false;
        }
        dependency = *producedTask;
        return true;
    };
    if(
        !appendClear(Name(s_RenderIdentityRenderRefractionClearDepth), depth, true)
        || !appendClear(Name(s_RenderIdentityRenderRefractionClearNormalIor), normalIor, false)
        || !appendClear(Name(s_RenderIdentityRenderRefractionClearTintCoverage), tintCoverage, false)
        || !appendClear(Name(s_RenderIdentityRenderRefractionClearInstance), instance, false)
        || !appendClear(Name(s_RenderIdentityRenderRefractionClearSpecularRoughness), specularRoughness, false, Core::Color(0.f, 0.f, 0.f, 1.f))
    )
        return MakeUnexpected(Failure{});

    const auto finalize = [&](){
        const Array<Core::GpuTaskResourceUse, 5u> uses = { ReadUse(depth), ReadUse(normalIor), ReadUse(tintCoverage), ReadUse(instance), ReadUse(specularRoughness) };
        Core::GpuTaskDesc desc = TaskDesc(Name(s_RenderIdentityRenderRefractionCaptureFinalize), "Refraction Capture Finalize", dependency);
        desc.setResourceUses(uses);
        return graph.addTask<FinalizeTask>(desc, FinalizeTask::Payload{});
    };
    if(!enabled)
        return finalize();
    if(!targets.avboit.refractionFramebuffer)
        return MakeUnexpected(Failure{});
    MaterialPassDrawItemPartitions drawItems{scratchArena};
    InstanceGpuDataVector instances{scratchArena};
    MaterialTypedByteDataVector typedBytes{scratchArena};
    CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector typedRanges{scratchArena};
#endif
    materialSystem.gatherMaterialPassDrawItems(
        targets.avboit.refractionFramebuffer.get(), MaterialPipelinePass::AvboitRefractionCapture, true,
        csgFrameState, RendererResourceLookupMode::PreparedOnly, &meshViewState,
        drawItems, instances, csgFrameData,
#if defined(NWB_DEBUG)
        typedRanges,
#endif
        typedBytes
    );
    if(drawItems.empty())
        return finalize();
    const b8 hasCsg = !drawItems.csg.empty();
    if(
        !frameBindings.frameReady(instances.size(), typedBytes.size())
        || !materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems.regular, frameBindings)
        || (hasCsg && (!csgFrameData.hasWork() || !csgResources.frameReady(csgFrameData) || !materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems.csg, frameBindings)))
    )
        return MakeUnexpected(Failure{});

    const Array<MaterialPassDrawItemsView, 2u> drawSets = {drawItems.regular, drawItems.csg};
    const auto geometrySet = GatherPreparedMaterialGeometryResourceSet(drawSets, Name(s_RenderIdentityRenderRefractionCaptureGeometry), "Refraction Capture Geometry", graph, scratchArena);
    if(!geometrySet)
        return MakeUnexpected(Failure{});
    const auto sampledTextureSet = GatherPreparedMaterialSampledTextureResourceSet(materialSystem, drawSets, Name(s_RenderIdentityRenderRefractionCaptureMaterialTextures), "Refraction Capture Material Textures", graph, scratchArena);
    if(!sampledTextureSet)
        return MakeUnexpected(Failure{});
    const Array<Core::GpuTaskResourceSetUse, 2u> setUses = {{
        {.resourceSet = *geometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
        {.resourceSet = *sampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
    }};
    const usize setUseCount = sampledTextureSet->valid() ? s_RefractionCaptureGeometryAndTextureSetUseCount : 1u;

    materialSystem.prepareMaterialPassInstanceUploadData(csgResources, instances);
    const b8 producesReusableGeometry = generatedGeometry.capture(drawItems, instances, frameBindings, meshViewState, MaterialPipelinePass::AvboitRefractionCapture);
    const auto materialInstancesResult = ImportBuffer(graph, frameBindings.instanceBuffer);
    const auto materialTypedResult = ImportBuffer(graph, frameBindings.materialTypedBuffer);
    const auto meshViewResult = ImportBuffer(graph, frameBindings.meshView.buffer);
    const auto slotsResult = ImportBuffer(graph, targets.bindless.slotsBuffer);
    const auto opaqueDepthResult = ImportTexture(graph, targets.depth);
    if(!materialInstancesResult || !materialTypedResult || !meshViewResult || !slotsResult || !opaqueDepthResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId materialInstances = *materialInstancesResult;
    const Core::GpuGraphResourceId materialTyped = *materialTypedResult;
    const Core::GpuGraphResourceId meshView = *meshViewResult;
    const Core::GpuGraphResourceId slots = *slotsResult;
    const Core::GpuGraphResourceId opaqueDepth = *opaqueDepthResult;

    AvboitRefractionCaptureResources captureResources(graph, dependency);
    if(
        !captureResources.appendUpload(Name(s_RenderIdentityRenderRefractionCaptureInstancesUpload), materialInstances, instances.data(), instances.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData))
        || !captureResources.appendUpload(Name(s_RenderIdentityRenderRefractionCaptureMaterialUpload), materialTyped, typedBytes.data(), typedBytes.size(), alignof(u32))
    )
        return MakeUnexpected(Failure{});

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> commonUses{scratchArena};
    commonUses.reserve(s_RefractionCaptureCommonUseCapacity);
    commonUses.push_back(ReadBufferUse(materialInstances, Core::BufferRange(0u, instances.size() * sizeof(InstanceGpuData))));
    commonUses.push_back(ReadBufferUse(materialTyped, Core::BufferRange(0u, typedBytes.size())));
    commonUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    commonUses.push_back(ReadUse(slots, Core::ResourceStates::ConstantBuffer));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgUses{scratchArena};
    if(hasCsg && !captureResources.appendCsg(csgSystem, targets, csgFrameData, csgResources, frameBindings, csgUses))
        return MakeUnexpected(Failure{});

    if(!objectGeometry.prepare(drawItems.regular.indexedDrawItems, frameBindings, scratchArena, dependency, commonUses))
        return MakeUnexpected(Failure{});
    if(!objectGeometry.prepare(drawItems.csg.indexedDrawItems, frameBindings, scratchArena, dependency, commonUses))
        return MakeUnexpected(Failure{});

    usize drawTaskIndex = 0u;
    const auto appendDraw = [&](const Span<const MaterialPassDrawItem> items, const b8 csg, const RenderPath::Enum renderPath, const b8 generate, const Core::GpuGraphResourceId output){
        if(items.empty())
            return true;
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
        uses.reserve(commonUses.size() + (csg ? csgUses.size() : 0u) + s_RefractionCaptureDrawExtraUseCapacity);
        uses.assign(commonUses.begin(), commonUses.end());
        if(csg)
            uses.insert(uses.end(), csgUses.begin(), csgUses.end());
        if(renderPath == RenderPath::ComputeEmulation)
            uses.push_back(generate ? WriteUse(output, Core::ResourceStates::UnorderedAccess) : ReadUse(output, ECSRenderDetail::s_ObjectGeometryRasterState));
        if(!generate){
            uses.push_back(ReadUse(opaqueDepth));
            uses.push_back(ReadWriteUse(depth, Core::ResourceStates::DepthWrite));
            uses.push_back(ReadWriteUse(normalIor, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(tintCoverage, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(instance, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(specularRoughness, Core::ResourceStates::RenderTarget));
        }
        const auto identityText = StringFormat(scratchArena, "render.refraction.capture.draw_{}", drawTaskIndex++);
        const auto identity = ToName(identityText);
        if(!identity || !*identity){
            dependency = {};
            return false;
        }
        Core::GpuTaskDesc desc = TaskDesc(**identity, generate ? "Refraction Capture Generate" : "Refraction Capture Raster", dependency);
        desc.setResourceUses(uses).setResourceSetUses({ setUses.data(), setUseCount });
        CaptureDrawTask::Payload payload{arena, materialSystem};
        payload.avboitTargets = targets.avboit;
        payload.frameBindings = frameBindings;
        payload.csgResources = csgResources;
        payload.drawItems.reserve(items.size());
        payload.drawItems.assign(items.begin(), items.end());
        payload.instanceCount = instances.size();
        payload.materialTypedByteCount = typedBytes.size();
        payload.csg = csg;
        payload.renderPath = renderPath;
        payload.generate = generate;
        payload.conservativeGeometryScissor = generate && producesReusableGeometry;
        const auto drawTask = graph.addTask<CaptureDrawTask>(desc, Move(payload));
        if(!drawTask){
            dependency = {};
            return false;
        }
        dependency = *drawTask;
        return true;
    };

    // Emulated draws keep a generator/raster pair so outputs outlive their draw.
    const Array<MaterialPassDrawItemsView, 2u> orderedSets = {drawItems.regular, drawItems.csg};
    for(usize setIndex = 0u; setIndex < LengthOf(orderedSets); ++setIndex){
        const MaterialPassDrawItemsView& items = orderedSets[setIndex];
        const b8 csg = setIndex != 0u;
        if(!appendDraw(items.meshDrawItems, csg, RenderPath::MeshShader, false, {}))
            return MakeUnexpected(Failure{});
        if(!appendDraw(items.indexedDrawItems, csg, RenderPath::VertexIndexed, false, {}))
            return MakeUnexpected(Failure{});
        for(const MaterialPassDrawItem& item : items.computeDrawItems){
            const auto outputResult = ImportBuffer(graph, item.meshResources.emulationVertexBuffer);
            const Span<const MaterialPassDrawItem> singleItem(&item, 1u);
            if(!outputResult || !appendDraw(singleItem, csg, RenderPath::ComputeEmulation, true, *outputResult) || !appendDraw(singleItem, csg, RenderPath::ComputeEmulation, false, *outputResult))
                return MakeUnexpected(Failure{});
        }
    }
    if(hasCsg){
        const AvboitRefractionCapInputs capInputs{
            scratchArena, graph, arena, csgSystem, targets, csgResources, csgFrameData,
            commonUses, csgUses, opaqueDepth, depth, normalIor, tintCoverage, instance, specularRoughness,
            sampledTextureSet->valid() ? &setUses[1] : nullptr, dependency
        };
        const auto capResult = AvboitRefractionCapBuilder::Declare(capInputs);
        if(!capResult)
            return MakeUnexpected(capResult.error());
        dependency = *capResult;
    }
    const auto completionResult = finalize();
    if(!completionResult){
        if(producesReusableGeometry)
            generatedGeometry.reset();
        return MakeUnexpected(completionResult.error());
    }
    const Core::GpuTaskId completion = *completionResult;
    if(producesReusableGeometry && !generatedGeometry.publishProducer(completion))
        return MakeUnexpected(Failure{});
    return completion;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
