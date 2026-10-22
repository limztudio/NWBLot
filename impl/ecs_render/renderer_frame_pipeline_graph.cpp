// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/reflection/scene_content_stamp.h>
#include <impl/ecs_render/raytrace/task_graph_post_gbuffer_normalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_finalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_float_clear_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_refraction_resolve.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/raytrace/task_graph_optical_scene_upload.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>
#include <impl/ecs_render/avboit/task_graph_refraction_capture.h>
#include <impl/ecs_render/avboit/generated_geometry_reuse.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_frame_recovery_task.h>
#include <impl/ecs_render/kernel/task_graph_frame_timing_end_task.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/deferred/task_graph_gbuffer_task.h>
#include <impl/ecs_render/deferred/task_graph_present_task.h>
#include <impl/ecs_render/deferred/task_graph_suffix_builder.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>
#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>
#include <impl/ecs_render/csg/transparent_csg_interval_builder.h>
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/lighting_stage_builder.h>
#include <impl/ecs_render/deferred/frame_tail_builder.h>
#include <impl/ecs_render/raytrace/hardware_caustics_stage_builder.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>
#include <impl/ecs_render/avboit/task_graph_timing_metadata.h>
#include <impl/ecs_render/avboit/clear_chain_builder.h>
#include <impl/ecs_render/avboit/compute_effect_chain_builder.h>
#include <impl/ecs_render/avboit/occupancy_record_builder.h>
#include <impl/ecs_render/avboit/extinction_record_builder.h>
#include <impl/ecs_render/avboit/accumulation_record_builder.h>
#include <impl/ecs_render/graph/frame_graph_software_bvh_build_state.h>
#include <impl/ecs_render/graph/frame_graph_trace_geometry.h>
#include <impl/ecs_render/graph/frame_graph_avboit_stage.h>
#include <impl/ecs_render/graph/frame_graph_transparent_csg_tasks.h>
#include <impl/ecs_render/graph/frame_graph_avboit_occupancy.h>
#include <impl/ecs_render/graph/frame_graph_avboit_extinction.h>
#include <impl/ecs_render/graph/frame_graph_avboit_accumulation.h>
#include <impl/ecs_render/graph/frame_graph_reflection_resolve.h>
#include <impl/ecs_render/avboit/avboit_pass_upload_helper.h>
#include <impl/ecs_render/avboit/material_upload_builder.h>
#include <impl/ecs_render/avboit/geometry_preparation_builder.h>
#include <impl/ecs_render/avboit/compute_emulation_capture.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/task/gpu/scheduler.h>
#include <core/graphics/gpu_timing.h>

#include <global/hash_utils.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererFramePipeline::buildDeferredLightingTaskGraph(
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::RendererFrameGraphFeatures& features,
    DeferredFrameTargets& deferredTargets,
    const CsgFrameState& csgFrameState,
    const bool clearAvboitTargets,
    const bool hasTransparentRenderers,
    const bool hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    const Core::AcquiredPresentationFrame& presentationFrame,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    GraphClearTimingRecordState& deferredClearTimingState,
    GraphClearTimingRecordState& opaqueCsgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
    Core::GpuTimingSubmissionTicket** const graphicsPrefixTimingTickets,
    const bool& asyncPrefixTimingSpansOnePacket,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
    GraphClearTimingRecordState& avboitClearTimingState,
    GraphClearTimingRecordState& transparentCsgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTiming,
    Optional<Core::GpuTimingMeasure>& avboitOccupancyComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& avboitExtinctionComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& avboitAccumulationComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket& avboitDepthWarpTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitExtinctionTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitIntegrationTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitAccumulationTimingTicket,
    Core::GpuTimingSubmissionTicket& shadowVisibilityTimingTicket,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityAsyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueSoftResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentSoftResolveTiming,
    bool& shadowVisibilityOpaqueProduced,
    bool& shadowVisibilityTransparentTraceProduced,
    u32& shadowVisibilityOpaqueFrameIndex,
    Core::GpuTimingSubmissionTicket& softwareCausticsTimingTicket,
    Core::GpuTimingSubmissionTicket& surfelGiTimingTicket,
    Optional<Core::GpuTimingMeasure>& surfelGiAsyncTiming,
    Core::GpuTimingSubmissionTicket& hardwareCausticsTimingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming,
    Core::GpuTimingSubmissionTicket& lightingTimingTicket,
    Core::GpuTimingSubmissionTicket& compositeTimingTicket,
    Core::GpuTimingSubmissionTicket& presentTimingTicket,
    const Core::QueueSubmissionToken& surfelCounterReadbackCompletionToken,
    const Core::QueueSubmissionToken& laggedLightingHistoryReadReadyToken,
    const Core::QueueSubmissionToken& laggedLightingHistoryWriterDrainToken,
    const bool includeLaggedLightingHistoryCapture
){
    using namespace RendererTaskGraphDetail;

    const RayTracingShadowPreparationResourceSnapshot rayTracingShadowResources = m_raytracingSystem.snapshotShadowPreparationResources();
    const RayTracingDeferredGraphResourceSnapshot rayTracingGraphResources = m_raytracingSystem.snapshotDeferredGraphResources();
    const RayTracingSurfelPersistentResourceSnapshot rayTracingSurfelResources = m_raytracingSystem.snapshotSurfelPersistentResources();

    m_deferredBindlessSlotsUploadTask = {};
    m_rayTraceMaterialContextSlotsUploadTask = {};
    m_csgTraceContextUploadTask = {};
    m_causticEmissionTargetsUploadTask = {};
    m_surfelFrameConstantsUploadTask = {};
    m_shadowInstanceMaterialUploadTask = {};
    m_shadowInstanceUploadTask = {};
    m_shadowMaterialTypedUploadTask = {};
    m_sceneBvhNodesUploadTask = {};
    m_sceneBvhInstancesUploadTask = {};
    m_deferredLaggedLightingHistorySlotsUploadTask = {};
    m_deferredFrameTimingBeginTask = {};
    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixCsgIntervalClearTask = {};
    resetGraphicsPrefixTaskState();
    resetSharedDeferredFrameTaskState();
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
    m_deferredPresentationOverlayRequired = false;
    resetDeferredTaskGraphRuntime();
    // Declaration runs between artifact discard and execution-time compilation.
    const Timer declarationBegin = TimerNow();
    if(!declareFrameTimingBeginTask(frameTimingTransaction))
        return;

    const auto& device = m_graphics.getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = computeFamilyIndex != Limit<u32>::s_Max && computeFamilyIndex != graphicsFamilyIndex;
    const bool useLaggedLightingHistory = dedicatedAsyncCompute && features.frameLaggedAsyncLightingEnabled && features.laggedLightingHistoryReady && features.laggedLightingHistoryReadReady;
    const bool declaresHardwareCaustics = features.hardwareCaustics;
    const bool capturesLaggedLightingHistory = includeLaggedLightingHistoryCapture && dedicatedAsyncCompute && features.frameLaggedAsyncLightingEnabled;
    const DeferredLaggedLightingHistoryResources* const history = useLaggedLightingHistory ? &deferredTargets.laggedLightingHistory : nullptr;
    const DeferredLaggedLightingHistoryResources* const captureHistory = capturesLaggedLightingHistory ? &deferredTargets.laggedLightingHistory : nullptr;
    if(!presentationFrame.valid())
        return;
    const Core::Framebuffer& presentationFramebuffer = *presentationFrame.framebuffer;
    const Core::FramebufferDesc& presentationFramebufferDesc = presentationFramebuffer.getDescription();
    const DeferredLightingGraphResources deferredLightingResources = m_deferredSystem.lightingGraphResources();
    const ECSRenderDetail::MeshFrameBindingSnapshot frameBindings = m_meshSystem.meshFrameBindingSnapshot();
    const bool retainRefractionCapPipeline = m_preparedRefractionActive && (csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork);
    const ECSRenderDetail::CsgGraphResourceSnapshot csgResources = m_csgSystem.csgGraphResourceSnapshot(retainRefractionCapPipeline);
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = frameBindings.meshView;
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !meshViewBufferSnapshot.valid()
        || (!csgFrameState.empty() && !frameBindings.bindingValid())
        || !deferredLightingResources.valid()
        || presentationFramebufferDesc.colorAttachments.size() != 1u
        || presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()
        || hasTransparentRenderers != features.hasTransparentRenderers
        || (useLaggedLightingHistory && (!history || !history->valid()))
        || (capturesLaggedLightingHistory && (!captureHistory || !captureHistory->valid()))
    )
        return;

    Core::Alloc::ScratchArena traceGeometryScratchArena(RendererArenaScope::s_TaskGraphArena);
    FrameGraphTraceGeometryImporter traceGeometryImporter(m_deferredLightingTaskGraph, m_raytracingSystem, m_meshSystem);
    const auto traceGeometryResult = traceGeometryImporter.declare(traceGeometryScratchArena, rayTracingShadowResources);
    if(!traceGeometryResult)
        return;
    const FrameGraphTraceGeometryResult& traceGeometry = *traceGeometryResult;
    DeferredGraphResourceImportBuilder deferredGraphResourceImportBuilder(m_deferredLightingTaskGraph);
    auto deferredGraphResources = deferredGraphResourceImportBuilder.declare(
        DeferredGraphResourceImportInputs{
            .targets = deferredTargets,
            .lightingResources = deferredLightingResources,
            .frameBindings = frameBindings,
            .meshViewSnapshot = meshViewBufferSnapshot,
            .csgResources = csgResources,
            .rayTracingResources = rayTracingGraphResources,
            .history = history,
            .captureHistory = captureHistory,
            .clearAvboitTargets = clearAvboitTargets,
            .capturesLaggedLightingHistory = capturesLaggedLightingHistory,
            .hasCsgFrameWork = !csgFrameState.empty(),
        }
    );
    if(!deferredGraphResources){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }
    const Core::GpuGraphResourceId albedo = deferredGraphResources->albedo;
    const Core::GpuGraphResourceId normal = deferredGraphResources->normal;
    const Core::GpuGraphResourceId worldPosition = deferredGraphResources->worldPosition;
    const Core::GpuGraphResourceId specularRoughness = deferredGraphResources->specularRoughness;
    const Core::GpuGraphResourceId depth = deferredGraphResources->depth;
    const Core::GpuGraphResourceId shadowVisibility = deferredGraphResources->shadowVisibility;
    const Core::GpuGraphResourceId causticIrradiance = deferredGraphResources->causticIrradiance;
    const Core::GpuGraphResourceId surfelIrradiance = deferredGraphResources->surfelIrradiance;
    const Core::GpuGraphResourceId currentShadowVisibility = deferredGraphResources->currentShadowVisibility;
    const Core::GpuGraphResourceId currentCausticIrradiance = deferredGraphResources->currentCausticIrradiance;
    const Core::GpuGraphResourceId currentSurfelIrradiance = deferredGraphResources->currentSurfelIrradiance;
    const Core::GpuGraphResourceId opaqueColor = deferredGraphResources->opaqueColor;
    const Core::GpuGraphResourceId sceneShading = deferredGraphResources->sceneShading;
    const Core::GpuGraphResourceId lights = deferredGraphResources->lights;
    const Core::GpuGraphResourceId meshView = deferredGraphResources->meshView;
    const Core::GpuGraphResourceId bindlessSlots = deferredGraphResources->bindlessSlots;
    const Core::GpuGraphResourceId currentBindlessSlots = deferredGraphResources->currentBindlessSlots;
    const Core::GpuGraphResourceId materialContextSlots = deferredGraphResources->materialContextSlots;
    const Core::GpuGraphResourceId historyCopyShadowVisibility = deferredGraphResources->historyCopyShadowVisibility;
    const Core::GpuGraphResourceId historyCopyCausticIrradiance = deferredGraphResources->historyCopyCausticIrradiance;
    const Core::GpuGraphResourceId historyCopySurfelIrradiance = deferredGraphResources->historyCopySurfelIrradiance;
    const Core::GpuGraphResourceId historyCopyDestinationShadowVisibility = deferredGraphResources->historyCopyDestinationShadowVisibility;
    const Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance = deferredGraphResources->historyCopyDestinationCausticIrradiance;
    const Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance = deferredGraphResources->historyCopyDestinationSurfelIrradiance;
    const Core::GpuGraphResourceId avboitAccumColor = deferredGraphResources->avboitAccumColor;
    const Core::GpuGraphResourceId avboitAccumExtinction = deferredGraphResources->avboitAccumExtinction;
    const Core::GpuGraphResourceId refractionDepth = deferredGraphResources->refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = deferredGraphResources->refractionNormalIor;
    const Core::GpuGraphResourceId refractionTintCoverage = deferredGraphResources->refractionTintCoverage;
    const Core::GpuGraphResourceId refractionInstance = deferredGraphResources->refractionInstance;
    const Core::GpuGraphResourceId refractionSpecularRoughness = deferredGraphResources->refractionSpecularRoughness;
    const Core::GpuGraphResourceId refractionResolve = deferredGraphResources->refractionResolve;
    const Core::GpuGraphResourceId avboitForegroundColor = deferredGraphResources->avboitForegroundColor;
    const Core::GpuGraphResourceId avboitForegroundExtinction = deferredGraphResources->avboitForegroundExtinction;

    if(!declareDeferredShadowPrepareTask(
        scratchArena,
        deferredTargets,
        rayTracingShadowResources,
        rayTracingGraphResources,
        currentBindlessSlots,
        materialContextSlots,
        traceGeometry.traceGeometry.data(),
        traceGeometry.traceGeometry.size(),
        traceGeometry.softwareBvhBuildState.data(),
        traceGeometry.softwareBvhBuildState.size(),
        traceGeometry.softwareTraceResourcesPrepared,
        shadowPrepareTimingTicket
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation packet"));
        return;
    }
    const Core::GpuTaskId shadowPrepareHandoffTask = m_deferredShadowPrepareAccelStructFinalizeTask.valid() ? m_deferredShadowPrepareAccelStructFinalizeTask : m_deferredShadowPrepareTask;

    const ECSRenderDetail::MeshViewBufferUpload meshViewUpload = m_meshSystem.prepareMeshViewBufferUpload(meshViewAspectRatio);
    const ECSRenderDetail::MeshViewGpuData& meshViewState = meshViewUpload.viewState;
    const bool meshViewUploadRequired = meshViewUpload.uploadRequired;

    ReflectionSceneContentStamp reflectionContentStamp;
    reflectionContentStamp.view = ComputeFnv64Bytes(&meshViewState, sizeof(meshViewState));
    ObjectGeometryCacheGraph objectGeometry(m_deferredLightingTaskGraph, m_materialSystem, m_meshSystem, scratchArena);
    auto graphicsPrefixResult = declareDeferredGraphicsPrefixTasks(
        scratchArena,
        deferredTargets,
        objectGeometry,
        shadowPrepareHandoffTask,
        csgFrameState,
        frameBindings,
        csgResources,
        hasOpaqueCsgFrameWork,
        meshViewAspectRatio,
        meshViewState,
        meshViewUploadRequired,
        *deferredGraphResources,
        traceGeometry.traceGeometry.data(),
        traceGeometry.traceGeometry.size(),
        traceGeometry.shadowTraceGeometrySet,
        asyncPrefixTiming,
        deferredClearTiming,
        deferredClearTimingState,
        opaqueCsgIntervalClearTimingState,
        opaqueRegularSharedComputeEmulationTiming,
        opaqueCsgIntervalSampleComputeEmulationTiming,
        graphicsPrefixTimingTickets,
        asyncPrefixTimingSpansOnePacket
    );
    if(!graphicsPrefixResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graphics-prefix packet"));
        return;
    }
    reflectionContentStamp.lighting = *graphicsPrefixResult;

    if(!m_raytracingSystem.prepareCausticOpticalReadTracking(meshViewBufferSnapshot)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare accepted optical reader tracking"));
        return;
    }
    const bool causticOpticalReader = declaresHardwareCaustics ? m_raytracingSystem.hasHwCausticWork(meshViewBufferSnapshot) : m_raytracingSystem.hasCausticWork(meshViewBufferSnapshot);

    const Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& causticTraceGeometryResources = declaresHardwareCaustics ? traceGeometry.hardwareTraceGeometry : traceGeometry.softwareTraceGeometry;
    const Core::GpuGraphResourceSetId causticTraceGeometrySet = declaresHardwareCaustics ? traceGeometry.hardwareTraceGeometrySet : traceGeometry.softwareTraceGeometrySet;

    // Freeze shadow routing after prefix handoff so transparent folding never sees the prior mask.
    RayTracingShadowVisibilityGraphPlanSnapshot rayTracingShadowVisibilityPlan = m_raytracingSystem.snapshotShadowVisibilityGraphPlan(declaresHardwareCaustics);
    if(rayTracingShadowVisibilityPlan.lightSpace.ready && hasOpaqueCsgFrameWork){
        if(!csgResources.bindingValid() || !frameBindings.meshView.bindingValid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not bind the current direct-shadow receiver cap state"));
            return;
        }
        rayTracingShadowVisibilityPlan.lightSpace.push.csgFlags |= NWB_CSG_SHADOW_FLAG_CURRENT_RECEIVER_CAPS;
        rayTracingShadowVisibilityPlan.lightSpace.receiverView = frameBindings.meshView.buffer;
        rayTracingShadowVisibilityPlan.lightSpace.push.receiverViewSlot = frameBindings.meshView.heapHandle.slot();
        rayTracingShadowVisibilityPlan.lightSpace.push.receiverCsgSampleStateSlot = csgResources.intervalSampleStateHeapHandle.slot();
        rayTracingShadowVisibilityPlan.lightSpace.receiverCsgSampleState = csgResources.intervalSampleState;
    }
    const RayTracingSceneGraphResources sceneResources = m_raytracingSystem.snapshotSceneGraphResources();
    RayTracingSceneGraphReads sceneReads(scratchArena);
    if(rayTracingShadowVisibilityPlan.hardwareTransparentTrace || (declaresHardwareCaustics && causticOpticalReader)){
        auto sceneReadsResult = ImportRayTracingSceneGraphReads(m_deferredLightingTaskGraph, sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), m_rayTraceMaterialContextSlotsUploadTask, scratchArena);
        if(!sceneReadsResult){
            sceneReads = RayTracingSceneGraphReads(scratchArena);
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared hardware optical scene reads"));
            return;
        }
        sceneReads = Move(*sceneReadsResult);
    }

    Core::GpuGraphResourceId causticOpticalScene;
    if(causticOpticalReader){
        if(declaresHardwareCaustics)
            causticOpticalScene = sceneReads.uses[LengthOf(sceneReads.uses) - 1u].resource;
        else{
            const auto optical = ImportRayTracingOpticalSceneBuffer(m_deferredLightingTaskGraph, sceneResources.opticalScene, m_rayTraceMaterialContextSlotsUploadTask, scratchArena);
            if(!optical){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software caustic optical scene reads"));
                return;
            }
            causticOpticalScene = optical->resource;
        }
    }

    if(useLaggedLightingHistory){
        Core::GpuExternalCompletionDesc lightingHistoryReadReadyDesc;
        lightingHistoryReadReadyDesc.setIdentity(Name("render.deferred_lighting.lagged_history_read_ready")).setMarkerLabel("Lagged Lighting History Read Ready").setToken(laggedLightingHistoryReadReadyToken);
        const auto mDeferredLightingHistoryReadReadyCompletionResult = m_deferredLightingTaskGraph.importExternalCompletion(lightingHistoryReadReadyDesc);
        if(!mDeferredLightingHistoryReadReadyCompletionResult){
            m_deferredLightingHistoryReadReadyCompletion = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history read-ready completion"));
            return;
        }
        m_deferredLightingHistoryReadReadyCompletion = *mDeferredLightingHistoryReadReadyCompletionResult;
    }

    if(features.laggedLightingHistoryWriterWaitPending){
        Core::GpuExternalCompletionDesc lightingHistoryWriterDrainDesc;
        lightingHistoryWriterDrainDesc.setIdentity(Name("render.deferred_lighting.lagged_history_writer_drain")).setMarkerLabel("Lagged Lighting History Writer Drain").setToken(laggedLightingHistoryWriterDrainToken);
        const auto mDeferredLightingHistoryWriterDrainCompletionResult = m_deferredLightingTaskGraph.importExternalCompletion(lightingHistoryWriterDrainDesc);
        if(!mDeferredLightingHistoryWriterDrainCompletionResult){
            m_deferredLightingHistoryWriterDrainCompletion = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history writer-drain completion"));
            return;
        }
        m_deferredLightingHistoryWriterDrainCompletion = *mDeferredLightingHistoryWriterDrainCompletionResult;
    }

    if(m_raytracingSystem.hasSurfelWork() && rayTracingSurfelResources.countReadbackSubmissionToken.valid()){
        Core::GpuExternalCompletionDesc surfelCounterReadbackCompletionDesc;
        surfelCounterReadbackCompletionDesc.setIdentity(Name("render.surfel_gi.counter_readback_complete")).setMarkerLabel("Surfel Counter Readback Complete").setToken(surfelCounterReadbackCompletionToken);
        const auto mDeferredSurfelGiCounterReadbackCompletionResult = m_deferredLightingTaskGraph.importExternalCompletion(surfelCounterReadbackCompletionDesc);
        if(!mDeferredSurfelGiCounterReadbackCompletionResult){
            m_deferredSurfelGiCounterReadbackCompletion = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel counter-readback completion"));
            return;
        }
        m_deferredSurfelGiCounterReadbackCompletion = *mDeferredSurfelGiCounterReadbackCompletionResult;
    }


    // Effects start from the prefix packet; declare Shadow/Software first for stable queue assignment.
    if(!declareDeferredShadowVisibilityTask(
        scratchArena,
        deferredTargets,
        deferredLightingResources,
        rayTracingShadowResources,
        rayTracingGraphResources,
        rayTracingShadowVisibilityPlan,
        sceneReads,
        declaresHardwareCaustics,
        worldPosition,
        normal,
        depth,
        currentShadowVisibility,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace ? traceGeometry.hardwareTraceGeometry.data() : traceGeometry.softwareTraceGeometry.data(),
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace ? traceGeometry.hardwareTraceGeometry.size() : traceGeometry.softwareTraceGeometry.size(),
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace ? traceGeometry.hardwareTraceGeometrySet : traceGeometry.softwareTraceGeometrySet,
        traceGeometry.traceMaterialSampledTextureSet,
        m_graphicsPrefixTask,
        features.laggedLightingHistoryWriterWaitPending ? m_deferredLightingHistoryWriterDrainCompletion : Core::GpuExternalCompletionId{},
        shadowVisibilityTimingTicket,
        shadowVisibilityAsyncTiming,
        shadowVisibilityTiming,
        opaqueSoftResolveTiming,
        transparentSoftResolveTiming,
        shadowVisibilityOpaqueProduced,
        shadowVisibilityTransparentTraceProduced,
        shadowVisibilityOpaqueFrameIndex
    ))
        return;
    if(!declaresHardwareCaustics && !declareDeferredSoftwareCausticsTask(
        scratchArena,
        declaresHardwareCaustics,
        deferredTargets,
        deferredLightingResources,
        rayTracingGraphResources,
        worldPosition,
        depth,
        currentCausticIrradiance,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        causticOpticalScene,
        traceGeometry.softwareTraceGeometry.data(),
        traceGeometry.softwareTraceGeometry.size(),
        traceGeometry.softwareTraceGeometrySet,
        traceGeometry.traceMaterialSampledTextureSet,
        softwareCausticsTimingTicket,
        causticPhotonTiming,
        causticResolveTiming
    ))
        return;
    const Core::GpuTaskId effectsTask = declaresHardwareCaustics ? m_deferredShadowVisibilityTask : m_deferredSoftwareCausticsTask;
    // Declare Surfel GI before hardware/AVBOIT to keep effects -> surfel -> suffix order.
    if(!declareDeferredSurfelGiTask(
        scratchArena,
        deferredTargets,
        deferredLightingResources,
        rayTracingGraphResources,
        rayTracingSurfelResources,
        worldPosition,
        normal,
        currentSurfelIrradiance,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        (rayTracingGraphResources.surfelUsesHardwareTrace ? traceGeometry.hardwareTraceGeometry.data() : traceGeometry.softwareTraceGeometry.data()),
        (rayTracingGraphResources.surfelUsesHardwareTrace ? traceGeometry.hardwareTraceGeometry.size() : traceGeometry.softwareTraceGeometry.size()),
        (rayTracingGraphResources.surfelUsesHardwareTrace ? traceGeometry.hardwareTraceGeometrySet : traceGeometry.softwareTraceGeometrySet),
        traceGeometry.traceMaterialSampledTextureSet,
        effectsTask,
        useLaggedLightingHistory,
        m_deferredSurfelGiCounterReadbackCompletion,
        surfelGiTimingTicket,
        surfelGiAsyncTiming
    ))
        return;


    // Declare Hardware Caustics before Lighting so declaration order owns the live irradiance RAW edge.
    if(declaresHardwareCaustics){
        HardwareCausticsStageBuilder hardwareCausticsStageBuilder(m_deferredLightingTaskGraph, m_raytracingSystem);
        auto hardwareCausticsStageResult = hardwareCausticsStageBuilder.declare(
            scratchArena,
            HardwareCausticsStageInputs{
                .targets = &deferredTargets,
                .lightingResources = &deferredLightingResources,
                .meshViewSnapshot = &meshViewBufferSnapshot,
                .rayTracingResources = &rayTracingGraphResources,
                .features = &features,
                .worldPosition = worldPosition,
                .depth = depth,
                .currentCausticIrradiance = currentCausticIrradiance,
                .currentBindlessSlots = currentBindlessSlots,
                .sceneShading = sceneShading,
                .lights = lights,
                .materialContextSlots = materialContextSlots,
                .opticalScene = causticOpticalScene,
                .hardwareTraceGeometrySet = causticTraceGeometrySet,
                .hardwareTraceGeometryResources = causticTraceGeometryResources.data(),
                .hardwareTraceGeometryResourceCount = causticTraceGeometryResources.size(),
                .traceMaterialSampledTextureSet = traceGeometry.traceMaterialSampledTextureSet,
                .graphicsPrefixTask = m_graphicsPrefixTask,
                .shadowPreparationReady = &m_shadowPreparationOutcome.ready,
                .historyWriterDrainCompletion = &m_deferredLightingHistoryWriterDrainCompletion,
                .accumulatorPersistentState = &m_hardwareCausticAccumulatorPersistentState,
                .declaresHardwareCaustics = declaresHardwareCaustics,
                .producerDispatched = &m_deferredCausticProducerDispatched,
                .timingTicket = &hardwareCausticsTimingTicket,
                .photonTiming = &causticPhotonTiming,
                .resolveTiming = &causticResolveTiming,
            }
        );
        if(!hardwareCausticsStageResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics stage"));
            return;
        }
        m_deferredCausticIrradianceClearTask = hardwareCausticsStageResult->causticIrradianceClearTask;
        m_deferredCausticAccumulatorNonTemporalClearTask = hardwareCausticsStageResult->causticAccumulatorNonTemporalClearTask;
        m_deferredCausticAccumulatorBootstrapClearTask = hardwareCausticsStageResult->causticAccumulatorBootstrapClearTask;
        m_deferredCausticAccumulatorDecayTask = hardwareCausticsStageResult->causticAccumulatorDecayTask;
        m_deferredCausticPhotonTask = hardwareCausticsStageResult->causticPhotonTask;
        m_deferredCausticGeometryTask = hardwareCausticsStageResult->causticGeometryTask;
        m_deferredCausticResolvePrepareTask = hardwareCausticsStageResult->causticResolvePrepareTask;
        m_deferredCausticResolveWaveletTask = hardwareCausticsStageResult->causticResolveWaveletTask;
        m_deferredCausticResolveSecondWaveletTask = hardwareCausticsStageResult->causticResolveSecondWaveletTask;
        m_deferredCausticResolveThirdWaveletTask = hardwareCausticsStageResult->causticResolveThirdWaveletTask;
        m_deferredCausticResolveFourthWaveletTask = hardwareCausticsStageResult->causticResolveFourthWaveletTask;
        m_deferredCausticResolveFifthWaveletTask = hardwareCausticsStageResult->causticResolveFifthWaveletTask;
        m_deferredCausticResolveUpsampleTask = hardwareCausticsStageResult->causticResolveUpsampleTask;
        m_deferredHardwareCausticsTask = hardwareCausticsStageResult->hardwareCausticsTask;
    }

    const bool opticalCaptureRequested = hasTransparentRenderers && (m_refractionEnabled || m_reflectionSettings.traceMode != ReflectionTraceMode::Disabled);
    const bool refractionActive = m_preparedRefractionActive && opticalCaptureRequested;
    RayTracingRefractionGraphResources refractionResources = m_preparedRefractionResources;
    FrameGraphAvboitStage avboitStage(m_deferredLightingTaskGraph, m_arena, m_graphics, m_materialSystem, m_csgSystem, m_avboitSystem, m_deferredTaskTimingFeedback);
    const auto avboitStageResult = avboitStage.declare(
        traceGeometryScratchArena,
        FrameGraphAvboitStageInputs{
            .targets = deferredTargets,
            .resources = *deferredGraphResources,
            .csgFrameState = csgFrameState,
            .frameBindings = frameBindings,
            .csgResources = csgResources,
            .meshViewState = meshViewState,
            .prefixTask = m_graphicsPrefixTask,
            .hasTransparentRenderers = hasTransparentRenderers,
            .clearTargets = clearAvboitTargets,
            .refractionCaptureEnabled = refractionActive && refractionResources.valid(),
        },
        FrameGraphAvboitStageTiming{
            .preTimingTicket = avboitPreTimingTicket,
            .depthWarpTimingTicket = avboitDepthWarpTimingTicket,
            .extinctionTimingTicket = avboitExtinctionTimingTicket,
            .integrationTimingTicket = avboitIntegrationTimingTicket,
            .accumulationTimingTicket = avboitAccumulationTimingTicket,
            .clearTimingState = avboitClearTimingState,
            .transparentCsgIntervalClearTimingState = transparentCsgIntervalClearTimingState,
            .transparentCsgIntervalsTiming = transparentCsgIntervalsTiming,
            .occupancyComputeEmulationTiming = avboitOccupancyComputeEmulationTiming,
            .extinctionComputeEmulationTiming = avboitExtinctionComputeEmulationTiming,
            .accumulationComputeEmulationTiming = avboitAccumulationComputeEmulationTiming,
        },
        objectGeometry
    );
    if(!avboitStageResult)
        return;
    const Core::GpuTaskId avboitFinalTask = *avboitStageResult;

    DeferredLightingStageBuilder deferredLightingStageBuilder(m_deferredLightingTaskGraph, m_deferredSystem);
    auto deferredLightingStageResult = deferredLightingStageBuilder.declare(
        DeferredLightingStageInputs{
            .targets = deferredTargets,
            .albedo = albedo,
            .normal = normal,
            .worldPosition = worldPosition,
            .depth = depth,
            .shadowVisibility = shadowVisibility,
            .causticIrradiance = causticIrradiance,
            .surfelIrradiance = surfelIrradiance,
            .sceneShading = sceneShading,
            .lights = lights,
            .bindlessSlots = bindlessSlots,
            .opaqueColor = opaqueColor,
            .graphicsPrefixTask = m_graphicsPrefixTask,
            .shadowVisibilityTask = m_deferredShadowVisibilityTask,
            .surfelGiTask = m_deferredSurfelGiTask,
            .avboitFinalTask = avboitFinalTask,
            .hardwareCausticsTask = m_deferredHardwareCausticsTask,
            .softwareCausticsTask = m_deferredSoftwareCausticsTask,
            .historyReadReadyCompletion = m_deferredLightingHistoryReadReadyCompletion,
            .history = history,
            .useLaggedLightingHistory = useLaggedLightingHistory,
            .declaresHardwareCaustics = declaresHardwareCaustics,
            .hasTransparentRenderers = hasTransparentRenderers,
        },
        lightingTimingTicket
    );
    if(!deferredLightingStageResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }
    m_deferredLaggedLightingHistorySlotsUploadTask = deferredLightingStageResult->historySlotsUploadTask;
    m_deferredLightingTask = deferredLightingStageResult->lightingTask;


    FrameGraphReflectionResolve reflectionResolve(m_deferredLightingTaskGraph, m_graphics, m_raytracingSystem, m_reflectionSystem);
    auto reflectionResolveResult = reflectionResolve.declare(
        FrameGraphReflectionResolveInputs{
            .targets = deferredTargets,
            .meshViewSnapshot = meshViewBufferSnapshot,
            .sceneResources = sceneResources,
            .reflectionSettings = m_reflectionSettings,
            .contentStamp = reflectionContentStamp,
            .csgFrameState = csgFrameState,
            .refractionResources = refractionResources,
            .reflectionFrameIndex = m_reflectionFrameIndex,
            .reflectionSceneAvailable = m_preparedReflectionSceneAvailable,
            .refractionActive = refractionActive,
            .useLaggedLightingHistory = useLaggedLightingHistory,
            .specularRoughness = specularRoughness,
            .refractionSpecularRoughness = refractionSpecularRoughness,
            .normal = normal,
            .depth = depth,
            .worldPosition = worldPosition,
            .refractionDepth = refractionDepth,
            .refractionNormalIor = refractionNormalIor,
            .refractionTintCoverage = refractionTintCoverage,
            .refractionInstance = refractionInstance,
            .refractionResolve = refractionResolve,
            .opaqueColor = opaqueColor,
            .meshView = meshView,
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .avboitAccumColor = avboitAccumColor,
            .avboitAccumExtinction = avboitAccumExtinction,
            .avboitForegroundColor = avboitForegroundColor,
            .avboitForegroundExtinction = avboitForegroundExtinction,
            .hardwareTraceGeometrySet = traceGeometry.hardwareTraceGeometrySet,
            .traceMaterialSampledTextureSet = traceGeometry.traceMaterialSampledTextureSet,
            .traceContextUploadTask = m_rayTraceMaterialContextSlotsUploadTask,
            .lightingTask = m_deferredLightingTask,
            .avboitFinalTask = avboitFinalTask,
            .surfelGiTask = m_deferredSurfelGiTask,
            .hardwarePreparationReady = &m_shadowPreparationOutcome.ready,
            .hardwareDispatchLogged = &m_reflectionHardwareLogged,
            .fallbackDispatchLogged = &m_reflectionFallbackLogged,
            .refractionHardwareLogged = &m_refractionHardwareLogged,
            .refractionScreenLogged = &m_refractionScreenLogged,
        },
        sceneReads,
        scratchArena
    );
    if(!reflectionResolveResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare reflection graph tasks"));
        return;
    }
    const ReflectionGraphResult reflectionGraph = reflectionResolveResult->reflectionGraph;
    const ReflectionCompositeInputs reflectionCompositeInputs = reflectionResolveResult->reflectionCompositeInputs;
    const Core::GpuTaskId refractionResolveTask = reflectionResolveResult->refractionResolveTask;
    refractionResources.opaqueReflectionSlot = reflectionCompositeInputs.opaqueRadianceSlot;

    DeferredGraphSuffixBuilder suffixBuilder(m_deferredLightingTaskGraph, m_deferredSystem, m_graphics, m_preparedTaskGraphPresentationContributor, m_preparedTaskGraphOutputLayerContributor);
    auto suffixResult = suffixBuilder.declare(
        DeferredGraphSuffixInputs{
            .targets = deferredTargets,
            .opaqueColor = opaqueColor,
            .avboitAccumColor = avboitAccumColor,
            .avboitAccumExtinction = avboitAccumExtinction,
            .avboitForegroundColor = avboitForegroundColor,
            .avboitForegroundExtinction = avboitForegroundExtinction,
            .refractionResolve = refractionResolve,
            .refractionDepth = refractionDepth,
            .currentBindlessSlots = currentBindlessSlots,
            .reflectionGraph = reflectionGraph,
            .reflectionCompositeInputs = reflectionCompositeInputs,
            .useLaggedLightingHistory = useLaggedLightingHistory,
            .lightingTask = m_deferredLightingTask,
            .avboitFinalTask = avboitFinalTask,
            .refractionResolveTask = refractionResolveTask,
            .surfelGiTask = m_deferredSurfelGiTask,
            .hardwareCausticsTask = declaresHardwareCaustics ? m_deferredHardwareCausticsTask : Core::GpuTaskId{},
            .presentationFrame = presentationFrame,
            .presentationFramebufferDesc = presentationFramebufferDesc,
        },
        deferredTargets,
        reflectionCompositeInputs,
        compositeTimingTicket,
        presentTimingTicket,
        asyncFinalTiming,
        m_deferredShadowVisibilityTask,
        frameTimingTransaction
    );
    if(!suffixResult)
        return;
    m_deferredCompositeTask = suffixResult->compositeTask;
    m_deferredPresentationOverlayRequired = suffixResult->overlayRequired;
    m_deferredPresentationOverlayTask = suffixResult->overlayTask;
    m_deferredPresentTask = suffixResult->presentTask;
    m_deferredFrameTimingEndTask = suffixResult->frameTimingEndTask;

    declareDeferredSurfelCountReadbackTask(rayTracingSurfelResources);

    DeferredFrameTailBuilder deferredFrameTailBuilder(m_deferredLightingTaskGraph);
    auto deferredFrameTailResult = deferredFrameTailBuilder.declare(
        DeferredFrameTailInputs{
            .frameTimingTransaction = frameTimingTransaction,
            .historyCopySubmissionToken = m_laggedLightingHistorySubmissionToken,
            .recoveryArmed = m_deferredFrameRecoveryArmed,
            .recoveryRetiresFrameTiming = m_deferredFrameRecoveryRetiresTiming,
            .terminalPresentationTask = m_deferredFrameTimingEndTask,
            .historyCopyShadowVisibility = historyCopyShadowVisibility,
            .historyCopyCausticIrradiance = historyCopyCausticIrradiance,
            .historyCopySurfelIrradiance = historyCopySurfelIrradiance,
            .historyCopyDestinationShadowVisibility = historyCopyDestinationShadowVisibility,
            .historyCopyDestinationCausticIrradiance = historyCopyDestinationCausticIrradiance,
            .historyCopyDestinationSurfelIrradiance = historyCopyDestinationSurfelIrradiance,
            .capturesLaggedLightingHistory = capturesLaggedLightingHistory,
        }
    );
    if(!deferredFrameTailResult)
        return;
    m_deferredLaggedLightingHistoryTask = deferredFrameTailResult->historyCopyTask;
    m_deferredFrameRecoveryTask = deferredFrameTailResult->recoveryTask;

    m_deferredLightingTaskGraphDeclarationSeconds = DurationInSeconds<f64>(TimerNow(), declarationBegin);
    m_deferredLightingTaskGraphDeclared = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

