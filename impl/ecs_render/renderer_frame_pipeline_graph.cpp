// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
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

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/task/gpu/capture/command_ir.h>
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
#include <impl/ecs_render/raytrace/hardware_caustics_stage_builder.h>

#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>
#include <impl/ecs_render/avboit/task_graph_timing_metadata.h>
#include <impl/ecs_render/avboit/clear_chain_builder.h>
#include <impl/ecs_render/avboit/compute_effect_chain_builder.h>
#include <impl/ecs_render/avboit/occupancy_record_builder.h>
#include <impl/ecs_render/avboit/avboit_pass_upload_helper.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_deferred_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PreparePacketEnvelopeMetrics(
    const Core::GpuTaskGraph::DeclarationReadView& graph,
    const Core::GpuCompiledGraph::ReadView& compiledGraph,
    Core::GpuTimingRecorder& timingRecorder,
    const u64 sourceFrameIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    const Core::GpuSubmissionPacketRange range = compiledGraph.packetTimingEnvelopeRange();
    if(!range.valid() || !compiledGraph.validPacketRange(range))
        return false;

    Vector<Core::GpuPacketEnvelopeMetricScope, Core::Alloc::ScratchArena> packetScopes{scratchArena};
    Vector<Core::GpuPacketEnvelopeMetricQueueOutput, Core::Alloc::ScratchArena> queueOutputs{scratchArena};
    packetScopes.reserve(range.packetCount);
    queueOutputs.reserve(range.packetCount);
    for(usize packetOffset = 0u; packetOffset < range.packetCount; ++packetOffset){
        const Core::GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(range.first.index + packetOffset);
        const Core::GpuCompiledPacketView packetView = compiledGraph.packet(packetID);
        if(!packetView.valid() || !packetView.plan->recordsPacketEnvelopeTiming || packetView.plan->taskCount == 0u)
            return false;

        const Name packetScopeName = Core::GpuTaskPacketTimingScopeName(graph.taskAt(packetView.tasks[0u].index).identity);
        if(!packetScopeName)
            return false;
        packetScopes.push_back(Core::GpuPacketEnvelopeMetricScope{
            .scopeName = packetScopeName,
            .physicalQueue = packetView.plan->queue,
        });

        bool hasQueueOutput = false;
        for(const Core::GpuPacketEnvelopeMetricQueueOutput& output : queueOutputs)
            hasQueueOutput = hasQueueOutput || output.physicalQueue == packetView.plan->queue;
        if(hasQueueOutput)
            continue;

        const Name internalIdleScopeName = RendererGpuTimingScope::DeferredGraphQueueInternalIdle(packetView.plan->queue, scratchArena);
        if(!internalIdleScopeName)
            return false;
        queueOutputs.push_back(Core::GpuPacketEnvelopeMetricQueueOutput{
            .physicalQueue = packetView.plan->queue,
            .internalIdleScopeName = internalIdleScopeName,
        });
    }

    return timingRecorder.preparePacketEnvelopeMetrics(
        sourceFrameIndex,
        MakeNotNull(static_cast<const Core::GpuPacketEnvelopeMetricScope*>(packetScopes.data())),
        packetScopes.size(),
        RendererGpuTimingScope::s_DeferredGraphQueueOverlap.identity,
        MakeNotNull(static_cast<const Core::GpuPacketEnvelopeMetricQueueOutput*>(queueOutputs.data())),
        queueOutputs.size()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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

    m_deferredLightingTaskGraphValid = false;
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
    m_deferredShadowPrepareHybridSoftwareTailTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixCsgIntervalClearTask = {};
    m_graphicsPrefixOpaqueComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_graphicsPrefixOpaqueSharedComputeEmulationTasks)
        task = {};
    m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = 0u;
    m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = {};
    m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgReceiverSpanTask = {};
    m_graphicsPrefixCsgIntervalCombineTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_deferredShadowVisibilityOpaqueTask = {};
    m_deferredShadowVisibilityOpaqueFirstWaveletTask = {};
    m_deferredShadowVisibilityOpaqueResolveTask = {};
    m_deferredShadowVisibilityTransparentTraceTask = {};
    m_deferredShadowVisibilityTransparentTemporalMergeTask = {};
    m_deferredShadowVisibilityTransparentFirstWaveletTask = {};
    m_deferredShadowVisibilityAdaptiveStatsClearTask = {};
    m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
    m_deferredShadowVisibilityAdaptiveStatsReadbackTask = {};
    m_deferredShadowVisibilityAllLitClearTask = {};
    m_deferredShadowVisibilityTask = {};
    m_deferredSoftwareCausticsTask = {};
    m_deferredCausticIrradianceClearTask = {};
    m_deferredCausticAccumulatorBootstrapClearTask = {};
    m_deferredCausticAccumulatorNonTemporalClearTask = {};
    m_deferredCausticAccumulatorDecayTask = {};
    m_deferredCausticPhotonTask = {};
    m_deferredCausticGeometryTask = {};
    m_deferredCausticResolvePrepareTask = {};
    m_deferredCausticResolveWaveletTask = {};
    m_deferredCausticResolveSecondWaveletTask = {};
    m_deferredCausticResolveThirdWaveletTask = {};
    m_deferredCausticResolveFourthWaveletTask = {};
    m_deferredCausticResolveFifthWaveletTask = {};
    m_deferredCausticResolveUpsampleTask = {};
    m_deferredCausticProducerDispatched = false;
    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiInitializationLifecycleTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
    m_deferredSurfelGiAgeFreeTask = {};
    m_deferredSurfelGiCellHeadClearTask = {};
    m_deferredSurfelGiHashBuildTask = {};
    m_deferredSurfelGiSpawnTask = {};
    m_deferredSurfelGiTraceBuildArgsTask = {};
    m_deferredSurfelGiTraceTask = {};
    m_deferredSurfelGiResolveTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    m_deferredHardwareCausticsTask = {};
    m_avboitSystem.resetTaskGraphStage();
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentationOverlayTask = {};
    m_deferredPresentTask = {};
    m_deferredFrameTimingEndTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredFrameRecoveryTask = {};
    m_deferredSurfelGiCounterReadbackCompletion = {};
    m_deferredLightingHistoryReadReadyCompletion = {};
    m_deferredLightingHistoryWriterDrainCompletion = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
    m_deferredPresentationOverlayRequired = false;
    resetDeferredTaskGraphRuntime();
    // Declaration runs between artifact discard and core compilation.
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
    Vector<Core::Buffer*, Core::Alloc::ScratchArena> softwareBvhBuildStateBuffers{ traceGeometryScratchArena };
    traceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceAttributeResources.reserve(preparedTraceGeometry.size());
    softwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    traceMaterialSampledTextureResources.reserve(preparedTraceMaterialSampledTextures.size());
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
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
    if(softwareTraceResourcesPrepared){
        ECSRenderDetail::MeshSoftwareBvhParentBuildStateVector meshSoftwareBvhParentBuildStates{ traceGeometryScratchArena };
        if(!m_meshSystem.collectSoftwareBvhParentBuildStates(meshSoftwareBvhParentBuildStates))
            return;
        softwareBvhBuildStateResources.reserve(meshSoftwareBvhParentBuildStates.size() + 3u);
        softwareBvhBuildStateBuffers.reserve(meshSoftwareBvhParentBuildStates.size() + 3u);
        const auto appendSoftwareBvhBuildState = [&](
            const Core::BufferHandle& buffer,
            const Name identity,
            const AStringView label
        ){
            if(!buffer || !identity)
                return false;
            for(Core::Buffer* const existing : softwareBvhBuildStateBuffers){
                if(existing == buffer.get())
                    return true;
            }
            const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
            if(!resource.valid())
                return false;
            softwareBvhBuildStateBuffers.push_back(buffer.get());
            softwareBvhBuildStateResources.push_back(resource);
            return true;
        };
        for(const ECSRenderDetail::MeshSoftwareBvhParentBuildState& state : meshSoftwareBvhParentBuildStates){
            if(!appendSoftwareBvhBuildState(state.buffer, state.identity, "Software BVH Parent")){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software BVH parent build state"));
                return;
            }
        }
        if(
            !rayTracingShadowResources.bvhSortKeysBuffer
            || !rayTracingShadowResources.bvhSortPayloadBuffer
            || !rayTracingShadowResources.bvhVisitCounterBuffer
            || !appendSoftwareBvhBuildState(
                rayTracingShadowResources.bvhSortKeysBuffer,
                Name("render.shadow_trace.sw_bvh_sort_keys"),
                "Software BVH Sort Keys"
            )
            || !appendSoftwareBvhBuildState(
                rayTracingShadowResources.bvhSortPayloadBuffer,
                Name("render.shadow_trace.sw_bvh_sort_payload"),
                "Software BVH Sort Payload"
            )
            || !appendSoftwareBvhBuildState(
                rayTracingShadowResources.bvhVisitCounterBuffer,
                Name("render.shadow_trace.sw_bvh_visit_counter"),
                "Software BVH Visit Counter"
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared software BVH build state"));
            return;
        }
    }
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
        : (m_deferredShadowPrepareHybridSoftwareTailTask.valid()
            ? m_deferredShadowPrepareHybridSoftwareTailTask
            : m_deferredShadowPrepareTask)
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
    if(!declareDeferredGraphicsPrefixTasks(
        deferredTargets,
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
        declaresHardwareCaustics,
        worldPosition,
        normal,
        depth,
        currentShadowVisibility,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        softwareTraceGeometryResources.data(),
        softwareTraceGeometryResources.size(),
        softwareTraceGeometrySet,
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
    const Core::GpuTaskId transparentCsgUploadTask = transparentCsgIntervalResult.uploadTask;
    const Core::GpuGraphResourceSetId transparentCsgMaterialGeometrySet = transparentCsgIntervalResult.materialGeometrySet;
    const Core::GpuGraphResourceSetId transparentCsgMaterialSampledTextureSet = transparentCsgIntervalResult.materialSampledTextureSet;

    // Declare interval-producer states here, before native recording, not on the occupancy task.
    const Core::BufferRange transparentCsgInstanceRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.instanceCount * sizeof(InstanceGpuData)
    );
    const Core::BufferRange transparentCsgMaterialTypedRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.materialTypedByteCount
    );
    const Core::BufferRange transparentCsgReceiverRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    const Core::BufferRange transparentCsgCutterRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData)
    );

    Core::Alloc::ScratchArena avboitIntervalResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalResourceUses{ avboitIntervalResourceScratch };
    avboitIntervalResourceUses.reserve(16u);
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        avboitIntervalResourceUses.push_back(ReadUse(depth));
        avboitIntervalResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadBufferUse(materialInstances, transparentCsgInstanceRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(materialTyped, transparentCsgMaterialTypedRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(csgReceiverRanges, transparentCsgReceiverRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(csgCutters, transparentCsgCutterRange));
        avboitIntervalResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // Sparse payloads are write-only from Unknown; ID/count stay ReadWrite from their clears.
        avboitIntervalResourceUses.push_back(
            WriteTextureUse(csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            WriteTextureUse(csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(WriteTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    const Core::GpuTaskResourceSetUse transparentCsgMaterialGeometrySetUse{
        .resourceSet = transparentCsgMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse transparentCsgMaterialSampledTextureSetUse{
        .resourceSet = transparentCsgMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse transparentCsgMaterialResourceSetUses[2u] = {};
    usize transparentCsgMaterialResourceSetUseCount = 0u;
    if(avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned){
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] =
            transparentCsgMaterialGeometrySetUse;
    }
    if(transparentCsgMaterialSampledTextureSet.valid()){
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] =
            transparentCsgMaterialSampledTextureSetUse;
    }
    avboitIntervalResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    avboitIntervalResourceUses.push_back(ReadUse(avboitMaterialDomain));
    avboitIntervalResourceUses.push_back(ReadWriteUse(avboitCsgDomain, Core::ResourceStates::ShaderResource));

    Core::GpuTaskSchedulingHint avboitIntervalScheduling;
    avboitIntervalScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitIntervalScheduling.forceSubmissionBoundary = false;
    avboitIntervalScheduling.allowPacketMerge = true;
    avboitIntervalScheduling.mergeWithPrevious = avboitPrePayload.transparentCsgStreamsUploaded;
    Core::GpuTaskDesc avboitIntervalDesc;
    avboitIntervalDesc
        .setIdentity(Name("render.avboit.intervals"))
        .setMarkerLabel("Transparent CSG Intervals")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(avboitIntervalScheduling)
        .setDependencies(&transparentCsgUploadTask, 1u)
        .setResourceUses(avboitIntervalResourceUses.data(), avboitIntervalResourceUses.size())
        .setResourceSetUses(
            transparentCsgMaterialResourceSetUseCount != 0u ? transparentCsgMaterialResourceSetUses : nullptr,
            transparentCsgMaterialResourceSetUseCount
        )
    ;
    const bool avboitCsgReceiverSpanGraphOwned =
        avboitPrePayload.transparentCsgStreamsUploaded
        && avboitPrePayload.transparentCsgSnapshot.captured
        && avboitPrePayload.deferTransparentCsgIntervalCombine
        && avboitCsgReceiverSpanPayload.transparentCsgSnapshot.captured
        && avboitCsgReceiverSpanPayload.csgFrameBuffersUploaded
    ;
    const bool avboitCsgIntervalCombineGraphOwned =
        avboitCsgReceiverSpanGraphOwned
        && avboitCsgIntervalCombinePayload.transparentCsgSnapshot.captured
        && avboitCsgIntervalCombinePayload.csgFrameBuffersUploaded
    ;
    Core::Alloc::ScratchArena avboitIntervalSpanResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalSpanResourceUses{
        avboitIntervalSpanResourceScratch
    };
    Core::Alloc::ScratchArena avboitIntervalCombineResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalCombineResourceUses{
        avboitIntervalCombineResourceScratch
    };
    if(avboitCsgReceiverSpanGraphOwned){
        avboitIntervalSpanResourceUses.reserve(6u);
        avboitIntervalSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(ReadUse(
            csgClipContextSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalSpanResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitCsgReceiverSpanPayload.materialSystem = &m_materialSystem;
        avboitCsgReceiverSpanPayload.csgSystem = &m_csgSystem;
        avboitCsgReceiverSpanPayload.targets = &deferredTargets;
        avboitCsgReceiverSpanPayload.timingTicket = &avboitPreTimingTicket;
        avboitCsgReceiverSpanPayload.transparentCsgIntervalsTiming = &transparentCsgIntervalsTiming;
        avboitCsgReceiverSpanPayload.receiverSpanInputImageStatesGraphOwned = true;
        avboitCsgReceiverSpanPayload.receiverSpanOutputImageStatesGraphOwned = true;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        avboitIntervalCombineResourceUses.reserve(11u);
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgCapBackNormal,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalDepth,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalId,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadUse(
            csgClipContextSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalCombineResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitCsgIntervalCombinePayload.materialSystem = &m_materialSystem;
        avboitCsgIntervalCombinePayload.csgSystem = &m_csgSystem;
        avboitCsgIntervalCombinePayload.targets = &deferredTargets;
        avboitCsgIntervalCombinePayload.timingTicket = &avboitPreTimingTicket;
        avboitCsgIntervalCombinePayload.transparentCsgIntervalsTiming = &transparentCsgIntervalsTiming;
        avboitCsgIntervalCombinePayload.intervalCombineInputImageStatesGraphOwned = true;
        avboitCsgIntervalCombinePayload.removedIntervalOutputImageStatesGraphOwned = true;
    }
    m_avboitSystem.taskGraphStage().m_preTask = m_deferredLightingTaskGraph.addTask<AvboitPreGraphTask>(
        avboitIntervalDesc,
        Move(avboitPrePayload)
    );
    if(!m_avboitSystem.taskGraphStage().m_preTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph task"));
        return;
    }

    Core::GpuTaskId avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_preTask;
    bool avboitIntervalOutputsGraphOwned = false;
    if(avboitCsgReceiverSpanGraphOwned){
        Core::GpuTaskSchedulingHint avboitIntervalSpanScheduling;
        avboitIntervalSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalSpanScheduling.forceSubmissionBoundary = false;
        avboitIntervalSpanScheduling.allowPacketMerge = true;
        avboitIntervalSpanScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalSpanDesc;
        avboitIntervalSpanDesc
            .setIdentity(Name("render.avboit.transparent_csg.receiver_span"))
            .setMarkerLabel("Transparent CSG Receiver Span")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitIntervalSpanScheduling)
            .setDependencies(&m_avboitSystem.taskGraphStage().m_preTask, 1u)
            .setResourceUses(
                avboitIntervalSpanResourceUses.data(),
                avboitIntervalSpanResourceUses.size()
            )
        ;
        m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::AvboitCsgReceiverSpanGraphTask
        >(
            avboitIntervalSpanDesc,
            Move(avboitCsgReceiverSpanPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-span graph task"));
            return;
        }
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        Core::GpuTaskSchedulingHint avboitIntervalCombineScheduling;
        avboitIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalCombineScheduling.forceSubmissionBoundary = false;
        avboitIntervalCombineScheduling.allowPacketMerge = true;
        avboitIntervalCombineScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalCombineDesc;
        avboitIntervalCombineDesc
            .setIdentity(Name("render.avboit.transparent_csg.interval_combine"))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitIntervalCombineScheduling)
            .setDependencies(&avboitIntervalCompletionTask, 1u)
            .setResourceUses(
                avboitIntervalCombineResourceUses.data(),
                avboitIntervalCombineResourceUses.size()
            )
        ;
        m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::AvboitCsgIntervalCombineGraphTask
        >(
            avboitIntervalCombineDesc,
            Move(avboitCsgIntervalCombinePayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-combine graph task"));
            return;
        }
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask;
        avboitIntervalOutputsGraphOwned = true;
    }

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
    const bool refractionActive = opticalCaptureRequested && m_raytracingSystem.prepareRefractionResources();
    RayTracingRefractionGraphResources refractionResources = m_raytracingSystem.snapshotRefractionGraphResources();
    refractionResources.refractionEnabled = m_refractionEnabled;
    if(!m_refractionEnabled){
        refractionResources.pipeline = refractionResources.screenFallbackPipeline;
        refractionResources.usesHardwareTrace = false;
        refractionResources.tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    }
    const Core::GpuTaskId refractionCaptureTask = DeclareAvboitRefractionCapture(
        m_deferredLightingTaskGraph, m_arena, m_materialSystem, m_csgSystem, deferredTargets,
        csgFrameState, csgResources, frameBindings, meshViewState, avboitIntervalCompletionTask,
        refractionActive && refractionResources.valid());
    if(!refractionCaptureTask.valid())
        return;
    Core::GpuTaskId occupancyUploadTask = refractionCaptureTask;
    bool occupancyCsgStreamsUploaded = false;
    bool occupancyRegularComputeEmulationPlanCaptured = false;
    bool occupancyCsgComputeEmulationPlanCaptured = false;
    bool occupancySharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan occupancySharedComputeEmulationPlan;
    usize occupancySharedComputeEmulationInstanceCount = 0u;
    usize occupancySharedComputeEmulationMaterialTypedByteCount = 0u;
    bool occupancyMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena occupancyMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId occupancyMaterialGeometrySet;
    Core::GpuGraphResourceSetId occupancyMaterialSampledTextureSet;
    if(hasTransparentRenderers){
        Core::Alloc::ScratchArena occupancyUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions occupancyDrawItems{ occupancyUploadScratch };
        InstanceGpuDataVector occupancyInstanceData{ occupancyUploadScratch };
        CsgFrameGpuData occupancyCsgFrameData{ occupancyUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector occupancyMaterialTypedRanges{ occupancyUploadScratch };
#endif
        MaterialTypedByteDataVector occupancyMaterialTypedBytes{ occupancyUploadScratch };
        AvboitPassUploadHelper occupancyUploadHelper(m_materialSystem);
        AvboitPassUploadResult occupancyUploadResult;
        if(!occupancyUploadHelper.gather(
            AvboitPassUploadInputs{
                .framebuffer = deferredTargets.avboit.lowFramebuffer.get(),
                .pass = MaterialPipelinePass::AvboitOccupancy,
                .csgFrameState = &csgFrameState,
                .frameBindings = &frameBindings,
                .csgResources = &csgResources,
                .meshViewState = &meshViewState,
                .materialInstances = materialInstances,
                .materialTyped = materialTyped,
                .csgReceiverRanges = csgReceiverRanges,
                .csgCutters = csgCutters,
                .csgClipContextSlots = csgClipContextSlots,
                .csgIntervalSampleState = Core::GpuGraphResourceId{},
            },
            occupancyDrawItems,
            occupancyInstanceData,
            occupancyCsgFrameData,
#if defined(NWB_DEBUG)
            occupancyMaterialTypedRanges,
#endif
            occupancyMaterialTypedBytes,
            occupancyUploadResult
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT occupancy resources were unavailable during graph declaration"));
            return;
        }

        const bool occupancyHasCsgDrawItems = occupancyUploadResult.hasCsgDrawItems;
        if(occupancyUploadResult.hasDrawItems){

            const MaterialPassDrawItems* const occupancyMaterialGeometryDrawSets[] = {
                &occupancyDrawItems.regular,
                &occupancyDrawItems.csg,
            };
            avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_deferredLightingTaskGraph,
                occupancyMaterialGeometryDrawSets,
                LengthOf(occupancyMaterialGeometryDrawSets),
                occupancyMaterialGeometryScratch,
                Name("render.avboit.occupancy.material_geometry"),
                "AVBOIT Occupancy Material Geometry",
                occupancyMaterialGeometrySet
            );
            if(!avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT occupancy material geometry states"));
                return;
            }
            occupancyMaterialSampledTexturesCollected =
                avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    occupancyMaterialGeometryDrawSets,
                    LengthOf(occupancyMaterialGeometryDrawSets),
                    occupancyMaterialGeometryScratch,
                    Name("render.avboit.occupancy.material_sampled_textures"),
                    "AVBOIT Occupancy Material Sampled Textures",
                    occupancyMaterialSampledTextureSet
                )
            ;
            if(!occupancyMaterialSampledTexturesCollected){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT occupancy material sampled textures"));
                return;
            }

            m_materialSystem.prepareMaterialPassInstanceUploadData(occupancyInstanceData, csgResources);
#if defined(NWB_DEBUG)
            if(
                occupancyInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || occupancyCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || occupancyCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT occupancy upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(occupancyInstanceData.size() == occupancyMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                occupancyMaterialTypedRanges,
                occupancyMaterialTypedBytes
            );
#endif

            const Core::GpuUploadBlobId occupancyInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                occupancyInstanceData.data(),
                occupancyInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId occupancyMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                occupancyMaterialTypedBytes.data(),
                occupancyMaterialTypedBytes.size(),
                alignof(u32)
            );
            if(!occupancyInstanceBlob.valid() || !occupancyMaterialTypedBlob.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT occupancy material upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint occupancyUploadScheduling;
            occupancyUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            occupancyUploadScheduling.forceSubmissionBoundary = false;
            occupancyUploadScheduling.allowPacketMerge = true;
            occupancyUploadScheduling.mergeWithPrevious = true;

            Core::GpuTaskDesc occupancyInstanceUploadDesc;
            occupancyInstanceUploadDesc
                .setIdentity(Name("render.avboit.occupancy.material_instances_upload"))
                .setMarkerLabel("AVBOIT Occupancy Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(occupancyUploadScheduling)
                .setDependencies(&occupancyUploadTask, 1u)
            ;
            occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                occupancyInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = occupancyInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!occupancyUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy material instance upload"));
                return;
            }

            Core::GpuTaskDesc occupancyMaterialTypedUploadDesc;
            occupancyMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.occupancy.material_typed_upload"))
                .setMarkerLabel("AVBOIT Occupancy Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(occupancyUploadScheduling)
                .setDependencies(&occupancyUploadTask, 1u)
            ;
            occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                occupancyMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = occupancyMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!occupancyUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy material typed upload"));
                return;
            }

            if(occupancyHasCsgDrawItems){
                CsgClipContextSlots occupancyCsgClipContextSlotData;
                if(!m_csgSystem.prepareCsgClipContextSlotData(
                    deferredTargets,
                    occupancyCsgFrameData,
                    csgResources,
                    frameBindings,
                    occupancyCsgClipContextSlotData
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT occupancy CSG context data"));
                    return;
                }
                const Core::GpuUploadBlobId occupancyCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                    occupancyCsgFrameData.receiverRanges.data(),
                    occupancyCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                    alignof(CsgReceiverRangeGpuData)
                );
                const Core::GpuUploadBlobId occupancyCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                    occupancyCsgFrameData.cutters.data(),
                    occupancyCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                    alignof(CsgCutterGpuData)
                );
                const Core::GpuUploadBlobId occupancyCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                    &occupancyCsgClipContextSlotData,
                    sizeof(occupancyCsgClipContextSlotData),
                    alignof(CsgClipContextSlots)
                );
                if(
                    !occupancyCsgReceiverRangesBlob.valid()
                    || !occupancyCsgCuttersBlob.valid()
                    || !occupancyCsgClipContextSlotsBlob.valid()
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT occupancy CSG upload data"));
                    return;
                }

                Core::GpuTaskDesc occupancyCsgReceiverRangesUploadDesc;
                occupancyCsgReceiverRangesUploadDesc
                    .setIdentity(Name("render.avboit.occupancy.csg_receiver_ranges_upload"))
                    .setMarkerLabel("AVBOIT Occupancy CSG Receiver Ranges Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(occupancyUploadScheduling)
                    .setDependencies(&occupancyUploadTask, 1u)
                ;
                occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    occupancyCsgReceiverRangesUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = occupancyCsgReceiverRangesBlob,
                        .destination = csgReceiverRanges,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!occupancyUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy CSG receiver-range upload"));
                    return;
                }

                Core::GpuTaskDesc occupancyCsgCuttersUploadDesc;
                occupancyCsgCuttersUploadDesc
                    .setIdentity(Name("render.avboit.occupancy.csg_cutters_upload"))
                    .setMarkerLabel("AVBOIT Occupancy CSG Cutters Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(occupancyUploadScheduling)
                    .setDependencies(&occupancyUploadTask, 1u)
                ;
                occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    occupancyCsgCuttersUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = occupancyCsgCuttersBlob,
                        .destination = csgCutters,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!occupancyUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy CSG cutter upload"));
                    return;
                }

                Core::GpuTaskDesc occupancyCsgClipContextSlotsUploadDesc;
                occupancyCsgClipContextSlotsUploadDesc
                    .setIdentity(Name("render.avboit.occupancy.csg_clip_context_slots_upload"))
                    .setMarkerLabel("AVBOIT Occupancy CSG Clip Context Slots Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(occupancyUploadScheduling)
                    .setDependencies(&occupancyUploadTask, 1u)
                ;
                occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    occupancyCsgClipContextSlotsUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = occupancyCsgClipContextSlotsBlob,
                        .destination = csgClipContextSlots,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!occupancyUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy CSG clip-context upload"));
                    return;
                }
                occupancyCsgStreamsUploaded = true;
            }

            avboitOccupancyPayload.occupancySnapshot.capture(
                occupancyDrawItems,
                occupancyCsgFrameData,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size()
            );
            avboitOccupancyPayload.occupancyPhasePrepared = true;
            avboitOccupancyPayload.occupancyStreamsUploaded = true;
            // A phase owns one alias-free stream; mixed work keeps local interleaving.
            occupancyRegularComputeEmulationPlanCaptured = occupancyDrawItems.csg.computeDrawItems.empty()
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && avboitOccupancyComputeEmulationPayload.plan.capture(occupancyDrawItems.regular, occupancyUploadScratch)
            ;
            occupancyCsgComputeEmulationPlanCaptured = occupancyDrawItems.regular.computeDrawItems.empty()
                && occupancyCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && avboitOccupancyComputeEmulationPayload.csgPlan.capture(
                    occupancyDrawItems.csg,
                    occupancyCsgFrameData,
                    occupancyUploadScratch
                )
            ;
            // All-compute draws share one output only as an explicit D/R sequence; keep mesh/CSG out.
            occupancySharedComputeEmulationPlanCaptured = !occupancyRegularComputeEmulationPlanCaptured
                && occupancyDrawItems.regular.meshDrawItems.empty()
                && occupancyDrawItems.csg.empty()
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && occupancySharedComputeEmulationPlan.capture(
                    occupancyDrawItems.regular,
                    ECSRenderDetail::s_SharedComputeEmulationMaximumDrawCount
                )
                && ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
                    occupancySharedComputeEmulationPlan.drawCount
                )
            ;
            NWB_ASSERT(
                !(occupancyRegularComputeEmulationPlanCaptured && occupancyCsgComputeEmulationPlanCaptured)
            );
            NWB_ASSERT(
                !occupancySharedComputeEmulationPlanCaptured
                || (!occupancyRegularComputeEmulationPlanCaptured
                    && !occupancyCsgComputeEmulationPlanCaptured)
            );
            if(occupancySharedComputeEmulationPlanCaptured){
                occupancySharedComputeEmulationInstanceCount = occupancyInstanceData.size();
                occupancySharedComputeEmulationMaterialTypedByteCount = occupancyMaterialTypedBytes.size();
            }
        }
        else{
            // Graph phase stays authoritative for empty sets; retain snapshot to skip native re-gather.
            avboitOccupancyPayload.occupancySnapshot.capture(
                occupancyDrawItems,
                occupancyCsgFrameData,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size()
            );
            avboitOccupancyPayload.occupancyPhasePrepared = true;
        }
    }


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
        avboitOccupancyRecordInputs,
        avboitOccupancyPayload,
        avboitOccupancyComputeEmulationPayload,
        avboitOccupancyRecordResult
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
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

    Core::GpuTaskId extinctionUploadTask = avboitDepthWarpCompletionTask;
    bool extinctionStreamsUploaded = false;
    bool extinctionCsgStreamsUploaded = false;
    bool extinctionRegularComputeEmulationPlanCaptured = false;
    bool extinctionCsgComputeEmulationPlanCaptured = false;
    bool extinctionSharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan extinctionSharedComputeEmulationPlan;
    usize extinctionSharedComputeEmulationInstanceCount = 0u;
    usize extinctionSharedComputeEmulationMaterialTypedByteCount = 0u;
    bool extinctionMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena extinctionMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId extinctionMaterialGeometrySet;
    Core::GpuGraphResourceSetId extinctionMaterialSampledTextureSet;
        Core::Alloc::ScratchArena extinctionUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions extinctionDrawItems{ extinctionUploadScratch };
        InstanceGpuDataVector extinctionInstanceData{ extinctionUploadScratch };
        CsgFrameGpuData extinctionCsgFrameData{ extinctionUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector extinctionMaterialTypedRanges{ extinctionUploadScratch };
#endif
        MaterialTypedByteDataVector extinctionMaterialTypedBytes{ extinctionUploadScratch };
        AvboitPassUploadHelper extinctionUploadHelper(m_materialSystem);
        AvboitPassUploadResult extinctionUploadResult;
        if(!extinctionUploadHelper.gather(
            AvboitPassUploadInputs{
                .framebuffer = deferredTargets.avboit.lowFramebuffer.get(),
                .pass = MaterialPipelinePass::AvboitExtinction,
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
            },
            extinctionDrawItems,
            extinctionInstanceData,
            extinctionCsgFrameData,
#if defined(NWB_DEBUG)
            extinctionMaterialTypedRanges,
#endif
            extinctionMaterialTypedBytes,
            extinctionUploadResult
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT extinction resources were unavailable during graph declaration"));
            return;
        }

        const bool extinctionHasCsgDrawItems = extinctionUploadResult.hasCsgDrawItems;
        if(extinctionUploadResult.hasDrawItems){

            const MaterialPassDrawItems* const extinctionMaterialGeometryDrawSets[] = {
                &extinctionDrawItems.regular,
                &extinctionDrawItems.csg,
            };
            avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_deferredLightingTaskGraph,
                extinctionMaterialGeometryDrawSets,
                LengthOf(extinctionMaterialGeometryDrawSets),
                extinctionMaterialGeometryScratch,
                Name("render.avboit.extinction.material_geometry"),
                "AVBOIT Extinction Material Geometry",
                extinctionMaterialGeometrySet
            );
            if(!avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT extinction material geometry states"));
                return;
            }
            extinctionMaterialSampledTexturesCollected =
                avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    extinctionMaterialGeometryDrawSets,
                    LengthOf(extinctionMaterialGeometryDrawSets),
                    extinctionMaterialGeometryScratch,
                    Name("render.avboit.extinction.material_sampled_textures"),
                    "AVBOIT Extinction Material Sampled Textures",
                    extinctionMaterialSampledTextureSet
                )
            ;
            if(!extinctionMaterialSampledTexturesCollected){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT extinction material sampled textures"));
                return;
            }

            m_materialSystem.prepareMaterialPassInstanceUploadData(extinctionInstanceData, csgResources);
#if defined(NWB_DEBUG)
            if(
                extinctionInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || extinctionCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || extinctionCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT extinction upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(extinctionInstanceData.size() == extinctionMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                extinctionMaterialTypedRanges,
                extinctionMaterialTypedBytes
            );
#endif

            const Core::GpuUploadBlobId extinctionInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                extinctionInstanceData.data(),
                extinctionInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId extinctionMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                extinctionMaterialTypedBytes.data(),
                extinctionMaterialTypedBytes.size(),
                alignof(u32)
            );
            if(!extinctionInstanceBlob.valid() || !extinctionMaterialTypedBlob.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT extinction material upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint extinctionUploadScheduling;
            extinctionUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            extinctionUploadScheduling.forceSubmissionBoundary = false;
            extinctionUploadScheduling.allowPacketMerge = true;
            extinctionUploadScheduling.mergeWithPrevious = true;

            Core::GpuTaskDesc extinctionInstanceUploadDesc;
            extinctionInstanceUploadDesc
                .setIdentity(Name("render.avboit.extinction.material_instances_upload"))
                .setMarkerLabel("AVBOIT Extinction Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(extinctionUploadScheduling)
                .setDependencies(&extinctionUploadTask, 1u)
            ;
            extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                extinctionInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = extinctionInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!extinctionUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction material instance upload"));
                return;
            }

            Core::GpuTaskDesc extinctionMaterialTypedUploadDesc;
            extinctionMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.extinction.material_typed_upload"))
                .setMarkerLabel("AVBOIT Extinction Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(extinctionUploadScheduling)
                .setDependencies(&extinctionUploadTask, 1u)
            ;
            extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                extinctionMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = extinctionMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!extinctionUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction material typed upload"));
                return;
            }

            if(extinctionHasCsgDrawItems){
                CsgClipContextSlots extinctionCsgClipContextSlotData;
                if(!m_csgSystem.prepareCsgClipContextSlotData(
                    deferredTargets,
                    extinctionCsgFrameData,
                    csgResources,
                    frameBindings,
                    extinctionCsgClipContextSlotData
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT extinction CSG context data"));
                    return;
                }
                const Core::GpuUploadBlobId extinctionCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                    extinctionCsgFrameData.receiverRanges.data(),
                    extinctionCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                    alignof(CsgReceiverRangeGpuData)
                );
                const Core::GpuUploadBlobId extinctionCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                    extinctionCsgFrameData.cutters.data(),
                    extinctionCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                    alignof(CsgCutterGpuData)
                );
                const Core::GpuUploadBlobId extinctionCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                    &extinctionCsgClipContextSlotData,
                    sizeof(extinctionCsgClipContextSlotData),
                    alignof(CsgClipContextSlots)
                );
                if(
                    !extinctionCsgReceiverRangesBlob.valid()
                    || !extinctionCsgCuttersBlob.valid()
                    || !extinctionCsgClipContextSlotsBlob.valid()
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT extinction CSG upload data"));
                    return;
                }

                Core::GpuTaskDesc extinctionCsgReceiverRangesUploadDesc;
                extinctionCsgReceiverRangesUploadDesc
                    .setIdentity(Name("render.avboit.extinction.csg_receiver_ranges_upload"))
                    .setMarkerLabel("AVBOIT Extinction CSG Receiver Ranges Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(extinctionUploadScheduling)
                    .setDependencies(&extinctionUploadTask, 1u)
                ;
                extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    extinctionCsgReceiverRangesUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = extinctionCsgReceiverRangesBlob,
                        .destination = csgReceiverRanges,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!extinctionUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction CSG receiver-range upload"));
                    return;
                }

                Core::GpuTaskDesc extinctionCsgCuttersUploadDesc;
                extinctionCsgCuttersUploadDesc
                    .setIdentity(Name("render.avboit.extinction.csg_cutters_upload"))
                    .setMarkerLabel("AVBOIT Extinction CSG Cutters Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(extinctionUploadScheduling)
                    .setDependencies(&extinctionUploadTask, 1u)
                ;
                extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    extinctionCsgCuttersUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = extinctionCsgCuttersBlob,
                        .destination = csgCutters,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!extinctionUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction CSG cutter upload"));
                    return;
                }

                Core::GpuTaskDesc extinctionCsgClipContextSlotsUploadDesc;
                extinctionCsgClipContextSlotsUploadDesc
                    .setIdentity(Name("render.avboit.extinction.csg_clip_context_slots_upload"))
                    .setMarkerLabel("AVBOIT Extinction CSG Clip Context Slots Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(extinctionUploadScheduling)
                    .setDependencies(&extinctionUploadTask, 1u)
                ;
                extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    extinctionCsgClipContextSlotsUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = extinctionCsgClipContextSlotsBlob,
                        .destination = csgClipContextSlots,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!extinctionUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction CSG clip-context upload"));
                    return;
                }
                extinctionCsgStreamsUploaded = true;
            }

            avboitExtinctionPayload.extinctionSnapshot.capture(
                extinctionDrawItems,
                extinctionCsgFrameData,
                extinctionInstanceData.size(),
                extinctionMaterialTypedBytes.size()
            );
            avboitExtinctionPayload.extinctionPhasePrepared = true;
            extinctionStreamsUploaded = true;
            // Mixed work keeps local interleaving; one handoff cannot preserve per-draw order.
            extinctionRegularComputeEmulationPlanCaptured = extinctionDrawItems.csg.computeDrawItems.empty()
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && avboitExtinctionComputeEmulationPayload.plan.capture(extinctionDrawItems.regular, extinctionUploadScratch)
            ;
            extinctionCsgComputeEmulationPlanCaptured = extinctionDrawItems.regular.computeDrawItems.empty()
                && extinctionCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && avboitExtinctionComputeEmulationPayload.csgPlan.capture(
                    extinctionDrawItems.csg,
                    extinctionCsgFrameData,
                    extinctionUploadScratch
                )
            ;
            extinctionSharedComputeEmulationPlanCaptured = !extinctionRegularComputeEmulationPlanCaptured
                && extinctionDrawItems.regular.meshDrawItems.empty()
                && extinctionDrawItems.csg.empty()
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && extinctionSharedComputeEmulationPlan.capture(
                    extinctionDrawItems.regular,
                    ECSRenderDetail::s_SharedComputeEmulationMaximumDrawCount
                )
                && ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
                    extinctionSharedComputeEmulationPlan.drawCount
                )
            ;
            if(extinctionSharedComputeEmulationPlanCaptured){
                extinctionSharedComputeEmulationInstanceCount = extinctionInstanceData.size();
                extinctionSharedComputeEmulationMaterialTypedByteCount = extinctionMaterialTypedBytes.size();
            }
            NWB_ASSERT(!(extinctionRegularComputeEmulationPlanCaptured && extinctionCsgComputeEmulationPlanCaptured));
            NWB_ASSERT(!(extinctionRegularComputeEmulationPlanCaptured && extinctionSharedComputeEmulationPlanCaptured));
            NWB_ASSERT(!(extinctionCsgComputeEmulationPlanCaptured && extinctionSharedComputeEmulationPlanCaptured));
        }
        else{
            // Keep graph ownership for empty phases; skip native re-gather of mutable state.
            avboitExtinctionPayload.extinctionSnapshot.capture(
                extinctionDrawItems,
                extinctionCsgFrameData,
                extinctionInstanceData.size(),
                extinctionMaterialTypedBytes.size()
            );
            avboitExtinctionPayload.extinctionPhasePrepared = true;
        }
    const bool extinctionCsgIntervalSampleImageStatesGraphOwned =
        avboitIntervalOutputsGraphOwned && extinctionCsgStreamsUploaded
    ;
    const bool extinctionCsgClipBufferStatesGraphOwned = extinctionCsgStreamsUploaded;
    NWB_ASSERT(
        !extinctionCsgIntervalSampleImageStatesGraphOwned
        || (
            avboitExtinctionPayload.extinctionPhasePrepared
            && avboitExtinctionPayload.extinctionSnapshot.captured
        )
    );
    NWB_ASSERT(
        !extinctionCsgClipBufferStatesGraphOwned
        || (
            avboitExtinctionPayload.extinctionPhasePrepared
            && avboitExtinctionPayload.extinctionSnapshot.captured
        )
    );
    avboitExtinctionPayload.extinctionCsgIntervalSampleImageStatesGraphOwned =
        extinctionCsgIntervalSampleImageStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionCsgClipBufferStatesGraphOwned =
        extinctionCsgClipBufferStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionMaterialFrameStatesGraphOwned = extinctionStreamsUploaded;
    avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned =
        extinctionStreamsUploaded
        && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId extinctionComputeEmulationOutputSet;
    Core::Alloc::ScratchArena extinctionComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool extinctionComputeEmulationPlanCaptured =
        extinctionRegularComputeEmulationPlanCaptured
        || extinctionCsgComputeEmulationPlanCaptured
    ;
    bool extinctionComputeEmulationOutputStatesGraphOwned = false;
    if(extinctionRegularComputeEmulationPlanCaptured){
        extinctionComputeEmulationOutputStatesGraphOwned = GatherImportedOutputBufferResourceSet(
            m_deferredLightingTaskGraph,
            avboitExtinctionComputeEmulationPayload.plan,
            extinctionComputeEmulationResourceScratch,
            Name("render.avboit.extinction.compute_emulation.outputs"),
            "AVBOIT Extinction Compute Emulation Outputs",
            extinctionComputeEmulationOutputSet
        );
    }
    else if(extinctionCsgComputeEmulationPlanCaptured){
        extinctionComputeEmulationOutputStatesGraphOwned =
            GatherImportedOutputBufferResourceSet(
                m_deferredLightingTaskGraph,
                avboitExtinctionComputeEmulationPayload.csgPlan,
                extinctionComputeEmulationResourceScratch,
                Name("render.avboit.extinction.csg_compute_emulation.outputs"),
                "AVBOIT Extinction CSG Compute Emulation Outputs",
                extinctionComputeEmulationOutputSet
            )
        ;
    }
    if(
        extinctionComputeEmulationPlanCaptured
        && !extinctionComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Extinction compute-emulation output states"
        ));
    }
    Core::GpuGraphResourceId extinctionSharedComputeEmulationOutput;
    const bool extinctionSharedComputeEmulationOutputStatesGraphOwned =
        extinctionSharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            extinctionSharedComputeEmulationPlan,
            "AVBOIT Extinction Shared Compute Emulation Output",
            extinctionSharedComputeEmulationOutput
        )
    ;
    if(
        extinctionSharedComputeEmulationPlanCaptured
        && !extinctionSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Extinction shared compute-emulation output"
        ));
    }
    avboitExtinctionPayload.extinctionComputeEmulationOutputStatesGraphOwned =
        extinctionRegularComputeEmulationPlanCaptured
        && extinctionComputeEmulationOutputStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionCsgComputeEmulationOutputStatesGraphOwned =
        extinctionCsgComputeEmulationPlanCaptured
        && extinctionComputeEmulationOutputStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionComputeEmulationTiming =
        extinctionComputeEmulationOutputStatesGraphOwned
            ? &avboitExtinctionComputeEmulationTiming
            : nullptr
    ;
    const Core::BufferRange extinctionInstanceRange(
        0u,
        avboitExtinctionPayload.extinctionSnapshot.instanceCount * sizeof(InstanceGpuData)
    );
    const Core::BufferRange extinctionMaterialTypedRange(0u, avboitExtinctionPayload.extinctionSnapshot.materialTypedByteCount);
    const Core::BufferRange extinctionReceiverRange(
        0u,
        avboitExtinctionPayload.extinctionSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    const Core::BufferRange extinctionCutterRange(
        0u,
        avboitExtinctionPayload.extinctionSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData)
    );

    Core::Alloc::ScratchArena extinctionResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionResourceUses{ extinctionResourceScratch };
    extinctionResourceUses.reserve(
        16u
        + (extinctionStreamsUploaded ? 7u : 0u)
        + (extinctionCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    // Keep the full raster contract on every route so crossings retain hazards and lowering.
    extinctionResourceUses.push_back(ReadUse(albedo));
    extinctionResourceUses.push_back(ReadUse(refractionInstance));
    extinctionResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
    extinctionResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
    extinctionResourceUses.push_back(ReadUse(depth));
    extinctionResourceUses.push_back(ReadWriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget));
    extinctionResourceUses.push_back(ReadUse(avboitDepthWarp));
    extinctionResourceUses.push_back(ReadUse(avboitControl));
    extinctionResourceUses.push_back(ReadWriteUse(avboitExtinction, Core::ResourceStates::UnorderedAccess));
    extinctionResourceUses.push_back(ReadWriteUse(avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess));
    if(extinctionStreamsUploaded){
        extinctionResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        extinctionResourceUses.push_back(ReadBufferUse(materialInstances, extinctionInstanceRange));
        extinctionResourceUses.push_back(ReadBufferUse(materialTyped, extinctionMaterialTypedRange));
        if(extinctionCsgStreamsUploaded){
            extinctionResourceUses.push_back(ReadBufferUse(csgReceiverRanges, extinctionReceiverRange));
            extinctionResourceUses.push_back(ReadBufferUse(csgCutters, extinctionCutterRange));
            extinctionResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // Interval producer owns this sample state through all low-raster phases.
            extinctionResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(extinctionCsgIntervalSampleImageStatesGraphOwned){
                // Interval producer wrote these aliases; graph lowers the same-UAV handoff.
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalDepth,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCapNormal,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalData,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCount,
                    csgRemovedIntervalCountSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
            }
        }
    }
    const Core::GpuTaskResourceSetUse extinctionMaterialGeometrySetUse{
        .resourceSet = extinctionMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse extinctionMaterialSampledTextureSetUse{
        .resourceSet = extinctionMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse extinctionComputeEmulationOutputUavSetUse{
        .resourceSet = extinctionComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse extinctionComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = extinctionComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse extinctionMaterialResourceSetUses[3u] = {};
    usize extinctionMaterialResourceSetUseCount = 0u;
    if(avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned)
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] = extinctionMaterialGeometrySetUse;
    if(extinctionMaterialSampledTextureSet.valid())
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] = extinctionMaterialSampledTextureSetUse;
    if(extinctionComputeEmulationOutputStatesGraphOwned){
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] =
            extinctionComputeEmulationOutputVertexBufferSetUse;
    }
    extinctionResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    extinctionResourceUses.push_back(ReadUse(avboitMaterialDomain));
    extinctionResourceUses.push_back(ReadUse(avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitExtinctionScheduling;
    avboitExtinctionScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitExtinctionScheduling.forceSubmissionBoundary = false;
    avboitExtinctionScheduling.allowPacketMerge = true;
    avboitExtinctionScheduling.mergeWithPrevious = true;
    avboitExtinctionScheduling.allowMergeAcrossConsumerFrontier = true;


    // Keep the final upload as stream anchor; replacing it hides a broken producer handoff.
    const Core::GpuTaskId extinctionStreamTask = extinctionUploadTask;
    if(extinctionStreamsUploaded)
        m_avboitSystem.taskGraphStage().m_extinctionStreamTask = extinctionStreamTask;
    Core::GpuTaskId extinctionDependency = extinctionUploadTask;
    if(extinctionComputeEmulationOutputStatesGraphOwned){
        avboitExtinctionComputeEmulationPayload.graphics = &m_graphics;
        avboitExtinctionComputeEmulationPayload.materialSystem = &m_materialSystem;
        avboitExtinctionComputeEmulationPayload.targets = &deferredTargets;
        avboitExtinctionComputeEmulationPayload.timingTicket = avboitExtinctionPayload.timingTicket;
        avboitExtinctionComputeEmulationPayload.extinctionTiming = &avboitExtinctionComputeEmulationTiming;
        avboitExtinctionComputeEmulationPayload.instanceCount = extinctionInstanceData.size();
        avboitExtinctionComputeEmulationPayload.materialTypedByteCount = extinctionMaterialTypedBytes.size();
        avboitExtinctionComputeEmulationPayload.materialDrawBuffersUploaded = extinctionStreamsUploaded;
        avboitExtinctionComputeEmulationPayload.csgFrameBuffersUploaded = extinctionCsgStreamsUploaded;
        avboitExtinctionComputeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            extinctionCsgIntervalSampleImageStatesGraphOwned;
        avboitExtinctionComputeEmulationPayload.csgClipBufferStatesGraphOwned =
            extinctionCsgClipBufferStatesGraphOwned;
        avboitExtinctionComputeEmulationPayload.materialFrameStatesGraphOwned =
            avboitExtinctionPayload.extinctionMaterialFrameStatesGraphOwned;
        avboitExtinctionComputeEmulationPayload.materialGeometryStatesGraphOwned =
            avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionComputeEmulationResourceUses{
            extinctionResourceScratch
        };
        extinctionComputeEmulationResourceUses.reserve(
            4u + (extinctionCsgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        extinctionComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        extinctionComputeEmulationResourceUses.push_back(
            ReadBufferUse(materialInstances, extinctionInstanceRange)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadBufferUse(materialTyped, extinctionMaterialTypedRange)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(extinctionCsgComputeEmulationPlanCaptured){
            extinctionComputeEmulationResourceUses.push_back(
                ReadBufferUse(csgReceiverRanges, extinctionReceiverRange)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadBufferUse(csgCutters, extinctionCutterRange)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse extinctionComputeEmulationResourceSetUses[3u] = {};
        usize extinctionComputeEmulationResourceSetUseCount = 0u;
        extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
            extinctionMaterialGeometrySetUse;
        if(extinctionMaterialSampledTextureSet.valid()){
            extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
                extinctionMaterialSampledTextureSetUse;
        }
        extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
            extinctionComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint extinctionComputeEmulationScheduling;
        extinctionComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        extinctionComputeEmulationScheduling.forceSubmissionBoundary = false;
        extinctionComputeEmulationScheduling.allowPacketMerge = true;
        extinctionComputeEmulationScheduling.mergeWithPrevious = true;
        // Next raster consumes the producer UAV output and shares its timing ticket.
        extinctionComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc extinctionComputeEmulationDesc;
        extinctionComputeEmulationDesc
            .setIdentity(extinctionCsgComputeEmulationPlanCaptured
                ? Name("render.avboit.extinction.csg_compute_emulation")
                : Name("render.avboit.extinction.compute_emulation"))
            .setMarkerLabel(extinctionCsgComputeEmulationPlanCaptured
                ? "AVBOIT Extinction CSG Compute Emulation"
                : "AVBOIT Extinction Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(extinctionComputeEmulationScheduling)
            .setDependencies(&extinctionDependency, 1u)
            .setResourceUses(
                extinctionComputeEmulationResourceUses.data(),
                extinctionComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                extinctionComputeEmulationResourceSetUses,
                extinctionComputeEmulationResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            AvboitExtinctionComputeEmulationGraphTask
        >(
            extinctionComputeEmulationDesc,
            Move(avboitExtinctionComputeEmulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Extinction compute-emulation producer"
            ));
            return;
        }
        extinctionDependency = m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask;
        avboitExtinctionScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(extinctionSharedComputeEmulationOutputStatesGraphOwned){
        // Keep the retained output concrete so the compiler preserves alternating uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionSharedGenerateResourceUses{
            extinctionResourceScratch
        };
        extinctionSharedGenerateResourceUses.reserve(5u);
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        extinctionSharedGenerateResourceUses.push_back(ReadBufferUse(materialInstances, extinctionInstanceRange));
        extinctionSharedGenerateResourceUses.push_back(ReadBufferUse(materialTyped, extinctionMaterialTypedRange));
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        extinctionSharedGenerateResourceUses.push_back(WriteUse(
            extinctionSharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionSharedRasterResourceUses{
            extinctionResourceScratch
        };
        extinctionSharedRasterResourceUses.assign(
            extinctionResourceUses.begin(),
            extinctionResourceUses.end()
        );
        extinctionSharedRasterResourceUses.push_back(ReadUse(
            extinctionSharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint extinctionSharedComputeEmulationScheduling;
        extinctionSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        extinctionSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        extinctionSharedComputeEmulationScheduling.allowPacketMerge = true;
        extinctionSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Integration and Accumulation consume the terminal raster; successors carry dependencies.
        extinctionSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addExtinctionSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &extinctionSharedComputeEmulationPlan,
            &avboitExtinctionComputeEmulationTiming,
            &frameBindings,
            extinctionSharedComputeEmulationInstanceCount,
            extinctionSharedComputeEmulationMaterialTypedByteCount,
            extinctionStreamsUploaded,
            extinctionMaterialFrameStatesGraphOwned = avboitExtinctionPayload.extinctionMaterialFrameStatesGraphOwned,
            extinctionMaterialGeometryStatesGraphOwned = avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned,
            avboitExtinctionTimingTicket = avboitExtinctionPayload.timingTicket,
            &extinctionSharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitExtinctionSharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool beginTiming,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(extinctionSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitExtinctionSharedComputeEmulationGraphTask::Payload payload;
            payload.frameBindings = frameBindings;
            payload.graphics = &m_graphics;
            payload.materialSystem = &m_materialSystem;
            payload.targets = &deferredTargets;
            payload.timingTicket = avboitExtinctionTimingTicket;
            payload.extinctionTiming = &avboitExtinctionComputeEmulationTiming;
            payload.plan = extinctionSharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = extinctionSharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = extinctionSharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = extinctionStreamsUploaded;
            payload.materialFrameStatesGraphOwned = extinctionMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = extinctionMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<AvboitExtinctionSharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using ExtinctionSharedPhase = AvboitExtinctionSharedComputeEmulationGraphTask::Phase;
        const Name extinctionSharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.extinction.shared_compute_emulation_generate_a"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_a"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_b"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_b"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_c"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_c"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_d"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_d"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_e"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_e"),
        };
        const AStringView extinctionSharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Extinction Shared Compute Emulation Generate A",
            "AVBOIT Extinction Shared Compute Emulation Raster A",
            "AVBOIT Extinction Shared Compute Emulation Generate B",
            "AVBOIT Extinction Shared Compute Emulation Raster B",
            "AVBOIT Extinction Shared Compute Emulation Generate C",
            "AVBOIT Extinction Shared Compute Emulation Raster C",
            "AVBOIT Extinction Shared Compute Emulation Generate D",
            "AVBOIT Extinction Shared Compute Emulation Raster D",
            "AVBOIT Extinction Shared Compute Emulation Generate E",
            "AVBOIT Extinction Shared Compute Emulation Raster E",
        };
        const usize extinctionSharedComputeEmulationPhaseCount =
            ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(
                extinctionSharedComputeEmulationPlan.drawCount
            )
        ;
        NWB_ASSERT(ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            extinctionSharedComputeEmulationPlan.drawCount
        ));
        NWB_ASSERT(extinctionSharedComputeEmulationPhaseCount <= LengthOf(extinctionSharedComputeEmulationPhaseIdentities));
        Core::GpuTaskId extinctionSharedComputeEmulationDependency = extinctionDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < extinctionSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase =
                phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
            m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTasks[phaseIndex] =
                addExtinctionSharedComputeEmulationPhase(
                    extinctionSharedComputeEmulationPhaseIdentities[phaseIndex],
                    extinctionSharedComputeEmulationPhaseMarkers[phaseIndex],
                    extinctionSharedComputeEmulationDependency,
                    isRasterPhase ? ExtinctionSharedPhase::Raster : ExtinctionSharedPhase::Generate,
                    phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
                    phaseIndex == 0u,
                    phaseIndex + 1u == extinctionSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? extinctionSharedRasterResourceUses
                        : extinctionSharedGenerateResourceUses,
                    extinctionMaterialResourceSetUses,
                    extinctionMaterialResourceSetUseCount
                )
            ;
            if(!m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Extinction shared compute-emulation phase"
                ));
                return;
            }
            extinctionSharedComputeEmulationDependency =
                m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTasks[phaseIndex];
        }
        m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTaskCount =
            extinctionSharedComputeEmulationPhaseCount;
        // Terminal raster is the Extinction endpoint; Integration follows with graph-derived ownership.
        m_avboitSystem.taskGraphStage().m_extinctionTask = extinctionSharedComputeEmulationDependency;
    }
    else{
    Core::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("render.avboit.extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(avboitExtinctionScheduling)
        .setDependencies(&extinctionDependency, 1u)
        .setResourceUses(extinctionResourceUses.data(), extinctionResourceUses.size())
        .setResourceSetUses(
            extinctionMaterialResourceSetUseCount != 0u ? extinctionMaterialResourceSetUses : nullptr,
            extinctionMaterialResourceSetUseCount
        )
    ;
    m_avboitSystem.taskGraphStage().m_extinctionTask = m_deferredLightingTaskGraph.addTask<AvboitExtinctionGraphTask>(
        extinctionDesc,
        Move(avboitExtinctionPayload)
    );
    if(!m_avboitSystem.taskGraphStage().m_extinctionTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
        return;
    }
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

    Core::GpuTaskId accumulationUploadTask = m_avboitSystem.taskGraphStage().m_integrationTask;
    bool accumulationStreamsUploaded = false;
    bool accumulationCsgStreamsUploaded = false;
    bool accumulationRegularComputeEmulationPlanCaptured = false;
    bool accumulationCsgComputeEmulationPlanCaptured = false;
    bool accumulationSharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan accumulationSharedComputeEmulationPlan;
    usize accumulationSharedComputeEmulationInstanceCount = 0u;
    usize accumulationSharedComputeEmulationMaterialTypedByteCount = 0u;
    bool accumulationMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena accumulationMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId accumulationMaterialGeometrySet;
    Core::GpuGraphResourceSetId accumulationMaterialSampledTextureSet;
    {
        Core::Alloc::ScratchArena accumulationUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions accumulationDrawItems{ accumulationUploadScratch };
        InstanceGpuDataVector accumulationInstanceData{ accumulationUploadScratch };
        CsgFrameGpuData accumulationCsgFrameData{ accumulationUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector accumulationMaterialTypedRanges{ accumulationUploadScratch };
#endif
        MaterialTypedByteDataVector accumulationMaterialTypedBytes{ accumulationUploadScratch };
        AvboitPassUploadHelper accumulationUploadHelper(m_materialSystem);
        AvboitPassUploadResult accumulationUploadResult;
        if(!accumulationUploadHelper.gather(
            AvboitPassUploadInputs{
                .framebuffer = deferredTargets.avboit.accumulationFramebuffer.get(),
                .pass = MaterialPipelinePass::AvboitAccumulate,
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
            },
            accumulationDrawItems,
            accumulationInstanceData,
            accumulationCsgFrameData,
#if defined(NWB_DEBUG)
            accumulationMaterialTypedRanges,
#endif
            accumulationMaterialTypedBytes,
            accumulationUploadResult
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT accumulation resources were unavailable during graph declaration"));
            return;
        }

        const bool accumulationHasCsgDrawItems = accumulationUploadResult.hasCsgDrawItems;
        if(accumulationUploadResult.hasDrawItems){

            const MaterialPassDrawItems* const accumulationMaterialGeometryDrawSets[] = {
                &accumulationDrawItems.regular,
                &accumulationDrawItems.csg,
            };
            avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_deferredLightingTaskGraph,
                accumulationMaterialGeometryDrawSets,
                LengthOf(accumulationMaterialGeometryDrawSets),
                accumulationMaterialGeometryScratch,
                Name("render.avboit.accumulation.material_geometry"),
                "AVBOIT Accumulation Material Geometry",
                accumulationMaterialGeometrySet
            );
            if(!avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT accumulation material geometry states"));
                return;
            }
            accumulationMaterialSampledTexturesCollected =
                avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    accumulationMaterialGeometryDrawSets,
                    LengthOf(accumulationMaterialGeometryDrawSets),
                    accumulationMaterialGeometryScratch,
                    Name("render.avboit.accumulation.material_sampled_textures"),
                    "AVBOIT Accumulation Material Sampled Textures",
                    accumulationMaterialSampledTextureSet
                )
            ;
            if(!accumulationMaterialSampledTexturesCollected){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT accumulation material sampled textures"));
                return;
            }

            m_materialSystem.prepareMaterialPassInstanceUploadData(accumulationInstanceData, csgResources);
#if defined(NWB_DEBUG)
            if(
                accumulationInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || accumulationCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || accumulationCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT accumulation upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(accumulationInstanceData.size() == accumulationMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                accumulationMaterialTypedRanges,
                accumulationMaterialTypedBytes
            );
#endif

            const Core::GpuUploadBlobId accumulationInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                accumulationInstanceData.data(),
                accumulationInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId accumulationMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                accumulationMaterialTypedBytes.data(),
                accumulationMaterialTypedBytes.size(),
                alignof(u32)
            );
            if(!accumulationInstanceBlob.valid() || !accumulationMaterialTypedBlob.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT accumulation material upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint accumulationUploadScheduling;
            accumulationUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            accumulationUploadScheduling.forceSubmissionBoundary = false;
            accumulationUploadScheduling.allowPacketMerge = true;
            accumulationUploadScheduling.mergeWithPrevious = true;

            Core::GpuTaskDesc accumulationInstanceUploadDesc;
            accumulationInstanceUploadDesc
                .setIdentity(Name("render.avboit.accumulation.material_instances_upload"))
                .setMarkerLabel("AVBOIT Accumulation Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulationUploadScheduling)
                .setDependencies(&accumulationUploadTask, 1u)
            ;
            accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                accumulationInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = accumulationInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!accumulationUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation material instance upload"));
                return;
            }

            Core::GpuTaskDesc accumulationMaterialTypedUploadDesc;
            accumulationMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.accumulation.material_typed_upload"))
                .setMarkerLabel("AVBOIT Accumulation Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulationUploadScheduling)
                .setDependencies(&accumulationUploadTask, 1u)
            ;
            accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                accumulationMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = accumulationMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!accumulationUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation material typed upload"));
                return;
            }

            if(accumulationHasCsgDrawItems){
                CsgClipContextSlots accumulationCsgClipContextSlotData;
                if(!m_csgSystem.prepareCsgClipContextSlotData(
                    deferredTargets,
                    accumulationCsgFrameData,
                    csgResources,
                    frameBindings,
                    accumulationCsgClipContextSlotData
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT accumulation CSG context data"));
                    return;
                }
                const Core::GpuUploadBlobId accumulationCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                    accumulationCsgFrameData.receiverRanges.data(),
                    accumulationCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                    alignof(CsgReceiverRangeGpuData)
                );
                const Core::GpuUploadBlobId accumulationCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                    accumulationCsgFrameData.cutters.data(),
                    accumulationCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                    alignof(CsgCutterGpuData)
                );
                const Core::GpuUploadBlobId accumulationCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                    &accumulationCsgClipContextSlotData,
                    sizeof(accumulationCsgClipContextSlotData),
                    alignof(CsgClipContextSlots)
                );
                if(
                    !accumulationCsgReceiverRangesBlob.valid()
                    || !accumulationCsgCuttersBlob.valid()
                    || !accumulationCsgClipContextSlotsBlob.valid()
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT accumulation CSG upload data"));
                    return;
                }

                Core::GpuTaskDesc accumulationCsgReceiverRangesUploadDesc;
                accumulationCsgReceiverRangesUploadDesc
                    .setIdentity(Name("render.avboit.accumulation.csg_receiver_ranges_upload"))
                    .setMarkerLabel("AVBOIT Accumulation CSG Receiver Ranges Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(accumulationUploadScheduling)
                    .setDependencies(&accumulationUploadTask, 1u)
                ;
                accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    accumulationCsgReceiverRangesUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = accumulationCsgReceiverRangesBlob,
                        .destination = csgReceiverRanges,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!accumulationUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation CSG receiver-range upload"));
                    return;
                }

                Core::GpuTaskDesc accumulationCsgCuttersUploadDesc;
                accumulationCsgCuttersUploadDesc
                    .setIdentity(Name("render.avboit.accumulation.csg_cutters_upload"))
                    .setMarkerLabel("AVBOIT Accumulation CSG Cutters Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(accumulationUploadScheduling)
                    .setDependencies(&accumulationUploadTask, 1u)
                ;
                accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    accumulationCsgCuttersUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = accumulationCsgCuttersBlob,
                        .destination = csgCutters,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!accumulationUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation CSG cutter upload"));
                    return;
                }

                Core::GpuTaskDesc accumulationCsgClipContextSlotsUploadDesc;
                accumulationCsgClipContextSlotsUploadDesc
                    .setIdentity(Name("render.avboit.accumulation.csg_clip_context_slots_upload"))
                    .setMarkerLabel("AVBOIT Accumulation CSG Clip Context Slots Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(accumulationUploadScheduling)
                    .setDependencies(&accumulationUploadTask, 1u)
                ;
                accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    accumulationCsgClipContextSlotsUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = accumulationCsgClipContextSlotsBlob,
                        .destination = csgClipContextSlots,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!accumulationUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation CSG clip-context upload"));
                    return;
                }
                accumulationCsgStreamsUploaded = true;
            }

            avboitAccumulationPayload.accumulationSnapshot.capture(
                accumulationDrawItems,
                accumulationCsgFrameData,
                accumulationInstanceData.size(),
                accumulationMaterialTypedBytes.size()
            );
            avboitAccumulationPayload.accumulationPhasePrepared = true;
            accumulationStreamsUploaded = true;
            // A phase owns one alias-free stream; mixed work keeps local interleaving.
            accumulationRegularComputeEmulationPlanCaptured = accumulationDrawItems.csg.computeDrawItems.empty()
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && avboitAccumulationComputeEmulationPayload.plan.capture(accumulationDrawItems.regular, accumulationUploadScratch)
            ;
            accumulationCsgComputeEmulationPlanCaptured = accumulationDrawItems.regular.computeDrawItems.empty()
                && accumulationCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && avboitAccumulationComputeEmulationPayload.csgPlan.capture(
                    accumulationDrawItems.csg,
                    accumulationCsgFrameData,
                    accumulationUploadScratch
                )
            ;
            // All-compute draws share one output only as an explicit D/R sequence; keep mesh/CSG out.
            accumulationSharedComputeEmulationPlanCaptured = !accumulationRegularComputeEmulationPlanCaptured
                && accumulationDrawItems.regular.meshDrawItems.empty()
                && accumulationDrawItems.csg.empty()
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && accumulationSharedComputeEmulationPlan.capture(
                    accumulationDrawItems.regular,
                    ECSRenderDetail::s_SharedComputeEmulationMaximumDrawCount
                )
                && ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
                    accumulationSharedComputeEmulationPlan.drawCount
                )
            ;
            NWB_ASSERT(
                !(accumulationRegularComputeEmulationPlanCaptured && accumulationCsgComputeEmulationPlanCaptured)
            );
            NWB_ASSERT(
                !accumulationSharedComputeEmulationPlanCaptured
                || (!accumulationRegularComputeEmulationPlanCaptured
                    && !accumulationCsgComputeEmulationPlanCaptured)
            );
            if(
                accumulationRegularComputeEmulationPlanCaptured
                || accumulationCsgComputeEmulationPlanCaptured
            ){
                avboitAccumulationComputeEmulationPayload.instanceCount = accumulationInstanceData.size();
                avboitAccumulationComputeEmulationPayload.materialTypedByteCount = accumulationMaterialTypedBytes.size();
            }
            if(accumulationSharedComputeEmulationPlanCaptured){
                accumulationSharedComputeEmulationInstanceCount = accumulationInstanceData.size();
                accumulationSharedComputeEmulationMaterialTypedByteCount = accumulationMaterialTypedBytes.size();
            }
        }
        else{
            // An empty captured phase stays authoritative; recording must not re-gather state.
            avboitAccumulationPayload.accumulationSnapshot.capture(
                accumulationDrawItems,
                accumulationCsgFrameData,
                accumulationInstanceData.size(),
                accumulationMaterialTypedBytes.size()
            );
            avboitAccumulationPayload.accumulationPhasePrepared = true;
        }
    }

    const bool accumulationCsgIntervalSampleImageStatesGraphOwned =
        avboitIntervalOutputsGraphOwned && accumulationCsgStreamsUploaded
    ;
    const bool accumulationCsgClipBufferStatesGraphOwned = accumulationCsgStreamsUploaded;
    NWB_ASSERT(
        !accumulationCsgIntervalSampleImageStatesGraphOwned
        || (
            avboitAccumulationPayload.accumulationPhasePrepared
            && avboitAccumulationPayload.accumulationSnapshot.captured
        )
    );
    NWB_ASSERT(
        !accumulationCsgClipBufferStatesGraphOwned
        || (
            avboitAccumulationPayload.accumulationPhasePrepared
            && avboitAccumulationPayload.accumulationSnapshot.captured
        )
    );
    avboitAccumulationPayload.accumulationCsgIntervalSampleImageStatesGraphOwned =
        accumulationCsgIntervalSampleImageStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationCsgClipBufferStatesGraphOwned =
        accumulationCsgClipBufferStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationMaterialFrameStatesGraphOwned = accumulationStreamsUploaded;
    avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned =
        accumulationStreamsUploaded
        && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId accumulationComputeEmulationOutputSet;
    Core::Alloc::ScratchArena accumulationComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool accumulationComputeEmulationPlanCaptured =
        accumulationRegularComputeEmulationPlanCaptured
        || accumulationCsgComputeEmulationPlanCaptured
    ;
    bool accumulationComputeEmulationOutputStatesGraphOwned = false;
    if(accumulationRegularComputeEmulationPlanCaptured){
        accumulationComputeEmulationOutputStatesGraphOwned = GatherImportedOutputBufferResourceSet(
            m_deferredLightingTaskGraph,
            avboitAccumulationComputeEmulationPayload.plan,
            accumulationComputeEmulationResourceScratch,
            Name("render.avboit.accumulation.compute_emulation.outputs"),
            "AVBOIT Accumulation Compute Emulation Outputs",
            accumulationComputeEmulationOutputSet
        );
    }
    else if(accumulationCsgComputeEmulationPlanCaptured){
        accumulationComputeEmulationOutputStatesGraphOwned =
            GatherImportedOutputBufferResourceSet(
                m_deferredLightingTaskGraph,
                avboitAccumulationComputeEmulationPayload.csgPlan,
                accumulationComputeEmulationResourceScratch,
                Name("render.avboit.accumulation.csg_compute_emulation.outputs"),
                "AVBOIT Accumulation CSG Compute Emulation Outputs",
                accumulationComputeEmulationOutputSet
            )
        ;
    }
    if(
        accumulationComputeEmulationPlanCaptured
        && !accumulationComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Accumulation compute-emulation output states"
        ));
    }
    avboitAccumulationPayload.accumulationComputeEmulationOutputStatesGraphOwned =
        accumulationRegularComputeEmulationPlanCaptured
        && accumulationComputeEmulationOutputStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationCsgComputeEmulationOutputStatesGraphOwned =
        accumulationCsgComputeEmulationPlanCaptured
        && accumulationComputeEmulationOutputStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationComputeEmulationTiming =
        accumulationComputeEmulationOutputStatesGraphOwned
            ? &avboitAccumulationComputeEmulationTiming
            : nullptr
    ;
    Core::GpuGraphResourceId accumulationSharedComputeEmulationOutput;
    const bool accumulationSharedComputeEmulationOutputStatesGraphOwned =
        accumulationSharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            accumulationSharedComputeEmulationPlan,
            "AVBOIT Accumulation Shared Compute Emulation Output",
            accumulationSharedComputeEmulationOutput
        )
    ;
    if(
        accumulationSharedComputeEmulationPlanCaptured
        && !accumulationSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Accumulation shared compute-emulation output state"
        ));
    }

    const Core::BufferRange accumulationInstanceRange(
        0u,
        avboitAccumulationPayload.accumulationSnapshot.instanceCount * sizeof(InstanceGpuData)
    );
    const Core::BufferRange accumulationMaterialTypedRange(
        0u,
        avboitAccumulationPayload.accumulationSnapshot.materialTypedByteCount
    );
    const Core::BufferRange accumulationReceiverRange(
        0u,
        avboitAccumulationPayload.accumulationSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    const Core::BufferRange accumulationCutterRange(
        0u,
        avboitAccumulationPayload.accumulationSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData)
    );

    Core::Alloc::ScratchArena accumulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationResourceUses{ accumulationResourceScratch };
    accumulationResourceUses.reserve(
        12u
        + (accumulationStreamsUploaded ? 7u : 0u)
        + (accumulationCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    accumulationResourceUses.push_back(ReadUse(albedo));
    accumulationResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
    accumulationResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));


    // accumulationFramebuffer binds depth read-only, tracked as DepthRead.
    accumulationResourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    accumulationResourceUses.push_back(ReadUse(avboitTransmittance));
    accumulationResourceUses.push_back(ReadUse(avboitDepthWarp));
    accumulationResourceUses.push_back(ReadUse(avboitControl));
    accumulationResourceUses.push_back(ReadUse(refractionInstance));
    accumulationResourceUses.push_back(ReadUse(refractionDepth));
    accumulationResourceUses.push_back(ReadWriteUse(avboitForegroundColor, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(avboitForegroundExtinction, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(avboitAccumColor, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(avboitAccumExtinction, Core::ResourceStates::RenderTarget));
    if(accumulationStreamsUploaded){
        accumulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        accumulationResourceUses.push_back(ReadBufferUse(materialInstances, accumulationInstanceRange));
        accumulationResourceUses.push_back(ReadBufferUse(materialTyped, accumulationMaterialTypedRange));
        if(accumulationCsgStreamsUploaded){
            accumulationResourceUses.push_back(ReadBufferUse(csgReceiverRanges, accumulationReceiverRange));
            accumulationResourceUses.push_back(ReadBufferUse(csgCutters, accumulationCutterRange));
            accumulationResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // Interval producer owns this state; accumulation only samples it.
            accumulationResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(accumulationCsgIntervalSampleImageStatesGraphOwned){
                // Interval producer wrote these aliases; graph lowers the same-UAV handoff.
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalDepth,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCapNormal,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalData,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCount,
                    csgRemovedIntervalCountSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
            }
        }
    }
    const Core::GpuTaskResourceSetUse accumulationMaterialGeometrySetUse{
        .resourceSet = accumulationMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse accumulationMaterialSampledTextureSetUse{
        .resourceSet = accumulationMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse accumulationComputeEmulationOutputUavSetUse{
        .resourceSet = accumulationComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse accumulationComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = accumulationComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse accumulationMaterialResourceSetUses[3u] = {};
    usize accumulationMaterialResourceSetUseCount = 0u;
    if(avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationMaterialGeometrySetUse;
    }
    if(accumulationMaterialSampledTextureSet.valid()){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationMaterialSampledTextureSetUse;
    }
    if(accumulationComputeEmulationOutputStatesGraphOwned){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationComputeEmulationOutputVertexBufferSetUse;
    }
    accumulationResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    accumulationResourceUses.push_back(ReadUse(avboitMaterialDomain));
    accumulationResourceUses.push_back(ReadUse(avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitAccumulationScheduling;
    avboitAccumulationScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitAccumulationScheduling.forceSubmissionBoundary = false;
    avboitAccumulationScheduling.allowPacketMerge = true;
    avboitAccumulationScheduling.mergeWithPrevious = true;
    avboitAccumulationScheduling.allowMergeAcrossConsumerFrontier = true;

    // Keep the final upload as stream anchor; replacing it hides a broken handoff.
    const Core::GpuTaskId accumulationStreamTask = accumulationUploadTask;
    if(accumulationStreamsUploaded)
        m_avboitSystem.taskGraphStage().m_accumulationStreamTask = accumulationStreamTask;
    Core::GpuTaskId accumulationDependency = accumulationUploadTask;
    if(accumulationComputeEmulationOutputStatesGraphOwned){
        avboitAccumulationComputeEmulationPayload.graphics = &m_graphics;
        avboitAccumulationComputeEmulationPayload.materialSystem = &m_materialSystem;
        avboitAccumulationComputeEmulationPayload.targets = &deferredTargets;
        avboitAccumulationComputeEmulationPayload.timingTicket = avboitAccumulationPayload.timingTicket;
        avboitAccumulationComputeEmulationPayload.accumulationTiming = &avboitAccumulationComputeEmulationTiming;
        avboitAccumulationComputeEmulationPayload.materialDrawBuffersUploaded = accumulationStreamsUploaded;
        avboitAccumulationComputeEmulationPayload.csgFrameBuffersUploaded = accumulationCsgStreamsUploaded;
        avboitAccumulationComputeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            accumulationCsgIntervalSampleImageStatesGraphOwned;
        avboitAccumulationComputeEmulationPayload.csgClipBufferStatesGraphOwned =
            accumulationCsgClipBufferStatesGraphOwned;
        avboitAccumulationComputeEmulationPayload.materialFrameStatesGraphOwned =
            avboitAccumulationPayload.accumulationMaterialFrameStatesGraphOwned;
        avboitAccumulationComputeEmulationPayload.materialGeometryStatesGraphOwned =
            avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationComputeEmulationResourceUses{
            accumulationResourceScratch
        };
        accumulationComputeEmulationResourceUses.reserve(
            4u + (accumulationCsgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        accumulationComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        accumulationComputeEmulationResourceUses.push_back(
            ReadBufferUse(materialInstances, accumulationInstanceRange)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadBufferUse(materialTyped, accumulationMaterialTypedRange)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(accumulationCsgComputeEmulationPlanCaptured){
            accumulationComputeEmulationResourceUses.push_back(
                ReadBufferUse(csgReceiverRanges, accumulationReceiverRange)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadBufferUse(csgCutters, accumulationCutterRange)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse accumulationComputeEmulationResourceSetUses[3u] = {};
        usize accumulationComputeEmulationResourceSetUseCount = 0u;
        accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
            accumulationMaterialGeometrySetUse;
        if(accumulationMaterialSampledTextureSet.valid()){
            accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
                accumulationMaterialSampledTextureSetUse;
        }
        accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
            accumulationComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint accumulationComputeEmulationScheduling;
        accumulationComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        accumulationComputeEmulationScheduling.forceSubmissionBoundary = false;
        accumulationComputeEmulationScheduling.allowPacketMerge = true;
        accumulationComputeEmulationScheduling.mergeWithPrevious = true;
        // Next raster consumes the producer UAV output and shares its timing ticket.
        accumulationComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc accumulationComputeEmulationDesc;
        accumulationComputeEmulationDesc
            .setIdentity(accumulationCsgComputeEmulationPlanCaptured
                ? Name("render.avboit.accumulation.csg_compute_emulation")
                : Name("render.avboit.accumulation.compute_emulation"))
            .setMarkerLabel(accumulationCsgComputeEmulationPlanCaptured
                ? "AVBOIT Accumulation CSG Compute Emulation"
                : "AVBOIT Accumulation Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(accumulationComputeEmulationScheduling)
            .setDependencies(&accumulationDependency, 1u)
            .setResourceUses(
                accumulationComputeEmulationResourceUses.data(),
                accumulationComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                accumulationComputeEmulationResourceSetUses,
                accumulationComputeEmulationResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            AvboitAccumulationComputeEmulationGraphTask
        >(
            accumulationComputeEmulationDesc,
            Move(avboitAccumulationComputeEmulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Accumulation compute-emulation producer"
            ));
            return;
        }
        accumulationDependency = m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask;
        avboitAccumulationScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(accumulationSharedComputeEmulationOutputStatesGraphOwned){
        // Retained output appears in every phase; keep it exact to preserve alternating uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationSharedGenerateResourceUses{
            accumulationResourceScratch
        };
        accumulationSharedGenerateResourceUses.reserve(5u);
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        accumulationSharedGenerateResourceUses.push_back(ReadBufferUse(materialInstances, accumulationInstanceRange));
        accumulationSharedGenerateResourceUses.push_back(ReadBufferUse(materialTyped, accumulationMaterialTypedRange));
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        accumulationSharedGenerateResourceUses.push_back(WriteUse(
            accumulationSharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationSharedRasterResourceUses{
            accumulationResourceScratch
        };
        accumulationSharedRasterResourceUses.assign(
            accumulationResourceUses.begin(),
            accumulationResourceUses.end()
        );
        accumulationSharedRasterResourceUses.push_back(ReadUse(
            accumulationSharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint accumulationSharedComputeEmulationScheduling;
        accumulationSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        accumulationSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        accumulationSharedComputeEmulationScheduling.allowPacketMerge = true;
        accumulationSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Keep the full alternating chain in AVBOIT Pre so one list owns timing and handoff.
        accumulationSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addAccumulationSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &accumulationSharedComputeEmulationPlan,
            &avboitAccumulationComputeEmulationTiming,
            &frameBindings,
            accumulationSharedComputeEmulationInstanceCount,
            accumulationSharedComputeEmulationMaterialTypedByteCount,
            accumulationStreamsUploaded,
            accumulationMaterialFrameStatesGraphOwned = avboitAccumulationPayload.accumulationMaterialFrameStatesGraphOwned,
            accumulationMaterialGeometryStatesGraphOwned = avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned,
            avboitAccumulationTimingTicket = avboitAccumulationPayload.timingTicket,
            &accumulationSharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitAccumulationSharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool beginTiming,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(accumulationSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitAccumulationSharedComputeEmulationGraphTask::Payload payload;
            payload.frameBindings = frameBindings;
            payload.graphics = &m_graphics;
            payload.materialSystem = &m_materialSystem;
            payload.targets = &deferredTargets;
            payload.timingTicket = avboitAccumulationTimingTicket;
            payload.accumulationTiming = &avboitAccumulationComputeEmulationTiming;
            payload.plan = accumulationSharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = accumulationSharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = accumulationSharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = accumulationStreamsUploaded;
            payload.materialFrameStatesGraphOwned = accumulationMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = accumulationMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<AvboitAccumulationSharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using AccumulationSharedPhase = AvboitAccumulationSharedComputeEmulationGraphTask::Phase;
        const Name accumulationSharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.accumulation.shared_compute_emulation_generate_a"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_a"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_b"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_b"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_c"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_c"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_d"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_d"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_e"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_e"),
        };
        const AStringView accumulationSharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Accumulation Shared Compute Emulation Generate A",
            "AVBOIT Accumulation Shared Compute Emulation Raster A",
            "AVBOIT Accumulation Shared Compute Emulation Generate B",
            "AVBOIT Accumulation Shared Compute Emulation Raster B",
            "AVBOIT Accumulation Shared Compute Emulation Generate C",
            "AVBOIT Accumulation Shared Compute Emulation Raster C",
            "AVBOIT Accumulation Shared Compute Emulation Generate D",
            "AVBOIT Accumulation Shared Compute Emulation Raster D",
            "AVBOIT Accumulation Shared Compute Emulation Generate E",
            "AVBOIT Accumulation Shared Compute Emulation Raster E",
        };
        const usize accumulationSharedComputeEmulationPhaseCount =
            ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(
                accumulationSharedComputeEmulationPlan.drawCount
            )
        ;
        NWB_ASSERT(ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            accumulationSharedComputeEmulationPlan.drawCount
        ));
        NWB_ASSERT(
            accumulationSharedComputeEmulationPhaseCount
            <= LengthOf(accumulationSharedComputeEmulationPhaseIdentities)
        );
        Core::GpuTaskId accumulationSharedComputeEmulationDependency = accumulationDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < accumulationSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase =
                phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
            m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTasks[phaseIndex] =
                addAccumulationSharedComputeEmulationPhase(
                    accumulationSharedComputeEmulationPhaseIdentities[phaseIndex],
                    accumulationSharedComputeEmulationPhaseMarkers[phaseIndex],
                    accumulationSharedComputeEmulationDependency,
                    isRasterPhase ? AccumulationSharedPhase::Raster : AccumulationSharedPhase::Generate,
                    phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
                    phaseIndex == 0u,
                    phaseIndex + 1u == accumulationSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? accumulationSharedRasterResourceUses
                        : accumulationSharedGenerateResourceUses,
                    accumulationMaterialResourceSetUses,
                    accumulationMaterialResourceSetUseCount
                )
            ;
            if(!m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Accumulation shared compute-emulation phase"
                ));
                return;
            }
            accumulationSharedComputeEmulationDependency =
                m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTasks[phaseIndex];
        }
        m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTaskCount =
            accumulationSharedComputeEmulationPhaseCount;
        // Terminal raster is the Accumulation endpoint feeding finalizer, timing, and tokens.
        m_avboitSystem.taskGraphStage().m_accumulationTask = accumulationSharedComputeEmulationDependency;
    }
    else{
        Core::GpuTaskDesc accumulationDesc;
        accumulationDesc
            .setIdentity(Name("render.avboit.accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitAccumulationScheduling)
            .setDependencies(&accumulationDependency, 1u)
            .setResourceUses(accumulationResourceUses.data(), accumulationResourceUses.size())
            .setResourceSetUses(
                accumulationMaterialResourceSetUseCount != 0u ? accumulationMaterialResourceSetUses : nullptr,
                accumulationMaterialResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_accumulationTask = m_deferredLightingTaskGraph.addTask<AvboitAccumulationGraphTask>(
            accumulationDesc,
            Move(avboitAccumulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_accumulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
            return;
        }
    }
    const Core::GpuTaskResourceUse accumulationFinalizeResourceUses[] = {
        ReadUse(avboitAccumColor, Core::ResourceStates::ShaderResource),
        ReadUse(avboitForegroundColor),
        ReadUse(avboitForegroundExtinction),
        ReadUse(avboitAccumExtinction, Core::ResourceStates::ShaderResource),
        ReadUse(depth, Core::ResourceStates::ShaderResource),
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
        .setIdentity(Name("render.avboit.accumulation_finalize"))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(accumulationFinalizeScheduling)
        .setDependencies(&m_avboitSystem.taskGraphStage().m_accumulationTask, 1u)
        .setResourceUses(accumulationFinalizeResourceUses, LengthOf(accumulationFinalizeResourceUses))
    ;
    m_avboitSystem.taskGraphStage().m_accumulationFinalizeTask = m_deferredLightingTaskGraph.addTask<AvboitAccumulationFinalizeGraphTask>(
        accumulationFinalizeDesc,
        AvboitAccumulationFinalizeGraphTask::Payload{}
    );
    if(!m_avboitSystem.taskGraphStage().m_accumulationFinalizeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation finalizer graph task"));
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


    const RayTracingSceneGraphResources sceneResources = m_raytracingSystem.snapshotSceneGraphResources();
    reflectionContentStamp.geometry = sceneResources.contentStamp.geometry;
    reflectionContentStamp.material = sceneResources.contentStamp.material;
    // CSG evaluation and lagged screen lighting do not have a matching content stamp for this first history policy.
    reflectionContentStamp.trusted = sceneResources.contentStamp.trusted && csgFrameState.empty() && !useLaggedLightingHistory;
    const ReflectionFrameSnapshot reflectionResources = m_reflectionSystem.snapshotFrameResources(
        deferredTargets, meshViewBufferSnapshot,
        m_preparedReflectionSceneAvailable ? sceneResources : RayTracingSceneGraphResources{},
        m_reflectionSettings, m_reflectionFrameIndex, reflectionContentStamp
    );
    if(!reflectionResources.valid())
        return;
    RayTracingSceneGraphReads sceneReads;
    if(reflectionResources.parameters.hardwareEnabled != 0u || refractionResources.usesHardwareTrace){
        sceneReads = ImportRayTracingSceneGraphReads(
            m_deferredLightingTaskGraph, sceneResources, m_raytracingSystem.sceneTlasBackingInitialState()
        );
        if(!sceneReads.valid())
            return;
    }
    const Core::GpuTaskResourceUse reflectionSurfaceReads[] = {
        ReadUse(specularRoughness), ReadUse(refractionSpecularRoughness), ReadUse(normal), ReadUse(depth), ReadUse(worldPosition),
        ReadUse(refractionDepth), ReadUse(refractionNormalIor),
        ReadUse(meshView, Core::ResourceStates::ConstantBuffer),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> reflectionHardwareReads{traceGeometryScratchArena};
    reflectionHardwareReads.reserve(LengthOf(sceneReads.uses) + 2u);
    if(sceneReads.valid()){
        for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
            reflectionHardwareReads.push_back(read);
        reflectionHardwareReads.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        reflectionHardwareReads.push_back(ReadUse(lights));
    }
    Core::GpuTaskResourceSetUse reflectionSets[2] = {};
    usize reflectionSetCount = 0u;
    for(const Core::GpuGraphResourceSetId set : {hardwareTraceGeometrySet, traceMaterialSampledTextureSet}){
        if(set.valid()){
            reflectionSets[reflectionSetCount++] = Core::GpuTaskResourceSetUse{
                .resourceSet = set, .range = {}, .requiredState = Core::ResourceStates::ShaderResource,
                .access = Core::GpuTaskResourceAccess::Read,
            };
        }
    }
    const ReflectionGraphInputs reflectionInputs{
        .opaqueDepth = depth,
        .opaqueColor = opaqueColor,
        .surfaceReads = reflectionSurfaceReads, .surfaceReadCount = LengthOf(reflectionSurfaceReads),
        .hardwareReads = reflectionHardwareReads.data(), .hardwareReadCount = reflectionHardwareReads.size(),
        .hardwareSetReads = reflectionSets, .hardwareSetReadCount = reflectionSetCount,
        .hardwarePreparationReady = &m_shadowPreparationOutcome.ready,
        .hardwareDispatchLogged = &m_reflectionHardwareLogged,
        .fallbackDispatchLogged = &m_reflectionFallbackLogged,
    };
    const ReflectionGraphResult reflectionGraph = DeclareReflectionTasks(
        m_deferredLightingTaskGraph, m_graphics, traceGeometryScratchArena,
        reflectionResources, reflectionInputs, m_deferredLightingTask
    );
    if(!reflectionGraph.valid())
        return;
    const ReflectionCompositeInputs reflectionCompositeInputs{
        .opaqueRadianceSlot = reflectionResources.parameters.opaqueRadianceSlot,
        .glassRadianceSlot = reflectionResources.parameters.glassRadianceSlot,
        .debugView = m_reflectionSettings.debugView,
    };
    refractionResources.opaqueReflectionSlot = reflectionCompositeInputs.opaqueRadianceSlot;

    Core::GpuTaskId refractionResolveTask;
    if(refractionActive && refractionResources.valid()){
        Core::Alloc::ScratchArena refractionScratch(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> refractionUses{refractionScratch};
        const Core::GpuGraphResourceId refractionInputs[] = {
            refractionDepth, refractionNormalIor, refractionTintCoverage, refractionInstance,
            opaqueColor, reflectionGraph.opaqueRadiance, worldPosition, depth, avboitAccumColor, avboitAccumExtinction,
            avboitForegroundColor, avboitForegroundExtinction
        };
        for(const auto input : refractionInputs)
            refractionUses.push_back(ReadUse(input));
        refractionUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(lights));
        refractionUses.push_back(WriteUse(refractionResolve, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskResourceSetUse refractionSets[3] = {};
        usize refractionSetCount = 0;
        if(refractionResources.usesHardwareTrace){
            for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
                refractionUses.push_back(read);
            const Core::GpuGraphResourceSetId sets[] = {
                hardwareTraceGeometrySet, traceMaterialSampledTextureSet
            };
            for(const auto set : sets){
                if(set.valid())
                    refractionSets[refractionSetCount++] = Core::GpuTaskResourceSetUse{
                        .resourceSet = set, .range = {}, .requiredState = Core::ResourceStates::ShaderResource,
                        .access = Core::GpuTaskResourceAccess::Read,
                    };
            }
        }
        const Core::GpuTaskId refractionDependencies[] = {
            reflectionGraph.completion, avboitFinalTask, m_deferredSurfelGiTask
        };
        Core::GpuTaskDesc refractionDesc;
        refractionDesc.setIdentity(Name("render.avboit.refraction_resolve"))
            .setMarkerLabel("AVBOIT Refraction Resolve").setQueue(ComputeQueueRequest())
            .setDependencies(refractionDependencies, LengthOf(refractionDependencies))
            .setResourceUses(refractionUses.data(), refractionUses.size())
            .setResourceSetUses(refractionSets, refractionSetCount);
        refractionResolveTask = m_deferredLightingTaskGraph.addTask<RefractionResolveGraphTask>(refractionDesc,
            RefractionResolveGraphTask::Payload{
                .system = &m_raytracingSystem, .targets = &deferredTargets, .resources = refractionResources,
                .hardwarePreparationReady = &m_shadowPreparationOutcome.ready,
                .dispatchLogged = refractionResources.usesHardwareTrace
                    ? &m_refractionHardwareLogged : &m_refractionScreenLogged,
                .screenFallbackDispatchLogged = &m_refractionScreenLogged,
            });
    }
    else{
        Core::GpuTaskDesc clearDesc;
        clearDesc.setIdentity(Name("render.avboit.refraction_resolve_clear"))
            .setMarkerLabel("AVBOIT Refraction Resolve Clear").setQueue(GraphicsUploadQueueRequest())
            .setDependencies(&avboitFinalTask, 1u);
        Core::GpuClearTextureTaskDesc clear;
        clear.destination = refractionResolve;
        clear.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clear.valueType = Core::GpuClearTextureTaskValueType::Float;
        clear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        refractionResolveTask = m_deferredLightingTaskGraph.addClearTextureTask(
            clearDesc, clear);
    }
    if(!refractionResolveTask.valid())
        return;

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

    // Keep this diagnostic behind the terminal presentation endpoint so whole-normal execution cannot absorb its
    // independent Transfer-preferred tail and it cannot delay lighting or presentation.
    declareDeferredSurfelCountReadbackTask(rayTracingSurfelResources);

    if(capturesLaggedLightingHistory){
        // The core built-in derives whole-resource CopySource/CopyDest declarations for these regions and retains
        // the imports itself. The array slices stay explicit only in the native copy body.
        Core::GpuCopyTextureTaskRegion historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 2u] = {};
        for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
            Core::GpuCopyTextureTaskRegion& region = historyCopyRegions[shadowSlot];
            region.source = historyCopyShadowVisibility;
            region.destination = historyCopyDestinationShadowVisibility;
            region.sourceSlice.setArraySlice(shadowSlot);
            region.destinationSlice.setArraySlice(shadowSlot);
        }
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].source = historyCopyCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].destination = historyCopyDestinationCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].source = historyCopySurfelIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].destination = historyCopyDestinationSurfelIrradiance;
        Core::GpuTaskSchedulingHint historyCopyScheduling;
        historyCopyScheduling.cost = Core::GpuTaskCostHint::Medium;
        historyCopyScheduling.forceSubmissionBoundary = true;
        historyCopyScheduling.allowPacketMerge = false;
        const Core::GpuTaskId historyCopyDependencies[] = { m_deferredFrameTimingEndTask };
        Core::GpuTaskDesc historyCopyDesc;
        historyCopyDesc
            .setIdentity(Name("render.lagged_history_copy"))
            .setMarkerLabel("Lagged Lighting History Copy")
            .setQueue(TransferQueueRequest())
            .setScheduling(historyCopyScheduling)
            .setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies))
        ;
        m_deferredLaggedLightingHistoryTask = m_deferredLightingTaskGraph.addCopyTextureTask(
            historyCopyDesc,
            Core::GpuCopyTextureTaskDesc{
                .regions = historyCopyRegions,
                .regionCount = LengthOf(historyCopyRegions),
                .acceptedToken = &m_laggedLightingHistorySubmissionToken,
            }
        );
        if(!m_deferredLaggedLightingHistoryTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred lagged-lighting history-copy task"));
            return;
        }
    }

    // Recovery is a late independent Graphics tail. It deliberately has no packet dependency on normal work: a
    // rejected suffix must not prevent it from retiring the accepted frame prefix. Its compiled packet asks the
    // graph transaction to join every accepted non-Graphics physical queue at submit time.
    const Core::GpuGraphResourceId recoveryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.frame_recovery.timing"), "Frame Recovery Timing")
    );
    if(!recoveryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred frame-recovery graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse recoveryResourceUses[] = {
        ReadWriteUse(recoveryDomain, Core::ResourceStates::Common),
    };
    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.cost = Core::GpuTaskCostHint::Tiny;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    Core::GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("render.frame_recovery"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(recoveryScheduling)
        .setResourceUses(recoveryResourceUses, LengthOf(recoveryResourceUses))
    ;
    m_deferredFrameRecoveryTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::FrameRecoveryGraphTask>(
        recoveryDesc,
        ECSRenderDetail::FrameRecoveryGraphTask::Payload{
            .frameTimingTransaction = &frameTimingTransaction,
            .armed = &m_deferredFrameRecoveryArmed,
            .retiresFrameTiming = &m_deferredFrameRecoveryRetiresTiming,
        }
    );
    if(!m_deferredFrameRecoveryTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-recovery graph task"));
        return;
    }

    // The backend owns physical queue discovery and identity. The renderer consumes this immutable view directly so
    // graph packets can target multiple same-class native queues without rebuilding a class-shaped topology here.
    const Core::GpuTaskGraphQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: no native physical queue registry is available for the deferred graph"));
        return;
    }
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions compileOptions;
    // A graphics prefix can now split immediately after work that enables a different physical queue. This exposes
    // the true cross-queue frontier while preserving the compiler's declaration-derived dependency order.
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    // Time the accepted normal-rendering packets from the packet containing frame-timing begin through the graph-owned
    // presentation endpoint. Late readback, history-copy, and recovery tails retain separate diagnostic/lifecycle policy.
    compileOptions.packetTimingEnvelope.firstTask = m_deferredShadowPrepareTask;
    compileOptions.packetTimingEnvelope.lastTask = m_deferredFrameTimingEndTask;
    m_deferredTaskTimingFeedback.configureCompileOptions(compileOptions, m_graphics.getFrameIndex());
    compileOptions.declarationSeconds = DurationInSeconds<f64>(TimerNow(), declarationBegin);
    const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
    if(!compiler.compile(
        declarations,
        m_deferredLightingTaskGraphAnalysis,
        topology,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        scratchArena,
        compileOptions
    )){
        const auto& analysisDiagnostic = m_deferredLightingTaskGraphAnalysis.diagnostic();
        const auto& queueDiagnostic = m_deferredLightingTaskGraphQueueAssignments.diagnostic();
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph compilation failed: analysis={} task={} resource={} queue={} queueTask={}")
            , static_cast<u32>(analysisDiagnostic.status), analysisDiagnostic.task.index, analysisDiagnostic.resource.index
            , static_cast<u32>(queueDiagnostic.status), queueDiagnostic.task.index
        );
        return;
    }
    const Core::GpuCompiledGraph::ReadView compiledPlan(m_deferredLightingCompiledGraph);
    if(
        !compiledPlan.validFor(declarations)
        || !__hidden_task_graph_deferred_lighting::PreparePacketEnvelopeMetrics(
        declarations,
        compiledPlan,
        m_graphics.gpuTiming(),
        m_graphics.getFrameIndex(),
        scratchArena
    ))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare deferred graph packet metrics"));
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingTaskGraphValid = true;


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

