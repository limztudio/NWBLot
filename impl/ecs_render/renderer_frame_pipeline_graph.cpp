// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/reflection/scene_content_stamp.h>

#include <global/hash_utils.h>

#include <impl/ecs_render/raytrace/task_graph_post_gbuffer_normalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_finalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_visibility_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_refraction_resolve.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>
#include <impl/ecs_render/avboit/task_graph_refraction_capture.h>
#include <impl/ecs_render/avboit/generated_geometry_reuse.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/task/gpu/scheduler.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_frame_recovery_task.h>
#include <impl/ecs_render/kernel/task_graph_frame_timing_end_task.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
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
#include <impl/ecs_render/graph/frame_graph_transparent_csg_tasks.h>
#include <impl/ecs_render/graph/frame_graph_avboit_occupancy.h>
#include <impl/ecs_render/graph/frame_graph_avboit_extinction.h>
#include <impl/ecs_render/graph/frame_graph_avboit_accumulation.h>
#include <impl/ecs_render/graph/frame_graph_reflection_resolve.h>
#include <impl/ecs_render/avboit/avboit_pass_upload_helper.h>
#include <impl/ecs_render/avboit/material_upload_builder.h>
#include <impl/ecs_render/avboit/geometry_preparation_builder.h>
#include <impl/ecs_render/avboit/compute_emulation_capture.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererFramePipeline::buildDeferredLightingTaskGraph(
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
    const bool* const asyncPrefixTimingSpansOnePacket,
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

    const RayTracingShadowPreparationResourceSnapshot rayTracingShadowResources =
        m_raytracingSystem.snapshotShadowPreparationResources()
    ;
    const RayTracingDeferredGraphResourceSnapshot rayTracingGraphResources =
        m_raytracingSystem.snapshotDeferredGraphResources()
    ;
    const RayTracingSurfelPersistentResourceSnapshot rayTracingSurfelResources =
        m_raytracingSystem.snapshotSurfelPersistentResources()
    ;

    m_deferredBindlessSlotsUploadTask = {};
    m_rayTraceMaterialContextSlotsUploadTask = {};
    m_causticEmissionTargetsUploadTask = {};
    m_surfelFrameConstantsUploadTask = {};
    m_shadowInstanceMaterialUploadTask = {};
    m_shadowInstanceUploadTask = {};
    m_shadowMaterialTypedUploadTask = {};
    m_sceneBvhNodesUploadTask = {};
    m_sceneBvhInstancesUploadTask = {};
    m_deferredLaggedLightingHistorySlotsUploadTask = {};
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

    const auto& device = m_graphics.getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const bool useLaggedLightingHistory = dedicatedAsyncCompute
        && features.frameLaggedAsyncLightingEnabled
        && features.laggedLightingHistoryReady
        && features.laggedLightingHistoryReadReady
    ;
    const bool declaresHardwareCaustics = features.hardwareCaustics;
    const bool capturesLaggedLightingHistory = includeLaggedLightingHistoryCapture
        && dedicatedAsyncCompute
        && features.frameLaggedAsyncLightingEnabled
    ;
    const DeferredLaggedLightingHistoryResources* const history = useLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    const DeferredLaggedLightingHistoryResources* const captureHistory = capturesLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    if(!presentationFrame.valid())
        return;
    const Core::Framebuffer& presentationFramebuffer = *presentationFrame.framebuffer;
    const Core::FramebufferDesc& presentationFramebufferDesc = presentationFramebuffer.getDescription();
    const DeferredLightingGraphResources deferredLightingResources = m_deferredSystem.lightingGraphResources();
    const ECSRenderDetail::MeshFrameBindingSnapshot frameBindings = m_meshSystem.meshFrameBindingSnapshot();
    const ECSRenderDetail::CsgGraphResourceSnapshot csgResources = m_csgSystem.csgGraphResourceSnapshot();
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

    // Preflight froze the mesh tables; import each buffer once and fan out IDs.
    Core::Alloc::ScratchArena traceGeometryScratchArena(RendererArenaScope::s_TaskGraphArena);
    if(!m_raytracingSystem.freezePreparedShadowTraceGeometryBuffers(traceGeometryScratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow-trace geometry buffers"));
        return;
    }
    const PreparedShadowTraceGeometryBufferVector& preparedTraceGeometry =
        m_raytracingSystem.preparedShadowTraceGeometryBuffers()
    ;
    const PreparedShadowTraceMaterialSampledTextureVector& preparedTraceMaterialSampledTextures =
        m_raytracingSystem.preparedShadowTraceMaterialSampledTextures()
    ;
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> traceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceAttributeResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareTraceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> traceMaterialSampledTextureResources{
        traceGeometryScratchArena
    };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareBvhBuildStateResources{ traceGeometryScratchArena };
    traceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceAttributeResources.reserve(preparedTraceGeometry.size());
    softwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    traceMaterialSampledTextureResources.reserve(preparedTraceMaterialSampledTextures.size());
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        Core::GpuGraphResourceDesc desc = BufferResourceDesc(preparedBuffer.identity, "Prepared Shadow Trace Geometry");
        desc.setInitialState(preparedBuffer.initialState);
        const Core::GpuGraphResourceId resource = m_deferredLightingTaskGraph.importBuffer(preparedBuffer.buffer, desc);
        if(!resource.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-trace geometry buffer"));
            return;
        }
        traceGeometryResources.push_back(resource);
        if(preparedBuffer.roles & (
            PreparedShadowTraceGeometryRole::HardwarePosition
            | PreparedShadowTraceGeometryRole::HardwareIndex
            | PreparedShadowTraceGeometryRole::HardwareAttribute
        ))
            hardwareTraceGeometryResources.push_back(resource);
        if(preparedBuffer.roles & PreparedShadowTraceGeometryRole::HardwareAttribute)
            hardwareTraceAttributeResources.push_back(resource);
        if(preparedBuffer.roles & (
            PreparedShadowTraceGeometryRole::SoftwareNode
            | PreparedShadowTraceGeometryRole::SoftwarePosition
            | PreparedShadowTraceGeometryRole::SoftwareIndex
            | PreparedShadowTraceGeometryRole::SoftwareAttribute
        ))
            softwareTraceGeometryResources.push_back(resource);
    }
    // Trace dispatchers select textures via frozen material context; reuse typed preflight import when owned.
    switch(ImportMaterialSampledTextureResources(
        m_deferredLightingTaskGraph,
        preparedTraceMaterialSampledTextures.data(),
        preparedTraceMaterialSampledTextures.size(),
        "Prepared Trace Material Sampled Texture",
        traceMaterialSampledTextureResources
    )){
    case SampledTextureImportResult::Success:
        break;
    case SampledTextureImportResult::GraphUnavailable:
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared trace material texture graph was unavailable"));
        return;
    case SampledTextureImportResult::MissingIdentity:
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared trace material texture has no stable identity"));
        return;
    case SampledTextureImportResult::ImportFailed:
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared trace material sampled texture"));
        return;
    }
    Core::GpuGraphResourceSetId shadowTraceGeometrySet;
    if(!traceGeometryResources.empty()){
        shadowTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.post_gbuffer_trace_geometry"))
                .setMarkerLabel("Post-G-Buffer Trace Geometry")
                .setMembers(traceGeometryResources.data(), traceGeometryResources.size())
        );
    }
    Core::GpuGraphResourceSetId softwareTraceGeometrySet;
    if(!softwareTraceGeometryResources.empty()){
        softwareTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.software_trace_geometry"))
                .setMarkerLabel("Software Trace Geometry")
                .setMembers(softwareTraceGeometryResources.data(), softwareTraceGeometryResources.size())
        );
    }
    Core::GpuGraphResourceSetId traceMaterialSampledTextureSet;
    if(!traceMaterialSampledTextureResources.empty()){
        traceMaterialSampledTextureSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.trace_material_sampled_textures"))
                .setMarkerLabel("Trace Material Sampled Textures")
                .setMembers(
                    traceMaterialSampledTextureResources.data(),
                    traceMaterialSampledTextureResources.size()
                )
        );
        if(!traceMaterialSampledTextureSet.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared trace material sampled textures"));
            return;
        }
    }
    Core::GpuGraphResourceSetId hardwareTraceGeometrySet;
    if(!hardwareTraceGeometryResources.empty()){
        hardwareTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.hardware_trace_geometry"))
                .setMarkerLabel("Hardware Trace Geometry")
                .setMembers(hardwareTraceGeometryResources.data(), hardwareTraceGeometryResources.size())
        );
    }
    Core::GpuGraphResourceSetId hardwareTraceAttributeSet;
    if(!hardwareTraceAttributeResources.empty()){
        hardwareTraceAttributeSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.hardware_trace_attributes"))
                .setMarkerLabel("Hardware Trace Attributes")
                .setMembers(hardwareTraceAttributeResources.data(), hardwareTraceAttributeResources.size())
        );
    }
    bool softwareTraceResourcesPrepared = false;
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        if(preparedBuffer.roles & (
            PreparedShadowTraceGeometryRole::SoftwareNode
            | PreparedShadowTraceGeometryRole::SoftwarePosition
            | PreparedShadowTraceGeometryRole::SoftwareIndex
            | PreparedShadowTraceGeometryRole::SoftwareAttribute
        )){
            softwareTraceResourcesPrepared = true;
            break;
        }
    }
    FrameGraphSoftwareBvhBuildStateImporter softwareBvhBuildStateImporter(
        m_deferredLightingTaskGraph,
        m_meshSystem
    );
    FrameGraphSoftwareBvhBuildStateResult softwareBvhBuildStateResult;
    if(!softwareBvhBuildStateImporter.declare(
        FrameGraphSoftwareBvhBuildStateInputs{
            .rayTracingShadowResources = &rayTracingShadowResources,
            .softwareTraceResourcesPrepared = softwareTraceResourcesPrepared,
        },
        traceGeometryScratchArena,
        softwareBvhBuildStateResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared software BVH build state"));
        return;
    }
    softwareBvhBuildStateResources = Move(softwareBvhBuildStateResult.buildStateResources);
    DeferredGraphResourceImportBuilder deferredGraphResourceImportBuilder(
        m_deferredLightingTaskGraph
    );
    DeferredGraphResourceImportResult deferredGraphResources;
    if(!deferredGraphResourceImportBuilder.declare(
        DeferredGraphResourceImportInputs{
            .targets = &deferredTargets,
            .lightingResources = &deferredLightingResources,
            .frameBindings = &frameBindings,
            .meshViewSnapshot = &meshViewBufferSnapshot,
            .csgResources = &csgResources,
            .rayTracingResources = &rayTracingGraphResources,
            .history = history,
            .captureHistory = captureHistory,
            .clearAvboitTargets = clearAvboitTargets,
            .capturesLaggedLightingHistory = capturesLaggedLightingHistory,
        },
        deferredGraphResources
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }
    const Core::GpuGraphResourceId albedo = deferredGraphResources.albedo;
    const Core::GpuGraphResourceId normal = deferredGraphResources.normal;
    const Core::GpuGraphResourceId worldPosition = deferredGraphResources.worldPosition;
    const Core::GpuGraphResourceId specularRoughness = deferredGraphResources.specularRoughness;
    const Core::GpuGraphResourceId depth = deferredGraphResources.depth;
    const Core::GpuGraphResourceId csgCapBackNormal = deferredGraphResources.csgCapBackNormal;
    const Core::GpuGraphResourceId csgIntervalDepth = deferredGraphResources.csgIntervalDepth;
    const Core::GpuGraphResourceId csgIntervalId = deferredGraphResources.csgIntervalId;
    const Core::GpuGraphResourceId csgReceiverEventData = deferredGraphResources.csgReceiverEventData;
    const Core::GpuGraphResourceId csgReceiverEventCount = deferredGraphResources.csgReceiverEventCount;
    const Core::GpuGraphResourceId csgReceiverSpanData = deferredGraphResources.csgReceiverSpanData;
    const Core::GpuGraphResourceId csgReceiverSpanCount = deferredGraphResources.csgReceiverSpanCount;
    const Core::GpuGraphResourceId csgRemovedIntervalDepth = deferredGraphResources.csgRemovedIntervalDepth;
    const Core::GpuGraphResourceId csgRemovedIntervalCapNormal = deferredGraphResources.csgRemovedIntervalCapNormal;
    const Core::GpuGraphResourceId csgRemovedIntervalData = deferredGraphResources.csgRemovedIntervalData;
    const Core::GpuGraphResourceId csgRemovedIntervalCount = deferredGraphResources.csgRemovedIntervalCount;
    const Core::GpuGraphResourceId shadowVisibility = deferredGraphResources.shadowVisibility;
    const Core::GpuGraphResourceId causticIrradiance = deferredGraphResources.causticIrradiance;
    const Core::GpuGraphResourceId surfelIrradiance = deferredGraphResources.surfelIrradiance;
    const Core::GpuGraphResourceId currentShadowVisibility = deferredGraphResources.currentShadowVisibility;
    const Core::GpuGraphResourceId currentCausticIrradiance = deferredGraphResources.currentCausticIrradiance;
    const Core::GpuGraphResourceId currentSurfelIrradiance = deferredGraphResources.currentSurfelIrradiance;
    const Core::GpuGraphResourceId opaqueColor = deferredGraphResources.opaqueColor;
    const Core::GpuGraphResourceId sceneShading = deferredGraphResources.sceneShading;
    const Core::GpuGraphResourceId lights = deferredGraphResources.lights;
    const Core::GpuGraphResourceId meshView = deferredGraphResources.meshView;
    const Core::GpuGraphResourceId materialInstances = deferredGraphResources.materialInstances;
    const Core::GpuGraphResourceId materialTyped = deferredGraphResources.materialTyped;
    const Core::GpuGraphResourceId csgReceiverRanges = deferredGraphResources.csgReceiverRanges;
    const Core::GpuGraphResourceId csgCutters = deferredGraphResources.csgCutters;
    const Core::GpuGraphResourceId csgClipContextSlots = deferredGraphResources.csgClipContextSlots;
    const Core::GpuGraphResourceId csgIntervalSampleState = deferredGraphResources.csgIntervalSampleState;
    const Core::GpuGraphResourceId bindlessSlots = deferredGraphResources.bindlessSlots;
    const Core::GpuGraphResourceId currentBindlessSlots = deferredGraphResources.currentBindlessSlots;
    const Core::GpuGraphResourceId materialContextSlots = deferredGraphResources.materialContextSlots;
    const Core::GpuGraphResourceId historyCopyShadowVisibility = deferredGraphResources.historyCopyShadowVisibility;
    const Core::GpuGraphResourceId historyCopyCausticIrradiance = deferredGraphResources.historyCopyCausticIrradiance;
    const Core::GpuGraphResourceId historyCopySurfelIrradiance = deferredGraphResources.historyCopySurfelIrradiance;
    const Core::GpuGraphResourceId historyCopyDestinationShadowVisibility = deferredGraphResources.historyCopyDestinationShadowVisibility;
    const Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance = deferredGraphResources.historyCopyDestinationCausticIrradiance;
    const Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance = deferredGraphResources.historyCopyDestinationSurfelIrradiance;
    const Core::GpuGraphResourceId avboitLowRaster = deferredGraphResources.avboitLowRaster;
    const Core::GpuGraphResourceId avboitAccumColor = deferredGraphResources.avboitAccumColor;
    const Core::GpuGraphResourceId avboitAccumExtinction = deferredGraphResources.avboitAccumExtinction;
    const Core::GpuGraphResourceId refractionDepth = deferredGraphResources.refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = deferredGraphResources.refractionNormalIor;
    const Core::GpuGraphResourceId refractionTintCoverage = deferredGraphResources.refractionTintCoverage;
    const Core::GpuGraphResourceId refractionInstance = deferredGraphResources.refractionInstance;
    const Core::GpuGraphResourceId refractionSpecularRoughness = deferredGraphResources.refractionSpecularRoughness;
    const Core::GpuGraphResourceId refractionResolve = deferredGraphResources.refractionResolve;
    const Core::GpuGraphResourceId avboitForegroundColor = deferredGraphResources.avboitForegroundColor;
    const Core::GpuGraphResourceId avboitForegroundExtinction = deferredGraphResources.avboitForegroundExtinction;
    const Core::GpuGraphResourceId avboitTransmittance = deferredGraphResources.avboitTransmittance;
    const Core::GpuGraphResourceId avboitCoverage = deferredGraphResources.avboitCoverage;
    const Core::GpuGraphResourceId avboitDepthWarp = deferredGraphResources.avboitDepthWarp;
    const Core::GpuGraphResourceId avboitControl = deferredGraphResources.avboitControl;
    const Core::GpuGraphResourceId avboitExtinction = deferredGraphResources.avboitExtinction;
    const Core::GpuGraphResourceId avboitExtinctionOverflow = deferredGraphResources.avboitExtinctionOverflow;
    const Core::GpuGraphResourceId avboitMaterialDomain = deferredGraphResources.avboitMaterialDomain;
    const Core::GpuGraphResourceId avboitCsgDomain = deferredGraphResources.avboitCsgDomain;
    const Core::TextureSubresourceSet csgPeelSubresources = deferredGraphResources.csgPeelSubresources;
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources = deferredGraphResources.csgReceiverEventDataSubresources;
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources = deferredGraphResources.csgReceiverEventCountSubresources;
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources = deferredGraphResources.csgReceiverSpanDataSubresources;
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources = deferredGraphResources.csgReceiverSpanCountSubresources;
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources = deferredGraphResources.csgRemovedIntervalSubresources;
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources = deferredGraphResources.csgRemovedIntervalCountSubresources;

    if(!declareDeferredShadowPrepareTask(
        deferredTargets,
        rayTracingShadowResources,
        rayTracingGraphResources,
        currentBindlessSlots,
        materialContextSlots,
        traceGeometryResources.data(),
        traceGeometryResources.size(),
        softwareBvhBuildStateResources.data(),
        softwareBvhBuildStateResources.size(),
        softwareTraceResourcesPrepared,
        frameTimingTransaction,
        shadowPrepareTimingTicket
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation packet"));
        return;
    }
    const Core::GpuTaskId shadowPrepareHandoffTask = m_deferredShadowPrepareAccelStructFinalizeTask.valid()
        ? m_deferredShadowPrepareAccelStructFinalizeTask
        : m_deferredShadowPrepareTask
    ;

    ECSRenderDetail::MeshViewGpuData meshViewState;
    bool meshViewUploadRequired = false;
    if(!m_meshSystem.prepareMeshViewBufferUpload(
        meshViewAspectRatio,
        meshViewState,
        meshViewUploadRequired
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare immutable mesh-view upload data"));
        return;
    }

    ReflectionSceneContentStamp reflectionContentStamp;
    reflectionContentStamp.view = ComputeFnv64Bytes(&meshViewState, sizeof(meshViewState));
    Core::Alloc::ScratchArena objectGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    ObjectGeometryCacheGraph objectGeometry(m_deferredLightingTaskGraph, m_materialSystem, m_meshSystem, objectGeometryScratch);
    if(!declareDeferredGraphicsPrefixTasks(
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
        albedo,
        normal,
        worldPosition,
        specularRoughness,
        depth,
        opaqueColor,
        sceneShading,
        lights,
        meshView,
        materialInstances,
        materialTyped,
        csgReceiverRanges,
        csgCutters,
        csgClipContextSlots,
        csgIntervalSampleState,
        csgCapBackNormal,
        csgIntervalDepth,
        csgIntervalId,
        csgReceiverEventData,
        csgReceiverEventCount,
        csgReceiverSpanData,
        csgReceiverSpanCount,
        csgRemovedIntervalDepth,
        csgRemovedIntervalCapNormal,
        csgRemovedIntervalData,
        csgRemovedIntervalCount,
        currentBindlessSlots,
        materialContextSlots,
        traceGeometryResources.data(),
        traceGeometryResources.size(),
        shadowTraceGeometrySet,
        asyncPrefixTiming,
        deferredClearTiming,
        deferredClearTimingState,
        opaqueCsgIntervalClearTimingState,
        opaqueRegularSharedComputeEmulationTiming,
        opaqueCsgIntervalSampleComputeEmulationTiming,
        graphicsPrefixTimingTickets,
        asyncPrefixTimingSpansOnePacket,
        reflectionContentStamp.lighting
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graphics-prefix packet"));
        return;
    }

    // Freeze shadow routing after prefix handoff so transparent folding never sees the prior mask.
    const RayTracingShadowVisibilityGraphPlanSnapshot rayTracingShadowVisibilityPlan =
        m_raytracingSystem.snapshotShadowVisibilityGraphPlan(declaresHardwareCaustics)
    ;
    const RayTracingSceneGraphResources sceneResources = m_raytracingSystem.snapshotSceneGraphResources();
    RayTracingSceneGraphReads sceneReads;
    if(rayTracingShadowVisibilityPlan.hardwareTransparentTrace){
        sceneReads = ImportRayTracingSceneGraphReads(
            m_deferredLightingTaskGraph, sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), traceGeometryScratchArena
        );
        if(!sceneReads.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import hardware transparent shadow scene reads"));
            return;
        }
    }

    if(useLaggedLightingHistory){
        Core::GpuExternalCompletionDesc lightingHistoryReadReadyDesc;
        lightingHistoryReadReadyDesc
            .setIdentity(Name("render.deferred_lighting.lagged_history_read_ready"))
            .setMarkerLabel("Lagged Lighting History Read Ready")
            .setToken(laggedLightingHistoryReadReadyToken)
        ;
        m_deferredLightingHistoryReadReadyCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
            lightingHistoryReadReadyDesc
        );
        if(!m_deferredLightingHistoryReadReadyCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history read-ready completion"));
            return;
        }
    }

    if(features.laggedLightingHistoryWriterWaitPending){
        Core::GpuExternalCompletionDesc lightingHistoryWriterDrainDesc;
        lightingHistoryWriterDrainDesc
            .setIdentity(Name("render.deferred_lighting.lagged_history_writer_drain"))
            .setMarkerLabel("Lagged Lighting History Writer Drain")
            .setToken(laggedLightingHistoryWriterDrainToken)
        ;
        m_deferredLightingHistoryWriterDrainCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
            lightingHistoryWriterDrainDesc
        );
        if(!m_deferredLightingHistoryWriterDrainCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history writer-drain completion"));
            return;
        }
    }

    if(
        m_raytracingSystem.hasSurfelWork()
        && rayTracingSurfelResources.countReadbackSubmissionToken.valid()
    ){
        Core::GpuExternalCompletionDesc surfelCounterReadbackCompletionDesc;
        surfelCounterReadbackCompletionDesc
            .setIdentity(Name("render.surfel_gi.counter_readback_complete"))
            .setMarkerLabel("Surfel Counter Readback Complete")
            .setToken(surfelCounterReadbackCompletionToken)
        ;
        m_deferredSurfelGiCounterReadbackCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
            surfelCounterReadbackCompletionDesc
        );
        if(!m_deferredSurfelGiCounterReadbackCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel counter-readback completion"));
            return;
        }
    }


    // Effects start from the prefix packet; declare Shadow/Software first for stable queue assignment.
    if(!declareDeferredShadowVisibilityTask(
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
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace ? hardwareTraceGeometryResources.data() : softwareTraceGeometryResources.data(),
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace ? hardwareTraceGeometryResources.size() : softwareTraceGeometryResources.size(),
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace ? hardwareTraceGeometrySet : softwareTraceGeometrySet,
        traceMaterialSampledTextureSet,
        m_graphicsPrefixTask,
        features.laggedLightingHistoryWriterWaitPending
            ? m_deferredLightingHistoryWriterDrainCompletion
            : Core::GpuExternalCompletionId{},
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
        softwareTraceGeometryResources.data(),
        softwareTraceGeometryResources.size(),
        softwareTraceGeometrySet,
        traceMaterialSampledTextureSet,
        softwareCausticsTimingTicket,
        causticPhotonTiming,
        causticResolveTiming
    ))
        return;
    const Core::GpuTaskId effectsTask = declaresHardwareCaustics
        ? m_deferredShadowVisibilityTask
        : m_deferredSoftwareCausticsTask
    ;
    // Declare Surfel GI before hardware/AVBOIT to keep effects -> surfel -> suffix order.
    if(!declareDeferredSurfelGiTask(
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
        (
            rayTracingGraphResources.surfelUsesHardwareTrace
                ? hardwareTraceGeometryResources.data()
                : softwareTraceGeometryResources.data()
        ),
        (
            rayTracingGraphResources.surfelUsesHardwareTrace
                ? hardwareTraceGeometryResources.size()
                : softwareTraceGeometryResources.size()
        ),
        (
            rayTracingGraphResources.surfelUsesHardwareTrace
                ? hardwareTraceGeometrySet
                : softwareTraceGeometrySet
        ),
        traceMaterialSampledTextureSet,
        effectsTask,
        m_deferredSurfelGiCounterReadbackCompletion,
        surfelGiTimingTicket,
        surfelGiAsyncTiming
    ))
        return;


    // Declare Hardware Caustics before Lighting so declaration order owns the live irradiance RAW edge.
    if(declaresHardwareCaustics){
        HardwareCausticsStageBuilder hardwareCausticsStageBuilder(
            m_deferredLightingTaskGraph,
            m_raytracingSystem
        );
        HardwareCausticsStageResult hardwareCausticsStageResult;
        if(!hardwareCausticsStageBuilder.declare(
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
                .hardwareTraceAttributeSet = hardwareTraceAttributeSet,
                .hardwareTraceAttributeResources = hardwareTraceAttributeResources.data(),
                .hardwareTraceAttributeResourceCount = hardwareTraceAttributeResources.size(),
                .traceMaterialSampledTextureSet = traceMaterialSampledTextureSet,
                .graphicsPrefixTask = m_graphicsPrefixTask,
                .shadowPreparationReady = &m_shadowPreparationOutcome.ready,
                .historyWriterDrainCompletion = &m_deferredLightingHistoryWriterDrainCompletion,
                .accumulatorPersistentState = &m_hardwareCausticAccumulatorPersistentState,
                .declaresHardwareCaustics = declaresHardwareCaustics,
                .producerDispatched = &m_deferredCausticProducerDispatched,
                .timingTicket = &hardwareCausticsTimingTicket,
                .photonTiming = &causticPhotonTiming,
                .resolveTiming = &causticResolveTiming,
            },
            hardwareCausticsStageResult
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics stage"));
            return;
        }
        m_deferredCausticIrradianceClearTask = hardwareCausticsStageResult.causticIrradianceClearTask;
        m_deferredCausticAccumulatorNonTemporalClearTask = hardwareCausticsStageResult.causticAccumulatorNonTemporalClearTask;
        m_deferredCausticAccumulatorBootstrapClearTask = hardwareCausticsStageResult.causticAccumulatorBootstrapClearTask;
        m_deferredCausticAccumulatorDecayTask = hardwareCausticsStageResult.causticAccumulatorDecayTask;
        m_deferredCausticPhotonTask = hardwareCausticsStageResult.causticPhotonTask;
        m_deferredCausticGeometryTask = hardwareCausticsStageResult.causticGeometryTask;
        m_deferredCausticResolvePrepareTask = hardwareCausticsStageResult.causticResolvePrepareTask;
        m_deferredCausticResolveWaveletTask = hardwareCausticsStageResult.causticResolveWaveletTask;
        m_deferredCausticResolveSecondWaveletTask = hardwareCausticsStageResult.causticResolveSecondWaveletTask;
        m_deferredCausticResolveThirdWaveletTask = hardwareCausticsStageResult.causticResolveThirdWaveletTask;
        m_deferredCausticResolveFourthWaveletTask = hardwareCausticsStageResult.causticResolveFourthWaveletTask;
        m_deferredCausticResolveFifthWaveletTask = hardwareCausticsStageResult.causticResolveFifthWaveletTask;
        m_deferredCausticResolveUpsampleTask = hardwareCausticsStageResult.causticResolveUpsampleTask;
        m_deferredHardwareCausticsTask = hardwareCausticsStageResult.hardwareCausticsTask;
    }

    AvboitPreGraphTask::Payload avboitPrePayload{ m_arena };
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload avboitCsgReceiverSpanPayload{ m_arena };
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload avboitCsgIntervalCombinePayload{ m_arena };
    avboitPrePayload.avboitSystem = &m_avboitSystem;
    avboitPrePayload.targets = &deferredTargets;
    avboitPrePayload.timingTicket = &avboitPreTimingTicket;
    avboitPrePayload.transparentCsgIntervalsTiming = &transparentCsgIntervalsTiming;
    avboitPrePayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitPrePayload.frameBindings = frameBindings;
    avboitPrePayload.csgResources = csgResources;
    avboitCsgReceiverSpanPayload.frameBindings = frameBindings;
    avboitCsgReceiverSpanPayload.csgResources = csgResources;
    avboitCsgIntervalCombinePayload.frameBindings = frameBindings;
    avboitCsgIntervalCombinePayload.csgResources = csgResources;


    // Freeze the transparent CSG interval producer before AVBOIT recording; snapshot covers receiver work only.
    TransparentCsgIntervalBuilder transparentCsgIntervalBuilder(
        m_deferredLightingTaskGraph,
        m_materialSystem,
        m_csgSystem,
        m_avboitSystem
    );
    TransparentCsgIntervalProducerResult transparentCsgIntervalResult;
    if(!transparentCsgIntervalBuilder.declare(
        TransparentCsgIntervalProducerInputs{
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .frameBindings = &frameBindings,
            .csgResources = &csgResources,
            .meshViewState = &meshViewState,
            .materialInstances = materialInstances,
            .materialTyped = materialTyped,
            .csgReceiverRanges = csgReceiverRanges,
            .csgCutters = csgCutters,
            .csgClipContextSlots = csgClipContextSlots,
            .csgIntervalSampleState = csgIntervalSampleState,
            .csgIntervalId = csgIntervalId,
            .csgReceiverEventCount = csgReceiverEventCount,
            .csgPeelSubresources = csgPeelSubresources,
            .csgReceiverEventCountSubresources = csgReceiverEventCountSubresources,
            .prefixTask = m_graphicsPrefixTask,
            .hasTransparentRenderers = hasTransparentRenderers,
        },
        avboitPrePayload,
        avboitCsgReceiverSpanPayload,
        avboitCsgIntervalCombinePayload,
        transparentCsgIntervalClearTimingState,
        transparentCsgIntervalResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval producer"));
        return;
    }
    FrameGraphTransparentCsgTasks transparentCsgTasks(
        m_deferredLightingTaskGraph,
        m_materialSystem,
        m_csgSystem,
        m_avboitSystem
    );
    FrameGraphTransparentCsgTaskResult transparentCsgTaskResult;
    if(!transparentCsgTasks.declare(
        FrameGraphTransparentCsgTaskInputs{
            .targets = &deferredTargets,
            .depth = depth,
            .meshView = meshView,
            .materialInstances = materialInstances,
            .materialTyped = materialTyped,
            .csgReceiverRanges = csgReceiverRanges,
            .csgCutters = csgCutters,
            .csgClipContextSlots = csgClipContextSlots,
            .csgIntervalSampleState = csgIntervalSampleState,
            .csgCapBackNormal = csgCapBackNormal,
            .csgIntervalDepth = csgIntervalDepth,
            .csgIntervalId = csgIntervalId,
            .csgReceiverEventData = csgReceiverEventData,
            .csgReceiverEventCount = csgReceiverEventCount,
            .csgReceiverSpanData = csgReceiverSpanData,
            .csgReceiverSpanCount = csgReceiverSpanCount,
            .csgRemovedIntervalDepth = csgRemovedIntervalDepth,
            .csgRemovedIntervalCapNormal = csgRemovedIntervalCapNormal,
            .csgRemovedIntervalData = csgRemovedIntervalData,
            .csgRemovedIntervalCount = csgRemovedIntervalCount,
            .currentBindlessSlots = currentBindlessSlots,
            .avboitMaterialDomain = avboitMaterialDomain,
            .avboitCsgDomain = avboitCsgDomain,
            .csgPeelSubresources = csgPeelSubresources,
            .csgReceiverEventDataSubresources = csgReceiverEventDataSubresources,
            .csgReceiverEventCountSubresources = csgReceiverEventCountSubresources,
            .csgReceiverSpanDataSubresources = csgReceiverSpanDataSubresources,
            .csgReceiverSpanCountSubresources = csgReceiverSpanCountSubresources,
            .csgRemovedIntervalSubresources = csgRemovedIntervalSubresources,
            .csgRemovedIntervalCountSubresources = csgRemovedIntervalCountSubresources,
            .timingTicket = &avboitPreTimingTicket,
            .transparentCsgIntervalsTiming = &transparentCsgIntervalsTiming,
            .transparentCsgUploadTask = transparentCsgIntervalResult.uploadTask,
            .transparentCsgMaterialGeometrySet = transparentCsgIntervalResult.materialGeometrySet,
            .transparentCsgMaterialSampledTextureSet = transparentCsgIntervalResult.materialSampledTextureSet,
        },
        avboitPrePayload,
        avboitCsgReceiverSpanPayload,
        avboitCsgIntervalCombinePayload,
        transparentCsgTaskResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph tasks"));
        return;
    }
    const Core::GpuTaskId avboitIntervalCompletionTask = transparentCsgTaskResult.intervalCompletionTask;
    const bool avboitIntervalOutputsGraphOwned = transparentCsgTaskResult.intervalOutputsGraphOwned;
    AvboitOccupancyGraphTask::Payload avboitOccupancyPayload{ m_arena };
    AvboitOccupancyComputeEmulationGraphTask::Payload avboitOccupancyComputeEmulationPayload{ m_arena };
    avboitOccupancyPayload.frameBindings = frameBindings;
    avboitOccupancyComputeEmulationPayload.frameBindings = frameBindings;
    avboitOccupancyPayload.avboitSystem = &m_avboitSystem;
    avboitOccupancyPayload.targets = &deferredTargets;
    avboitOccupancyPayload.timingTicket = &avboitPreTimingTicket;
    avboitOccupancyPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitOccupancyPayload.csgResources = csgResources;
    avboitOccupancyComputeEmulationPayload.csgResources = csgResources;

    const bool opticalCaptureRequested = hasTransparentRenderers
        && (m_refractionEnabled || m_reflectionSettings.traceMode != ReflectionTraceMode::Disabled);
    const bool refractionActive = m_preparedRefractionActive && opticalCaptureRequested;
    RayTracingRefractionGraphResources refractionResources = m_preparedRefractionResources;
    Core::Alloc::ScratchArena generatedGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    AvboitGeneratedGeometryReuse generatedGeometry(generatedGeometryScratch);
    const Core::GpuTaskId refractionCaptureTask = DeclareAvboitRefractionCapture(
        m_deferredLightingTaskGraph, m_arena, m_materialSystem, m_csgSystem, deferredTargets,
        csgFrameState, csgResources, frameBindings, meshViewState, objectGeometry, generatedGeometry, avboitIntervalCompletionTask,
        refractionActive && refractionResources.valid());
    if(!refractionCaptureTask.valid())
        return;
    FrameGraphAvboitOccupancyUploadChain occupancyUploadChain(
        m_deferredLightingTaskGraph,
        m_materialSystem,
        m_csgSystem
    );
    FrameGraphAvboitOccupancyUploadResult occupancyUploadChainResult;
    if(!occupancyUploadChain.declare(
        FrameGraphAvboitOccupancyUploadInputs{
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .frameBindings = &frameBindings,
            .csgResources = &csgResources,
            .meshViewState = &meshViewState,
            .materialInstances = materialInstances,
            .materialTyped = materialTyped,
            .csgReceiverRanges = csgReceiverRanges,
            .csgCutters = csgCutters,
            .csgClipContextSlots = csgClipContextSlots,
            .uploadTask = refractionCaptureTask,
            .intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned,
            .hasTransparentRenderers = hasTransparentRenderers,
        },
        avboitOccupancyPayload,
        avboitOccupancyComputeEmulationPayload,
        generatedGeometry,
        occupancyUploadChainResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy upload chain"));
        return;
    }
    const Core::GpuTaskId occupancyUploadTask = occupancyUploadChainResult.uploadTask;
    const Core::GpuGraphResourceSetId occupancyMaterialGeometrySet = occupancyUploadChainResult.materialGeometrySet;
    const Core::GpuGraphResourceSetId occupancyMaterialSampledTextureSet = occupancyUploadChainResult.materialSampledTextureSet;
    const bool occupancyCsgStreamsUploaded = occupancyUploadChainResult.csgStreamsUploaded;
    const bool occupancyRegularComputeEmulationPlanCaptured = occupancyUploadChainResult.regularComputeEmulationPlanCaptured;
    const Core::GpuTaskId occupancyReusedGeometryProducer = occupancyUploadChainResult.reusedGeometryProducer;
    const bool occupancyProducesReusableGeometry = occupancyUploadChainResult.producesReusableGeometry;
    const bool occupancyCsgComputeEmulationPlanCaptured = occupancyUploadChainResult.csgComputeEmulationPlanCaptured;
    const bool occupancySharedComputeEmulationPlanCaptured = occupancyUploadChainResult.sharedComputeEmulationPlanCaptured;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan occupancySharedComputeEmulationPlan = occupancyUploadChainResult.sharedComputeEmulationPlan;
    const usize occupancySharedComputeEmulationInstanceCount = occupancyUploadChainResult.sharedComputeEmulationInstanceCount;
    const usize occupancySharedComputeEmulationMaterialTypedByteCount = occupancyUploadChainResult.sharedComputeEmulationMaterialTypedByteCount;


    AvboitClearChainBuilder avboitClearChainBuilder(
        m_deferredLightingTaskGraph,
        m_avboitSystem
    );
    AvboitClearChainResult avboitClearChainResult;
    if(!avboitClearChainBuilder.declare(
        AvboitClearChainInputs{
            .lowRaster = avboitLowRaster,
            .accumColor = avboitAccumColor,
            .accumExtinction = avboitAccumExtinction,
            .foregroundColor = avboitForegroundColor,
            .foregroundExtinction = avboitForegroundExtinction,
            .transmittance = avboitTransmittance,
            .coverage = avboitCoverage,
            .depthWarp = avboitDepthWarp,
            .control = avboitControl,
            .extinction = avboitExtinction,
            .extinctionOverflow = avboitExtinctionOverflow,
            .uploadTask = occupancyUploadTask,
            .clearTargets = clearAvboitTargets,
        },
        avboitClearTimingState,
        avboitClearChainResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT clear chain"));
        return;
    }
    Core::GpuTaskId avboitClearTask = avboitClearChainResult.clearTask;


    AvboitOccupancyRecordInputs avboitOccupancyRecordInputs{ m_arena };
    avboitOccupancyRecordInputs.targets = &deferredTargets;
    avboitOccupancyRecordInputs.albedo = albedo;
    avboitOccupancyRecordInputs.normal = normal;
    avboitOccupancyRecordInputs.worldPosition = worldPosition;
    avboitOccupancyRecordInputs.depth = depth;
    avboitOccupancyRecordInputs.refractionInstance = refractionInstance;
    avboitOccupancyRecordInputs.avboitLowRaster = avboitLowRaster;
    avboitOccupancyRecordInputs.avboitCoverage = avboitCoverage;
    avboitOccupancyRecordInputs.avboitMaterialDomain = avboitMaterialDomain;
    avboitOccupancyRecordInputs.avboitCsgDomain = avboitCsgDomain;
    avboitOccupancyRecordInputs.meshView = meshView;
    avboitOccupancyRecordInputs.materialInstances = materialInstances;
    avboitOccupancyRecordInputs.materialTyped = materialTyped;
    avboitOccupancyRecordInputs.csgReceiverRanges = csgReceiverRanges;
    avboitOccupancyRecordInputs.csgCutters = csgCutters;
    avboitOccupancyRecordInputs.csgClipContextSlots = csgClipContextSlots;
    avboitOccupancyRecordInputs.csgIntervalSampleState = csgIntervalSampleState;
    avboitOccupancyRecordInputs.csgRemovedIntervalDepth = csgRemovedIntervalDepth;
    avboitOccupancyRecordInputs.csgRemovedIntervalCapNormal = csgRemovedIntervalCapNormal;
    avboitOccupancyRecordInputs.csgRemovedIntervalData = csgRemovedIntervalData;
    avboitOccupancyRecordInputs.csgRemovedIntervalCount = csgRemovedIntervalCount;
    avboitOccupancyRecordInputs.csgRemovedIntervalSubresources = csgRemovedIntervalSubresources;
    avboitOccupancyRecordInputs.csgRemovedIntervalCountSubresources = csgRemovedIntervalCountSubresources;
    avboitOccupancyRecordInputs.currentBindlessSlots = currentBindlessSlots;
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
    avboitOccupancyRecordInputs.preTimingTicket = &avboitPreTimingTicket;
    avboitOccupancyRecordInputs.occupancyComputeEmulationTiming = &avboitOccupancyComputeEmulationTiming;
    AvboitOccupancyRecordBuilder avboitOccupancyRecordBuilder(
        m_deferredLightingTaskGraph,
        m_graphics,
        m_materialSystem,
        m_avboitSystem
    );
    AvboitOccupancyRecordResult avboitOccupancyRecordResult;
    if(!avboitOccupancyRecordBuilder.declare(
        frameBindings,
        csgResources,
        objectGeometry,
        avboitOccupancyRecordInputs,
        avboitOccupancyPayload,
        avboitOccupancyComputeEmulationPayload,
        avboitOccupancyRecordResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
        return;
    }
    if(occupancyProducesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return;
    }


    AvboitComputeEffectChainBuilder avboitComputeEffectChainBuilder(
        m_deferredLightingTaskGraph,
        m_avboitSystem
    );
    AvboitDepthWarpStageResult avboitDepthWarpStageResult;
    if(!avboitComputeEffectChainBuilder.declareDepthWarp(
        AvboitDepthWarpStageInputs{
            .targets = &deferredTargets.avboit,
            .coverage = avboitCoverage,
            .depthWarp = avboitDepthWarp,
            .control = avboitControl,
            .currentBindlessSlots = currentBindlessSlots,
            .occupancyTask = m_avboitSystem.taskGraphStage().m_occupancyTask,
            .depthWarpTimingTicket = &avboitDepthWarpTimingTicket,
            .timingFeedback = &m_deferredTaskTimingFeedback,
            .hasTransparentRenderers = hasTransparentRenderers,
        },
        avboitDepthWarpStageResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT depth-warp graph task"));
        return;
    }
    const Core::GpuTaskId avboitDepthWarpCompletionTask = avboitDepthWarpStageResult.completionTask;

    if(hasTransparentRenderers){
    // Snapshot Extinction after Depth Warp so phases never overwrite each other.
    AvboitExtinctionGraphTask::Payload avboitExtinctionPayload{ m_arena };
    AvboitExtinctionComputeEmulationGraphTask::Payload avboitExtinctionComputeEmulationPayload{ m_arena };
    avboitExtinctionPayload.frameBindings = frameBindings;
    avboitExtinctionComputeEmulationPayload.frameBindings = frameBindings;
    avboitExtinctionPayload.avboitSystem = &m_avboitSystem;
    avboitExtinctionPayload.targets = &deferredTargets;
    avboitExtinctionPayload.timingTicket = &avboitExtinctionTimingTicket;
    avboitExtinctionPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitExtinctionPayload.csgResources = csgResources;
    avboitExtinctionComputeEmulationPayload.csgResources = csgResources;

    FrameGraphAvboitExtinctionUploadChain extinctionUploadChain(
        m_deferredLightingTaskGraph,
        m_materialSystem,
        m_csgSystem
    );
    FrameGraphAvboitExtinctionUploadResult extinctionUploadChainResult;
    if(!extinctionUploadChain.declare(
        FrameGraphAvboitExtinctionUploadInputs{
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .frameBindings = &frameBindings,
            .csgResources = &csgResources,
            .meshViewState = &meshViewState,
            .materialInstances = materialInstances,
            .materialTyped = materialTyped,
            .csgReceiverRanges = csgReceiverRanges,
            .csgCutters = csgCutters,
            .csgClipContextSlots = csgClipContextSlots,
            .csgIntervalSampleState = csgIntervalSampleState,
            .uploadTask = avboitDepthWarpCompletionTask,
            .intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned,
            .hasTransparentRenderers = hasTransparentRenderers,
        },
        avboitExtinctionPayload,
        avboitExtinctionComputeEmulationPayload,
        generatedGeometry,
        extinctionUploadChainResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction upload chain"));
        return;
    }
    const Core::GpuTaskId extinctionUploadTask = extinctionUploadChainResult.uploadTask;
    const Core::GpuGraphResourceSetId extinctionMaterialGeometrySet = extinctionUploadChainResult.materialGeometrySet;
    const Core::GpuGraphResourceSetId extinctionMaterialSampledTextureSet = extinctionUploadChainResult.materialSampledTextureSet;
    const bool extinctionStreamsUploaded = extinctionUploadChainResult.streamsUploaded;
    const bool extinctionCsgStreamsUploaded = extinctionUploadChainResult.csgStreamsUploaded;
    const bool extinctionRegularComputeEmulationPlanCaptured = extinctionUploadChainResult.regularComputeEmulationPlanCaptured;
    const Core::GpuTaskId extinctionReusedGeometryProducer = extinctionUploadChainResult.reusedGeometryProducer;
    const bool extinctionProducesReusableGeometry = extinctionUploadChainResult.producesReusableGeometry;
    const bool extinctionCsgComputeEmulationPlanCaptured = extinctionUploadChainResult.csgComputeEmulationPlanCaptured;
    const bool extinctionSharedComputeEmulationPlanCaptured = extinctionUploadChainResult.sharedComputeEmulationPlanCaptured;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan extinctionSharedComputeEmulationPlan = extinctionUploadChainResult.sharedComputeEmulationPlan;
    const usize extinctionSharedComputeEmulationInstanceCount = extinctionUploadChainResult.sharedComputeEmulationInstanceCount;
    const usize extinctionSharedComputeEmulationMaterialTypedByteCount = extinctionUploadChainResult.sharedComputeEmulationMaterialTypedByteCount;
    AvboitExtinctionRecordInputs avboitExtinctionRecordInputs{ m_arena };
    avboitExtinctionRecordInputs.targets = &deferredTargets;
    avboitExtinctionRecordInputs.albedo = albedo;
    avboitExtinctionRecordInputs.normal = normal;
    avboitExtinctionRecordInputs.worldPosition = worldPosition;
    avboitExtinctionRecordInputs.depth = depth;
    avboitExtinctionRecordInputs.refractionInstance = refractionInstance;
    avboitExtinctionRecordInputs.avboitLowRaster = avboitLowRaster;
    avboitExtinctionRecordInputs.avboitDepthWarp = avboitDepthWarp;
    avboitExtinctionRecordInputs.avboitExtinction = avboitExtinction;
    avboitExtinctionRecordInputs.avboitControl = avboitControl;
    avboitExtinctionRecordInputs.avboitExtinctionOverflow = avboitExtinctionOverflow;
    avboitExtinctionRecordInputs.avboitMaterialDomain = avboitMaterialDomain;
    avboitExtinctionRecordInputs.avboitCsgDomain = avboitCsgDomain;
    avboitExtinctionRecordInputs.meshView = meshView;
    avboitExtinctionRecordInputs.materialInstances = materialInstances;
    avboitExtinctionRecordInputs.materialTyped = materialTyped;
    avboitExtinctionRecordInputs.csgReceiverRanges = csgReceiverRanges;
    avboitExtinctionRecordInputs.csgCutters = csgCutters;
    avboitExtinctionRecordInputs.csgClipContextSlots = csgClipContextSlots;
    avboitExtinctionRecordInputs.csgIntervalSampleState = csgIntervalSampleState;
    avboitExtinctionRecordInputs.csgRemovedIntervalDepth = csgRemovedIntervalDepth;
    avboitExtinctionRecordInputs.csgRemovedIntervalCapNormal = csgRemovedIntervalCapNormal;
    avboitExtinctionRecordInputs.csgRemovedIntervalData = csgRemovedIntervalData;
    avboitExtinctionRecordInputs.csgRemovedIntervalCount = csgRemovedIntervalCount;
    avboitExtinctionRecordInputs.csgRemovedIntervalSubresources = csgRemovedIntervalSubresources;
    avboitExtinctionRecordInputs.csgRemovedIntervalCountSubresources = csgRemovedIntervalCountSubresources;
    avboitExtinctionRecordInputs.currentBindlessSlots = currentBindlessSlots;
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
    avboitExtinctionRecordInputs.extinctionTimingTicket = &avboitExtinctionTimingTicket;
    avboitExtinctionRecordInputs.extinctionComputeEmulationTiming = &avboitExtinctionComputeEmulationTiming;
    AvboitExtinctionRecordBuilder avboitExtinctionRecordBuilder(
        m_deferredLightingTaskGraph,
        m_graphics,
        m_materialSystem,
        m_avboitSystem
    );
    AvboitExtinctionRecordResult avboitExtinctionRecordResult;
    if(!avboitExtinctionRecordBuilder.declare(
        frameBindings,
        csgResources,
        objectGeometry,
        avboitExtinctionRecordInputs,
        avboitExtinctionPayload,
        avboitExtinctionComputeEmulationPayload,
        avboitExtinctionRecordResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
        return;
    }
    if(extinctionProducesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return;
    }

    AvboitIntegrationStageResult avboitIntegrationStageResult;
    if(!avboitComputeEffectChainBuilder.declareIntegration(
        AvboitIntegrationStageInputs{
            .targets = &deferredTargets.avboit,
            .extinction = avboitExtinction,
            .control = avboitControl,
            .extinctionOverflow = avboitExtinctionOverflow,
            .transmittance = avboitTransmittance,
            .currentBindlessSlots = currentBindlessSlots,
            .extinctionTask = m_avboitSystem.taskGraphStage().m_extinctionTask,
            .integrationTimingTicket = &avboitIntegrationTimingTicket,
            .timingFeedback = &m_deferredTaskTimingFeedback,
        },
        avboitIntegrationStageResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return;
    }


// Accumulation is another independent write point; freeze bytes after integration.
    AvboitAccumulationGraphTask::Payload avboitAccumulationPayload{ m_arena };
    AvboitAccumulationComputeEmulationGraphTask::Payload avboitAccumulationComputeEmulationPayload{ m_arena };
    avboitAccumulationPayload.frameBindings = frameBindings;
    avboitAccumulationComputeEmulationPayload.frameBindings = frameBindings;
    avboitAccumulationPayload.avboitSystem = &m_avboitSystem;
    avboitAccumulationPayload.targets = &deferredTargets;
    avboitAccumulationPayload.timingTicket = &avboitAccumulationTimingTicket;
    avboitAccumulationPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitAccumulationPayload.csgResources = csgResources;
    avboitAccumulationComputeEmulationPayload.csgResources = csgResources;

    FrameGraphAvboitAccumulationUploadChain accumulationUploadChain(
        m_deferredLightingTaskGraph,
        m_materialSystem,
        m_csgSystem
    );
    FrameGraphAvboitAccumulationUploadResult accumulationUploadChainResult;
    if(!accumulationUploadChain.declare(
        FrameGraphAvboitAccumulationUploadInputs{
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .frameBindings = &frameBindings,
            .csgResources = &csgResources,
            .meshViewState = &meshViewState,
            .materialInstances = materialInstances,
            .materialTyped = materialTyped,
            .csgReceiverRanges = csgReceiverRanges,
            .csgCutters = csgCutters,
            .csgClipContextSlots = csgClipContextSlots,
            .csgIntervalSampleState = csgIntervalSampleState,
            .uploadTask = m_avboitSystem.taskGraphStage().m_integrationTask,
            .intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned,
        },
        avboitAccumulationPayload,
        avboitAccumulationComputeEmulationPayload,
        generatedGeometry,
        accumulationUploadChainResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation upload chain"));
        return;
    }
    const Core::GpuTaskId accumulationUploadTask = accumulationUploadChainResult.uploadTask;
    const Core::GpuGraphResourceSetId accumulationMaterialGeometrySet = accumulationUploadChainResult.materialGeometrySet;
    const Core::GpuGraphResourceSetId accumulationMaterialSampledTextureSet = accumulationUploadChainResult.materialSampledTextureSet;
    const bool accumulationStreamsUploaded = accumulationUploadChainResult.streamsUploaded;
    const bool accumulationCsgStreamsUploaded = accumulationUploadChainResult.csgStreamsUploaded;
    const bool accumulationRegularComputeEmulationPlanCaptured = accumulationUploadChainResult.regularComputeEmulationPlanCaptured;
    const Core::GpuTaskId accumulationReusedGeometryProducer = accumulationUploadChainResult.reusedGeometryProducer;
    const bool accumulationProducesReusableGeometry = accumulationUploadChainResult.producesReusableGeometry;
    const bool accumulationCsgComputeEmulationPlanCaptured = accumulationUploadChainResult.csgComputeEmulationPlanCaptured;
    const bool accumulationSharedComputeEmulationPlanCaptured = accumulationUploadChainResult.sharedComputeEmulationPlanCaptured;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan accumulationSharedComputeEmulationPlan = accumulationUploadChainResult.sharedComputeEmulationPlan;
    const usize accumulationSharedComputeEmulationInstanceCount = accumulationUploadChainResult.sharedComputeEmulationInstanceCount;
    const usize accumulationSharedComputeEmulationMaterialTypedByteCount = accumulationUploadChainResult.sharedComputeEmulationMaterialTypedByteCount;

    AvboitAccumulationRecordInputs avboitAccumulationRecordInputs{ m_arena };
    avboitAccumulationRecordInputs.targets = &deferredTargets;
    avboitAccumulationRecordInputs.albedo = albedo;
    avboitAccumulationRecordInputs.normal = normal;
    avboitAccumulationRecordInputs.worldPosition = worldPosition;
    avboitAccumulationRecordInputs.depth = depth;
    avboitAccumulationRecordInputs.refractionInstance = refractionInstance;
    avboitAccumulationRecordInputs.refractionDepth = refractionDepth;
    avboitAccumulationRecordInputs.avboitDepthWarp = avboitDepthWarp;
    avboitAccumulationRecordInputs.avboitTransmittance = avboitTransmittance;
    avboitAccumulationRecordInputs.avboitControl = avboitControl;
    avboitAccumulationRecordInputs.avboitForegroundColor = avboitForegroundColor;
    avboitAccumulationRecordInputs.avboitForegroundExtinction = avboitForegroundExtinction;
    avboitAccumulationRecordInputs.avboitAccumColor = avboitAccumColor;
    avboitAccumulationRecordInputs.avboitAccumExtinction = avboitAccumExtinction;
    avboitAccumulationRecordInputs.avboitMaterialDomain = avboitMaterialDomain;
    avboitAccumulationRecordInputs.avboitCsgDomain = avboitCsgDomain;
    avboitAccumulationRecordInputs.meshView = meshView;
    avboitAccumulationRecordInputs.materialInstances = materialInstances;
    avboitAccumulationRecordInputs.materialTyped = materialTyped;
    avboitAccumulationRecordInputs.csgReceiverRanges = csgReceiverRanges;
    avboitAccumulationRecordInputs.csgCutters = csgCutters;
    avboitAccumulationRecordInputs.csgClipContextSlots = csgClipContextSlots;
    avboitAccumulationRecordInputs.csgIntervalSampleState = csgIntervalSampleState;
    avboitAccumulationRecordInputs.csgRemovedIntervalDepth = csgRemovedIntervalDepth;
    avboitAccumulationRecordInputs.csgRemovedIntervalCapNormal = csgRemovedIntervalCapNormal;
    avboitAccumulationRecordInputs.csgRemovedIntervalData = csgRemovedIntervalData;
    avboitAccumulationRecordInputs.csgRemovedIntervalCount = csgRemovedIntervalCount;
    avboitAccumulationRecordInputs.csgRemovedIntervalSubresources = csgRemovedIntervalSubresources;
    avboitAccumulationRecordInputs.csgRemovedIntervalCountSubresources = csgRemovedIntervalCountSubresources;
    avboitAccumulationRecordInputs.currentBindlessSlots = currentBindlessSlots;
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
    avboitAccumulationRecordInputs.accumulationTimingTicket = &avboitAccumulationTimingTicket;
    avboitAccumulationRecordInputs.accumulationComputeEmulationTiming = &avboitAccumulationComputeEmulationTiming;
    AvboitAccumulationRecordBuilder avboitAccumulationRecordBuilder(
        m_deferredLightingTaskGraph,
        m_graphics,
        m_materialSystem,
        m_avboitSystem
    );
    AvboitAccumulationRecordResult avboitAccumulationRecordResult;
    if(!avboitAccumulationRecordBuilder.declare(
        frameBindings,
        csgResources,
        objectGeometry,
        avboitAccumulationRecordInputs,
        avboitAccumulationPayload,
        avboitAccumulationComputeEmulationPayload,
        avboitAccumulationRecordResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
        return;
    }
    if(accumulationProducesReusableGeometry){
        const Core::GpuTaskId producer = m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask;
        if(!producer.valid())
            generatedGeometry.reset();
        else if(!generatedGeometry.publishProducer(producer))
            return;
    }

    }
    const Core::GpuTaskId avboitFinalTask = hasTransparentRenderers
        ? m_avboitSystem.taskGraphStage().m_accumulationFinalizeTask
        : m_avboitSystem.taskGraphStage().m_occupancyTask
    ;

    DeferredLightingStageBuilder deferredLightingStageBuilder(
        m_deferredLightingTaskGraph,
        m_deferredSystem
    );
    DeferredLightingStageResult deferredLightingStageResult;
    if(!deferredLightingStageBuilder.declare(
        DeferredLightingStageInputs{
            .targets = &deferredTargets,
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
        m_deferredLaggedLightingHistorySlotsUploadTask,
        lightingTimingTicket,
        deferredLightingStageResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }
    m_deferredLightingTask = deferredLightingStageResult.lightingTask;


    FrameGraphReflectionResolve reflectionResolve(
        m_deferredLightingTaskGraph,
        m_graphics,
        m_raytracingSystem,
        m_reflectionSystem
    );
    FrameGraphReflectionResolveResult reflectionResolveResult;
    if(!reflectionResolve.declare(
        FrameGraphReflectionResolveInputs{
            .targets = &deferredTargets,
            .meshViewSnapshot = &meshViewBufferSnapshot,
            .sceneResources = &sceneResources,
            .reflectionSettings = &m_reflectionSettings,
            .contentStamp = &reflectionContentStamp,
            .csgFrameState = &csgFrameState,
            .refractionResources = &refractionResources,
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
            .hardwareTraceGeometrySet = hardwareTraceGeometrySet,
            .traceMaterialSampledTextureSet = traceMaterialSampledTextureSet,
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
        traceGeometryScratchArena,
        reflectionResolveResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare reflection graph tasks"));
        return;
    }
    const ReflectionGraphResult reflectionGraph = reflectionResolveResult.reflectionGraph;
    const ReflectionCompositeInputs reflectionCompositeInputs = reflectionResolveResult.reflectionCompositeInputs;
    const Core::GpuTaskId refractionResolveTask = reflectionResolveResult.refractionResolveTask;
    refractionResources.opaqueReflectionSlot = reflectionCompositeInputs.opaqueRadianceSlot;

    DeferredGraphSuffixBuilder suffixBuilder(
        m_deferredLightingTaskGraph,
        m_deferredSystem,
        m_graphics,
        m_preparedTaskGraphPresentationContributor
    );
    DeferredGraphSuffixResult suffixResult;
    if(!suffixBuilder.declare(
        DeferredGraphSuffixInputs{
            .targets = &deferredTargets,
            .opaqueColor = opaqueColor,
            .avboitAccumColor = avboitAccumColor,
            .avboitAccumExtinction = avboitAccumExtinction,
            .avboitForegroundColor = avboitForegroundColor,
            .avboitForegroundExtinction = avboitForegroundExtinction,
            .refractionResolve = refractionResolve,
            .currentBindlessSlots = currentBindlessSlots,
            .reflectionGraph = reflectionGraph,
            .reflectionCompositeInputs = reflectionCompositeInputs,
            .lightingTask = m_deferredLightingTask,
            .avboitFinalTask = avboitFinalTask,
            .refractionResolveTask = refractionResolveTask,
            .surfelGiTask = m_deferredSurfelGiTask,
            .presentationFrame = &presentationFrame,
            .presentationFramebufferDesc = &presentationFramebufferDesc,
            .useLaggedLightingHistory = useLaggedLightingHistory,
        },
        deferredTargets,
        reflectionCompositeInputs,
        compositeTimingTicket,
        presentTimingTicket,
        asyncFinalTiming,
        m_deferredShadowVisibilityTask,
        frameTimingTransaction,
        suffixResult
    ))
        return;
    m_deferredCompositeTask = suffixResult.compositeTask;
    m_deferredPresentationOverlayRequired = suffixResult.overlayRequired;
    m_deferredPresentationOverlayTask = suffixResult.overlayTask;
    m_deferredPresentTask = suffixResult.presentTask;
    m_deferredFrameTimingEndTask = suffixResult.frameTimingEndTask;

    declareDeferredSurfelCountReadbackTask(rayTracingSurfelResources);

    DeferredFrameTailBuilder deferredFrameTailBuilder(m_deferredLightingTaskGraph);
    DeferredFrameTailResult deferredFrameTailResult;
    if(!deferredFrameTailBuilder.declare(
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
        },
        deferredFrameTailResult
    ))
        return;
    m_deferredLaggedLightingHistoryTask = deferredFrameTailResult.historyCopyTask;
    m_deferredFrameRecoveryTask = deferredFrameTailResult.recoveryTask;

    m_deferredLightingTaskGraphDeclarationSeconds = DurationInSeconds<f64>(TimerNow(), declarationBegin);
    m_deferredLightingTaskGraphDeclared = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

