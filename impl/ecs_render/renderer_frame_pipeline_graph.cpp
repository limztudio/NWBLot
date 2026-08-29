// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <impl/ecs_render/raytrace/task_graph_post_gbuffer_normalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_finalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_visibility_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_frame_recovery_task.h>
#include <impl/ecs_render/kernel/task_graph_frame_timing_end_task.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/deferred/task_graph_clear_timing.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/deferred/task_graph_gbuffer_task.h>
#include <impl/ecs_render/deferred/task_graph_present_task.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/csg/task_graph_clear_timing.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/csg/task_graph_resource_sets.h>

#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>
#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>

#include <impl/ecs_render/avboit/task_graph_clear_timing.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>
#include <impl/ecs_render/avboit/task_graph_resource_sets.h>
#include <impl/ecs_render/avboit/task_graph_timing_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_deferred_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PreparePacketEnvelopeMetrics(
    const Core::GpuTaskGraph& graph,
    const Core::GpuCompiledGraph& compiledGraph,
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
        const Core::GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
        const Core::GpuTaskId* const packetTasks = compiledGraph.packetTasks(packetID);
        if(!packet.recordsPacketEnvelopeTiming || packet.taskCount == 0u || !packetTasks)
            return false;

        const Name packetScopeName = Core::GpuTaskPacketTimingScopeName(graph.taskAt(packetTasks[0u].index).identity);
        if(!packetScopeName)
            return false;
        packetScopes.push_back(Core::GpuPacketEnvelopeMetricScope{
            .scopeName = packetScopeName,
            .physicalQueue = packet.queue,
        });

        bool hasQueueOutput = false;
        for(const Core::GpuPacketEnvelopeMetricQueueOutput& output : queueOutputs)
            hasQueueOutput = hasQueueOutput || output.physicalQueue == packet.queue;
        if(hasQueueOutput)
            continue;

        const Name internalIdleScopeName = RendererGpuTimingScope::DeferredGraphQueueInternalIdle(packet.queue, scratchArena);
        if(!internalIdleScopeName)
            return false;
        queueOutputs.push_back(Core::GpuPacketEnvelopeMetricQueueOutput{
            .physicalQueue = packet.queue,
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
    ECSRenderDetail::DeferredClearTimingRecordState& deferredClearTimingState,
    ECSRenderDetail::CsgIntervalClearTimingRecordState& opaqueCsgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
    Core::GpuTimingSubmissionTicket** const graphicsPrefixTimingTickets,
    const bool* const asyncPrefixTimingSpansOnePacket,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
    ECSRenderDetail::AvboitClearTimingRecordState& avboitClearTimingState,
    ECSRenderDetail::CsgIntervalClearTimingRecordState& transparentCsgIntervalClearTimingState,
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
    m_graphicsPrefixDeferredClearFirstTask = {};
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
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    // This renderer-owned declaration/build attempt starts after stale graph artifacts are discarded and ends
    // immediately before core compilation, whose total duration remains separate.
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

    // Preflight has already frozen the exact visible HW/SW mesh tables. Keep retained handles here rather than the
    // raw descriptor-table pointers, import every physical buffer once, and fan the same IDs out to all packets.
    // A fresh/replaced buffer starts from its creation state; a buffer normalized by an accepted earlier Prefix is
    // explicitly imported as SRV so the first packet never claims a stale state.
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
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    // These outputs begin with a graph-owned write. Fresh managed subresources lower from Undefined; accepted retained
    // state is restored to descriptor state at packet close and reused by StateTracker on later packets.
    const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);
        desc.setInitialState(Core::ResourceStates::Unknown);
        return m_deferredLightingTaskGraph.importTexture(texture, desc);
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const auto importCurrentBindlessSlots = [&](const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(
            deferredTargets.bindless.slotsBuffer,
            BufferResourceDesc(identity, label)
        );
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
    // Shadow, caustic, and surfel closest-hit dispatchers use the frozen material context to select these Texture2D
    // assets through the bindless heap. Reuse a typed preflight import when G-buffer/AVBOIT already owns it, rather
    // than introducing an opaque descriptor domain around the trace paths.
    for(const Core::TextureHandle& texture : preparedTraceMaterialSampledTextures){
        Core::GpuGraphResourceId resource = m_deferredLightingTaskGraph.findImportedTexture(texture);
        if(!resource.valid()){
            const Name textureIdentity = texture ? texture->getCreationDescription().name : NAME_NONE;
            if(!textureIdentity){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared trace material texture has no stable identity"));
                return;
            }
            resource = importTexture(texture, textureIdentity, "Prepared Trace Material Sampled Texture");
        }
        if(!resource.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared trace material sampled texture"));
            return;
        }
        traceMaterialSampledTextureResources.push_back(resource);
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
    const Core::GpuGraphResourceId albedo = importTexture(
        deferredTargets.albedo,
        Name("render.deferred_lighting.albedo"),
        "G-Buffer Albedo"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.deferred_lighting.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.deferred_lighting.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.deferred_lighting.depth"),
        "G-Buffer Depth"
    );


// The CSG working setÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Âpeel targets, receiver-event/span images, and removed-interval outputsÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Âis declared by
    // the graph. Its exact clear/StorageImage handoffs are visible here; the wider CSG target lifecycle remains in
    // native compatibility producers for its own bounded migration.
    const Core::GpuGraphResourceId csgCapBackNormal = importTexture(
        deferredTargets.csgCapBackNormal,
        Name("render.deferred.csg_cap_back_normal"),
        "CSG Cap Back Normal"
    );
    const Core::GpuGraphResourceId csgIntervalDepth = importTexture(
        deferredTargets.csgIntervalDepth,
        Name("render.deferred.csg_interval_depth"),
        "CSG Interval Depth"
    );
    const Core::GpuGraphResourceId csgIntervalId = importTexture(
        deferredTargets.csgIntervalId,
        Name("render.deferred.csg_interval_id"),
        "CSG Interval ID"
    );
    const Core::GpuGraphResourceId csgReceiverEventData = importTexture(
        deferredTargets.csgReceiverEventData,
        Name("render.deferred.csg_receiver_event_data"),
        "CSG Receiver Event Data"
    );
    const Core::GpuGraphResourceId csgReceiverEventCount = importTexture(
        deferredTargets.csgReceiverEventCount,
        Name("render.deferred.csg_receiver_event_count"),
        "CSG Receiver Event Count"
    );
    const Core::GpuGraphResourceId csgReceiverSpanData = importTexture(
        deferredTargets.csgReceiverSpanData,
        Name("render.deferred.csg_receiver_span_data"),
        "CSG Receiver Span Data"
    );
    const Core::GpuGraphResourceId csgReceiverSpanCount = importTexture(
        deferredTargets.csgReceiverSpanCount,
        Name("render.deferred.csg_receiver_span_count"),
        "CSG Receiver Span Count"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalDepth = importTexture(
        deferredTargets.csgRemovedIntervalDepth,
        Name("render.deferred.csg_removed_interval_depth"),
        "CSG Removed Interval Depth"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalCapNormal = importTexture(
        deferredTargets.csgRemovedIntervalCapNormal,
        Name("render.deferred.csg_removed_interval_cap_normal"),
        "CSG Removed Interval Cap Normal"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalData = importTexture(
        deferredTargets.csgRemovedIntervalData,
        Name("render.deferred.csg_removed_interval_data"),
        "CSG Removed Interval Data"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalCount = importTexture(
        deferredTargets.csgRemovedIntervalCount,
        Name("render.deferred.csg_removed_interval_count"),
        "CSG Removed Interval Count"
    );
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        history ? history->shadowVisibility : deferredTargets.shadowVisibility,
        Name("render.deferred_lighting.shadow_visibility"),
        history ? "Lagged Shadow Visibility" : "Shadow Visibility"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        history ? history->causticIrradiance : deferredTargets.causticIrradiance,
        Name("render.deferred_lighting.caustic_irradiance"),
        history ? "Lagged Caustic Irradiance" : "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,
        Name("render.deferred_lighting.surfel_irradiance"),
        history ? "Lagged Surfel Irradiance" : "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId currentShadowVisibility = !history
        ? shadowVisibility
        : importTexture(
            deferredTargets.shadowVisibility,
            Name("render.deferred_shadow_visibility.current_output"),
            "Shadow Visibility"
        )
    ;
    const Core::GpuGraphResourceId currentCausticIrradiance = !history
        ? causticIrradiance
        : importTexture(
            deferredTargets.causticIrradiance,
            Name("render.deferred_effects.current_caustic_irradiance"),
            "Caustic Irradiance"
        )
    ;
    const Core::GpuGraphResourceId currentSurfelIrradiance = !history
        ? surfelIrradiance
        : importTexture(
            deferredTargets.surfelIrradiance,
            Name("render.deferred_surfel_gi.current_irradiance"),
            "Surfel Irradiance"
        )
    ;
    const Core::GpuGraphResourceId opaqueColor = importFirstWriteTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_lighting.opaque_color"),
        "Opaque Color"
    );
    const Core::GpuGraphResourceId sceneShading = importBuffer(
        deferredLightingResources.sceneShadingBuffer,
        Name("render.deferred_lighting.scene_shading"),
        "Scene Shading"
    );
    const Core::GpuGraphResourceId lights = importBuffer(
        deferredLightingResources.lightBuffer,
        Name("render.deferred_lighting.lights"),
        "Lights"
    );
    const Core::GpuGraphResourceId meshView = importBuffer(
        meshViewBufferSnapshot.buffer,
        Name("render.deferred.mesh_view"),
        "Mesh View"
    );
    const Core::GpuGraphResourceId materialInstances = frameBindings.instanceBuffer
        ? importBuffer(
            frameBindings.instanceBuffer,
            Name("render.deferred.material_instances"),
            "Material Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId materialTyped = frameBindings.materialTypedBuffer
        ? importBuffer(
            frameBindings.materialTypedBuffer,
            Name("render.deferred.material_typed"),
            "Material Typed Data"
        )
        : Core::GpuGraphResourceId{}
    ;
    ECSRenderDetail::CsgGraphResourceBuffers csgGraphResources;
    m_csgSystem.populateCsgGraphResourceBuffers(csgGraphResources);
    const Core::GpuGraphResourceId csgReceiverRanges = csgGraphResources.receiverRanges
        ? importBuffer(
            csgGraphResources.receiverRanges,
            Name("render.deferred.csg_receiver_ranges"),
            "CSG Receiver Ranges"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId csgCutters = csgGraphResources.cutters
        ? importBuffer(
            csgGraphResources.cutters,
            Name("render.deferred.csg_cutters"),
            "CSG Cutters"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId csgClipContextSlots = csgGraphResources.clipContextSlots
        ? importBuffer(
            csgGraphResources.clipContextSlots,
            Name("render.deferred.csg_clip_context_slots"),
            "CSG Clip Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId csgIntervalSampleState = csgGraphResources.intervalSampleState
        ? importBuffer(
            csgGraphResources.intervalSampleState,
            Name("render.deferred.csg_interval_sample_state"),
            "CSG Interval Sample State"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId bindlessSlots = history
        ? importBuffer(
            history->slotsBuffer,
            Name("render.deferred_lighting.bindless_slots"),
            "Lagged Deferred Bindless Slots"
        )
        : importCurrentBindlessSlots(
            Name("render.deferred_lighting.bindless_slots"),
            "Deferred Bindless Slots"
        )
    ;
    const Core::GpuGraphResourceId currentBindlessSlots =
        !history || deferredTargets.bindless.slotsBuffer.get() == history->slotsBuffer.get()
            ? bindlessSlots
            : importCurrentBindlessSlots(
                Name("render.deferred_composite.bindless_slots"),
                "Deferred Bindless Slots"
            )
    ;
    const Core::GpuGraphResourceId materialContextSlots = rayTracingGraphResources.materialContextSlotsBuffer
        ? importBuffer(
            rayTracingGraphResources.materialContextSlotsBuffer,
            Name("render.deferred.material_context_slots"),
            "Ray Trace Material Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;


// The optional history copy is declared in this graph after Present, but records only after its accepted
    // producer snapshots exist. Active lighting samples the history resources above, so reuse those exact graph
    // identities for copy destinations and import only the current producer images that are otherwise absent.
    Core::GpuGraphResourceId historyCopyShadowVisibility;
    Core::GpuGraphResourceId historyCopyCausticIrradiance;
    Core::GpuGraphResourceId historyCopySurfelIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationShadowVisibility;
    Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance;
    if(capturesLaggedLightingHistory){
        historyCopyShadowVisibility = currentShadowVisibility;
        historyCopyCausticIrradiance = currentCausticIrradiance;
        historyCopySurfelIrradiance = history
            ? currentSurfelIrradiance
            : surfelIrradiance
        ;
        historyCopyDestinationShadowVisibility = history
            ? shadowVisibility
            : importFirstWriteTexture(
                captureHistory->shadowVisibility,
                Name("render.lagged_history_copy.history_shadow_visibility"),
                "History Shadow Visibility"
            )
        ;
        historyCopyDestinationCausticIrradiance = history
            ? causticIrradiance
            : importFirstWriteTexture(
                captureHistory->causticIrradiance,
                Name("render.lagged_history_copy.history_caustic_irradiance"),
                "History Caustic Irradiance"
            )
        ;
        historyCopyDestinationSurfelIrradiance = history
            ? surfelIrradiance
            : importFirstWriteTexture(
                captureHistory->surfelIrradiance,
                Name("render.lagged_history_copy.history_surfel_irradiance"),
                "History Surfel Irradiance"
            )
        ;
    }


// AVBOIT shares the deferred graph's G-buffer and current bindless imports. Its private targets remain
    // distinct resources, while the compiler owns every producer/consumer state seed through Lighting and
    // Composite on both the live and active-lagged routes.
    const Core::GpuGraphResourceId avboitLowRaster = importTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.avboit.low_raster"),
        "AVBOIT Low Raster"
    );
    const Core::GpuGraphResourceId avboitAccumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.avboit.accum_color"),
        "AVBOIT Accumulated Color"
    );
    const Core::GpuGraphResourceId avboitAccumExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.avboit.accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    const Core::GpuGraphResourceId avboitTransmittance = importTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.avboit.transmittance"),
        "AVBOIT Transmittance"
    );
    const Core::GpuGraphResourceId avboitCoverage = importBuffer(
        deferredTargets.avboit.coverageBuffer,
        Name("render.avboit.coverage"),
        "AVBOIT Coverage"
    );
    const Core::GpuGraphResourceId avboitDepthWarp = importBuffer(
        deferredTargets.avboit.depthWarpBuffer,
        Name("render.avboit.depth_warp"),
        "AVBOIT Depth Warp"
    );
    const Core::GpuGraphResourceId avboitControl = importBuffer(
        deferredTargets.avboit.controlBuffer,
        Name("render.avboit.control"),
        "AVBOIT Control"
    );
    const Core::GpuGraphResourceId avboitExtinction = importBuffer(
        deferredTargets.avboit.extinctionBuffer,
        Name("render.avboit.extinction"),
        "AVBOIT Extinction"
    );
    const Core::GpuGraphResourceId avboitExtinctionOverflow = importBuffer(
        deferredTargets.avboit.extinctionOverflowBuffer,
        Name("render.avboit.extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    const Core::GpuGraphResourceId avboitMaterialDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry")
    );
    const Core::GpuGraphResourceId avboitCsgDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.csg_domain"), "Transparent CSG Intervals")
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !csgCapBackNormal.valid()
        || !csgIntervalDepth.valid()
        || !csgIntervalId.valid()
        || !csgReceiverEventData.valid()
        || !csgReceiverEventCount.valid()
        || !csgReceiverSpanData.valid()
        || !csgReceiverSpanCount.valid()
        || !csgRemovedIntervalDepth.valid()
        || !csgRemovedIntervalCapNormal.valid()
        || !csgRemovedIntervalData.valid()
        || !csgRemovedIntervalCount.valid()
        || !shadowVisibility.valid()
        || !causticIrradiance.valid()
        || !surfelIrradiance.valid()
        || !currentShadowVisibility.valid()
        || !currentCausticIrradiance.valid()
        || !currentSurfelIrradiance.valid()
        || !opaqueColor.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !meshView.valid()
        || !bindlessSlots.valid()
        || !currentBindlessSlots.valid()
        || (rayTracingGraphResources.materialContextSlotsBuffer && !materialContextSlots.valid())
        || (capturesLaggedLightingHistory && (
            !historyCopyShadowVisibility.valid()
            || !historyCopyCausticIrradiance.valid()
            || !historyCopySurfelIrradiance.valid()
            || !historyCopyDestinationShadowVisibility.valid()
            || !historyCopyDestinationCausticIrradiance.valid()
            || !historyCopyDestinationSurfelIrradiance.valid()
        ))
        || !avboitLowRaster.valid()
        || !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !avboitTransmittance.valid()
        || !avboitCoverage.valid()
        || !avboitDepthWarp.valid()
        || !avboitControl.valid()
        || !avboitExtinction.valid()
        || !avboitExtinctionOverflow.valid()
        || !avboitMaterialDomain.valid()
        || !avboitCsgDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }
    const Core::TextureSubresourceSet csgPeelSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgPeelLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverEventLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverSpanLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgRemovedIntervalLayerCount
    );
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources(0u, 1u, 0u, 1u);

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

    if(!declareDeferredGraphicsPrefixTasks(
        deferredTargets,
        shadowPrepareHandoffTask,
        csgFrameState,
        frameBindings,
        hasOpaqueCsgFrameWork,
        meshViewAspectRatio,
        meshViewState,
        meshViewUploadRequired,
        albedo,
        normal,
        worldPosition,
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
        asyncPrefixTimingSpansOnePacket
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graphics-prefix packet"));
        return;
    }

    // Graphics-prefix declaration publishes the current light classification. Freeze shadow routing only after that
    // owner-mediated handoff so transparent folding never observes the prior frame's soft-shadow mask.
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


// Effects start from the accepted graphics-prefix packet and remain compiler-owned through the deferred suffix.
    // Shadow/Software are declared first so their queue assignments are stable before Surf, AVBOIT, and Lighting.
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
    // Surfel GI remains the terminal effects task. Declaring it before hardware/AVBOIT preserves the established
    // effects -> surfel -> suffix order without renderer-side completion stitching.
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


// Hardware Caustics belongs to this graph so the live irradiance producer/consumer transition is compiler-owned.
    // It is declared before Lighting: declaration order establishes the live current-irradiance RAW edge, while the
    // lagged route uses distinct current/history targets and intentionally has no Hardware-to-Lighting dependency.
    if(declaresHardwareCaustics){
        const Core::GpuGraphResourceId causticAccumulator = importTexture(
            deferredTargets.causticAccumulator,
            Name("render.hardware_caustics.accumulator"),
            "Caustic Accumulator"
        );
        const Core::GpuGraphResourceId causticHistory = importTexture(
            deferredTargets.causticHistory,
            Name("render.hardware_caustics.history"),
            "Caustic History"
        );
        const Core::GpuGraphResourceId causticResolveHalf = importTexture(
            deferredTargets.causticResolveHalf,
            Name("render.hardware_caustics.resolve_half"),
            "Caustic Resolve Half"
        );
        const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
            deferredTargets.causticResolveGeometry,
            Name("render.hardware_caustics.resolve_geometry"),
            "Caustic Resolve Geometry"
        );
        const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
            HazardDomainDesc(Name("render.hardware_caustics.scene_geometry"), "Scene Acceleration and Geometry")
        );
        if(
            !causticAccumulator.valid()
            || !causticHistory.valid()
            || !causticResolveHalf.valid()
            || !causticResolveGeometry.valid()
            || !sceneGeometryDomain.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import hardware-caustics graph resources"));
            return;
        }

        Core::Alloc::ScratchArena hardwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwarePhotonResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareGeometryResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolvePrepareResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveSecondWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveThirdWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveFourthWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveFifthWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveUpsampleResourceUses{ hardwareCausticsScratchArena };
        const bool hardwareTraceAttributeStatesGraphOwned = hardwareTraceAttributeSet.valid();
        const Core::GpuTaskResourceSetUse hardwareTraceAttributeSetUse{
            .resourceSet = hardwareTraceAttributeSet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        const Core::GpuTaskResourceSetUse traceMaterialSampledTextureSetUse{
            .resourceSet = traceMaterialSampledTextureSet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        Core::GpuTaskResourceSetUse hardwarePhotonResourceSetUses[2u] = {};
        usize hardwarePhotonResourceSetUseCount = 0u;
        if(hardwareTraceAttributeStatesGraphOwned){
            hardwarePhotonResourceSetUses[hardwarePhotonResourceSetUseCount++] = hardwareTraceAttributeSetUse;
        }
        if(traceMaterialSampledTextureSet.valid()){
            hardwarePhotonResourceSetUses[hardwarePhotonResourceSetUseCount++] = traceMaterialSampledTextureSetUse;
        }
        hardwarePhotonResourceUses.reserve(15u + (
            hardwareTraceAttributeStatesGraphOwned ? 0u : hardwareTraceAttributeResources.size()
        ));
        hardwareGeometryResourceUses.reserve(3u);
        hardwareResolvePrepareResourceUses.reserve(3u);
        hardwareResolveWaveletResourceUses.reserve(3u);
        hardwareResolveSecondWaveletResourceUses.reserve(3u);
        hardwareResolveThirdWaveletResourceUses.reserve(3u);
        hardwareResolveFourthWaveletResourceUses.reserve(3u);
        hardwareResolveFifthWaveletResourceUses.reserve(3u);
        hardwareResolveUpsampleResourceUses.reserve(5u);
        hardwarePhotonResourceUses.push_back(ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwarePhotonResourceUses.push_back(ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(
            sceneShading,
            Core::ResourceStates::ConstantBuffer
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        hardwarePhotonResourceUses.push_back(ReadUse(sceneGeometryDomain));
        hardwarePhotonResourceUses.push_back(ReadWriteTextureUse(
            causticAccumulator,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        // Geometry downsample writes its fresh cache after photons. The following wavelet callback reads that cache,
        // so the compiler owns their exact UAV-to-SRV handoff while ping-pong transitions remain local.
        hardwareGeometryResourceUses.push_back(ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareGeometryResourceUses.push_back(ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareGeometryResourceUses.push_back(WriteTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        // Prepare consumes both immutable graph-produced inputs, then writes the parity-selected first ping-pong
        // target. It does not sample the ping-pong input for this stage; wavelets own that alternating sequence.
        constexpr bool s_HardwareCausticResolvePrepareWritesHalf = (NWB_CAUSTIC_RESOLVE_PASS_COUNT % 2u) == 0u;
        hardwareResolvePrepareResourceUses.push_back(ReadTextureUse(
            causticAccumulator,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolvePrepareResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveSecondWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveThirdWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveFourthWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveFifthWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        if(s_HardwareCausticResolvePrepareWritesHalf){
            hardwareResolvePrepareResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        else{
            hardwareResolvePrepareResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }

        // Upsample receives its exact final graph handoff. The following timing-close callback carries no resource use.
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(WriteTextureUse(
            currentCausticIrradiance,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        const auto appendOptionalReadBuffer = [&](
            const Core::BufferHandle& buffer,
            const Name& identity,
            const AStringView label,
            const Core::ResourceStates::Mask state
        ){
            if(!buffer)
                return true;
            const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
            if(!resource.valid())
                return false;
            hardwarePhotonResourceUses.push_back(ReadUse(resource, state));
            return true;
        };
        bool optionalResourcesImported =
            appendOptionalReadBuffer(
                meshViewBufferSnapshot.buffer,
                Name("render.deferred.mesh_view"),
                "Mesh View",
                Core::ResourceStates::ConstantBuffer
            )
            && appendOptionalReadBuffer(
                rayTracingGraphResources.shadowInstanceMaterialBuffer,
                Name("render.deferred_effects.instance_material"),
                "Shadow Instance Materials",
                Core::ResourceStates::ShaderResource
            )
            && appendOptionalReadBuffer(
                rayTracingGraphResources.shadowMaterialTypedBuffer,
                Name("render.deferred_effects.material_typed"),
                "Shadow Typed Materials",
                Core::ResourceStates::ShaderResource
            )
            && appendOptionalReadBuffer(
                rayTracingGraphResources.shadowInstanceBuffer,
                Name("render.deferred_effects.shadow_instances"),
                "Shadow Instances",
                Core::ResourceStates::ShaderResource
            )
            && appendOptionalReadBuffer(
                rayTracingGraphResources.causticEmissionTargetBuffer,
                Name("render.hardware_caustics.emission_targets"),
                "Caustic Emission Targets",
                Core::ResourceStates::ShaderResource
            )
        ;
        if(materialContextSlots.valid()){
            hardwarePhotonResourceUses.push_back(ReadUse(
                materialContextSlots,
                Core::ResourceStates::ConstantBuffer
            ));
        }
        // Hardware caustic closest-hit shaders directly heap-load the selected mesh attribute streams.  These are
        // centrally imported retained handles, so declaring them here gives the compiler the Prefix -> Caustics
        // SRV handoff instead of relying on the recorder's manual staging loop.
        for(const Core::GpuGraphResourceId resource : hardwareTraceAttributeResources){
            if(!resource.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid prepared hardware-caustics attribute resource"));
                return;
            }
            if(!hardwareTraceAttributeStatesGraphOwned)
                hardwarePhotonResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
        if(rayTracingGraphResources.sceneTlas){
            const Core::GpuGraphResourceId tlas = m_deferredLightingTaskGraph.importAccelStruct(
                rayTracingGraphResources.sceneTlas,
                AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
                    .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
            );
            optionalResourcesImported = optionalResourcesImported && tlas.valid();
            if(tlas.valid()){
                hardwarePhotonResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
            }
        }
        if(!optionalResourcesImported){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a hardware-caustics dynamic resource"));
            return;
        }

        const Core::GpuTaskId hardwareDependencies[] = { m_graphicsPrefixTask };
        const Core::GpuExternalCompletionId* const hardwareExternalDependencies = features.laggedLightingHistoryWriterWaitPending
            ? &m_deferredLightingHistoryWriterDrainCompletion
            : nullptr
        ;
        const usize hardwareExternalDependencyCount = features.laggedLightingHistoryWriterWaitPending ? 1u : 0u;
        Core::GpuTaskSchedulingHint hardwareScheduling;
        hardwareScheduling.cost = Core::GpuTaskCostHint::Large;
        hardwareScheduling.forceSubmissionBoundary = true;
        hardwareScheduling.allowPacketMerge = false;
        EnableSameFamilyComputeEffectRouting(hardwareScheduling);
        EnableCrossFamilyComputeEffectRouting(hardwareScheduling);
        const Core::GpuTaskExternalStateSource accumulatorStateSources[] = {
            Core::GpuTaskExternalStateSource{
                .states = m_hardwareCausticAccumulatorPersistentState.source(),
            },
        };
        const usize accumulatorStateSourceCount = m_hardwareCausticAccumulatorPersistentState.valid()
            ? LengthOf(accumulatorStateSources)
            : 0u
        ;

        // The lagged-history completion must protect the first writer too: clear starts the existing Hardware
        // Caustics Graphics packet and the ray-tracing producer merges into it below. A fresh temporal accumulator
        // inserts its typed zero clear between that no-producer result and the producer callback.
        Core::GpuTaskSchedulingHint irradianceClearScheduling;
        irradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        irradianceClearScheduling.allowPacketMerge = true;
        // Start Hardware Caustics on the selected same-class Graphics lane when available, including an alternate
        // Graphics family only when its declared resources support the crossing. Every later direct successor
        // retains that lane through the graph's normal physical-queue dependency plan.
        EnableSameFamilyComputeEffectRouting(irradianceClearScheduling, false);
        EnableCrossFamilyComputeEffectRouting(irradianceClearScheduling);


        Core::GpuTaskDesc irradianceClearDesc;
        irradianceClearDesc
            .setIdentity(Name("render.hardware_caustics.irradiance_clear"))
            .setMarkerLabel("Hardware Caustics Irradiance Clear")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(irradianceClearScheduling)
            .setDependencies(hardwareDependencies, LengthOf(hardwareDependencies))
            .setExternalDependencies(hardwareExternalDependencies, hardwareExternalDependencyCount)
        ;
        Core::GpuClearTextureTaskDesc irradianceClear;
        irradianceClear.destination = currentCausticIrradiance;
        irradianceClear.subresources = ECSRenderDetail::s_FramebufferSubresources;
        irradianceClear.valueType = Core::GpuClearTextureTaskValueType::Float;
        irradianceClear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        const Core::GpuTaskId irradianceClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            irradianceClearDesc,
            irradianceClear
        );
        if(!irradianceClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics irradiance clear"));
            return;
        }
        m_deferredCausticIrradianceClearTask = irradianceClearTask;

        Core::GpuTaskId causticsDependency = irradianceClearTask;
        const bool graphOwnsNonTemporalAccumulatorClear = rayTracingGraphResources.causticTemporalDecay <= 0.f;
        if(graphOwnsNonTemporalAccumulatorClear){
            Core::GpuTaskSchedulingHint accumulatorNonTemporalClearScheduling = irradianceClearScheduling;
            accumulatorNonTemporalClearScheduling.mergeWithPrevious = true;
            // The direct accumulator clear must remain in the accepted Hardware Caustics producer/timing packet.
            accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;
            EnableSameFamilyComputeEffectRouting(accumulatorNonTemporalClearScheduling);
            Core::GpuTaskDesc accumulatorNonTemporalClearDesc;
            accumulatorNonTemporalClearDesc
                .setIdentity(Name("render.hardware_caustics.accumulator_non_temporal_clear"))
                .setMarkerLabel("Hardware Caustics Accumulator Clear")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulatorNonTemporalClearScheduling)
                .setDependencies(&causticsDependency, 1u)
                .setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)
            ;
            Core::GpuClearTextureTaskDesc accumulatorNonTemporalClear;
            accumulatorNonTemporalClear.destination = causticAccumulator;
            accumulatorNonTemporalClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
            accumulatorNonTemporalClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
            accumulatorNonTemporalClear.uintValue = Core::UIntColor(0u);
            const Core::GpuTaskId accumulatorNonTemporalClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
                accumulatorNonTemporalClearDesc,
                accumulatorNonTemporalClear
            );
            if(!accumulatorNonTemporalClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics non-temporal accumulator clear"));
                return;
            }
            m_deferredCausticAccumulatorNonTemporalClearTask = accumulatorNonTemporalClearTask;
            causticsDependency = accumulatorNonTemporalClearTask;
        }
        const bool graphOwnsAccumulatorBootstrapClear =
            !rayTracingGraphResources.causticAccumulatorInitialized
            && rayTracingGraphResources.causticTemporalDecay > 0.f
        ;
        if(graphOwnsAccumulatorBootstrapClear){
            Core::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling;
            accumulatorBootstrapClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
            accumulatorBootstrapClearScheduling.allowPacketMerge = true;
            accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
            // The bootstrap clear directly follows irradiance clear and must remain in the accepted producer packet.
            accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;
            EnableSameFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
            EnableCrossFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
            Core::GpuTaskDesc accumulatorBootstrapClearDesc;
            accumulatorBootstrapClearDesc
                .setIdentity(Name("render.hardware_caustics.accumulator_bootstrap_clear"))
                .setMarkerLabel("Hardware Caustics Accumulator Bootstrap Clear")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulatorBootstrapClearScheduling)
                .setDependencies(&irradianceClearTask, 1u)
                .setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)
            ;
            Core::GpuClearTextureTaskDesc accumulatorBootstrapClear;
            accumulatorBootstrapClear.destination = causticAccumulator;
            accumulatorBootstrapClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
            accumulatorBootstrapClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
            accumulatorBootstrapClear.uintValue = Core::UIntColor(0u);
            const Core::GpuTaskId accumulatorBootstrapClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
                accumulatorBootstrapClearDesc,
                accumulatorBootstrapClear
            );
            if(!accumulatorBootstrapClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator bootstrap clear"));
                return;
            }
            m_deferredCausticAccumulatorBootstrapClearTask = accumulatorBootstrapClearTask;
            causticsDependency = accumulatorBootstrapClearTask;
        }

        const bool graphOwnsAccumulatorDecay =
            rayTracingGraphResources.causticAccumulatorInitialized
            && rayTracingGraphResources.causticTemporalDecay > 0.f
        ;
        if(graphOwnsAccumulatorDecay){
            Core::GpuTaskSchedulingHint accumulatorDecayScheduling;
            accumulatorDecayScheduling.cost = Core::GpuTaskCostHint::Tiny;
            accumulatorDecayScheduling.allowPacketMerge = true;
            accumulatorDecayScheduling.mergeWithPrevious = true;
            // The direct accumulator decay must remain in the accepted Hardware Caustics producer/timing packet.
            accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;
            EnableSameFamilyComputeEffectRouting(accumulatorDecayScheduling);
            EnableCrossFamilyComputeEffectRouting(accumulatorDecayScheduling);
            const Core::GpuTaskResourceUse accumulatorDecayUses[] = {
                ReadWriteTextureUse(
                    causticAccumulator,
                    ECSRenderDetail::s_CausticAccumulatorSubresources,
                    Core::ResourceStates::UnorderedAccess
                ),
            };
            Core::GpuTaskDesc accumulatorDecayDesc;
            accumulatorDecayDesc
                .setIdentity(Name("render.hardware_caustics.accumulator_decay"))
                .setMarkerLabel("Hardware Caustics Accumulator Decay")
                .setQueue(GraphicsPreferredComputeQueueRequest())
                .setScheduling(accumulatorDecayScheduling)
                .setDependencies(&causticsDependency, 1u)
                .setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)
                .setResourceUses(accumulatorDecayUses, LengthOf(accumulatorDecayUses))
            ;
            const Core::GpuTaskId accumulatorDecayTask = m_raytracingSystem.declareCausticAccumulatorDecayTask(
                m_deferredLightingTaskGraph,
                accumulatorDecayDesc,
                deferredTargets,
                meshViewBufferSnapshot,
                &m_shadowPreparationOutcome.ready,
                rayTracingGraphResources.causticTemporalDecay,
                true,
                hardwareCausticsTimingTicket,
                &causticPhotonTiming,
                true
            );
            if(!accumulatorDecayTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator decay"));
                return;
            }
            m_deferredCausticAccumulatorDecayTask = accumulatorDecayTask;
            causticsDependency = accumulatorDecayTask;
        }

        Core::GpuTaskSchedulingHint hardwareCausticsScheduling = hardwareScheduling;
        hardwareCausticsScheduling.forceSubmissionBoundary = false;
        hardwareCausticsScheduling.allowPacketMerge = true;
        hardwareCausticsScheduling.mergeWithPrevious = true;
        // Photon, geometry, and resolve stages are explicit immediate successors in one Hardware Caustics timing chain.
        hardwareCausticsScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc hardwarePhotonDesc;
        hardwarePhotonDesc
            .setIdentity(Name("render.hardware_caustics.photons"))
            .setMarkerLabel("Hardware Caustic Photons")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareCausticsScheduling)
            .setDependencies(&causticsDependency, 1u)
            .setResourceUses(hardwarePhotonResourceUses.data(), hardwarePhotonResourceUses.size())
            .setResourceSetUses(
                hardwarePhotonResourceSetUseCount != 0u ? hardwarePhotonResourceSetUses : nullptr,
                hardwarePhotonResourceSetUseCount
            )
        ;
        m_deferredCausticPhotonTask = m_raytracingSystem.declareHardwareCausticsTask(
            m_deferredLightingTaskGraph,
            hardwarePhotonDesc,
            deferredTargets,
            deferredLightingResources,
            meshViewBufferSnapshot,
            &m_shadowPreparationOutcome.ready,
            hardwareCausticsTimingTicket,
            true,
            graphOwnsAccumulatorBootstrapClear,
            graphOwnsNonTemporalAccumulatorClear,
            graphOwnsAccumulatorDecay,
            true,
            &causticPhotonTiming,
            &m_deferredCausticProducerDispatched
        );
        if(!m_deferredCausticPhotonTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics photon graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareGeometryScheduling = hardwareCausticsScheduling;
        hardwareGeometryScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareGeometryDesc;
        hardwareGeometryDesc
            .setIdentity(Name("render.hardware_caustics.geometry_downsample"))
            .setMarkerLabel("Hardware Caustics Geometry Downsample")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareGeometryScheduling)
            .setDependencies(&m_deferredCausticPhotonTask, 1u)
            .setResourceUses(hardwareGeometryResourceUses.data(), hardwareGeometryResourceUses.size())
        ;
        m_deferredCausticGeometryTask = m_raytracingSystem.declareCausticGeometryDownsampleTask(
            m_deferredLightingTaskGraph,
            hardwareGeometryDesc,
            deferredTargets,
            hardwareCausticsTimingTicket,
            &m_deferredCausticProducerDispatched,
            &causticResolveTiming,
            true
        );
        if(!m_deferredCausticGeometryTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics geometry graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolvePrepareScheduling = hardwareGeometryScheduling;
        hardwareResolvePrepareScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolvePrepareDesc;
        hardwareResolvePrepareDesc
            .setIdentity(Name("render.hardware_caustics.resolve_prepare"))
            .setMarkerLabel("Hardware Caustics Resolve Prepare")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolvePrepareScheduling)
            .setDependencies(&m_deferredCausticGeometryTask, 1u)
            .setResourceUses(hardwareResolvePrepareResourceUses.data(), hardwareResolvePrepareResourceUses.size())
        ;
        m_deferredCausticResolvePrepareTask = m_raytracingSystem.declareCausticResolvePrepareTask(
            m_deferredLightingTaskGraph,
            hardwareResolvePrepareDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolvePrepareTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-prepare graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveWaveletScheduling = hardwareResolvePrepareScheduling;
        hardwareResolveWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveWaveletDesc;
        hardwareResolveWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveWaveletScheduling)
            .setDependencies(&m_deferredCausticResolvePrepareTask, 1u)
            .setResourceUses(hardwareResolveWaveletResourceUses.data(), hardwareResolveWaveletResourceUses.size())
        ;
        m_deferredCausticResolveWaveletTask = m_raytracingSystem.declareCausticResolveWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics first-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveSecondWaveletScheduling = hardwareResolveWaveletScheduling;
        hardwareResolveSecondWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveSecondWaveletDesc;
        hardwareResolveSecondWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_second_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Second Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveSecondWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveSecondWaveletResourceUses.data(),
                hardwareResolveSecondWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveSecondWaveletTask = m_raytracingSystem.declareCausticResolveSecondWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveSecondWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveSecondWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics second-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveThirdWaveletScheduling = hardwareResolveSecondWaveletScheduling;
        hardwareResolveThirdWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveThirdWaveletDesc;
        hardwareResolveThirdWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_third_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Third Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveThirdWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveSecondWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveThirdWaveletResourceUses.data(),
                hardwareResolveThirdWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveThirdWaveletTask = m_raytracingSystem.declareCausticResolveThirdWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveThirdWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveThirdWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics third-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveFourthWaveletScheduling = hardwareResolveThirdWaveletScheduling;
        hardwareResolveFourthWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveFourthWaveletDesc;
        hardwareResolveFourthWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_fourth_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Fourth Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveFourthWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveThirdWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveFourthWaveletResourceUses.data(),
                hardwareResolveFourthWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveFourthWaveletTask = m_raytracingSystem.declareCausticResolveFourthWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveFourthWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveFourthWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fourth-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveFifthWaveletScheduling = hardwareResolveFourthWaveletScheduling;
        hardwareResolveFifthWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveFifthWaveletDesc;
        hardwareResolveFifthWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_fifth_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Fifth Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveFifthWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveFourthWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveFifthWaveletResourceUses.data(),
                hardwareResolveFifthWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveFifthWaveletTask = m_raytracingSystem.declareCausticResolveFifthWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveFifthWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveFifthWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fifth-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveUpsampleScheduling = hardwareResolveFifthWaveletScheduling;
        hardwareResolveUpsampleScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveUpsampleDesc;
        hardwareResolveUpsampleDesc
            .setIdentity(Name("render.hardware_caustics.resolve_upsample"))
            .setMarkerLabel("Hardware Caustics Resolve Upsample")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveUpsampleScheduling)
            .setDependencies(&m_deferredCausticResolveFifthWaveletTask, 1u)
            .setResourceUses(hardwareResolveUpsampleResourceUses.data(), hardwareResolveUpsampleResourceUses.size())
        ;
        m_deferredCausticResolveUpsampleTask = m_raytracingSystem.declareCausticResolveUpsampleTask(
            m_deferredLightingTaskGraph,
            hardwareResolveUpsampleDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveUpsampleTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-upsample graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveScheduling = hardwareResolveUpsampleScheduling;
        hardwareResolveScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveDesc;
        hardwareResolveDesc
            .setIdentity(Name("render.hardware_caustics.resolve_timing_close"))
            .setMarkerLabel("Hardware Caustics Resolve Timing Close")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(hardwareResolveScheduling)
            .setDependencies(&m_deferredCausticResolveUpsampleTask, 1u)
        ;
        m_deferredHardwareCausticsTask = m_raytracingSystem.declareCausticResolveTask(
            m_deferredLightingTaskGraph,
            hardwareResolveDesc,
            hardwareCausticsTimingTicket,
            &m_deferredCausticProducerDispatched,
            &causticResolveTiming
        );
        if(!m_deferredHardwareCausticsTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve graph task"));
            return;
        }
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


// Freeze the transparent CSG interval producer before AVBOIT native recording.  Its shared instance/material
    // and CSG buffers are intentionally overwritten by the later occupancy/extinction/accumulation compatibility
    // paths, so this snapshot applies only to the receiver-surface interval work immediately before occupancy.
    Core::GpuTaskId transparentCsgUploadTask = m_graphicsPrefixTask;
    Core::Alloc::ScratchArena transparentCsgMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId transparentCsgMaterialGeometrySet;
    Core::GpuGraphResourceSetId transparentCsgMaterialSampledTextureSet;
    const bool hasTransparentCsgFrameWork = hasTransparentRenderers
        && (csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork)
    ;
    if(hasTransparentCsgFrameWork){
        Core::Alloc::ScratchArena transparentCsgUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions transparentCsgDrawItems{ transparentCsgUploadScratch };
        InstanceGpuDataVector transparentCsgInstanceData{ transparentCsgUploadScratch };
        CsgFrameGpuData transparentCsgFrameData{ transparentCsgUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector transparentCsgMaterialTypedRanges{ transparentCsgUploadScratch };
#endif
        MaterialTypedByteDataVector transparentCsgMaterialTypedBytes{ transparentCsgUploadScratch };
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            true,
            csgFrameState,
            transparentCsgDrawItems,
            transparentCsgInstanceData,
            transparentCsgFrameData,
#if defined(NWB_DEBUG)
            transparentCsgMaterialTypedRanges,
#endif
            transparentCsgMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly,
            &meshViewState
        );

        if(!transparentCsgDrawItems.csgReceiverSurface.empty() && transparentCsgFrameData.hasWork()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !csgReceiverRanges.valid()
                || !csgCutters.valid()
                || !csgClipContextSlots.valid()
                || !csgIntervalSampleState.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    transparentCsgInstanceData,
                    transparentCsgMaterialTypedBytes
                )
                || !m_csgSystem.csgFrameBuffersReady(transparentCsgFrameData)
                || !m_materialSystem.materialPassDrawResourcesReady(transparentCsgDrawItems.csgReceiverSurface)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared transparent CSG interval resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const transparentCsgMaterialGeometryDrawSets[] = {
                &transparentCsgDrawItems.csgReceiverSurface,
            };
            avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                transparentCsgMaterialGeometryDrawSets,
                LengthOf(transparentCsgMaterialGeometryDrawSets),
                transparentCsgMaterialGeometryScratch,
                Name("render.avboit.intervals.transparent_csg_material_geometry"),
                "Transparent CSG Material Geometry",
                transparentCsgMaterialGeometrySet
            );
            if(!avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material geometry states"));
                return;
            }
            const bool transparentCsgMaterialSampledTexturesCollected =
                avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    transparentCsgMaterialGeometryDrawSets,
                    LengthOf(transparentCsgMaterialGeometryDrawSets),
                    transparentCsgMaterialGeometryScratch,
                    Name("render.avboit.intervals.transparent_csg_material_sampled_textures"),
                    "Transparent CSG Material Sampled Textures",
                    transparentCsgMaterialSampledTextureSet
                )
            ;
            if(!transparentCsgMaterialSampledTexturesCollected){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material sampled textures"));
                return;
            }

            m_materialSystem.prepareMaterialPassInstanceUploadData(transparentCsgInstanceData);
#if defined(NWB_DEBUG)
            if(
                transparentCsgInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || transparentCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || transparentCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: transparent CSG interval upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(transparentCsgInstanceData.size() == transparentCsgMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                transparentCsgMaterialTypedRanges,
                transparentCsgMaterialTypedBytes
            );
#endif

            CsgClipContextSlots transparentCsgClipContextSlotData;
            CsgIntervalSampleStateGpuData transparentCsgIntervalSampleStateData;
            if(
                !m_csgSystem.prepareCsgClipContextSlotData(
                    deferredTargets,
                    transparentCsgFrameData,
                    frameBindings,
                    transparentCsgClipContextSlotData
                )
                || !m_csgSystem.prepareCsgIntervalSampleStateData(
                    deferredTargets,
                    transparentCsgFrameData,
                    frameBindings,
                    transparentCsgIntervalSampleStateData
                )
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot transparent CSG interval auxiliary upload data"));
                return;
            }

            const Core::GpuUploadBlobId transparentCsgInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgInstanceData.data(),
                transparentCsgInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgMaterialTypedBytes.data(),
                transparentCsgMaterialTypedBytes.size(),
                alignof(u32)
            );
            const Core::GpuUploadBlobId transparentCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgFrameData.receiverRanges.data(),
                transparentCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                alignof(CsgReceiverRangeGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgFrameData.cutters.data(),
                transparentCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                alignof(CsgCutterGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                &transparentCsgClipContextSlotData,
                sizeof(transparentCsgClipContextSlotData),
                alignof(CsgClipContextSlots)
            );
            const Core::GpuUploadBlobId transparentCsgIntervalSampleStateBlob =
                m_deferredLightingTaskGraph.copyUploadData(
                    &transparentCsgIntervalSampleStateData,
                    sizeof(transparentCsgIntervalSampleStateData),
                    alignof(CsgIntervalSampleStateGpuData)
                )
            ;
            if(
                !transparentCsgInstanceBlob.valid()
                || !transparentCsgMaterialTypedBlob.valid()
                || !transparentCsgReceiverRangesBlob.valid()
                || !transparentCsgCuttersBlob.valid()
                || !transparentCsgClipContextSlotsBlob.valid()
                || !transparentCsgIntervalSampleStateBlob.valid()
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable transparent CSG interval upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint transparentCsgUploadScheduling;
            transparentCsgUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            transparentCsgUploadScheduling.forceSubmissionBoundary = false;
            transparentCsgUploadScheduling.allowPacketMerge = true;
            transparentCsgUploadScheduling.mergeWithPrevious = true;
            // Hardware Caustics and AVBOIT have independent timing and acceptance submissions even when both route
            // to Graphics. Start this frozen AVBOIT upload chain in its own packet, then merge every following
            // upload/clear/interval callback into that new semantic packet.
            Core::GpuTaskSchedulingHint transparentCsgFirstUploadScheduling = transparentCsgUploadScheduling;
            transparentCsgFirstUploadScheduling.mergeWithPrevious = false;

            Core::GpuTaskDesc transparentCsgInstanceUploadDesc;
            transparentCsgInstanceUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_instances_upload"))
                .setMarkerLabel("Transparent CSG Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgFirstUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material instance upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgMaterialTypedUploadDesc;
            transparentCsgMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_typed_upload"))
                .setMarkerLabel("Transparent CSG Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material typed upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgReceiverRangesUploadDesc;
            transparentCsgReceiverRangesUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.receiver_ranges_upload"))
                .setMarkerLabel("Transparent CSG Receiver Ranges Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgReceiverRangesUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgReceiverRangesBlob,
                    .destination = csgReceiverRanges,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-range upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgCuttersUploadDesc;
            transparentCsgCuttersUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.cutters_upload"))
                .setMarkerLabel("Transparent CSG Cutters Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgCuttersUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgCuttersBlob,
                    .destination = csgCutters,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG cutter upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgClipContextSlotsUploadDesc;
            transparentCsgClipContextSlotsUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.clip_context_slots_upload"))
                .setMarkerLabel("Transparent CSG Clip Context Slots Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgClipContextSlotsUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgClipContextSlotsBlob,
                    .destination = csgClipContextSlots,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG clip-context upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgIntervalSampleStateUploadDesc;
            transparentCsgIntervalSampleStateUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.interval_sample_state_upload"))
                .setMarkerLabel("Transparent CSG Interval State Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgIntervalSampleStateUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgIntervalSampleStateBlob,
                    .destination = csgIntervalSampleState,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-state upload"));
                return;
            }

            avboitPrePayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            avboitCsgReceiverSpanPayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            avboitCsgReceiverSpanPayload.csgFrameBuffersUploaded = true;
            avboitCsgIntervalCombinePayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            if(
                !avboitPrePayload.transparentCsgSnapshot.captured
                || !avboitCsgReceiverSpanPayload.transparentCsgSnapshot.captured
                || !avboitCsgIntervalCombinePayload.transparentCsgSnapshot.captured
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not capture transparent CSG interval graph snapshots"));
                return;
            }
            avboitCsgIntervalCombinePayload.csgFrameBuffersUploaded = true;
            avboitPrePayload.transparentCsgStreamsUploaded = true;
        }
    }


// Prepared transparent CSG uses the same persistent interval values and peel targets as opaque CSG. Place its
    // frozen rect clear immediately after immutable stream uploads so the graph owns CopyDest -> UAV ordering,
    // then declare the CSG StorageImage working set on the producer task. An unprepared compatibility path
    // continues to call the legacy all-target helper.
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        transparentCsgIntervalClearScheduling.forceSubmissionBoundary = false;
        transparentCsgIntervalClearScheduling.allowPacketMerge = true;
        transparentCsgIntervalClearScheduling.mergeWithPrevious = true;
        const auto makeTransparentCsgIntervalClearTaskDesc = [&transparentCsgIntervalClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgIntervalClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const Core::Rect transparentCsgClearRect = avboitPrePayload.transparentCsgSnapshot.csgWorkRegion.resolveRect(
            deferredTargets.width,
            deferredTargets.height
        );
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearBeginHooks{
            .context = &transparentCsgIntervalClearTimingState,
            .beforeClear = &ECSRenderDetail::BeginCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearEndHooks{
            .context = &transparentCsgIntervalClearTimingState,
            .afterClear = &ECSRenderDetail::EndCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask =
            m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
                makeTransparentCsgIntervalClearTaskDesc(
                    Name("render.avboit.transparent_csg.interval_clear"),
                    "Transparent CSG Interval Id Clear",
                    transparentCsgUploadTask
                ),
                Core::GpuClearTextureRectUIntTaskDesc{
                    .destination = csgIntervalId,
                    .subresources = csgPeelSubresources,
                    .rect = transparentCsgClearRect,
                    .uintValue = Core::UIntColor(0u),
                    .recordHooks = transparentCsgIntervalClearBeginHooks,
                }
            );
        if(!m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG interval-id clear"));
            return;
        }
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearTailScheduling = transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc transparentCsgIntervalClearTailDesc;
        transparentCsgIntervalClearTailDesc
            .setIdentity(Name("render.avboit.transparent_csg.receiver_event_count_clear"))
            .setMarkerLabel("Transparent CSG Receiver Event Count Clear")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(transparentCsgIntervalClearTailScheduling)
            .setDependencies(&m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask, 1u)
        ;
        m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask = m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
            transparentCsgIntervalClearTailDesc,
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = csgReceiverEventCount,
                .subresources = csgReceiverEventCountSubresources,
                .rect = transparentCsgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = transparentCsgIntervalClearEndHooks,
            }
        );
        if(!m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG receiver-event clear"));
            return;
        }
        transparentCsgUploadTask = m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask;
        avboitPrePayload.transparentCsgIntervalTargetsGraphOwned = true;
        avboitPrePayload.transparentCsgIntervalPeelTargetStatesGraphOwned = true;
        avboitPrePayload.transparentCsgReceiverSurfaceImageStatesGraphOwned = true;
        // The following Span/Combine callbacks own their exact UAV handoffs, while direct and aggregate
        // compatibility calls retain native fences.
        avboitPrePayload.deferTransparentCsgIntervalCombine = true;
        avboitPrePayload.transparentCsgClipBufferStatesGraphOwned = true;
        avboitPrePayload.transparentCsgMaterialFrameStatesGraphOwned = true;
        NWB_ASSERT(
            avboitPrePayload.transparentCsgStreamsUploaded
            && avboitPrePayload.transparentCsgSnapshot.captured
        );
    }

    // The interval producer consumes the first frozen transparent CSG stream. Its graph-visible states must be
    // declared here, before its native work records, rather than on the later occupancy task.
    Core::Alloc::ScratchArena avboitIntervalResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalResourceUses{ avboitIntervalResourceScratch };
    avboitIntervalResourceUses.reserve(16u);
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        avboitIntervalResourceUses.push_back(ReadUse(depth));
        avboitIntervalResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
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
    avboitOccupancyPayload.avboitSystem = &m_avboitSystem;
    avboitOccupancyPayload.targets = &deferredTargets;
    avboitOccupancyPayload.timingTicket = &avboitPreTimingTicket;
    avboitOccupancyPayload.hasTransparentRenderers = hasTransparentRenderers;

    Core::GpuTaskId occupancyUploadTask = avboitIntervalCompletionTask;
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
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.avboit.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitOccupancy,
            true,
            csgFrameState,
            occupancyDrawItems,
            occupancyInstanceData,
            occupancyCsgFrameData,
#if defined(NWB_DEBUG)
            occupancyMaterialTypedRanges,
#endif
            occupancyMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly,
            &meshViewState
        );

        const bool occupancyHasCsgDrawItems = !occupancyDrawItems.csg.empty();
        if(!occupancyDrawItems.empty()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    occupancyInstanceData,
                    occupancyMaterialTypedBytes
                )
                || !m_materialSystem.materialPassDrawResourcesReady(occupancyDrawItems.regular)
                || (occupancyHasCsgDrawItems && (
                    !occupancyCsgFrameData.hasWork()
                    ||
                    !csgReceiverRanges.valid()
                    || !csgCutters.valid()
                    || !csgClipContextSlots.valid()
                    || !m_csgSystem.csgFrameBuffersReady(occupancyCsgFrameData)
                    || !m_materialSystem.materialPassDrawResourcesReady(occupancyDrawItems.csg)
                ))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT occupancy resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const occupancyMaterialGeometryDrawSets[] = {
                &occupancyDrawItems.regular,
                &occupancyDrawItems.csg,
            };
            avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
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

            m_materialSystem.prepareMaterialPassInstanceUploadData(occupancyInstanceData);
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
            // A phase may graph-own exactly one alias-free compute stream. Mixed regular/CSG work retains the
            // established local interleaving because a single producer/raster handoff cannot preserve its draw
            // order.
            occupancyRegularComputeEmulationPlanCaptured = occupancyDrawItems.csg.computeDrawItems.empty()
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && avboitOccupancyComputeEmulationPayload.plan.capture(
                    m_meshSystem,
                    occupancyDrawItems.regular
                )
            ;
            occupancyCsgComputeEmulationPlanCaptured = occupancyDrawItems.regular.computeDrawItems.empty()
                && occupancyCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && avboitOccupancyComputeEmulationPayload.csgPlan.capture(
                    m_meshSystem,
                    occupancyDrawItems.csg,
                    occupancyCsgFrameData
                )
            ;
            // The all-compute two-through-five-draw case can preserve one shared generated output only
            // as an explicit D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) -> R(D) -> D(E) -> R(E)]
            // sequence. Keep mesh and CSG work out of this narrow slice so the aggregate Occupancy callback is
            // never partially replayed around its phases.
            occupancySharedComputeEmulationPlanCaptured = !occupancyRegularComputeEmulationPlanCaptured
                && occupancyDrawItems.regular.meshDrawItems.empty()
                && occupancyDrawItems.csg.empty()
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && occupancySharedComputeEmulationPlan.capture(
                    m_meshSystem,
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
            // The graph phase is still authoritative for an empty visible set. Retaining the empty snapshot prevents
            // native recording from re-gathering mutable renderer state as a compatibility fallback.
            avboitOccupancyPayload.occupancySnapshot.capture(
                occupancyDrawItems,
                occupancyCsgFrameData,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size()
            );
            avboitOccupancyPayload.occupancyPhasePrepared = true;
        }
    }


// Preserve the native order: phase-local material/CSG uploads first, then the serial AVBOIT target values, then
    // occupancy. Each value now records as a typed built-in clear, so the graph owns all nine CopyDest operations
    // instead of a custom native thunk hiding them behind one broad resource-use declaration.
    Core::GpuTaskId avboitClearTask = occupancyUploadTask;
    if(clearAvboitTargets){
        Core::GpuTaskSchedulingHint avboitClearScheduling;
        avboitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        avboitClearScheduling.forceSubmissionBoundary = false;
        avboitClearScheduling.allowPacketMerge = true;
        avboitClearScheduling.mergeWithPrevious = true;
        // The direct serial target-clear chain and its Occupancy successor own one AVBOIT Pre timing packet even
        // when split Depth Warp consumes it from another queue.
        avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto makeAvboitClearTaskDesc = [&avboitClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(avboitClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const auto makeAvboitFloatClearDesc = [](
            const Core::GpuGraphResourceId destination,
            const Core::Color& value,
            const Core::GpuClearTextureTaskRecordHooks& recordHooks = {}
        ){
            Core::GpuClearTextureTaskDesc clearDesc;
            clearDesc.destination = destination;
            clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
            clearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
            clearDesc.floatValue = value;
            clearDesc.recordHooks = recordHooks;
            return clearDesc;
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearBeginHooks{
            .context = &avboitClearTimingState,
            .beforeClear = &ECSRenderDetail::BeginAvboitClearTiming,
            .discarded = &ECSRenderDetail::DiscardAvboitClearTiming,
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearEndHooks{
            .context = &avboitClearTimingState,
            .afterClear = &ECSRenderDetail::EndAvboitClearTiming,
            .discarded = &ECSRenderDetail::DiscardAvboitClearTiming,
        };
        const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.low_raster"),
                "AVBOIT Clear Low Raster",
                occupancyUploadTask
            ),
            makeAvboitFloatClearDesc(avboitLowRaster, transparentBlack, avboitClearBeginHooks)
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT low-raster clear"));
            return;
        }
        m_avboitSystem.taskGraphStage().m_clearFirstTask = avboitClearTask;
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.accum_color"),
                "AVBOIT Clear Accumulation Color",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(avboitAccumColor, transparentBlack)
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT accumulation-color clear"));
            return;
        }
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.accum_extinction"),
                "AVBOIT Clear Accumulation Extinction",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(avboitAccumExtinction, transparentBlack)
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT accumulation-extinction clear"));
            return;
        }
        const auto appendAvboitBufferClear = [&](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuGraphResourceId destination,
            const u32 value
        ){
            avboitClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                makeAvboitClearTaskDesc(identity, markerLabel, avboitClearTask),
                Core::GpuClearBufferTaskDesc{
                    .destination = destination,
                    .clearValue = value,
                }
            );
            return avboitClearTask.valid();
        };
        if(
            !appendAvboitBufferClear(
                Name("render.avboit.clear.coverage"),
                "AVBOIT Clear Coverage",
                avboitCoverage,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.depth_warp"),
                "AVBOIT Clear Depth Warp",
                avboitDepthWarp,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.control"),
                "AVBOIT Clear Control",
                avboitControl,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.extinction"),
                "AVBOIT Clear Extinction",
                avboitExtinction,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.extinction_overflow"),
                "AVBOIT Clear Extinction Overflow",
                avboitExtinctionOverflow,
                NWB_AVBOIT_OVERFLOW_INVALID
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT buffer clear"));
            return;
        }
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.transmittance"),
                "AVBOIT Clear Transmittance",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(
                avboitTransmittance,
                Core::Color(1.f, 1.f, 1.f, 1.f),
                avboitClearEndHooks
            )
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT transmittance clear"));
            return;
        }
        m_avboitSystem.taskGraphStage().m_clearTask = avboitClearTask;
    }

    const bool occupancyCsgIntervalSampleImageStatesGraphOwned =
        avboitIntervalOutputsGraphOwned && occupancyCsgStreamsUploaded
    ;
    const bool occupancyCsgClipBufferStatesGraphOwned = occupancyCsgStreamsUploaded;
    NWB_ASSERT(
        !occupancyCsgIntervalSampleImageStatesGraphOwned
        || (
            avboitOccupancyPayload.occupancyStreamsUploaded
            && avboitOccupancyPayload.occupancySnapshot.captured
        )
    );
    NWB_ASSERT(
        !occupancyCsgClipBufferStatesGraphOwned
        || (
            avboitOccupancyPayload.occupancyStreamsUploaded
            && avboitOccupancyPayload.occupancySnapshot.captured
        )
    );
    avboitOccupancyPayload.occupancyCsgIntervalSampleImageStatesGraphOwned =
        occupancyCsgIntervalSampleImageStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyCsgClipBufferStatesGraphOwned =
        occupancyCsgClipBufferStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyMaterialFrameStatesGraphOwned = avboitOccupancyPayload.occupancyStreamsUploaded;
    avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned =
        avboitOccupancyPayload.occupancyStreamsUploaded
        && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId occupancyComputeEmulationOutputSet;
    Core::Alloc::ScratchArena occupancyComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool occupancyComputeEmulationPlanCaptured =
        occupancyRegularComputeEmulationPlanCaptured
        || occupancyCsgComputeEmulationPlanCaptured
    ;
    bool occupancyComputeEmulationOutputStatesGraphOwned = false;
    if(occupancyRegularComputeEmulationPlanCaptured){
        occupancyComputeEmulationOutputStatesGraphOwned = GatherAvboitAliasFreeComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            avboitOccupancyComputeEmulationPayload.plan,
            occupancyComputeEmulationResourceScratch,
            Name("render.avboit.occupancy.compute_emulation.outputs"),
            "AVBOIT Occupancy Compute Emulation Outputs",
            occupancyComputeEmulationOutputSet
        );
    }
    else if(occupancyCsgComputeEmulationPlanCaptured){
        occupancyComputeEmulationOutputStatesGraphOwned =
            GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
                m_deferredLightingTaskGraph,
                avboitOccupancyComputeEmulationPayload.csgPlan,
                occupancyComputeEmulationResourceScratch,
                Name("render.avboit.occupancy.csg_compute_emulation.outputs"),
                "AVBOIT Occupancy CSG Compute Emulation Outputs",
                occupancyComputeEmulationOutputSet
            )
        ;
    }
    if(
        occupancyComputeEmulationPlanCaptured
        && !occupancyComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Occupancy compute-emulation output states"
        ));
    }
    avboitOccupancyPayload.occupancyComputeEmulationOutputStatesGraphOwned =
        occupancyRegularComputeEmulationPlanCaptured
        && occupancyComputeEmulationOutputStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyCsgComputeEmulationOutputStatesGraphOwned =
        occupancyCsgComputeEmulationPlanCaptured
        && occupancyComputeEmulationOutputStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyComputeEmulationTiming =
        occupancyComputeEmulationOutputStatesGraphOwned
            ? &avboitOccupancyComputeEmulationTiming
            : nullptr
    ;
    Core::GpuGraphResourceId occupancySharedComputeEmulationOutput;
    const bool occupancySharedComputeEmulationOutputStatesGraphOwned =
        occupancySharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            occupancySharedComputeEmulationPlan,
            "AVBOIT Occupancy Shared Compute Emulation Output",
            occupancySharedComputeEmulationOutput
        )
    ;
    if(
        occupancySharedComputeEmulationPlanCaptured
        && !occupancySharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Occupancy shared compute-emulation output state"
        ));
    }

    Core::Alloc::ScratchArena avboitPreResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitPreResourceUses{ avboitPreResourceScratch };
    avboitPreResourceUses.reserve(
        13u
        + (avboitOccupancyPayload.occupancyStreamsUploaded ? 7u : 0u)
        + (occupancyCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    avboitPreResourceUses.push_back(ReadUse(albedo));
    avboitPreResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
    avboitPreResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
    avboitPreResourceUses.push_back(ReadUse(depth));
    avboitPreResourceUses.push_back(ReadWriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget));
    avboitPreResourceUses.push_back(ReadWriteUse(avboitCoverage, Core::ResourceStates::UnorderedAccess));
    if(avboitOccupancyPayload.occupancyStreamsUploaded){
        avboitPreResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        avboitPreResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        avboitPreResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        if(occupancyCsgStreamsUploaded){
            avboitPreResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            avboitPreResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            avboitPreResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // The preceding full-resolution interval producer owns this state. Occupancy only samples it.
            avboitPreResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        }
    }
    if(occupancyCsgIntervalSampleImageStatesGraphOwned){
        // The prepared interval producer wrote these aliases in the preceding AVBOIT task. The occupancy material
        // shaders load them through StorageImage descriptors, so the graph lowers the required UAV handoff here.
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    const Core::GpuTaskResourceSetUse occupancyMaterialGeometrySetUse{
        .resourceSet = occupancyMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse occupancyMaterialSampledTextureSetUse{
        .resourceSet = occupancyMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse occupancyComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = occupancyComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse occupancyMaterialResourceSetUses[3u] = {};
    usize occupancyMaterialResourceSetUseCount = 0u;
    if(avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned)
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] = occupancyMaterialGeometrySetUse;
    if(occupancyMaterialSampledTextureSet.valid())
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] = occupancyMaterialSampledTextureSetUse;
    if(occupancyComputeEmulationOutputStatesGraphOwned){
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] =
            occupancyComputeEmulationOutputVertexBufferSetUse;
    }
    avboitPreResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    avboitPreResourceUses.push_back(ReadUse(avboitMaterialDomain));
    avboitPreResourceUses.push_back(ReadUse(avboitCsgDomain, Core::ResourceStates::ShaderResource));

    const Core::GpuTaskResourceSetUse occupancyComputeEmulationOutputUavSetUse{
        .resourceSet = occupancyComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    // Keep the final immutable upload as the stream anchor. The optional generator only becomes Occupancy's
    // immediate dependency; replacing this anchor would hide a broken upload/clear-to-producer handoff.
    const Core::GpuTaskId occupancyStreamTask = occupancyUploadTask;
    if(avboitOccupancyPayload.occupancyStreamsUploaded)
        m_avboitSystem.taskGraphStage().m_occupancyStreamTask = occupancyStreamTask;
    Core::GpuTaskId occupancyDependency = avboitClearTask;

    Core::GpuTaskSchedulingHint avboitOccupancyScheduling;
    avboitOccupancyScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitOccupancyScheduling.forceSubmissionBoundary = false;
    avboitOccupancyScheduling.allowPacketMerge = true;
    avboitOccupancyScheduling.mergeWithPrevious = true;
    // Occupancy directly closes the serial AVBOIT Pre packet after its uploads and clears; preserve that timing
    // and acceptance contract when later split stages form a cross-queue consumer frontier.
    avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;
    if(occupancyComputeEmulationOutputStatesGraphOwned){
        avboitOccupancyComputeEmulationPayload.graphics = &m_graphics;
        avboitOccupancyComputeEmulationPayload.meshSystem = &m_meshSystem;
        avboitOccupancyComputeEmulationPayload.materialSystem = &m_materialSystem;
        avboitOccupancyComputeEmulationPayload.csgSystem = &m_csgSystem;
        avboitOccupancyComputeEmulationPayload.targets = &deferredTargets;
        avboitOccupancyComputeEmulationPayload.timingTicket = &avboitPreTimingTicket;
        avboitOccupancyComputeEmulationPayload.occupancyTiming = &avboitOccupancyComputeEmulationTiming;
        avboitOccupancyComputeEmulationPayload.instanceCount = avboitOccupancyPayload.occupancySnapshot.instanceCount;
        avboitOccupancyComputeEmulationPayload.materialTypedByteCount =
            avboitOccupancyPayload.occupancySnapshot.materialTypedByteCount;
        avboitOccupancyComputeEmulationPayload.materialDrawBuffersUploaded =
            avboitOccupancyPayload.occupancyStreamsUploaded;
        avboitOccupancyComputeEmulationPayload.csgFrameBuffersUploaded = occupancyCsgStreamsUploaded;
        avboitOccupancyComputeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            occupancyCsgIntervalSampleImageStatesGraphOwned;
        avboitOccupancyComputeEmulationPayload.csgClipBufferStatesGraphOwned =
            occupancyCsgClipBufferStatesGraphOwned;
        avboitOccupancyComputeEmulationPayload.materialFrameStatesGraphOwned =
            avboitOccupancyPayload.occupancyMaterialFrameStatesGraphOwned;
        avboitOccupancyComputeEmulationPayload.materialGeometryStatesGraphOwned =
            avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancyComputeEmulationResourceUses{
            occupancyComputeEmulationResourceScratch
        };
        occupancyComputeEmulationResourceUses.reserve(
            4u + (occupancyCsgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        occupancyComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        occupancyComputeEmulationResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        occupancyComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        occupancyComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(occupancyCsgComputeEmulationPlanCaptured){
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse occupancyComputeEmulationResourceSetUses[3u] = {};
        usize occupancyComputeEmulationResourceSetUseCount = 0u;
        occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
            occupancyMaterialGeometrySetUse;
        if(occupancyMaterialSampledTextureSet.valid()){
            occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
                occupancyMaterialSampledTextureSetUse;
        }
        occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
            occupancyComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint occupancyComputeEmulationScheduling;
        occupancyComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        occupancyComputeEmulationScheduling.forceSubmissionBoundary = false;
        occupancyComputeEmulationScheduling.allowPacketMerge = true;
        occupancyComputeEmulationScheduling.mergeWithPrevious = true;
        // Depth Warp is a later Compute consumer. Retain the immediate producer/raster pair in AVBOIT Pre's
        // Graphics packet so one timing ticket and graph-owned UAV-to-VertexBuffer handoff remain authoritative.
        occupancyComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc occupancyComputeEmulationDesc;
        occupancyComputeEmulationDesc
            .setIdentity(occupancyCsgComputeEmulationPlanCaptured
                ? Name("render.avboit.occupancy.csg_compute_emulation")
                : Name("render.avboit.occupancy.compute_emulation"))
            .setMarkerLabel(occupancyCsgComputeEmulationPlanCaptured
                ? "AVBOIT Occupancy CSG Compute Emulation"
                : "AVBOIT Occupancy Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(occupancyComputeEmulationScheduling)
            .setDependencies(&occupancyDependency, 1u)
            .setResourceUses(
                occupancyComputeEmulationResourceUses.data(),
                occupancyComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                occupancyComputeEmulationResourceSetUses,
                occupancyComputeEmulationResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            AvboitOccupancyComputeEmulationGraphTask
        >(
            occupancyComputeEmulationDesc,
            Move(avboitOccupancyComputeEmulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Occupancy compute-emulation producer"
            ));
            return;
        }
        occupancyDependency = m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask;
        avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(occupancySharedComputeEmulationOutputStatesGraphOwned){
        // The one retained output appears in every phase, so keep it as an exact resource rather than placing it
        // in a resource set whose duplicate expansion would erase the alternating UAV/VertexBuffer uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancySharedGenerateResourceUses{
            avboitPreResourceScratch
        };
        occupancySharedGenerateResourceUses.reserve(5u);
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            materialInstances,
            Core::ResourceStates::ShaderResource
        ));
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            materialTyped,
            Core::ResourceStates::ShaderResource
        ));
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        occupancySharedGenerateResourceUses.push_back(WriteUse(
            occupancySharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancySharedRasterResourceUses{
            avboitPreResourceScratch
        };
        occupancySharedRasterResourceUses.assign(
            avboitPreResourceUses.begin(),
            avboitPreResourceUses.end()
        );
        occupancySharedRasterResourceUses.push_back(ReadUse(
            occupancySharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint occupancySharedComputeEmulationScheduling;
        occupancySharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        occupancySharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        occupancySharedComputeEmulationScheduling.allowPacketMerge = true;
        occupancySharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Every phase is an explicit immediate successor. Keep the full alternating chain in AVBOIT Pre despite
        // Depth Warp's later Compute consumer so one command list owns the timing scope and its output handoff.
        occupancySharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addOccupancySharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &occupancySharedComputeEmulationPlan,
            &avboitOccupancyComputeEmulationTiming,
            occupancySharedComputeEmulationInstanceCount,
            occupancySharedComputeEmulationMaterialTypedByteCount,
            occupancyStreamsUploaded = avboitOccupancyPayload.occupancyStreamsUploaded,
            occupancyMaterialFrameStatesGraphOwned = avboitOccupancyPayload.occupancyMaterialFrameStatesGraphOwned,
            occupancyMaterialGeometryStatesGraphOwned = avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned,
            &avboitPreTimingTicket,
            &occupancySharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitOccupancySharedComputeEmulationGraphTask::Phase phase,
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
                .setScheduling(occupancySharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitOccupancySharedComputeEmulationGraphTask::Payload payload;
            payload.graphics = &m_graphics;
            payload.meshSystem = &m_meshSystem;
            payload.materialSystem = &m_materialSystem;
            payload.targets = &deferredTargets;
            payload.timingTicket = &avboitPreTimingTicket;
            payload.occupancyTiming = &avboitOccupancyComputeEmulationTiming;
            payload.plan = occupancySharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = occupancySharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = occupancySharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = occupancyStreamsUploaded;
            payload.materialFrameStatesGraphOwned = occupancyMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = occupancyMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<AvboitOccupancySharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using OccupancySharedPhase = AvboitOccupancySharedComputeEmulationGraphTask::Phase;
        const Name occupancySharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.occupancy.shared_compute_emulation_generate_a"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_a"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_b"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_b"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_c"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_c"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_d"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_d"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_e"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_e"),
        };
        const AStringView occupancySharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Occupancy Shared Compute Emulation Generate A",
            "AVBOIT Occupancy Shared Compute Emulation Raster A",
            "AVBOIT Occupancy Shared Compute Emulation Generate B",
            "AVBOIT Occupancy Shared Compute Emulation Raster B",
            "AVBOIT Occupancy Shared Compute Emulation Generate C",
            "AVBOIT Occupancy Shared Compute Emulation Raster C",
            "AVBOIT Occupancy Shared Compute Emulation Generate D",
            "AVBOIT Occupancy Shared Compute Emulation Raster D",
            "AVBOIT Occupancy Shared Compute Emulation Generate E",
            "AVBOIT Occupancy Shared Compute Emulation Raster E",
        };
        const usize occupancySharedComputeEmulationPhaseCount =
            ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(
                occupancySharedComputeEmulationPlan.drawCount
            )
        ;
        NWB_ASSERT(ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            occupancySharedComputeEmulationPlan.drawCount
        ));
        NWB_ASSERT(
            occupancySharedComputeEmulationPhaseCount
            <= LengthOf(occupancySharedComputeEmulationPhaseIdentities)
        );
        Core::GpuTaskId occupancySharedComputeEmulationDependency = occupancyDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < occupancySharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase =
                phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
            m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTasks[phaseIndex] =
                addOccupancySharedComputeEmulationPhase(
                    occupancySharedComputeEmulationPhaseIdentities[phaseIndex],
                    occupancySharedComputeEmulationPhaseMarkers[phaseIndex],
                    occupancySharedComputeEmulationDependency,
                    isRasterPhase ? OccupancySharedPhase::Raster : OccupancySharedPhase::Generate,
                    phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
                    phaseIndex == 0u,
                    phaseIndex + 1u == occupancySharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? occupancySharedRasterResourceUses
                        : occupancySharedGenerateResourceUses,
                    occupancyMaterialResourceSetUses,
                    occupancyMaterialResourceSetUseCount
                )
            ;
            if(!m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Occupancy shared compute-emulation phase"
                ));
                return;
            }
            occupancySharedComputeEmulationDependency =
                m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTasks[phaseIndex];
        }
        m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTaskCount =
            occupancySharedComputeEmulationPhaseCount;
        // The terminal raster is the existing Occupancy semantic endpoint: Depth Warp, timing, state cache, and
        // accepted-token publication remain tied to this packet-local task.
        m_avboitSystem.taskGraphStage().m_occupancyTask = occupancySharedComputeEmulationDependency;
    }
    else{
        Core::GpuTaskDesc avboitOccupancyDesc;
        avboitOccupancyDesc
            .setIdentity(Name("render.avboit.pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitOccupancyScheduling)
            .setDependencies(&occupancyDependency, 1u)
            .setResourceUses(avboitPreResourceUses.data(), avboitPreResourceUses.size())
            .setResourceSetUses(
                occupancyMaterialResourceSetUseCount != 0u ? occupancyMaterialResourceSetUses : nullptr,
                occupancyMaterialResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_occupancyTask = m_deferredLightingTaskGraph.addTask<AvboitOccupancyGraphTask>(
            avboitOccupancyDesc,
            Move(avboitOccupancyPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_occupancyTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
            return;
        }
    }

    Core::GpuTaskSchedulingHint avboitComputeScheduling;
    avboitComputeScheduling.cost = Core::GpuTaskCostHint::Medium;
    avboitComputeScheduling.forceSubmissionBoundary = false;
    avboitComputeScheduling.allowPacketMerge = true;
    avboitComputeScheduling.mergeWithPrevious = true;
    avboitComputeScheduling.allowMergeAcrossConsumerFrontier = true;
    // Depth Warp and Integration independently prefer Compute, while the compiler may collapse either task onto
    // its direct Graphics predecessor. Preserve direct affinity after a Graphics collapse and keep auxiliary
    // Compute transports available when the compiler retains the preferred queue class.
    EnableSameFamilyComputeEffectRouting(avboitComputeScheduling);
    EnableCrossFamilyComputeEffectRouting(avboitComputeScheduling);
    // Accepted samples use the queue class and exact transport chosen by this compile.
    avboitComputeScheduling.allowTimingFeedbackRouting = true;
    avboitComputeScheduling.allowCrossClassTimingFeedbackRouting = true;
    const Core::GpuTaskTimingMetadata avboitComputeStageTiming =
        AvboitComputeStageTimingMetadata(deferredTargets.avboit)
    ;
    Core::GpuTaskId avboitDepthWarpCompletionTask = m_avboitSystem.taskGraphStage().m_occupancyTask;
    if(hasTransparentRenderers){
        const Core::GpuTaskResourceUse depthWarpResourceUses[] = {
            ReadUse(avboitCoverage, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(avboitDepthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(avboitControl, Core::ResourceStates::UnorderedAccess),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Core::GpuTaskId preDependency[] = { m_avboitSystem.taskGraphStage().m_occupancyTask };
        Core::GpuTaskDesc depthWarpDesc;
        depthWarpDesc
            .setIdentity(Name("render.avboit.depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(ComputeQueueRequest())
            .setScheduling(avboitComputeScheduling)
            .setTimingMetadata(avboitComputeStageTiming)
            .setDependencies(preDependency, LengthOf(preDependency))
            .setResourceUses(depthWarpResourceUses, LengthOf(depthWarpResourceUses))
        ;
        m_avboitSystem.taskGraphStage().m_depthWarpTask = m_deferredLightingTaskGraph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &avboitDepthWarpTimingTicket,
                .timingFeedback = &m_deferredTaskTimingFeedback,
                .timingScope = &RendererGpuTimingScope::s_AvboitDepthWarp,
            }
        );
        if(!m_avboitSystem.taskGraphStage().m_depthWarpTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT depth-warp graph task"));
            return;
        }
        avboitDepthWarpCompletionTask = m_avboitSystem.taskGraphStage().m_depthWarpTask;
    }

    if(hasTransparentRenderers){
    // Extinction is a distinct shared-buffer write point. Snapshot and publish it only after Depth Warp so neither
    // phase can overwrite the other phase's instance/typed/CSG stream.
    AvboitExtinctionGraphTask::Payload avboitExtinctionPayload{ m_arena };
    AvboitExtinctionComputeEmulationGraphTask::Payload avboitExtinctionComputeEmulationPayload{ m_arena };
    avboitExtinctionPayload.avboitSystem = &m_avboitSystem;
    avboitExtinctionPayload.targets = &deferredTargets;
    avboitExtinctionPayload.timingTicket = &avboitExtinctionTimingTicket;
    avboitExtinctionPayload.hasTransparentRenderers = hasTransparentRenderers;

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
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.avboit.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitExtinction,
            true,
            csgFrameState,
            extinctionDrawItems,
            extinctionInstanceData,
            extinctionCsgFrameData,
#if defined(NWB_DEBUG)
            extinctionMaterialTypedRanges,
#endif
            extinctionMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly,
            &meshViewState
        );

        const bool extinctionHasCsgDrawItems = !extinctionDrawItems.csg.empty();
        if(!extinctionDrawItems.empty()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    extinctionInstanceData,
                    extinctionMaterialTypedBytes
                )
                || !m_materialSystem.materialPassDrawResourcesReady(extinctionDrawItems.regular)
                || (extinctionHasCsgDrawItems && (
                    !extinctionCsgFrameData.hasWork()
                    || !csgReceiverRanges.valid()
                    || !csgCutters.valid()
                    || !csgClipContextSlots.valid()
                    || !csgIntervalSampleState.valid()
                    || !m_csgSystem.csgFrameBuffersReady(extinctionCsgFrameData)
                    || !m_materialSystem.materialPassDrawResourcesReady(extinctionDrawItems.csg)
                ))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT extinction resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const extinctionMaterialGeometryDrawSets[] = {
                &extinctionDrawItems.regular,
                &extinctionDrawItems.csg,
            };
            avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
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

            m_materialSystem.prepareMaterialPassInstanceUploadData(extinctionInstanceData);
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
            // A phase may graph-own one alias-free stream or a narrowly retained regular shared stream.
            // Mixed regular/CSG work and every other shared-output shape retain local interleaving because a single
            // producer/raster handoff cannot preserve their per-draw overwrite order.
            extinctionRegularComputeEmulationPlanCaptured = extinctionDrawItems.csg.computeDrawItems.empty()
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && avboitExtinctionComputeEmulationPayload.plan.capture(
                    m_meshSystem,
                    extinctionDrawItems.regular
                )
            ;
            extinctionCsgComputeEmulationPlanCaptured = extinctionDrawItems.regular.computeDrawItems.empty()
                && extinctionCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && avboitExtinctionComputeEmulationPayload.csgPlan.capture(
                    m_meshSystem,
                    extinctionDrawItems.csg,
                    extinctionCsgFrameData
                )
            ;
            extinctionSharedComputeEmulationPlanCaptured = !extinctionRegularComputeEmulationPlanCaptured
                && extinctionDrawItems.regular.meshDrawItems.empty()
                && extinctionDrawItems.csg.empty()
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && extinctionSharedComputeEmulationPlan.capture(
                    m_meshSystem,
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
            // Preserve graph ownership even when a transparent frame has no ready extinction draws: native recording
            // consumes this explicit empty phase rather than regathering mutable renderer state.
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
        extinctionComputeEmulationOutputStatesGraphOwned = GatherAvboitAliasFreeComputeEmulationResourceSet(
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
            GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
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
    Core::Alloc::ScratchArena extinctionResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionResourceUses{ extinctionResourceScratch };
    extinctionResourceUses.reserve(
        16u
        + (extinctionStreamsUploaded ? 7u : 0u)
        + (extinctionCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    // Queue placement cannot change native descriptor access. Keep the complete raster resource contract on every
    // route so compiler-selected crossings retain the same hazards and state lowering.
    extinctionResourceUses.push_back(ReadUse(albedo));
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
        extinctionResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        extinctionResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        if(extinctionCsgStreamsUploaded){
            extinctionResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            extinctionResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            extinctionResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // The full-resolution interval producer owns this sample state throughout all low-raster AVBOIT phases.
            extinctionResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(extinctionCsgIntervalSampleImageStatesGraphOwned){
                // The prepared transparent interval producer wrote these aliases. Extinction loads them through
                // StorageImage descriptors, so the graph lowers its same-UAV handoff before this thunk records.
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


// Keep the final immutable upload as the semantic stream anchor. The optional producer becomes only the
    // immediate Extinction dependency; replacing this anchor would hide a broken upload-to-producer handoff.
    const Core::GpuTaskId extinctionStreamTask = extinctionUploadTask;
    if(extinctionStreamsUploaded)
        m_avboitSystem.taskGraphStage().m_extinctionStreamTask = extinctionStreamTask;
    Core::GpuTaskId extinctionDependency = extinctionUploadTask;
    if(extinctionComputeEmulationOutputStatesGraphOwned){
        avboitExtinctionComputeEmulationPayload.graphics = &m_graphics;
        avboitExtinctionComputeEmulationPayload.meshSystem = &m_meshSystem;
        avboitExtinctionComputeEmulationPayload.materialSystem = &m_materialSystem;
        avboitExtinctionComputeEmulationPayload.csgSystem = &m_csgSystem;
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
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(extinctionCsgComputeEmulationPlanCaptured){
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
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
        // The next raster consumes this producer's graph-owned UAV output and shares its Extinction timing ticket.
        // Keep the immediate pair intact even if FrontierSafe sees Integration on a later Compute packet.
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
        // Keep the retained output concrete rather than placing it in a duplicate-expanding resource set.  The
        // alternating phases need their distinct UAV/VertexBuffer uses preserved by the compiler.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionSharedGenerateResourceUses{
            extinctionResourceScratch
        };
        extinctionSharedGenerateResourceUses.reserve(5u);
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            materialInstances,
            Core::ResourceStates::ShaderResource
        ));
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            materialTyped,
            Core::ResourceStates::ShaderResource
        ));
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
        // Integration and later Accumulation consume the terminal raster.  Every immediate D/R successor carries
        // its explicit dependency, so retaining this one Graphics packet remains FrontierSafe.
        extinctionSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addExtinctionSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &extinctionSharedComputeEmulationPlan,
            &avboitExtinctionComputeEmulationTiming,
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
            payload.graphics = &m_graphics;
            payload.meshSystem = &m_meshSystem;
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
        // The terminal raster is the Extinction semantic endpoint.  The common typed Integration task immediately
        // follows it, so packet ranges, timing, and accepted-token ownership remain graph-derived.
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
    // Integration is one semantic Compute-preferred successor. The compiler owns both its queue and the
    // Extinction/UAV-to-Integration/SRV state lowering.
    const Core::GpuTaskResourceUse integrationResourceUses[] = {
        ReadUse(avboitExtinction),
        ReadUse(avboitControl),
        ReadUse(avboitExtinctionOverflow),
        ReadWriteUse(avboitTransmittance, Core::ResourceStates::UnorderedAccess),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    const Core::GpuTaskId integrationDependency[] = { m_avboitSystem.taskGraphStage().m_extinctionTask };
    Core::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("render.avboit.integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(ComputeQueueRequest())
        .setScheduling(avboitComputeScheduling)
        .setTimingMetadata(avboitComputeStageTiming)
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
    ;
    m_avboitSystem.taskGraphStage().m_integrationTask = m_deferredLightingTaskGraph.addTask<AvboitIntegrationGraphTask>(
        integrationDesc,
        AvboitIntegrationGraphTask::Payload{
            .avboitSystem = &m_avboitSystem,
            .targets = &deferredTargets.avboit,
            .timingTicket = &avboitIntegrationTimingTicket,
            .timingFeedback = &m_deferredTaskTimingFeedback,
            .timingScope = &RendererGpuTimingScope::s_AvboitIntegration,
        }
    );
    if(!m_avboitSystem.taskGraphStage().m_integrationTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return;
    }


// Accumulation is another independent write point for the shared material/CSG buffers. Freeze and publish its
    // bytes after integration, rather than letting native recording re-gather mutable scene state after extinction.
    AvboitAccumulationGraphTask::Payload avboitAccumulationPayload{ m_arena };
    AvboitAccumulationComputeEmulationGraphTask::Payload avboitAccumulationComputeEmulationPayload{ m_arena };
    avboitAccumulationPayload.avboitSystem = &m_avboitSystem;
    avboitAccumulationPayload.targets = &deferredTargets;
    avboitAccumulationPayload.timingTicket = &avboitAccumulationTimingTicket;
    avboitAccumulationPayload.hasTransparentRenderers = hasTransparentRenderers;

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
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.avboit.accumulationFramebuffer.get(),
            MaterialPipelinePass::AvboitAccumulate,
            true,
            csgFrameState,
            accumulationDrawItems,
            accumulationInstanceData,
            accumulationCsgFrameData,
#if defined(NWB_DEBUG)
            accumulationMaterialTypedRanges,
#endif
            accumulationMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly,
            &meshViewState
        );

        const bool accumulationHasCsgDrawItems = !accumulationDrawItems.csg.empty();
        if(!accumulationDrawItems.empty()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    accumulationInstanceData,
                    accumulationMaterialTypedBytes
                )
                || !m_materialSystem.materialPassDrawResourcesReady(accumulationDrawItems.regular)
                || (accumulationHasCsgDrawItems && (
                    !accumulationCsgFrameData.hasWork()
                    || !csgReceiverRanges.valid()
                    || !csgCutters.valid()
                    || !csgClipContextSlots.valid()
                    || !csgIntervalSampleState.valid()
                    || !m_csgSystem.csgFrameBuffersReady(accumulationCsgFrameData)
                    || !m_materialSystem.materialPassDrawResourcesReady(accumulationDrawItems.csg)
                ))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT accumulation resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const accumulationMaterialGeometryDrawSets[] = {
                &accumulationDrawItems.regular,
                &accumulationDrawItems.csg,
            };
            avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
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

            m_materialSystem.prepareMaterialPassInstanceUploadData(accumulationInstanceData);
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
            // A phase may graph-own exactly one alias-free compute stream. Mixed regular/CSG work retains the
            // established local interleaving because one producer/raster handoff cannot preserve its draw order.
            accumulationRegularComputeEmulationPlanCaptured = accumulationDrawItems.csg.computeDrawItems.empty()
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && avboitAccumulationComputeEmulationPayload.plan.capture(
                    m_meshSystem,
                    accumulationDrawItems.regular
                )
            ;
            accumulationCsgComputeEmulationPlanCaptured = accumulationDrawItems.regular.computeDrawItems.empty()
                && accumulationCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && avboitAccumulationComputeEmulationPayload.csgPlan.capture(
                    m_meshSystem,
                    accumulationDrawItems.csg,
                    accumulationCsgFrameData
                )
            ;
            // The all-compute two-through-five-draw case can preserve one shared generated output only
            // as an explicit D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) -> R(D) -> D(E) -> R(E)]
            // sequence. Keep mesh and CSG work out of this narrow slice so the aggregate accumulation callback
            // is never partially replayed around its phases.
            accumulationSharedComputeEmulationPlanCaptured = !accumulationRegularComputeEmulationPlanCaptured
                && accumulationDrawItems.regular.meshDrawItems.empty()
                && accumulationDrawItems.csg.empty()
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && accumulationSharedComputeEmulationPlan.capture(
                    m_meshSystem,
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
            // An empty captured phase is still authoritative: recording must not re-gather mutable renderer state.
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
        accumulationComputeEmulationOutputStatesGraphOwned = GatherAvboitAliasFreeComputeEmulationResourceSet(
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
            GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
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


// accumulationFramebuffer binds deferred depth read-only, which Vulkan tracks as DepthRead rather than SRV.
    accumulationResourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    accumulationResourceUses.push_back(ReadUse(avboitTransmittance));
    accumulationResourceUses.push_back(ReadUse(avboitDepthWarp));
    accumulationResourceUses.push_back(ReadUse(avboitControl));
    accumulationResourceUses.push_back(ReadWriteUse(avboitAccumColor, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(avboitAccumExtinction, Core::ResourceStates::RenderTarget));
    if(accumulationStreamsUploaded){
        accumulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        accumulationResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        accumulationResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        if(accumulationCsgStreamsUploaded){
            accumulationResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            accumulationResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            accumulationResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // This remains the full-resolution interval producer's state; accumulation only samples it.
            accumulationResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(accumulationCsgIntervalSampleImageStatesGraphOwned){
                // The prepared transparent interval producer wrote these aliases. Accumulation loads them through
                // StorageImage descriptors, so the graph lowers its same-UAV handoff before this thunk records.
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

    // Keep the final immutable upload as the semantic stream anchor. The optional producer becomes only the
    // immediate Accumulation dependency; replacing this anchor would hide a broken upload-to-producer handoff.
    const Core::GpuTaskId accumulationStreamTask = accumulationUploadTask;
    if(accumulationStreamsUploaded)
        m_avboitSystem.taskGraphStage().m_accumulationStreamTask = accumulationStreamTask;
    Core::GpuTaskId accumulationDependency = accumulationUploadTask;
    if(accumulationComputeEmulationOutputStatesGraphOwned){
        avboitAccumulationComputeEmulationPayload.graphics = &m_graphics;
        avboitAccumulationComputeEmulationPayload.meshSystem = &m_meshSystem;
        avboitAccumulationComputeEmulationPayload.materialSystem = &m_materialSystem;
        avboitAccumulationComputeEmulationPayload.csgSystem = &m_csgSystem;
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
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(accumulationCsgComputeEmulationPlanCaptured){
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
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
        // The next raster consumes this producer's graph-owned UAV output and shares its Accumulation timing
        // ticket. Keep the immediate pair intact even if Composite observes the finalizer from a later Compute
        // packet.
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
        // The one retained output appears in every phase, so keep it as an exact resource rather than placing it
        // in a resource set whose duplicate expansion would erase the alternating UAV/VertexBuffer uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationSharedGenerateResourceUses{
            accumulationResourceScratch
        };
        accumulationSharedGenerateResourceUses.reserve(5u);
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            materialInstances,
            Core::ResourceStates::ShaderResource
        ));
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            materialTyped,
            Core::ResourceStates::ShaderResource
        ));
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
        // Every phase is an explicit immediate successor. Keep the full alternating chain in AVBOIT Pre despite
        // Composite's later Compute consumer so one command list owns the timing scope and finalizer handoff.
        accumulationSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addAccumulationSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &accumulationSharedComputeEmulationPlan,
            &avboitAccumulationComputeEmulationTiming,
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
            payload.graphics = &m_graphics;
            payload.meshSystem = &m_meshSystem;
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
        // The terminal raster is the existing Accumulation semantic endpoint: it feeds the unchanged finalizer,
        // timing ticket, state cache, record range, and accepted-token publication path.
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
        ReadUse(avboitAccumExtinction, Core::ResourceStates::ShaderResource),
        ReadUse(depth, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint accumulationFinalizeScheduling;
    accumulationFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    accumulationFinalizeScheduling.forceSubmissionBoundary = false;
    accumulationFinalizeScheduling.allowPacketMerge = true;
    accumulationFinalizeScheduling.mergeWithPrevious = true;
    // The finalizer is Accumulation's direct semantic tail and must retain its timing/acceptance packet before
    // Lighting observes the restored shader-readable depth state from another queue.
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

    const Core::GpuExternalCompletionId laggedLightingExternalDependencies[] = {
        m_deferredLightingHistoryReadReadyCompletion,
    };
    const Core::GpuExternalCompletionId* const lightingExternalDependencies = useLaggedLightingHistory
        ? laggedLightingExternalDependencies
        : nullptr
    ;
    const usize lightingExternalDependencyCount = useLaggedLightingHistory
        ? LengthOf(laggedLightingExternalDependencies)
        : 0u
    ;


// Live Lighting joins Shadow/Software, Surfel GI, AVBOIT, and Hardware Caustics through internal graph edges.
    // Active lagged Lighting instead reads history and stays independent from the current-frame producers.
    const Core::GpuTaskId hardwareLightingDependencies[] = {
        m_deferredShadowVisibilityTask,
        m_deferredSurfelGiTask,
        avboitFinalTask,
        m_deferredHardwareCausticsTask,
    };
    const Core::GpuTaskId softwareLightingDependencies[] = {
        m_deferredShadowVisibilityTask,
        m_deferredSoftwareCausticsTask,
        m_deferredSurfelGiTask,
        avboitFinalTask,
    };
    // Lagged Lighting normally reads its shared G-buffer inputs from the accepted prefix while history supplies the
    // temporal effects. Transparent AVBOIT accumulation temporarily binds current deferred depth as DepthRead, so
    // its finalizer must complete before Lighting can observe ShaderResource layout again.
    const Core::GpuTaskId laggedLightingDependencies[] = {
        m_graphicsPrefixTask,
        avboitFinalTask,
    };
    const usize laggedLightingDependencyCount = hasTransparentRenderers ? 2u : 1u;
    const Core::GpuTaskId laggedLightingSelectorUploadDependencies[] = { m_graphicsPrefixTask };
    const bool laggedBindlessSlotsGraphOwned = useLaggedLightingHistory && !history->slotsUploaded;
    if(laggedBindlessSlotsGraphOwned){
        const Core::GpuUploadBlobId laggedBindlessSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
            &history->slots,
            sizeof(history->slots),
            alignof(DeferredBindlessResourceSlots)
        );
        if(!laggedBindlessSlotsBlob.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain lagged lighting-history selector upload data"));
            return;
        }

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(Name("render.lagged_lighting.bindless_slots_upload"))
            .setMarkerLabel("Lagged Lighting Bindless Slots Upload")
            .setQueue(ComputeUploadQueueRequest())
            .setScheduling(uploadScheduling)
            .setDependencies(
                laggedLightingSelectorUploadDependencies,
                LengthOf(laggedLightingSelectorUploadDependencies)
            )
        ;
        m_deferredLaggedLightingHistorySlotsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = laggedBindlessSlotsBlob,
                .destination = bindlessSlots,
                // Automatic-state selector buffers publish Common; Deferred Lighting owns the following
                // ConstantBuffer transition in this same externally gated packet.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_deferredLaggedLightingHistorySlotsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare lagged lighting-history selector upload"));
            return;
        }
    }
    const Core::GpuTaskId laggedLightingWithSelectorDependencies[] = {
        m_graphicsPrefixTask,
        m_deferredLaggedLightingHistorySlotsUploadTask,
        avboitFinalTask,
    };
    const Core::GpuTaskId* const lightingDependencies = declaresHardwareCaustics
        ? (useLaggedLightingHistory ? laggedLightingDependencies : hardwareLightingDependencies)
        : (useLaggedLightingHistory ? laggedLightingDependencies : softwareLightingDependencies)
    ;
    const Core::GpuTaskId* const resolvedLightingDependencies = laggedBindlessSlotsGraphOwned
        ? laggedLightingWithSelectorDependencies
        : lightingDependencies
    ;
    const usize lightingDependencyCount = laggedBindlessSlotsGraphOwned
        ? (hasTransparentRenderers ? 3u : 2u)
        : (useLaggedLightingHistory
            ? laggedLightingDependencyCount
            : LengthOf(hardwareLightingDependencies))
    ;
    // Active lagged Lighting receives compiler-owned state seeds for its shared prefix inputs while it reads
    // history. Transparent depth is the explicit exception: the finalizer dependency above orders its temporary
    // AVBOIT DepthRead layout before Lighting samples ShaderResource state.
    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadTextureUse(
            albedo,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(
            normal,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources),
        ReadTextureUse(causticIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadUse(
            sceneShading,
            Core::ResourceStates::ConstantBuffer
        ),
        ReadUse(lights, Core::ResourceStates::ShaderResource),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(opaqueColor, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = !laggedBindlessSlotsGraphOwned;
    scheduling.allowPacketMerge = laggedBindlessSlotsGraphOwned;
    scheduling.mergeWithPrevious = laggedBindlessSlotsGraphOwned;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(resolvedLightingDependencies, lightingDependencyCount)
        .setExternalDependencies(lightingExternalDependencies, lightingExternalDependencyCount)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredLightingTask = m_deferredSystem.declareDeferredLightingTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        useLaggedLightingHistory,
        lightingTimingTicket
    );
    if(!m_deferredLightingTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }

    // Composite remains a distinct packet and joins both graph-owned AVBOIT and Lighting. It retains the current
    // bindless selector in lagged mode rather than inheriting Lighting's history selector.
    const Core::GpuGraphResourceId compositeColor = importFirstWriteTexture(
        deferredTargets.compositeColor,
        Name("render.deferred_composite.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId compositeBindlessSlots = currentBindlessSlots;
    if(
        !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !compositeColor.valid()
        || !compositeBindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse compositeResourceUses[] = {
        ReadUse(opaqueColor),
        ReadUse(avboitAccumColor),
        ReadUse(avboitAccumExtinction),
        ReadUse(
            compositeBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = Core::GpuTaskCostHint::Medium;
    compositeScheduling.avoidQueueCrossing = useLaggedLightingHistory;
    compositeScheduling.forceSubmissionBoundary = true;
    compositeScheduling.allowPacketMerge = false;
    const Core::GpuTaskId compositeDependencies[] = {
        m_deferredLightingTask,
        avboitFinalTask,
    };
    Core::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("render.deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(ComputeQueueRequest())
        .setScheduling(compositeScheduling)
        .setDependencies(compositeDependencies, LengthOf(compositeDependencies))
        .setResourceUses(compositeResourceUses, LengthOf(compositeResourceUses))
    ;
    m_deferredCompositeTask = m_deferredSystem.declareDeferredCompositeTask(
        m_deferredLightingTaskGraph,
        compositeDesc,
        deferredTargets,
        compositeTimingTicket
    );
    if(!m_deferredCompositeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return;
    }

    Core::GpuGraphResourceDesc backBufferDesc = TextureResourceDesc(
        Name("render.deferred_present.backbuffer"),
        "Presentation Back Buffer"
    );
    backBufferDesc
        .setInitialState(presentationFrame.backBuffer.nativeInitialState)
        .setExternalFinalState(Core::ResourceStates::Present)
    ;
    const Core::GpuGraphResourceId backbuffer = m_deferredLightingTaskGraph.importTexture(
        presentationFrame.backBuffer.texture,
        backBufferDesc
    );
    if(!backbuffer.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse presentResourceUses[] = {
        ReadUse(compositeColor),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(
            backbuffer,
            presentationFramebufferDesc.colorAttachments[0].subresources,
            Core::ResourceStates::RenderTarget
        ),
    };
    Core::GpuTaskSchedulingHint presentScheduling;
    presentScheduling.cost = Core::GpuTaskCostHint::Medium;
    presentScheduling.avoidQueueCrossing = useLaggedLightingHistory;
    presentScheduling.forceSubmissionBoundary = true;
    presentScheduling.allowPacketMerge = false;
    const Core::GpuTaskId presentDependencies[] = {
        m_deferredCompositeTask,
        m_deferredSurfelGiTask,
    };
    const usize presentDependencyCount = useLaggedLightingHistory ? LengthOf(presentDependencies) : 1u;
    Core::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("render.deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(presentScheduling)
        .setDependencies(presentDependencies, presentDependencyCount)
        .setResourceUses(presentResourceUses, LengthOf(presentResourceUses))
    ;
    m_deferredPresentTask = m_deferredLightingTaskGraph.addTask<DeferredPresentGraphTask>(
        presentDesc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = &m_deferredSystem,
            .graphics = &m_graphics,
            .targets = &deferredTargets,
            .presentationFrame = presentationFrame,
            .backBuffer = backbuffer,
            .asyncFinalTiming = &asyncFinalTiming,
            .timingTicket = &presentTimingTicket,
            .shadowVisibilityTask = &m_deferredShadowVisibilityTask,
        }
    );
    if(!m_deferredPresentTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return;
    }

    // UI/overlay work must be declared before the independent diagnostic and history-copy tails. Its explicit
    // dependency on Deferred Present makes it the final presentation contributor that the timing endpoint follows,
    // instead of leaving Graphics::render() to submit a later untracked backbuffer write.
    m_deferredPresentationOverlayRequired =
        m_preparedTaskGraphPresentationContributor
        && m_preparedTaskGraphPresentationContributor->hasTaskGraphPresentationWork()
    ;
    if(m_deferredPresentationOverlayRequired){
        m_deferredPresentationOverlayTask = m_preparedTaskGraphPresentationContributor->declareTaskGraphPresentation(
            m_deferredLightingTaskGraph,
            presentationFrame,
            backbuffer,
            m_deferredPresentTask
        );
        if(!m_deferredPresentationOverlayTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: presentation contributor did not declare its final graph task"));
            return;
        }
    }

    // The critical-path query must end after the last graph-owned presentation contributor, while recovery keeps a
    // separate non-publishing endpoint for a rejected suffix. The distinct packet is also the only packet that may
    // carry the swap-chain presentation signal.
    const Core::GpuTaskId frameTimingEndDependency = m_deferredPresentationOverlayTask.valid()
        ? m_deferredPresentationOverlayTask
        : m_deferredPresentTask
    ;
    Core::GpuTaskSchedulingHint frameTimingEndScheduling;
    frameTimingEndScheduling.cost = Core::GpuTaskCostHint::Tiny;
    frameTimingEndScheduling.forceSubmissionBoundary = true;
    frameTimingEndScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc frameTimingEndDesc;
    frameTimingEndDesc
        .setIdentity(Name("render.frame_timing_end"))
        .setMarkerLabel("Frame Timing End")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(frameTimingEndScheduling)
        .setDependencies(&frameTimingEndDependency, 1u)
    ;
    m_deferredFrameTimingEndTask = m_deferredLightingTaskGraph.addTask<FrameTimingEndGraphTask>(
        frameTimingEndDesc,
        FrameTimingEndGraphTask::Payload{
            .frameTimingTransaction = &frameTimingTransaction,
        }
    );
    if(!m_deferredFrameTimingEndTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-timing endpoint graph task"));
        return;
    }
    if(!m_deferredLightingTaskGraph.declarePresentEndpoint(Core::GpuPresentEndpoint{
        .producer = m_deferredFrameTimingEndTask,
        .backBuffer = backbuffer,
    })){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graph presentation endpoint"));
        return;
    }

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
    if(!compiler.compile(
        m_deferredLightingTaskGraph,
        m_deferredLightingTaskGraphAnalysis,
        topology,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        scratchArena,
        compileOptions
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred AVBOIT/lighting/composite/present task graph"));
        return;
    }
    if(!__hidden_task_graph_deferred_lighting::PreparePacketEnvelopeMetrics(
        m_deferredLightingTaskGraph,
        m_deferredLightingCompiledGraph,
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

