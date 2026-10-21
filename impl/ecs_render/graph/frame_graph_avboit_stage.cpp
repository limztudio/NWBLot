// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_avboit_stage.h"

#include <impl/ecs_render/avboit/accumulation_record_builder.h>
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
#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>
#include <impl/ecs_render/csg/transparent_csg_interval_builder.h>
#include <impl/ecs_render/graph/frame_graph_avboit_accumulation.h>
#include <impl/ecs_render/graph/frame_graph_avboit_extinction.h>
#include <impl/ecs_render/graph/frame_graph_avboit_occupancy.h>
#include <impl/ecs_render/graph/frame_graph_transparent_csg_tasks.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphAvboitStage::FrameGraphAvboitStage(
    Core::GpuTaskGraph& graph,
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem,
    RendererAvboitSystem& avboitSystem,
    RendererTaskTimingFeedback& timingFeedback
)noexcept
    : m_graph(graph)
    , m_arena(arena)
    , m_graphics(graphics)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem)
    , m_avboitSystem(avboitSystem)
    , m_timingFeedback(timingFeedback)
{}


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
    auto transparentCsgIntervalResult = transparentCsgIntervalBuilder.declare(
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
    auto transparentCsgTaskResult = transparentCsgTasks.declare(
        scratchArena,
        FrameGraphTransparentCsgTaskInputs{
            .targets = inputs.targets,
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
            .transparentCsgIntervalsTiming = &timing.transparentCsgIntervalsTiming,
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
    const bool avboitIntervalOutputsGraphOwned = transparentCsgTaskResult->intervalOutputsGraphOwned;
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
        m_graph, m_arena, m_materialSystem, m_csgSystem, inputs.targets,
        inputs.csgFrameState, inputs.csgResources, inputs.frameBindings, inputs.meshViewState, objectGeometry, generatedGeometry, avboitIntervalCompletionTask,
        inputs.refractionCaptureEnabled
    );
    if(!refractionCaptureTaskResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId refractionCaptureTask = *refractionCaptureTaskResult;
    FrameGraphAvboitOccupancyUploadChain occupancyUploadChain(m_graph, m_materialSystem, m_csgSystem);
    auto occupancyUploadChainResult = occupancyUploadChain.declare(
        scratchArena,
        FrameGraphAvboitOccupancyUploadInputs{
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
    const Core::GpuTaskId occupancyUploadTask = occupancyUploadChainResult->uploadTask;
    const Core::GpuGraphResourceSetId occupancyMaterialGeometrySet = occupancyUploadChainResult->materialGeometrySet;
    const Core::GpuGraphResourceSetId occupancyMaterialSampledTextureSet = occupancyUploadChainResult->materialSampledTextureSet;
    const bool occupancyCsgStreamsUploaded = occupancyUploadChainResult->csgStreamsUploaded;
    const bool occupancyRegularComputeEmulationPlanCaptured = occupancyUploadChainResult->regularComputeEmulationPlanCaptured;
    const Core::GpuTaskId occupancyReusedGeometryProducer = occupancyUploadChainResult->reusedGeometryProducer;
    const bool occupancyProducesReusableGeometry = occupancyUploadChainResult->producesReusableGeometry;
    const bool occupancyCsgComputeEmulationPlanCaptured = occupancyUploadChainResult->csgComputeEmulationPlanCaptured;
    const bool occupancySharedComputeEmulationPlanCaptured = occupancyUploadChainResult->sharedComputeEmulationPlanCaptured;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan occupancySharedComputeEmulationPlan = occupancyUploadChainResult->sharedComputeEmulationPlan;
    const usize occupancySharedComputeEmulationInstanceCount = occupancyUploadChainResult->sharedComputeEmulationInstanceCount;
    const usize occupancySharedComputeEmulationMaterialTypedByteCount = occupancyUploadChainResult->sharedComputeEmulationMaterialTypedByteCount;


    AvboitClearChainBuilder avboitClearChainBuilder(m_graph, m_avboitSystem);
    auto avboitClearChainResult = avboitClearChainBuilder.declare(
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
            .uploadTask = occupancyUploadTask,
            .clearTargets = inputs.clearTargets,
        },
        timing.clearTimingState
    );
    if(!avboitClearChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT clear chain"));
        return MakeUnexpected(Failure{});
    }
    Core::GpuTaskId avboitClearTask = avboitClearChainResult->clearTask;


    AvboitOccupancyRecordInputs avboitOccupancyRecordInputs{ m_arena, inputs.targets, timing.preTimingTicket, timing.occupancyComputeEmulationTiming };
    avboitOccupancyRecordInputs.albedo = inputs.resources.albedo;
    avboitOccupancyRecordInputs.normal = inputs.resources.normal;
    avboitOccupancyRecordInputs.worldPosition = inputs.resources.worldPosition;
    avboitOccupancyRecordInputs.depth = inputs.resources.depth;
    avboitOccupancyRecordInputs.refractionInstance = inputs.resources.refractionInstance;
    avboitOccupancyRecordInputs.avboitLowRaster = inputs.resources.avboitLowRaster;
    avboitOccupancyRecordInputs.avboitCoverage = inputs.resources.avboitCoverage;
    avboitOccupancyRecordInputs.avboitMaterialDomain = inputs.resources.avboitMaterialDomain;
    avboitOccupancyRecordInputs.avboitCsgDomain = inputs.resources.avboitCsgDomain;
    avboitOccupancyRecordInputs.meshView = inputs.resources.meshView;
    avboitOccupancyRecordInputs.materialInstances = inputs.resources.materialInstances;
    avboitOccupancyRecordInputs.materialTyped = inputs.resources.materialTyped;
    avboitOccupancyRecordInputs.csgReceiverRanges = inputs.resources.csgReceiverRanges;
    avboitOccupancyRecordInputs.csgCutters = inputs.resources.csgCutters;
    avboitOccupancyRecordInputs.csgClipContextSlots = inputs.resources.csgClipContextSlots;
    avboitOccupancyRecordInputs.csgIntervalSampleState = inputs.resources.csgIntervalSampleState;
    avboitOccupancyRecordInputs.csgRemovedIntervalDepth = inputs.resources.csgRemovedIntervalDepth;
    avboitOccupancyRecordInputs.csgRemovedIntervalCapNormal = inputs.resources.csgRemovedIntervalCapNormal;
    avboitOccupancyRecordInputs.csgRemovedIntervalData = inputs.resources.csgRemovedIntervalData;
    avboitOccupancyRecordInputs.csgRemovedIntervalCount = inputs.resources.csgRemovedIntervalCount;
    avboitOccupancyRecordInputs.csgRemovedIntervalSubresources = inputs.resources.csgRemovedIntervalSubresources;
    avboitOccupancyRecordInputs.csgRemovedIntervalCountSubresources = inputs.resources.csgRemovedIntervalCountSubresources;
    avboitOccupancyRecordInputs.currentBindlessSlots = inputs.resources.currentBindlessSlots;
    avboitOccupancyRecordInputs.clearTask = avboitClearTask;
    avboitOccupancyRecordInputs.uploadTask = occupancyUploadTask;
    avboitOccupancyRecordInputs.materialGeometrySet = occupancyMaterialGeometrySet;
    avboitOccupancyRecordInputs.materialSampledTextureSet = occupancyMaterialSampledTextureSet;
    avboitOccupancyRecordInputs.intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned;
    avboitOccupancyRecordInputs.csgStreamsUploaded = occupancyCsgStreamsUploaded;
    avboitOccupancyRecordInputs.regularComputeEmulationPlanCaptured = occupancyRegularComputeEmulationPlanCaptured;
    avboitOccupancyRecordInputs.reusedGeometryProducer = occupancyReusedGeometryProducer;
    avboitOccupancyRecordInputs.producesReusableGeometry = occupancyProducesReusableGeometry;
    avboitOccupancyRecordInputs.csgComputeEmulationPlanCaptured = occupancyCsgComputeEmulationPlanCaptured;
    avboitOccupancyRecordInputs.sharedComputeEmulationPlanCaptured = occupancySharedComputeEmulationPlanCaptured;
    avboitOccupancyRecordInputs.sharedComputeEmulationPlan = occupancySharedComputeEmulationPlan;
    avboitOccupancyRecordInputs.sharedComputeEmulationInstanceCount = occupancySharedComputeEmulationInstanceCount;
    avboitOccupancyRecordInputs.sharedComputeEmulationMaterialTypedByteCount = occupancySharedComputeEmulationMaterialTypedByteCount;
    AvboitOccupancyRecordBuilder avboitOccupancyRecordBuilder(m_graph, m_graphics, m_materialSystem, m_avboitSystem);
    auto avboitOccupancyRecordResult = avboitOccupancyRecordBuilder.declare(scratchArena, inputs.frameBindings, inputs.csgResources, objectGeometry, avboitOccupancyRecordInputs, avboitOccupancyPayload, avboitOccupancyComputeEmulationPayload);
    if(!avboitOccupancyRecordResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
        return MakeUnexpected(Failure{});
    }
    if(occupancyProducesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return MakeUnexpected(Failure{});
    }


    AvboitComputeEffectChainBuilder avboitComputeEffectChainBuilder(m_graph, m_avboitSystem);
    auto avboitDepthWarpStageResult = avboitComputeEffectChainBuilder.declareDepthWarp(
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
        if(!declareExtinction(scratchArena, inputs, timing, objectGeometry, generatedGeometry, avboitComputeEffectChainBuilder, avboitDepthWarpCompletionTask, avboitIntervalOutputsGraphOwned))
            return MakeUnexpected(Failure{});
        if(!declareAccumulation(scratchArena, inputs, timing, objectGeometry, generatedGeometry, avboitIntervalOutputsGraphOwned))
            return MakeUnexpected(Failure{});
        return m_avboitSystem.taskGraphStage().m_accumulationFinalizeTask;
    }
    return m_avboitSystem.taskGraphStage().m_occupancyTask;
}


bool FrameGraphAvboitStage::declareExtinction(
    Core::Alloc::ScratchArena& scratchArena,
    const FrameGraphAvboitStageInputs& inputs,
    const FrameGraphAvboitStageTiming& timing,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitGeneratedGeometryReuse& generatedGeometry,
    AvboitComputeEffectChainBuilder& avboitComputeEffectChainBuilder,
    const Core::GpuTaskId avboitDepthWarpCompletionTask,
    const bool avboitIntervalOutputsGraphOwned
){
    using namespace RendererTaskGraphDetail;

    AvboitExtinctionGraphTask::Payload avboitExtinctionPayload{ m_arena, m_avboitSystem, inputs.targets, timing.extinctionTimingTicket };
    AvboitExtinctionComputeEmulationGraphTask::Payload avboitExtinctionComputeEmulationPayload{ m_arena, m_graphics, m_materialSystem, inputs.targets, timing.extinctionTimingTicket, timing.extinctionComputeEmulationTiming };
    avboitExtinctionPayload.frameBindings = inputs.frameBindings;
    avboitExtinctionComputeEmulationPayload.frameBindings = inputs.frameBindings;
    avboitExtinctionPayload.hasTransparentRenderers = inputs.hasTransparentRenderers;
    avboitExtinctionPayload.csgResources = inputs.csgResources;
    avboitExtinctionComputeEmulationPayload.csgResources = inputs.csgResources;

    FrameGraphAvboitExtinctionUploadChain extinctionUploadChain(m_graph, m_materialSystem, m_csgSystem);
    auto extinctionUploadChainResult = extinctionUploadChain.declare(
        scratchArena,
        FrameGraphAvboitExtinctionUploadInputs{
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
    const Core::GpuTaskId extinctionUploadTask = extinctionUploadChainResult->uploadTask;
    const Core::GpuGraphResourceSetId extinctionMaterialGeometrySet = extinctionUploadChainResult->materialGeometrySet;
    const Core::GpuGraphResourceSetId extinctionMaterialSampledTextureSet = extinctionUploadChainResult->materialSampledTextureSet;
    const bool extinctionStreamsUploaded = extinctionUploadChainResult->streamsUploaded;
    const bool extinctionCsgStreamsUploaded = extinctionUploadChainResult->csgStreamsUploaded;
    const bool extinctionRegularComputeEmulationPlanCaptured = extinctionUploadChainResult->regularComputeEmulationPlanCaptured;
    const Core::GpuTaskId extinctionReusedGeometryProducer = extinctionUploadChainResult->reusedGeometryProducer;
    const bool extinctionProducesReusableGeometry = extinctionUploadChainResult->producesReusableGeometry;
    const bool extinctionCsgComputeEmulationPlanCaptured = extinctionUploadChainResult->csgComputeEmulationPlanCaptured;
    const bool extinctionSharedComputeEmulationPlanCaptured = extinctionUploadChainResult->sharedComputeEmulationPlanCaptured;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan extinctionSharedComputeEmulationPlan = extinctionUploadChainResult->sharedComputeEmulationPlan;
    const usize extinctionSharedComputeEmulationInstanceCount = extinctionUploadChainResult->sharedComputeEmulationInstanceCount;
    const usize extinctionSharedComputeEmulationMaterialTypedByteCount = extinctionUploadChainResult->sharedComputeEmulationMaterialTypedByteCount;
    AvboitExtinctionRecordInputs avboitExtinctionRecordInputs{ m_arena, inputs.targets, timing.extinctionTimingTicket, timing.extinctionComputeEmulationTiming };
    avboitExtinctionRecordInputs.albedo = inputs.resources.albedo;
    avboitExtinctionRecordInputs.normal = inputs.resources.normal;
    avboitExtinctionRecordInputs.worldPosition = inputs.resources.worldPosition;
    avboitExtinctionRecordInputs.depth = inputs.resources.depth;
    avboitExtinctionRecordInputs.refractionInstance = inputs.resources.refractionInstance;
    avboitExtinctionRecordInputs.avboitLowRaster = inputs.resources.avboitLowRaster;
    avboitExtinctionRecordInputs.avboitDepthWarp = inputs.resources.avboitDepthWarp;
    avboitExtinctionRecordInputs.avboitExtinction = inputs.resources.avboitExtinction;
    avboitExtinctionRecordInputs.avboitControl = inputs.resources.avboitControl;
    avboitExtinctionRecordInputs.avboitExtinctionOverflow = inputs.resources.avboitExtinctionOverflow;
    avboitExtinctionRecordInputs.avboitMaterialDomain = inputs.resources.avboitMaterialDomain;
    avboitExtinctionRecordInputs.avboitCsgDomain = inputs.resources.avboitCsgDomain;
    avboitExtinctionRecordInputs.meshView = inputs.resources.meshView;
    avboitExtinctionRecordInputs.materialInstances = inputs.resources.materialInstances;
    avboitExtinctionRecordInputs.materialTyped = inputs.resources.materialTyped;
    avboitExtinctionRecordInputs.csgReceiverRanges = inputs.resources.csgReceiverRanges;
    avboitExtinctionRecordInputs.csgCutters = inputs.resources.csgCutters;
    avboitExtinctionRecordInputs.csgClipContextSlots = inputs.resources.csgClipContextSlots;
    avboitExtinctionRecordInputs.csgIntervalSampleState = inputs.resources.csgIntervalSampleState;
    avboitExtinctionRecordInputs.csgRemovedIntervalDepth = inputs.resources.csgRemovedIntervalDepth;
    avboitExtinctionRecordInputs.csgRemovedIntervalCapNormal = inputs.resources.csgRemovedIntervalCapNormal;
    avboitExtinctionRecordInputs.csgRemovedIntervalData = inputs.resources.csgRemovedIntervalData;
    avboitExtinctionRecordInputs.csgRemovedIntervalCount = inputs.resources.csgRemovedIntervalCount;
    avboitExtinctionRecordInputs.csgRemovedIntervalSubresources = inputs.resources.csgRemovedIntervalSubresources;
    avboitExtinctionRecordInputs.csgRemovedIntervalCountSubresources = inputs.resources.csgRemovedIntervalCountSubresources;
    avboitExtinctionRecordInputs.currentBindlessSlots = inputs.resources.currentBindlessSlots;
    avboitExtinctionRecordInputs.depthWarpCompletionTask = avboitDepthWarpCompletionTask;
    avboitExtinctionRecordInputs.uploadTask = extinctionUploadTask;
    avboitExtinctionRecordInputs.materialGeometrySet = extinctionMaterialGeometrySet;
    avboitExtinctionRecordInputs.materialSampledTextureSet = extinctionMaterialSampledTextureSet;
    avboitExtinctionRecordInputs.intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned;
    avboitExtinctionRecordInputs.csgStreamsUploaded = extinctionCsgStreamsUploaded;
    avboitExtinctionRecordInputs.streamsUploaded = extinctionStreamsUploaded;
    avboitExtinctionRecordInputs.regularComputeEmulationPlanCaptured = extinctionRegularComputeEmulationPlanCaptured;
    avboitExtinctionRecordInputs.reusedGeometryProducer = extinctionReusedGeometryProducer;
    avboitExtinctionRecordInputs.producesReusableGeometry = extinctionProducesReusableGeometry;
    avboitExtinctionRecordInputs.csgComputeEmulationPlanCaptured = extinctionCsgComputeEmulationPlanCaptured;
    avboitExtinctionRecordInputs.sharedComputeEmulationPlanCaptured = extinctionSharedComputeEmulationPlanCaptured;
    avboitExtinctionRecordInputs.sharedComputeEmulationPlan = extinctionSharedComputeEmulationPlan;
    avboitExtinctionRecordInputs.sharedComputeEmulationInstanceCount = extinctionSharedComputeEmulationInstanceCount;
    avboitExtinctionRecordInputs.sharedComputeEmulationMaterialTypedByteCount = extinctionSharedComputeEmulationMaterialTypedByteCount;
    AvboitExtinctionRecordBuilder avboitExtinctionRecordBuilder(m_graph, m_graphics, m_materialSystem, m_avboitSystem);
    auto avboitExtinctionRecordResult = avboitExtinctionRecordBuilder.declare(scratchArena, inputs.frameBindings, inputs.csgResources, objectGeometry, avboitExtinctionRecordInputs, avboitExtinctionPayload, avboitExtinctionComputeEmulationPayload);
    if(!avboitExtinctionRecordResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
        return false;
    }
    if(extinctionProducesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return false;
    }

    auto avboitIntegrationStageResult = avboitComputeEffectChainBuilder.declareIntegration(
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
    );
    if(!avboitIntegrationStageResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return false;
    }
    return true;
}

bool FrameGraphAvboitStage::declareAccumulation(
    Core::Alloc::ScratchArena& scratchArena,
    const FrameGraphAvboitStageInputs& inputs,
    const FrameGraphAvboitStageTiming& timing,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitGeneratedGeometryReuse& generatedGeometry,
    const bool avboitIntervalOutputsGraphOwned
){
    using namespace RendererTaskGraphDetail;

    AvboitAccumulationGraphTask::Payload avboitAccumulationPayload{ m_arena, m_avboitSystem, inputs.targets, timing.accumulationTimingTicket };
    AvboitAccumulationComputeEmulationGraphTask::Payload avboitAccumulationComputeEmulationPayload{ m_arena, m_graphics, m_materialSystem, inputs.targets, timing.accumulationTimingTicket, timing.accumulationComputeEmulationTiming };
    avboitAccumulationPayload.frameBindings = inputs.frameBindings;
    avboitAccumulationComputeEmulationPayload.frameBindings = inputs.frameBindings;
    avboitAccumulationPayload.hasTransparentRenderers = inputs.hasTransparentRenderers;
    avboitAccumulationPayload.csgResources = inputs.csgResources;
    avboitAccumulationComputeEmulationPayload.csgResources = inputs.csgResources;

    FrameGraphAvboitAccumulationUploadChain accumulationUploadChain(m_graph, m_materialSystem, m_csgSystem);
    auto accumulationUploadChainResult = accumulationUploadChain.declare(
        scratchArena,
        FrameGraphAvboitAccumulationUploadInputs{
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
        },
        avboitAccumulationPayload,
        avboitAccumulationComputeEmulationPayload,
        generatedGeometry
    );
    if(!accumulationUploadChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation upload chain"));
        return false;
    }
    const Core::GpuTaskId accumulationUploadTask = accumulationUploadChainResult->uploadTask;
    const Core::GpuGraphResourceSetId accumulationMaterialGeometrySet = accumulationUploadChainResult->materialGeometrySet;
    const Core::GpuGraphResourceSetId accumulationMaterialSampledTextureSet = accumulationUploadChainResult->materialSampledTextureSet;
    const bool accumulationStreamsUploaded = accumulationUploadChainResult->streamsUploaded;
    const bool accumulationCsgStreamsUploaded = accumulationUploadChainResult->csgStreamsUploaded;
    const bool accumulationRegularComputeEmulationPlanCaptured = accumulationUploadChainResult->regularComputeEmulationPlanCaptured;
    const Core::GpuTaskId accumulationReusedGeometryProducer = accumulationUploadChainResult->reusedGeometryProducer;
    const bool accumulationProducesReusableGeometry = accumulationUploadChainResult->producesReusableGeometry;
    const bool accumulationCsgComputeEmulationPlanCaptured = accumulationUploadChainResult->csgComputeEmulationPlanCaptured;
    const bool accumulationSharedComputeEmulationPlanCaptured = accumulationUploadChainResult->sharedComputeEmulationPlanCaptured;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan accumulationSharedComputeEmulationPlan = accumulationUploadChainResult->sharedComputeEmulationPlan;
    const usize accumulationSharedComputeEmulationInstanceCount = accumulationUploadChainResult->sharedComputeEmulationInstanceCount;
    const usize accumulationSharedComputeEmulationMaterialTypedByteCount = accumulationUploadChainResult->sharedComputeEmulationMaterialTypedByteCount;

    AvboitAccumulationRecordInputs avboitAccumulationRecordInputs{ m_arena, inputs.targets, timing.accumulationTimingTicket, timing.accumulationComputeEmulationTiming };
    avboitAccumulationRecordInputs.albedo = inputs.resources.albedo;
    avboitAccumulationRecordInputs.normal = inputs.resources.normal;
    avboitAccumulationRecordInputs.worldPosition = inputs.resources.worldPosition;
    avboitAccumulationRecordInputs.depth = inputs.resources.depth;
    avboitAccumulationRecordInputs.refractionInstance = inputs.resources.refractionInstance;
    avboitAccumulationRecordInputs.refractionDepth = inputs.resources.refractionDepth;
    avboitAccumulationRecordInputs.avboitDepthWarp = inputs.resources.avboitDepthWarp;
    avboitAccumulationRecordInputs.avboitTransmittance = inputs.resources.avboitTransmittance;
    avboitAccumulationRecordInputs.avboitControl = inputs.resources.avboitControl;
    avboitAccumulationRecordInputs.avboitForegroundColor = inputs.resources.avboitForegroundColor;
    avboitAccumulationRecordInputs.avboitForegroundExtinction = inputs.resources.avboitForegroundExtinction;
    avboitAccumulationRecordInputs.avboitAccumColor = inputs.resources.avboitAccumColor;
    avboitAccumulationRecordInputs.avboitAccumExtinction = inputs.resources.avboitAccumExtinction;
    avboitAccumulationRecordInputs.avboitMaterialDomain = inputs.resources.avboitMaterialDomain;
    avboitAccumulationRecordInputs.avboitCsgDomain = inputs.resources.avboitCsgDomain;
    avboitAccumulationRecordInputs.meshView = inputs.resources.meshView;
    avboitAccumulationRecordInputs.materialInstances = inputs.resources.materialInstances;
    avboitAccumulationRecordInputs.materialTyped = inputs.resources.materialTyped;
    avboitAccumulationRecordInputs.csgReceiverRanges = inputs.resources.csgReceiverRanges;
    avboitAccumulationRecordInputs.csgCutters = inputs.resources.csgCutters;
    avboitAccumulationRecordInputs.csgClipContextSlots = inputs.resources.csgClipContextSlots;
    avboitAccumulationRecordInputs.csgIntervalSampleState = inputs.resources.csgIntervalSampleState;
    avboitAccumulationRecordInputs.csgRemovedIntervalDepth = inputs.resources.csgRemovedIntervalDepth;
    avboitAccumulationRecordInputs.csgRemovedIntervalCapNormal = inputs.resources.csgRemovedIntervalCapNormal;
    avboitAccumulationRecordInputs.csgRemovedIntervalData = inputs.resources.csgRemovedIntervalData;
    avboitAccumulationRecordInputs.csgRemovedIntervalCount = inputs.resources.csgRemovedIntervalCount;
    avboitAccumulationRecordInputs.csgRemovedIntervalSubresources = inputs.resources.csgRemovedIntervalSubresources;
    avboitAccumulationRecordInputs.csgRemovedIntervalCountSubresources = inputs.resources.csgRemovedIntervalCountSubresources;
    avboitAccumulationRecordInputs.currentBindlessSlots = inputs.resources.currentBindlessSlots;
    avboitAccumulationRecordInputs.integrationTask = m_avboitSystem.taskGraphStage().m_integrationTask;
    avboitAccumulationRecordInputs.uploadTask = accumulationUploadTask;
    avboitAccumulationRecordInputs.materialGeometrySet = accumulationMaterialGeometrySet;
    avboitAccumulationRecordInputs.materialSampledTextureSet = accumulationMaterialSampledTextureSet;
    avboitAccumulationRecordInputs.intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned;
    avboitAccumulationRecordInputs.csgStreamsUploaded = accumulationCsgStreamsUploaded;
    avboitAccumulationRecordInputs.streamsUploaded = accumulationStreamsUploaded;
    avboitAccumulationRecordInputs.regularComputeEmulationPlanCaptured = accumulationRegularComputeEmulationPlanCaptured;
    avboitAccumulationRecordInputs.reusedGeometryProducer = accumulationReusedGeometryProducer;
    avboitAccumulationRecordInputs.producesReusableGeometry = accumulationProducesReusableGeometry;
    avboitAccumulationRecordInputs.csgComputeEmulationPlanCaptured = accumulationCsgComputeEmulationPlanCaptured;
    avboitAccumulationRecordInputs.sharedComputeEmulationPlanCaptured = accumulationSharedComputeEmulationPlanCaptured;
    avboitAccumulationRecordInputs.sharedComputeEmulationPlan = accumulationSharedComputeEmulationPlan;
    avboitAccumulationRecordInputs.sharedComputeEmulationInstanceCount = accumulationSharedComputeEmulationInstanceCount;
    avboitAccumulationRecordInputs.sharedComputeEmulationMaterialTypedByteCount = accumulationSharedComputeEmulationMaterialTypedByteCount;
    AvboitAccumulationRecordBuilder avboitAccumulationRecordBuilder(m_graph, m_graphics, m_materialSystem, m_avboitSystem);
    auto avboitAccumulationRecordResult = avboitAccumulationRecordBuilder.declare(scratchArena, inputs.frameBindings, inputs.csgResources, objectGeometry, avboitAccumulationRecordInputs, avboitAccumulationPayload, avboitAccumulationComputeEmulationPayload);
    if(!avboitAccumulationRecordResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
        return false;
    }
    if(accumulationProducesReusableGeometry){
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

