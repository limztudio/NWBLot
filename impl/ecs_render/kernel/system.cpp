// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/ecs_scene/components.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Packet-range checks below encode the fixed semantic topology of the renderer graph. Keep the counts named so
// validation does not silently drift when a task is added to one of those ranges.
inline constexpr usize s_SinglePacketCount = 1u;
inline constexpr usize s_SoftwareShadowEffectsPacketCount = 2u;
inline constexpr usize s_SurfelGiMergedPreparationAndCopyPacketCount = 2u;
inline constexpr usize s_SurfelGiSeparatePreparationAndCopyPacketCount = 3u;
inline constexpr usize s_AvboitAsyncComputePacketCount = 5u;
inline constexpr usize s_DeferredLightingCompositePacketCount = 2u;
inline constexpr usize s_PresentationOverlayPacketCount = 1u;

// These fixed arrays describe the maximum number of independently retained sources/tickets that the graph binds
// for each semantic packet. They are capacities, not a runtime packet-count assumption.
inline constexpr usize s_ShadowVisibilityStateSourceCapacity = 3u;
inline constexpr usize s_SoftwareCausticsStateSourceCapacity = 3u;
inline constexpr usize s_SurfelGiStateSourceCapacity = 4u;
inline constexpr usize s_HardwareCausticsStateSourceCapacity = 2u;
inline constexpr usize s_SingleStateSourceCapacity = 1u;
inline constexpr usize s_SinglePacketStateBindingCapacity = 1u;
inline constexpr usize s_SingleExternalCompletionTokenCapacity = 1u;
inline constexpr usize s_DeferredPacketStateBindingCapacity = 8u;
inline constexpr usize s_AvboitTimingTicketCapacity = 7u;
inline constexpr usize s_LaggedLightingHistoryStateSourceCapacity = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_csgShapeRegistry(arena)
    , m_frameGraphRendererLabel(arena)
    , m_deferredLightingTaskGraph(arena)
    , m_deferredLightingTaskGraphAnalysis(arena)
    , m_deferredLightingTaskGraphQueueAssignments(arena)
    , m_deferredLightingCompiledGraph(arena)
    , m_deferredLightingRecordedGraph(arena)
    , m_deferredLightingSubmissionTransaction(arena)
    , m_deferredTaskTimingFeedback(arena, graphics)
    , m_meshState(arena)
    , m_materialState(arena)
    , m_rayTracingState(arena)
    , m_shadowComputePersistentState(arena)
    , m_shadowVisibilityReturnState(arena)
    , m_shadowPreparePersistentState(arena)
    , m_causticsComputePersistentState(arena)
    , m_hardwareCausticAccumulatorPersistentState(arena)
    , m_causticIrradianceLightingState(arena)
    , m_causticIrradianceReturnState(arena)
    , m_surfelGiComputePersistentState(arena)
    , m_surfelGiCounterPersistentState(arena)
    , m_surfelIrradianceReturnState(arena)
    , m_shaderSystem(*this)
    , m_meshSystem(*this)
    , m_materialSystem(*this)
    , m_csgSystem(*this)
    , m_deferredSystem(*this)
    , m_avboitSystem(*this)
    , m_raytracingSystem(*this)
{
    if(!RegisterBuiltInCsgShapeTypes(m_csgShapeRegistry))
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register built-in CSG shape types"));

    readAccess<NWB::Impl::Scene::ActiveCameraComponent>();
    readAccess<NWB::Impl::Scene::TransformComponent>();
    readAccess<NWB::Impl::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    readAccess<MaterialInstanceComponent>();
    readAccess<StaticCsgMeshComponent>();
    readAccess<SkinnedCsgMeshComponent>();
    readAccess<CsgCutterComponent>();
    m_deferredTaskTimingFeedback.activate();
}
RendererSystem::~RendererSystem(){
    m_deferredTaskTimingFeedback.deactivate();
}


bool RendererSystem::setTaskGraphTimingFeedbackPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy){
    return m_deferredTaskTimingFeedback.setPolicy(policy);
}


Core::GpuTaskGraphRuntimeStatistics RendererSystem::deferredTaskGraphRuntimeStatistics()const noexcept{
    return Core::CollectGpuTaskGraphRuntimeStatistics(
        m_deferredLightingCompiledGraph,
        m_deferredLightingRecordedGraph,
        m_deferredLightingSubmissionTransaction
    );
}


#if !defined(NWB_FINAL)
void RendererSystem::forceHybridSceneTraversalFallbackForTesting()noexcept{
    m_raytracingSystem.forceHybridSceneTraversalFallbackForTesting();
}

void RendererSystem::forceHybridSceneTraversalFallbackEveryFrameForTesting()noexcept{
    m_raytracingSystem.forceHybridSceneTraversalFallbackEveryFrameForTesting();
}

void RendererSystem::forceHybridHardwareFallbackSnapshotStaleForTesting()noexcept{
    m_raytracingSystem.forceHybridHardwareFallbackSnapshotStaleForTesting();
}

void RendererSystem::setGraphOwnedSoftTransparentShadowFoldEnabledForTesting(const bool enabled)noexcept{
    m_graphOwnedSoftTransparentShadowFoldEnabledForTesting = enabled;
    m_graphOwnedSoftTransparentShadowFoldBenchmarkForTesting = true;
    m_reportedGraphOwnedSoftTransparentShadowFoldBenchmarkForTesting = false;
}
#endif


void RendererSystem::reportLaggedLightingTransition(const LaggedLightingReport report, const u64 targetGeneration){
    if(m_laggedLightingReport == report && m_laggedLightingReportGeneration == targetGeneration)
        return;

    m_laggedLightingReport = report;
    m_laggedLightingReportGeneration = targetGeneration;
    switch(report){
    case LaggedLightingReport::Unreported:
        break;
    case LaggedLightingReport::NoDedicatedAsyncCompute:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated AsyncCompute lane, target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::BootstrapAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting bootstrap accepted (target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::ActiveHistoryAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting active history accepted (target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::CurrentFrameAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting current-frame path accepted (target generation {})"),
            targetGeneration
        );
        break;
    }
}

void RendererSystem::invalidateLaggedLightingHistorySubmission()noexcept{
    m_laggedLightingHistorySubmissionToken = Core::QueueSubmissionToken{};
}

void RendererSystem::invalidateLaggedLightingHistoryWriterDrain()noexcept{
    m_laggedLightingHistoryWriterDrainToken = Core::QueueSubmissionToken{};
    m_laggedLightingHistoryWriterDrainGeneration = 0u;
}

void RendererSystem::resetLaggedLightingHistoryReadTracking()noexcept{
    invalidateLaggedLightingHistorySubmission();
    m_laggedLightingHistoryGeneration = 0u;
}

void RendererSystem::resetLaggedLightingHistoryTracking()noexcept{
    resetLaggedLightingHistoryReadTracking();
    invalidateLaggedLightingHistoryWriterDrain();
}

void RendererSystem::resetTargetGenerationStateHandoffs()noexcept{
    // Replaced targets invalidate retained compute-local state.
    m_shadowComputePersistentState.reset();
    m_shadowVisibilityReturnState.reset();
    m_causticsComputePersistentState.reset();
    m_hardwareCausticAccumulatorPersistentState.reset();
    m_causticIrradianceLightingState.reset();
    m_causticIrradianceReturnState.reset();
    m_surfelGiComputePersistentState.reset();
    m_surfelGiCounterPersistentState.reset();
    m_surfelIrradianceReturnState.reset();
}

void RendererSystem::resetInvalidatedResourceStateHandoffs()noexcept{
    // The shadow-preparation packet owns its serial export; only retained cross-frame state is reset here.
    resetTargetGenerationStateHandoffs();
    m_shadowPreparePersistentState.reset();
}

void RendererSystem::resetFrameRecordingStateHandoffs()noexcept{
    // Preserve accepted compute-local state across fresh recordings.
    m_causticIrradianceLightingState.reset();
}

void RendererSystem::resetAbandonedFrameStateHandoffs()noexcept{
    // Rejected frames keep only cross-frame state from earlier accepted producers. The hardware caustic accumulator
    // changes only in its accepted callback, so a rejected record must preserve its prior warm-decay source.
    m_causticIrradianceLightingState.reset();
}

void RendererSystem::resetRejectedShadowVisibilityStateHandoffs()noexcept{
    // Preserve accepted Graphics prefix state when graph-owned shadow visibility rejects.
    m_causticIrradianceLightingState.reset();
}


bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    m_raytracingSystem.logCapabilityOnce();
    if(width == 0 || height == 0)
        return true;

    if(!prepareGpuTimingScopes())
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU timing scope preparation failed; timing samples may be skipped"));

    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;
    bool targetsReady = deferredTargets.valid() && deferredTargets.width == width && deferredTargets.height == height;
    if(!targetsReady){
        // New targets invalidate stale compute scratch and visibility returns.
        resetTargetGenerationStateHandoffs();
        resetLaggedLightingHistoryTracking();
        targetsReady = m_deferredSystem.createDeferredFrameTargets(width, height);
    }
    if(!targetsReady)
        return false;

    if(Core::Framebuffer* presentationFramebuffer = m_graphics.getCurrentFramebuffer()){
        if(!m_deferredSystem.createDeferredPresentPipeline(presentationFramebuffer))
            return false;
    }

    if(!m_avboitSystem.createAvboitPipelines())
        return false;

    if(!m_graphics.queryFeatureSupport(Core::Feature::Meshlets)){
        if(!m_materialSystem.createComputeEmulationResources())
            return false;
    }

    if(!m_meshSystem.createMeshViewBuffer())
        return false;

    if(!m_csgSystem.createCsgIntervalPeelResources(deferredTargets, true))
        return false;

    return true;
}

void RendererSystem::invalidateResources(){
    m_deferredTaskTimingFeedback.reset();
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;
    m_preparedHasTransparentRenderers = false;
    m_preparedShadowVisibilityResourcesValid = false;
    m_preparedShadowVisibilityReady = false;
    m_raytracingSystem.discardPreflightShadowVisibilityResources();
    m_raytracingSystem.invalidatePreparedShadowTraceGeometryBuffers();
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
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredLightingTaskGraphValid = false;
    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareHybridSoftwareTailTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
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
    m_deferredAvboitClearFirstTask = {};
    m_deferredAvboitClearTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitCsgReceiverSpanTask = {};
    m_deferredAvboitCsgIntervalCombineTask = {};
    m_deferredAvboitOccupancyStreamTask = {};
    m_deferredAvboitOccupancyComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitOccupancySharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitOccupancySharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitOccupancyTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionStreamTask = {};
    m_deferredAvboitExtinctionComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitExtinctionSharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitExtinctionSharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationStreamTask = {};
    m_deferredAvboitAccumulationComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitAccumulationSharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitAccumulationSharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitAccumulationTask = {};
    m_deferredAvboitAccumulationFinalizeTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentationOverlayTask = {};
    m_deferredPresentTask = {};
    m_deferredFrameTimingEndTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredFrameRecoveryTask = {};
    m_deferredSurfelGiCounterReadbackCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
    m_preparedTaskGraphPresentationContributor = nullptr;
    m_deferredPresentationOverlayRequired = false;
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    resetInvalidatedResourceStateHandoffs();
    resetLaggedLightingHistoryTracking();
    m_frameRenderRecoveryFailed = false;
    // Retire heap-retained TLAS resources before releasing their backing state.
    if(m_rayTracingState.m_tlasHeapHandle.valid()){
        auto& device = m_graphics.getDevice();
        device.getDescriptorHeap().free(m_rayTracingState.m_tlasHeapHandle);
    }
    m_raytracingSystem.releaseCausticEmissionTargetHeapHandle();
    m_raytracingSystem.releaseRayTraceMaterialContextHeapHandles();
    m_raytracingSystem.releaseSwBvhScratchHeapHandles();
    m_raytracingSystem.releaseSurfelGiHeapHandles();
    m_deferredSystem.resetDeferredFrameTargets();
    m_meshSystem.releaseAllMeshGeometryHeapHandles();
    m_meshSystem.releaseMeshFrameHeapHandles();
    m_meshState.invalidateResources();
    m_materialSystem.releaseMaterialResourceReferences();
    m_materialState.invalidateResources();
    m_drawState.invalidateResources();
    m_csgSystem.releaseCsgClipContextHeapHandles();
    m_csgState.invalidateResources();
    m_deferredState.invalidateResources();
    m_avboitState.invalidateResources();
    m_rayTracingState.invalidateResources();
}

void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

bool RendererSystem::prepareGpuTimingScopes(){
    auto& device = m_graphics.getDevice();
    // A timestamp range consumes a begin/end query pair. High-frequency raster/mesh scopes may emit many ranges;
    // the sort and interval-clear scopes each emit two ranges.
    static constexpr u32 s_GpuTimingQueriesPerRange = 2u;
    static constexpr u32 s_GpuTimingQueriesPerTwoRangeScope = 2u * s_GpuTimingQueriesPerRange;
    static constexpr u32 s_GpuTimingHighFrequencyScopeQueryBudget = 128u;

    struct ScopeReservation{
        const Core::GpuTimingScopeDefinition* scope;
        u32 queryCount;
    };
    const ScopeReservation scopeReservations[] = {
        { &RendererGpuTimingScope::s_MeshDispatch, s_GpuTimingHighFrequencyScopeQueryBudget },
        { &RendererGpuTimingScope::s_Raster, s_GpuTimingHighFrequencyScopeQueryBudget },
        { &RendererGpuTimingScope::s_Frame, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncPrefix, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncShadow, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncSurfelGi, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncFinal, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredClear, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowVisibility, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowOpaqueTrace, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowGeometryDownsample, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowOpaqueTemporal, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowOpaqueResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowTransparentTrace, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowTransparentTemporal, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowTransparentResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SwBvhSort, s_GpuTimingQueriesPerTwoRangeScope },
        { &RendererGpuTimingScope::s_CausticPhotons, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CausticResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredLighting, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredComposite, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredPresent, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_MaterialUpload, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_OpaqueRegular, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_OpaqueCsg, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgUpload, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgSampleStateUpload, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgIntervalClear, s_GpuTimingQueriesPerTwoRangeScope },
        { &RendererGpuTimingScope::s_CsgIntervalPeel, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgReceiverSpanBuild, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgIntervalCombine, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgCapFill, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_TransparentCsgIntervals, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitClear, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitOccupancy, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitDepthWarp, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitExtinction, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitIntegration, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitAccumulate, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelSpawn, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelAgeFree, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelHashBuild, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelTrace, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelUpsample, s_GpuTimingQueriesPerRange },
    };

    for(const ScopeReservation& reservation : scopeReservations){
        if(!m_graphics.gpuTiming().prepareScopeQueries(reservation.scope->identity, device, reservation.queryCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare GPU timing scope '{}'"), StringConvert(reservation.scope->identity.c_str()));
            return false;
        }
    }

    return true;
}

bool RendererSystem::prepareResources(Core::Framebuffer* framebuffer){
    m_preparedShadowVisibilityResourcesValid = false;
    m_preparedShadowVisibilityReady = false;
    m_preparedHasTransparentRenderers = false;
    m_preparedTaskGraphPresentationContributor = nullptr;

    if(!framebuffer)
        return false;

    m_meshSystem.pruneRuntimeMeshResources();
    m_preparedHasTransparentRenderers = m_materialSystem.prepareVisibleMaterialSurfaceInfos();
    m_materialSystem.prepareVisibleMaterialInstanceMutableCache();
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;

    if(!m_deferredState.m_targets.valid())
        return true;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);
    m_preparedCsgFrameState = HasCsgFrameCandidates(m_world)
        ? m_csgSystem.buildFrameState(scratchArena)
        : CsgFrameState{}
    ;
    m_preparedCsgFrameStateValid = true;
    const bool hasCsgFrameWork = !m_preparedCsgFrameState.empty();
    if(hasCsgFrameWork && !deferredTargets.csgIntervalTargetsValid())
        return false;

    // CSG receiver ranges are addressed by material-pass instance index.  A CSG-active material pass reserves one
    // range for every compatible renderer, not only the renderers that receive CSG clipping, so the complete
    // renderer view is the safe prepass capacity bound for either material pass.
    const usize csgReceiverRangeCount = hasCsgFrameWork
        ? m_world.view<RendererComponent>().candidateCount()
        : 0u
    ;

    if(!m_materialSystem.prepareMaterialPassResources(
        deferredTargets.framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        m_preparedCsgFrameState,
        nullptr
    ))
        return false;

    if(
        m_preparedHasTransparentRenderers
        && !m_avboitSystem.prepareAvboitPassResources(deferredTargets, m_preparedCsgFrameState)
    )
        return false;

    // Refresh mesh descriptors after transparent preparation can grow shared buffers.
    if(
        m_preparedHasTransparentRenderers
        && !m_materialSystem.prepareMaterialPassResources(
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            false,
            m_preparedCsgFrameState,
            nullptr
        )
    )
        return false;

    // All material passes have now established their shared frame buffers.  Create CSG resources once from this
    // renderer-owned prepass so draw paths only consume the prepared layouts and handles.
    if(
        hasCsgFrameWork
        && !m_csgSystem.prepareCsgFrameResources(
            csgReceiverRangeCount,
            static_cast<usize>(m_preparedCsgFrameState.cutterCount)
        )
    )
        return false;

    // Resource selection and capacity growth happen before shared-graph compilation.  The first deferred packet
    // records the corresponding GPU work later, after every selected handle has been imported declaratively.
    m_raytracingSystem.discardSurfelResourceInitialization();
    if(!m_raytracingSystem.preflightShadowVisibilityResources(deferredTargets, scratchArena)){
        m_preparedShadowVisibilityResourcesValid = false;
        m_preparedShadowVisibilityReady = false;
        m_raytracingSystem.discardPreflightShadowVisibilityResources();
        return false;
    }
    m_preparedShadowVisibilityResourcesValid = true;

    if(Core::IGpuTaskGraphPresentationContributor* const contributor = m_graphics.taskGraphPresentationContributor()){
        if(contributor->prepareTaskGraphPresentation(framebuffer))
            m_preparedTaskGraphPresentationContributor = contributor;
        else
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: presentation contributor preparation failed; rendering scene output without its overlay"));
    }

    return true;
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
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
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearFirstTask = {};
    m_graphicsPrefixDeferredClearTask = {};
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
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredLightingTaskGraphValid = false;
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
    m_deferredAvboitClearFirstTask = {};
    m_deferredAvboitClearTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitCsgReceiverSpanTask = {};
    m_deferredAvboitCsgIntervalCombineTask = {};
    m_deferredAvboitOccupancyStreamTask = {};
    m_deferredAvboitOccupancyComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitOccupancySharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitOccupancySharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitOccupancyTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionStreamTask = {};
    m_deferredAvboitExtinctionComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitExtinctionSharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitExtinctionSharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationStreamTask = {};
    m_deferredAvboitAccumulationComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitAccumulationSharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitAccumulationSharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitAccumulationTask = {};
    m_deferredAvboitAccumulationFinalizeTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentationOverlayTask = {};
    m_deferredPresentTask = {};
    m_deferredFrameTimingEndTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredFrameRecoveryTask = {};
    m_deferredSurfelGiCounterReadbackCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
    m_deferredPresentationOverlayRequired = false;
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);

    if(!framebuffer)
        return;

    if(!m_deferredState.m_targets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    NWB_ASSERT(m_preparedShadowVisibilityResourcesValid);
    if(!m_preparedShadowVisibilityResourcesValid){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow-visibility resource preflight was unavailable"));
        return;
    }

    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    const bool hasTransparentRenderers = m_preparedHasTransparentRenderers;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto& device = m_graphics.getDevice();
    if(m_graphics.isDeviceRecreationRequested() || device.isDeviceLost()){
        if(device.isDeviceLost())
            m_graphics.requestDeviceRecreation();
        return;
    }
    if(m_frameRenderRecoveryFailed){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame render recovery failed; rendering is suspended until resources are recreated"));
        return;
    }
    // Renderer scheduling queries the physical transport exposed to the graph, not the legacy unresolved lane.
    // A Compute entry exists only when Vulkan created an enabled, distinct async-compute queue for this device.
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

    // The next graph declaration clears the normal history-tail output token before its replacement can accept.
    // Keep only an incomplete accepted tail as the writer drain. A completed tail cannot race live producers, while a
    // later accepted tail is a proven successor of an older drain and protects a rejected declaration/submission retry.
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
            // Target generations prevent history from using recycled slots after resize.
            invalidateLaggedLightingHistorySubmission();
            m_laggedLightingHistoryGeneration = laggedLightingHistoryTargetGeneration;
        }
    }
    else{
        resetLaggedLightingHistoryReadTracking();
    }
    // Declaring the next history-copy tail intentionally resets the member token before that new packet accepts.
    // Snapshot the prior accepted tail for every current-frame external dependency before graph declaration.
    const Core::QueueSubmissionToken priorLaggedLightingHistoryToken = m_laggedLightingHistorySubmissionToken;
    const Core::QueueSubmissionToken priorLaggedLightingHistoryWriterDrainToken =
        m_laggedLightingHistoryWriterDrainToken
    ;
    const bool laggedLightingHistoryWriterWaitPending = priorLaggedLightingHistoryWriterDrainToken.valid();
    const Core::QueueSubmissionToken laggedLightingHistoryWriterCompletionToken = priorLaggedLightingHistoryWriterDrainToken;
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    const bool laggedAsyncLightingSchedule =
        laggedAsyncLightingRequested
        && laggedLightingHistoryResourcesReady
        && priorLaggedLightingHistoryToken.valid()
    ;
    const bool laggedLightingHistoryCompletionRequired =
        laggedAsyncLightingSchedule
        || laggedLightingHistoryWriterWaitPending
    ;
    // History capture is graph-owned and remains available whenever the opt-in path has a distinct compute
    // transport. Its tail is optional: a failed tail build must leave the current frame's deferred path intact.
    const bool requestsLaggedLightingHistoryCapture = laggedAsyncLightingRequested;
    m_preparedShadowVisibilityReady = false;
    // Compile every independent graph before native recording. The graphics prefix records all five ordered tasks
    // natively from mesh-view setup through post-G-buffer normalization.
    const ECSRenderDetail::RendererFrameGraphFeatures frameGraphFeatures{
        .frameLaggedAsyncLightingEnabled = m_frameLaggedAsyncLightingEnabled,
        .laggedLightingHistoryReady = laggedLightingHistoryResourcesReady,
        .laggedLightingHistoryAccepted = priorLaggedLightingHistoryToken.valid(),
        .laggedLightingHistoryWriterWaitPending = laggedLightingHistoryWriterWaitPending,
        .hasTransparentRenderers = hasTransparentRenderers,
        .hardwareCaustics = hardwareShadowSupported,
    };
    resetFrameRecordingStateHandoffs();
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Preserve CPU mirrors so rejected recordings can be retried exactly.
    struct PostGbufferPacketCpuState{
        u64 swShadowEdgeStatsPendingSubmissionID = 0u;
        Core::QueueSubmissionToken surfelCountReadbackSubmissionToken;

        u32 softShadowFrameIndex = 0u;
        u32 swShadowEdgeStatsTick = 0u;
        u32 swShadowEdgeStatsPendingTick = 0u;
        u32 causticTemporalReuseFrameCount = 0u;
        u32 swCausticFrameIndex = 0u;
        u32 hwCausticFrameIndex = 0u;
        u32 surfelFrameIndex = 0u;
        u32 surfelCountReadbackFrame = 0u;
        u32 shadowSlotCount = 0u;
        u32 softShadowSlotMask = 0u;
        u32 causticLightCount = 0u;

        bool avboitTargetsNeedClear = true;
        bool deferredBindlessSlotsUploaded = false;
        bool swShadowEdgeStatsPending = false;
        bool swShadowEdgeStatsPendingSubmissionUnconfirmed = false;
        bool swShadowDispatchLogged = false;
        bool causticAccumulatorInitialized = false;
        bool swCausticDispatchLogged = false;
        bool hwCausticDispatchLogged = false;
        bool causticEmissionGateLogged = false;
        bool surfelSeeded = false;
        Core::GpuPhysicalQueueId swShadowEdgeStatsPendingSubmissionPhysicalQueue;
    };
    const PostGbufferPacketCpuState postGbufferPacketCpuState{
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID,
        m_rayTracingState.m_surfelCountReadbackSubmissionToken,
        m_rayTracingState.m_softShadowFrameIndex,
        m_rayTracingState.m_swShadowEdgeStatsTick,
        m_rayTracingState.m_swShadowEdgeStatsPendingTick,
        m_rayTracingState.m_causticTemporalReuseFrameCount,
        m_rayTracingState.m_swCausticFrameIndex,
        m_rayTracingState.m_hwCausticFrameIndex,
        m_rayTracingState.m_surfelFrameIndex,
        m_rayTracingState.m_surfelCountReadbackFrame,
        m_rayTracingState.m_shadowSlotCount,
        m_rayTracingState.m_softShadowSlotMask,
        m_rayTracingState.m_causticLightCount,
        m_avboitState.m_targetsNeedClear,
        deferredTargets.bindless.slotsUploaded,
        m_rayTracingState.m_swShadowEdgeStatsPending,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionUnconfirmed,
        m_rayTracingState.m_swShadowDispatchLogged,
        m_rayTracingState.m_causticAccumulatorInitialized,
        m_rayTracingState.m_swCausticDispatchLogged,
        m_rayTracingState.m_hwCausticDispatchLogged,
        m_rayTracingState.m_causticEmissionGateLogged,
        m_rayTracingState.m_surfelSeeded,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue,
    };
    const auto restorePrefixCpuState = [&](){
        // Rejected G-buffer recording invalidates CPU upload mirrors.
        m_drawState.m_meshViewGpuDataValid = false;
        m_deferredState.m_sceneShadingGpuDataValid = false;
        m_deferredState.m_lightGpuDataValid = false;
    };

    const auto restoreShadowCpuState = [&](){
        m_rayTracingState.m_softShadowFrameIndex = postGbufferPacketCpuState.softShadowFrameIndex;
        m_rayTracingState.m_swShadowEdgeStatsTick = postGbufferPacketCpuState.swShadowEdgeStatsTick;
        m_rayTracingState.m_swShadowEdgeStatsPending = postGbufferPacketCpuState.swShadowEdgeStatsPending;
        m_rayTracingState.m_swShadowEdgeStatsPendingTick = postGbufferPacketCpuState.swShadowEdgeStatsPendingTick;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionID;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionPhysicalQueue;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionUnconfirmed = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionUnconfirmed;
        m_rayTracingState.m_swShadowDispatchLogged = postGbufferPacketCpuState.swShadowDispatchLogged;
    };

    const auto restoreCausticsCpuState = [&](){
        m_rayTracingState.m_causticAccumulatorInitialized = postGbufferPacketCpuState.causticAccumulatorInitialized;
        m_rayTracingState.m_causticTemporalReuseFrameCount = postGbufferPacketCpuState.causticTemporalReuseFrameCount;
        m_rayTracingState.m_swCausticFrameIndex = postGbufferPacketCpuState.swCausticFrameIndex;
        m_rayTracingState.m_hwCausticFrameIndex = postGbufferPacketCpuState.hwCausticFrameIndex;
        m_rayTracingState.m_swCausticDispatchLogged = postGbufferPacketCpuState.swCausticDispatchLogged;
        m_rayTracingState.m_hwCausticDispatchLogged = postGbufferPacketCpuState.hwCausticDispatchLogged;
        m_rayTracingState.m_causticEmissionGateLogged = postGbufferPacketCpuState.causticEmissionGateLogged;
    };
    const auto restoreSurfelGiCpuState = [&](){
        m_rayTracingState.m_surfelFrameIndex = postGbufferPacketCpuState.surfelFrameIndex;
        m_rayTracingState.m_surfelSeeded = postGbufferPacketCpuState.surfelSeeded;
        m_rayTracingState.m_surfelCountReadbackFrame = postGbufferPacketCpuState.surfelCountReadbackFrame;
        m_rayTracingState.m_surfelCountReadbackSubmissionToken = postGbufferPacketCpuState.surfelCountReadbackSubmissionToken;
    };
    const auto restoreAvboitCpuState = [&](){
        m_avboitState.m_targetsNeedClear = postGbufferPacketCpuState.avboitTargetsNeedClear;
    };
    const auto restorePostGbufferEffectsCpuState = [&](){
        restoreCausticsCpuState();
        restoreSurfelGiCpuState();
        restoreAvboitCpuState();
    };
    const auto restorePostGbufferPacketCpuState = [&](const bool restoreBindlessSlots){
        if(restoreBindlessSlots)
            deferredTargets.bindless.slotsUploaded = postGbufferPacketCpuState.deferredBindlessSlotsUploaded;
        restorePrefixCpuState();
        restoreShadowCpuState();
        restorePostGbufferEffectsCpuState();
    };
    // The current graph must retain this token even if Surfel GI consumes the completed diagnostic while recording.
    const Core::QueueSubmissionToken surfelCounterReadbackCompletionToken =
        m_rayTracingState.m_surfelCountReadbackSubmissionToken
    ;

    // Each semantic prefix stage starts with its own ticket. After frontier-safe compilation, tasks that share a
    // native packet are rebound to one ticket, while a split prefix retains one complete ticket per submission.
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
    // The optional AsyncPrefix query spans Mesh View Setup through Normalize, so it is only valid when compilation
    // keeps those endpoints in one native submission.
    bool asyncPrefixTimingSpansOnePacket = true;
    Optional<Core::GpuTimingMeasure> asyncPrefixTiming;
    Optional<Core::GpuTimingMeasure> deferredClearTiming;
    ECSRenderDetail::DeferredClearTimingRecordState deferredClearTimingState{
        .graphics = &m_graphics,
        .timing = &deferredClearTiming,
        .timingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(
            ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::DeferredClear
        )],
    };
    Optional<Core::GpuTimingMeasure> opaqueCsgIntervalClearTiming;
    ECSRenderDetail::CsgIntervalClearTimingRecordState opaqueCsgIntervalClearTimingState{
        .graphics = &m_graphics,
        .timing = &opaqueCsgIntervalClearTiming,
        .rebindableTimingTicket = &graphicsPrefixTimingTickets[static_cast<usize>(
            ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::Gbuffer
        )],
    };
    // The small shared-output opaque sequence spans G-buffer's mesh prelude and four, six, or eight graph callbacks.
    // Keep its measurement alive through graph declaration, recording, submission, and rejection just like CSG
    // intervals.
    Optional<Core::GpuTimingMeasure> opaqueRegularSharedComputeEmulationTiming;
    // Opaque CSG interval-sample compute/raster can span two graph callbacks while retaining the old material scope.
    Optional<Core::GpuTimingMeasure> opaqueCsgIntervalSampleComputeEmulationTiming;
    Core::GpuTimingSubmissionTicket shadowVisibilityTimingTicket(m_graphics.gpuTiming());
    // The prepared soft-transparent route spans opaque resolve across its first-wavelet and tail callbacks, and
    // begins transparent resolve in temporal merge when active (otherwise its first wavelet). The terminal fold
    // still closes the aggregate Shadow Visibility range. The monolithic task leaves these empty.
    Optional<Core::GpuTimingMeasure> shadowVisibilityAsyncTiming;
    Optional<Core::GpuTimingMeasure> shadowVisibilityTiming;
    Optional<Core::GpuTimingMeasure> opaqueSoftResolveTiming;
    Optional<Core::GpuTimingMeasure> transparentSoftResolveTiming;
    bool shadowVisibilityOpaqueProduced = false;
    bool shadowVisibilityTransparentTraceProduced = false;
    u32 shadowVisibilityOpaqueFrameIndex = 0u;
    Core::GpuTimingSubmissionTicket softwareCausticsTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket surfelGiTimingTicket(m_graphics.gpuTiming());
    // Age/free begins this interval and the remaining GI callback closes it after the graph-owned cell-head clear.
    Optional<Core::GpuTimingMeasure> surfelGiAsyncTiming;
    Core::GpuTimingSubmissionTicket hardwareCausticsTimingTicket(m_graphics.gpuTiming());
    // A warm temporal decay starts this interval in its graph task and the selected photon producer closes it.
    Optional<Core::GpuTimingMeasure> causticPhotonTiming;
    // Geometry downsample begins the resolve interval and wavelet resolve closes it in the same selected packet.
    Optional<Core::GpuTimingMeasure> causticResolveTiming;
    const bool clearAvboitTargets = hasTransparentRenderers || m_avboitState.m_targetsNeedClear;
    Core::GpuTimingSubmissionTicket avboitPreTimingTicket(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> avboitClearTiming;
    ECSRenderDetail::AvboitClearTimingRecordState avboitClearTimingState{
        .graphics = &m_graphics,
        .timing = &avboitClearTiming,
        .timingTicket = &avboitPreTimingTicket,
    };
    Optional<Core::GpuTimingMeasure> transparentCsgIntervalClearTiming;
    ECSRenderDetail::CsgIntervalClearTimingRecordState transparentCsgIntervalClearTimingState{
        .graphics = &m_graphics,
        .timing = &transparentCsgIntervalClearTiming,
        .timingTicket = &avboitPreTimingTicket,
    };
    // Prepared transparent CSG begins this interval in AVBOIT Pre and closes it in its graph-owned Combine callback.
    Optional<Core::GpuTimingMeasure> transparentCsgIntervalsTiming;
    // The split Occupancy handoff starts this interval in its compute producer and closes it in the raster consumer.
    Optional<Core::GpuTimingMeasure> avboitOccupancyComputeEmulationTiming;
    // The split Extinction handoff starts this interval in its compute producer and closes it in the raster consumer.
    Optional<Core::GpuTimingMeasure> avboitExtinctionComputeEmulationTiming;
    // The split Accumulation handoff starts this interval in its compute producer and closes it in the raster consumer.
    Optional<Core::GpuTimingMeasure> avboitAccumulationComputeEmulationTiming;
    Core::GpuTimingSubmissionTicket avboitDepthWarpTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitExtinctionTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitIntegrationTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitAccumulationTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket deferredLightingTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket deferredCompositeTimingTicket(m_graphics.gpuTiming());
    // Publish the frame endpoint only after the terminal Graphics Present packet accepts. This also covers the
    // serialized Graphics-only route when no dedicated compute family exists.
    Core::GpuTimingFrameTransaction frameTimingTransaction(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncFinalTiming;
    Core::GpuTimingSubmissionTicket deferredPresentTimingTicket(m_graphics.gpuTiming());
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
        framebuffer,
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
            framebuffer,
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
            false
        );
    }
    const bool captureLaggedLightingHistory = m_deferredLaggedLightingHistoryTask.valid();
    // An optional setup task may be in another packet, where its ordinary graph dependency already supplies the
    // ordering. When it shares the consumer's packet, require strict compiler task order without exposing that
    // packet's internal task array to renderer policy.
    const auto optionalTaskPrecedesInSharedPacket = [&](
        const Core::GpuTaskId optionalTask,
        const Core::GpuTaskId consumerTask
    ){
        return !optionalTask.valid()
            || !m_deferredLightingCompiledGraph.tasksSharePacket(optionalTask, consumerTask)
            || m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(optionalTask, consumerTask)
        ;
    };
    const auto taskIsCompiled = [&](const Core::GpuTaskId task){
        return m_deferredLightingCompiledGraph.findTask(task) != nullptr;
    };
    // Pure-software per-mesh typed clears and their native compute callbacks are part of the same accepting Shadow
    // Preparation packet. The semantic range still starts at Shadow Preparation, so a split would otherwise omit
    // recorded predecessor work and allow CPU topology publication without its sentinel/compute chain.
    const bool shadowPrepareSoftwareBvhBuildsMerged =
        !m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid()
            ? !m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
            : (
                m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
                && m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_deferredShadowPrepareSoftwareBvhBuildFirstTask
                )
                && m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_deferredShadowPrepareSoftwareBvhBuildLastTask
                )
            )
    ;
    // The optional hybrid tail records real work, but preserves the former aggregate callback's acceptance and
    // fallback boundary by remaining in this exact first Graphics packet.
    const bool shadowPrepareHybridSoftwareTailMerged =
        !m_deferredShadowPrepareHybridSoftwareTailTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_deferredShadowPrepareHybridSoftwareTailTask
        )
    ;
    // The state-only finalizer is part of the accepting Shadow Preparation contract. It may be a separate graph
    // callback, but every frozen AS/backing transition must remain in the same first Graphics submission as its
    // build so CPU cache publication and the retained packet-state handoff stay atomic.
    const bool shadowPrepareAccelStructFinalizeMerged =
        !m_deferredShadowPrepareAccelStructFinalizeTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_deferredShadowPrepareAccelStructFinalizeTask
        )
    ;
    // The prefix submission range is anchored at Shadow Preparation. A selector upload is safe only when the
    // compiler keeps it in that exact packet; otherwise it would be recorded but omitted from the accepted range.
    const bool deferredBindlessSlotsUploadMergedIntoShadowPreparePacket =
        !m_deferredBindlessSlotsUploadTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_deferredBindlessSlotsUploadTask
        )
    ;
    // This selector is consumed by later Compute trace tasks, but the prefix submission range starts at Shadow
    // Preparation. Keep the immutable upload in that exact first packet so Shadow Preparation becomes the handoff.
    const bool rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket =
        !m_rayTraceMaterialContextSlotsUploadTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_rayTraceMaterialContextSlotsUploadTask
        )
    ;
    // The caustic input upload is optional for an empty refractive set, but a nonempty payload must live in the
    // exact Shadow Preparation packet that owns the following ShaderResource handoff to later Compute consumers.
    const bool causticEmissionTargetsUploadMergedIntoShadowPreparePacket =
        !m_causticEmissionTargetsUploadTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_causticEmissionTargetsUploadTask
        )
    ;
    // Active surfel frames freeze a fresh constant payload before compilation. It must share Shadow Preparation's
    // first Graphics packet so that task remains the handoff producer for the asynchronous Surfel-GI consumer.
    const bool surfelFrameConstantsUploadMergedIntoShadowPreparePacket =
        !m_surfelFrameConstantsUploadTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredShadowPrepareTask,
            m_surfelFrameConstantsUploadTask
        )
    ;
    // This ABI-coupled triple must remain in the first accepted Graphics packet. Shadow Preparation supersedes the
    // upload writers as the ShaderResource producer observed by later asynchronous trace passes.
    const bool shadowMaterialContextUploadsMergedIntoShadowPreparePacket =
        (!m_shadowInstanceMaterialUploadTask.valid()
            || m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowPrepareTask,
                m_shadowInstanceMaterialUploadTask
            ))
        && (!m_shadowInstanceUploadTask.valid()
            || m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowPrepareTask,
                m_shadowInstanceUploadTask
            ))
        && (!m_shadowMaterialTypedUploadTask.valid()
            || m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowPrepareTask,
                m_shadowMaterialTypedUploadTask
            ))
    ;
    // Scene-BVH nodes index the companion leaf-instance stream. Keep the whole immutable pair in the accepting
    // Shadow Preparation packet so it becomes the only ShaderResource producer exposed to later Compute work.
    const bool sceneBvhUploadsMergedIntoShadowPreparePacket =
        m_sceneBvhNodesUploadTask.valid() == m_sceneBvhInstancesUploadTask.valid()
        && (!m_sceneBvhNodesUploadTask.valid()
            || (
                m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_sceneBvhNodesUploadTask
                )
                && m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredShadowPrepareTask,
                    m_sceneBvhInstancesUploadTask
                )
            ))
    ;
    // The optional CSG callbacks alias the G-buffer task on ordinary frames. Each retains an independent semantic
    // timing anchor when a FrontierSafe boundary splits the Graphics prefix.
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
            !m_deferredLightingCompiledGraph.findTask(task)
            || (
                prefixTaskIndex != 0u
                && !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
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
            if(!m_deferredLightingCompiledGraph.tasksSharePacket(
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
    // GpuTimingMeasure is deliberately submission-local. Skip this optional long-lived scope when the compiler
    // exposes a frontier between its endpoints. Immutable built-in uploads may add untimed packets between these
    // semantic anchors; their enclosing Graphics submission remains graph-owned and deterministic.
    asyncPrefixTimingSpansOnePacket = graphicsPrefixTimingBindingsValid
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_graphicsPrefixMeshViewSetupTask,
            m_graphicsPrefixTask
        )
    ;
    const bool shadowVisibilityPreparedTasksMerged =
        !m_deferredShadowVisibilityOpaqueTask.valid()
        || (
            m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityOpaqueTask
            )
            && m_deferredShadowVisibilityOpaqueFirstWaveletTask.valid()
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityOpaqueFirstWaveletTask
            )
            && m_deferredShadowVisibilityOpaqueResolveTask.valid()
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityOpaqueResolveTask
            )
            && m_deferredShadowVisibilityTransparentTraceTask.valid()
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityTransparentTraceTask
            )
            && (
                !m_deferredShadowVisibilityTransparentTemporalMergeTask.valid()
                || m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredShadowVisibilityTask,
                    m_deferredShadowVisibilityTransparentTemporalMergeTask
                )
            )
            && m_deferredShadowVisibilityTransparentFirstWaveletTask.valid()
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityTransparentFirstWaveletTask
            )
        )
    ;
    // The normal monolithic callback relies on the preceding typed all-lit clear. Keep that clear in its semantic
    // packet so task-range recording/submission still encloses the CopyDest -> UAV handoff. Prepared split-soft
    // frames deliberately retain their native fallback clear and therefore must not declare this primitive.
    const bool shadowVisibilityAllLitClearMerged = m_deferredShadowVisibilityOpaqueTask.valid()
        ? !m_deferredShadowVisibilityAllLitClearTask.valid()
        : m_deferredShadowVisibilityAllLitClearTask.valid()
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAllLitClearTask
            )
    ;
    // The adaptive clear/copy primitives deliberately keep the original Shadow Visibility acceptance endpoint.
    // A split would leave part of the chain outside its semantic record/submit range and would make the frozen
    // diagnostic state observable before the traversal packet accepts.
    const bool shadowVisibilityAdaptivePrimitivesMerged =
        (!m_deferredShadowVisibilityAdaptiveStatsClearTask.valid()
            || m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAdaptiveStatsClearTask
            ))
        && (!m_deferredShadowVisibilityAdaptiveCounterClearTask.valid()
            || m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAdaptiveCounterClearTask
            ))
        && (!m_deferredShadowVisibilityAdaptiveStatsReadbackTask.valid()
            || m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredShadowVisibilityTask,
                m_deferredShadowVisibilityAdaptiveStatsReadbackTask
            ))
    ;
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredShadowVisibilityTask);
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_graphicsPrefixTask);
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueComputeEmulationQueue =
        m_graphicsPrefixOpaqueComputeEmulationTask.valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(m_graphicsPrefixOpaqueComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueSharedComputeEmulationQueue =
        m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount != 0u
        && m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u].valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(
                m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueCsgReceiverComputeEmulationQueue =
        m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationQueue =
        m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(
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
            m_deferredLightingCompiledGraph.queueInfoForTask(graphicsPrefixTimingTasks[prefixTaskIndex]);
        graphicsPrefixPacketsAreGraphics = queue && queue->queueClass == Core::CommandQueue::Graphics;
    }
    // The deferred-clear measure begins and ends inside the first and terminal typed clear tasks.  The terminal
    // clear owns the later asynchronous handoff, so do not record unless the entire bracket is one Graphics packet.
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixDeferredClearQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_graphicsPrefixDeferredClearTask);
    const bool graphicsPrefixDeferredClearBundleMerged =
        taskIsCompiled(m_graphicsPrefixDeferredClearFirstTask)
        && taskIsCompiled(m_graphicsPrefixDeferredClearTask)
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_graphicsPrefixDeferredClearFirstTask,
            m_graphicsPrefixDeferredClearTask
        )
        && graphicsPrefixDeferredClearQueue
        && graphicsPrefixDeferredClearQueue->queueClass == Core::CommandQueue::Graphics
    ;
    // The optional alias-free regular-emulation producer shares G-buffer's primary Graphics packet. Its required
    // UAV-to-VertexBuffer boundary is packet-local and G-buffer's timing/range remains the semantic endpoint.
    const bool graphicsPrefixOpaqueComputeEmulationMerged =
        !m_graphicsPrefixOpaqueComputeEmulationTask.valid()
        || (
            taskIsCompiled(m_graphicsPrefixOpaqueComputeEmulationTask)
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_graphicsPrefixOpaqueComputeEmulationTask,
                m_graphicsPrefixGbufferTask
            )
            && graphicsPrefixOpaqueComputeEmulationQueue
            && graphicsPrefixOpaqueComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // A shared persistent generated-vertex output cannot be reduced to an endpoint coalescing check: every
    // alternating callback must remain in exact packet order, or a later local dispatch/raster bridge could
    // silently return. Keep the G-buffer prelude before the contiguous D(A) -> R(A) -> ... run.
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
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_graphicsPrefixGbufferTask,
                m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex];
            const Core::GpuPhysicalQueueInfo* const queue = m_deferredLightingCompiledGraph.queueInfoForTask(task);
            if(
                !task.valid()
                || !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !m_deferredLightingCompiledGraph.tasksSharePacket(
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
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks,
            phaseCount
        ) && m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(
            m_graphicsPrefixGbufferTask,
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u]
        );
    }();
    // Receiver-surface CSG can independently retain the compatibility path, but when its alias-free producer is
    // declared it must share G-buffer's primary Graphics packet for the same compiler-owned UAV-to-VertexBuffer
    // handoff and semantic prefix range.
    const bool graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged =
        !m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
        || (
            taskIsCompiled(m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask,
                m_graphicsPrefixGbufferTask
            )
            && graphicsPrefixOpaqueCsgReceiverComputeEmulationQueue
            && graphicsPrefixOpaqueCsgReceiverComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // Interval-sample emulation starts after Combine, but Combine retains its own semantic timing packet and may
    // safely split at a compiler frontier. The producer/raster pair itself must stay contiguous in one Graphics
    // packet before accepting the timing scope that crosses their command-list recording.
    const bool graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged = [&](){
        if(!m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid())
            return true;
        if(
            !m_graphicsPrefixCsgIntervalCombineTask.valid()
            || !m_graphicsPrefixCsgIntervalSampleTask.valid()
            || !graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationQueue
            || graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationQueue->queueClass
                != Core::CommandQueue::Graphics
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_graphicsPrefixCsgIntervalCombineTask,
                m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask
            )
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask,
                m_graphicsPrefixCsgIntervalSampleTask
            )
        )
            return false;
        const Core::GpuTaskId sequence[] = {
            m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask,
            m_graphicsPrefixCsgIntervalSampleTask,
        };
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            sequence,
            LengthOf(sequence)
        );
    }();
    // The opaque CSG work-region clear keeps its original one-range timing scope in first/last typed primitives.
    // They must remain with G-buffer's rebound Graphics ticket; a split would bind query ownership to a different
    // native submission even though the compiler still preserves resource ordering.
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixCsgIntervalClearQueue =
        m_graphicsPrefixCsgIntervalClearTask.valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(m_graphicsPrefixCsgIntervalClearTask)
            : nullptr
    ;
    const bool graphicsPrefixCsgIntervalClearBundleMerged = !hasOpaqueCsgFrameWork
        ? (!m_graphicsPrefixCsgIntervalClearFirstTask.valid() && !m_graphicsPrefixCsgIntervalClearTask.valid())
        : (
            m_graphicsPrefixCsgIntervalClearFirstTask.valid()
            && m_graphicsPrefixCsgIntervalClearTask.valid()
            && taskIsCompiled(m_graphicsPrefixCsgIntervalClearFirstTask)
            && taskIsCompiled(m_graphicsPrefixCsgIntervalClearTask)
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_graphicsPrefixCsgIntervalClearFirstTask,
                m_graphicsPrefixCsgIntervalClearTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_graphicsPrefixCsgIntervalClearTask,
                m_graphicsPrefixGbufferTask
            )
            && graphicsPrefixCsgIntervalClearQueue
            && graphicsPrefixCsgIntervalClearQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    const Core::GpuPhysicalQueueInfo* const shadowPrepareQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredShadowPrepareTask);
    const Core::GpuPhysicalQueueInfo* const softwareCausticsQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredSoftwareCausticsTask);
    const bool shadowVisibilityRunsOnCompute = shadowVisibilityQueue
        && shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute
    ;
    const bool softwareCausticsRunsOnCompute = !hardwareShadowSupported
        && softwareCausticsQueue
        && softwareCausticsQueue->queueClass == Core::CommandQueue::Compute
    ;
    const bool avboitPrePacketContainsClear = !clearAvboitTargets || (
        m_deferredAvboitClearFirstTask.valid()
        && m_deferredAvboitClearTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitClearFirstTask
        )
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitClearTask
        )
    );
    // Prepared transparent CSG clears the two persistent interval values through the same first/last primitive
    // bracket. Keep both with AVBOIT Pre so its stable timing ticket and accepted endpoint remain authoritative.
    const bool avboitPrePacketContainsTransparentCsgClear =
        (!m_deferredAvboitTransparentCsgIntervalClearFirstTask.valid()
            && !m_deferredAvboitTransparentCsgIntervalClearTask.valid())
        || (
            m_deferredAvboitTransparentCsgIntervalClearFirstTask.valid()
            && m_deferredAvboitTransparentCsgIntervalClearTask.valid()
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitTransparentCsgIntervalClearFirstTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitTransparentCsgIntervalClearTask
            )
        )
    ;
    const bool avboitPrePacketContainsOccupancy = m_deferredLightingCompiledGraph.tasksSharePacket(
        m_deferredAvboitPreTask,
        m_deferredAvboitOccupancyTask
    )
    ;
    // The transparent Span/Combine callbacks consume the frozen CSG stream before phase-local occupancy uploads
    // replace it. They share AVBOIT Pre's timing and external state source, so a split is rejected before recording.
    const bool avboitPrePacketContainsCsgReceiverSpan =
        !m_deferredAvboitCsgReceiverSpanTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitCsgReceiverSpanTask
        )
    ;
    const bool avboitPrePacketContainsCsgIntervalCombine =
        !m_deferredAvboitCsgIntervalCombineTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitCsgIntervalCombineTask
        )
    ;
    const Core::GpuPhysicalQueueInfo* const avboitPreQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitPreTask);
    const Core::GpuPhysicalQueueInfo* const avboitOccupancyQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitOccupancyTask);
    const Core::GpuPhysicalQueueInfo* const avboitOccupancyComputeEmulationQueue =
        m_deferredAvboitOccupancyComputeEmulationTask.valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitOccupancyComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitOccupancySharedComputeEmulationQueue =
        m_deferredAvboitOccupancySharedComputeEmulationTaskCount != 0u
            && m_deferredAvboitOccupancySharedComputeEmulationTasks[0u].valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(
                m_deferredAvboitOccupancySharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    const bool avboitUsesAsyncCompute = m_deferredAvboitDepthWarpTask.valid();
    const bool avboitExtinctionPacketContainsStreams = !m_deferredAvboitExtinctionStreamTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitExtinctionTask,
            m_deferredAvboitExtinctionStreamTask
        )
    ;
    const bool avboitUnsplitPrePacketContainsExtinction = !hasTransparentRenderers
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitExtinctionTask
        )
    ;
    const bool avboitUnsplitPrePacketContainsIntegration = !hasTransparentRenderers
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitIntegrationTask
        )
    ;
    const bool avboitAccumulationPacketContainsStreams = !m_deferredAvboitAccumulationStreamTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitAccumulationTask,
            m_deferredAvboitAccumulationStreamTask
        )
    ;
    // The graph-only finalizer lowers the final attachment transition, so it is part of accumulation's accepted
    // Graphics packet and its timing/submission endpoint. A split here would let Composite bypass that handoff.
    const bool avboitAccumulationPacketContainsFinalizer = !hasTransparentRenderers
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitAccumulationTask,
            m_deferredAvboitAccumulationFinalizeTask
        )
    ;
    const bool avboitUnsplitPrePacketContainsAccumulation = !hasTransparentRenderers
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitAccumulationTask
        )
    ;
    const Core::GpuPhysicalQueueInfo* const avboitDepthWarpQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitDepthWarpTask);
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitExtinctionTask);
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionComputeEmulationQueue =
        m_deferredAvboitExtinctionComputeEmulationTask.valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitExtinctionComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionSharedComputeEmulationQueue =
        m_deferredAvboitExtinctionSharedComputeEmulationTaskCount != 0u
            && m_deferredAvboitExtinctionSharedComputeEmulationTasks[0u].valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(
                m_deferredAvboitExtinctionSharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitIntegrationQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitIntegrationTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitAccumulationTask);
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationComputeEmulationQueue =
        m_deferredAvboitAccumulationComputeEmulationTask.valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredAvboitAccumulationComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationSharedComputeEmulationQueue =
        m_deferredAvboitAccumulationSharedComputeEmulationTaskCount != 0u
            && m_deferredAvboitAccumulationSharedComputeEmulationTasks[0u].valid()
            ? m_deferredLightingCompiledGraph.queueInfoForTask(
                m_deferredAvboitAccumulationSharedComputeEmulationTasks[0u]
            )
            : nullptr
    ;
    // Integration is now a typed unsplit AVBOIT tail instead of native work hidden inside Extinction.  Require the
    // exact Extinction -> Integration boundary and its one-packet Pre handoff before packet recording/submission.
    const bool avboitUnsplitIntegrationTailMerged = [&](){
        if(avboitUsesAsyncCompute || !hasTransparentRenderers)
            return true;
        if(
            !m_deferredAvboitExtinctionTask.valid()
            || !m_deferredAvboitIntegrationTask.valid()
            || !m_deferredAvboitAccumulationTask.valid()
            || !avboitPreQueue
            || !avboitExtinctionQueue
            || !avboitIntegrationQueue
            || !avboitAccumulationQueue
            || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
            || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
            || avboitIntegrationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitExtinctionTask
            )
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitIntegrationTask
            )
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitAccumulationTask
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(
                m_deferredAvboitIntegrationTask,
                m_deferredAvboitAccumulationTask
            )
        )
            return false;
        const Core::GpuTaskId tailSequence[] = {
            m_deferredAvboitExtinctionTask,
            m_deferredAvboitIntegrationTask,
        };
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            tailSequence,
            LengthOf(tailSequence)
        );
    }();
    // Occupancy's measurement spans the generator and raster consumer. Require the exact Pre-packet order so
    // graph declarations, rather than a callback-local transition, own the output UAV-to-VertexBuffer handoff on
    // both the split and unsplit AVBOIT routes.
    const bool avboitOccupancyComputeEmulationMerged = [&](){
        if(!m_deferredAvboitOccupancyComputeEmulationTask.valid())
            return true;
        if(
            !m_deferredAvboitOccupancyStreamTask.valid()
            || !avboitOccupancyComputeEmulationQueue
            || !avboitOccupancyQueue
            || !avboitPreQueue
            || avboitOccupancyComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitOccupancyQueue->queueClass != Core::CommandQueue::Graphics
            || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitOccupancyComputeEmulationTask
            )
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitOccupancyComputeEmulationTask,
                m_deferredAvboitOccupancyTask
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_deferredAvboitOccupancyStreamTask,
                m_deferredAvboitOccupancyComputeEmulationTask
            )
            || (m_deferredAvboitClearTask.valid()
                && !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                    m_deferredAvboitClearTask,
                    m_deferredAvboitOccupancyComputeEmulationTask
                ))
        )
            return false;
        const Core::GpuTaskId producerRasterSequence[] = {
            m_deferredAvboitOccupancyComputeEmulationTask,
            m_deferredAvboitOccupancyTask,
        };
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            producerRasterSequence,
            LengthOf(producerRasterSequence)
        ) && optionalTaskPrecedesInSharedPacket(
            m_deferredAvboitOccupancyStreamTask,
            m_deferredAvboitOccupancyComputeEmulationTask
        ) && optionalTaskPrecedesInSharedPacket(
            m_deferredAvboitClearTask,
            m_deferredAvboitOccupancyComputeEmulationTask
        );
    }();
    // A shared output needs more than the ordinary producer/raster endpoint check: every alternating D/R callback
    // must be contiguous in AVBOIT Pre's one Graphics packet, or a later local bridge could overwrite the retained
    // output between the compiler-owned UAV and VertexBuffer phases.
    const bool avboitOccupancySharedComputeEmulationMerged = [&](){
        const usize phaseCount = m_deferredAvboitOccupancySharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : m_deferredAvboitOccupancySharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(
            !ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount)
            || m_deferredAvboitOccupancyComputeEmulationTask.valid()
            || !m_deferredAvboitOccupancyStreamTask.valid()
            || !m_deferredAvboitClearTask.valid()
            || !taskIsCompiled(m_deferredAvboitPreTask)
            || !avboitOccupancySharedComputeEmulationQueue
            || !avboitOccupancyQueue
            || !avboitPreQueue
            || avboitUsesAsyncCompute
            || avboitOccupancySharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitOccupancyQueue->queueClass != Core::CommandQueue::Graphics
            || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
            || m_deferredAvboitOccupancyTask
                != m_deferredAvboitOccupancySharedComputeEmulationTasks[phaseCount - 1u]
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitOccupancySharedComputeEmulationTasks[0u]
            )
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitOccupancyStreamTask
            )
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitClearTask
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_deferredAvboitOccupancyStreamTask,
                m_deferredAvboitOccupancySharedComputeEmulationTasks[0u]
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_deferredAvboitClearTask,
                m_deferredAvboitOccupancySharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = m_deferredAvboitOccupancySharedComputeEmulationTasks[phaseIndex];
            if(!task.valid())
                return false;
            const Core::GpuPhysicalQueueInfo* const queue = m_deferredLightingCompiledGraph.queueInfoForTask(task);
            if(
                !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredAvboitOccupancySharedComputeEmulationTasks[0u],
                    task
                )
            )
                return false;
        }
        for(usize phaseIndex = phaseCount;
            phaseIndex < LengthOf(m_deferredAvboitOccupancySharedComputeEmulationTasks);
            ++phaseIndex
        ){
            if(m_deferredAvboitOccupancySharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            m_deferredAvboitOccupancySharedComputeEmulationTasks,
            phaseCount
        ) && m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitOccupancyStreamTask
        ) && m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(
            m_deferredAvboitOccupancyStreamTask,
            m_deferredAvboitClearTask
        ) && m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(
            m_deferredAvboitClearTask,
            m_deferredAvboitOccupancySharedComputeEmulationTasks[0u]
        );
    }();
    const bool avboitOccupancyAllComputeEmulationMerged =
        avboitOccupancyComputeEmulationMerged
        && avboitOccupancySharedComputeEmulationMerged
    ;
    // Extinction's measurement spans the generator and raster consumer. Require their exact packet order so the
    // graph, rather than a callback-local transition, owns the output UAV-to-VertexBuffer handoff on both routes.
    const bool avboitExtinctionComputeEmulationMerged = [&](){
        if(!m_deferredAvboitExtinctionComputeEmulationTask.valid())
            return true;
        if(
            !m_deferredAvboitExtinctionStreamTask.valid()
            || !avboitExtinctionComputeEmulationQueue
            || !avboitExtinctionQueue
            || avboitExtinctionComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitExtinctionComputeEmulationTask,
                m_deferredAvboitExtinctionTask
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_deferredAvboitExtinctionStreamTask,
                m_deferredAvboitExtinctionComputeEmulationTask
            )
            || (!avboitUsesAsyncCompute && (
                !taskIsCompiled(m_deferredAvboitPreTask)
                || !avboitPreQueue
                || !avboitOccupancyQueue
                || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
                || avboitOccupancyQueue->queueClass != Core::CommandQueue::Graphics
                || !m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredAvboitPreTask,
                    m_deferredAvboitExtinctionComputeEmulationTask
                )
                || !m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredAvboitOccupancyTask,
                    m_deferredAvboitExtinctionComputeEmulationTask
                )
            ))
        )
            return false;
        const Core::GpuTaskId producerRasterSequence[] = {
            m_deferredAvboitExtinctionComputeEmulationTask,
            m_deferredAvboitExtinctionTask,
        };
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            producerRasterSequence,
            LengthOf(producerRasterSequence)
        ) && optionalTaskPrecedesInSharedPacket(
            m_deferredAvboitExtinctionStreamTask,
            m_deferredAvboitExtinctionComputeEmulationTask
        ) && (avboitUsesAsyncCompute || (
            optionalTaskPrecedesInSharedPacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitExtinctionComputeEmulationTask
            ) && optionalTaskPrecedesInSharedPacket(
                m_deferredAvboitOccupancyTask,
                m_deferredAvboitExtinctionComputeEmulationTask
            )
        ));
    }();
    // A shared generated-vertex output requires the exact alternating D/R chain to remain contiguous in AVBOIT
    // Pre's one Graphics packet. Otherwise an intervening dispatch could overwrite the retained output before its
    // corresponding raster phase consumes the compiler-owned VertexBuffer handoff.
    const bool avboitExtinctionSharedComputeEmulationMerged = [&](){
        const usize phaseCount = m_deferredAvboitExtinctionSharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : m_deferredAvboitExtinctionSharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(
            !ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount)
            || m_deferredAvboitExtinctionComputeEmulationTask.valid()
            || !m_deferredAvboitExtinctionStreamTask.valid()
            || !taskIsCompiled(m_deferredAvboitExtinctionTask)
            || !taskIsCompiled(m_deferredAvboitIntegrationTask)
            || !taskIsCompiled(m_deferredAvboitPreTask)
            || !avboitExtinctionSharedComputeEmulationQueue
            || !avboitExtinctionQueue
            || !avboitIntegrationQueue
            || !avboitPreQueue
            || avboitUsesAsyncCompute
            || avboitExtinctionSharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
            || avboitIntegrationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
            || m_deferredAvboitExtinctionTask
                != m_deferredAvboitExtinctionSharedComputeEmulationTasks[phaseCount - 1u]
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitExtinctionSharedComputeEmulationTasks[0u]
            )
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitIntegrationTask
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_deferredAvboitExtinctionStreamTask,
                m_deferredAvboitExtinctionSharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = m_deferredAvboitExtinctionSharedComputeEmulationTasks[phaseIndex];
            if(!task.valid())
                return false;
            const Core::GpuPhysicalQueueInfo* const queue = m_deferredLightingCompiledGraph.queueInfoForTask(task);
            if(
                !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredAvboitExtinctionSharedComputeEmulationTasks[0u],
                    task
                )
            )
                return false;
        }
        for(usize phaseIndex = phaseCount;
            phaseIndex < LengthOf(m_deferredAvboitExtinctionSharedComputeEmulationTasks);
            ++phaseIndex
        ){
            if(m_deferredAvboitExtinctionSharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        const Core::GpuTaskId integrationBoundary[] = {
            m_deferredAvboitExtinctionSharedComputeEmulationTasks[phaseCount - 1u],
            m_deferredAvboitIntegrationTask,
        };
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            m_deferredAvboitExtinctionSharedComputeEmulationTasks,
            phaseCount
        ) && m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            integrationBoundary,
            LengthOf(integrationBoundary)
        ) && m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitExtinctionSharedComputeEmulationTasks[0u]
        ) && optionalTaskPrecedesInSharedPacket(
            m_deferredAvboitExtinctionStreamTask,
            m_deferredAvboitExtinctionSharedComputeEmulationTasks[0u]
        );
    }();
    const bool avboitExtinctionAllComputeEmulationMerged =
        avboitExtinctionComputeEmulationMerged
        && avboitExtinctionSharedComputeEmulationMerged
    ;
    // Accumulation's measurement spans the generator and raster consumer. Require their exact packet order so the
    // graph, rather than a callback-local transition, owns the output UAV-to-VertexBuffer handoff before the
    // following graph-owned attachment finalizer. The unsplit route additionally keeps this chain in AVBOIT Pre's
    // single accepting Graphics packet and timing endpoint.
    const bool avboitAccumulationComputeEmulationMerged = [&](){
        if(!m_deferredAvboitAccumulationComputeEmulationTask.valid())
            return true;
        if(
            !m_deferredAvboitAccumulationStreamTask.valid()
            || !avboitAccumulationComputeEmulationQueue
            || !avboitAccumulationQueue
            || avboitAccumulationComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitAccumulationComputeEmulationTask,
                m_deferredAvboitAccumulationTask
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_deferredAvboitAccumulationStreamTask,
                m_deferredAvboitAccumulationComputeEmulationTask
            )
            || (!avboitUsesAsyncCompute && (
                !taskIsCompiled(m_deferredAvboitPreTask)
                || !avboitPreQueue
                || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
                || !m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredAvboitPreTask,
                    m_deferredAvboitAccumulationComputeEmulationTask
                )
            ))
        )
            return false;
        const Core::GpuTaskId producerRasterSequence[] = {
            m_deferredAvboitAccumulationComputeEmulationTask,
            m_deferredAvboitAccumulationTask,
        };
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            producerRasterSequence,
            LengthOf(producerRasterSequence)
        ) && optionalTaskPrecedesInSharedPacket(
            m_deferredAvboitAccumulationStreamTask,
            m_deferredAvboitAccumulationComputeEmulationTask
        ) && (avboitUsesAsyncCompute || optionalTaskPrecedesInSharedPacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitAccumulationComputeEmulationTask
        ));
    }();
    // A shared output needs more than the ordinary producer/raster endpoint check: every alternating D/R callback
    // must be contiguous in AVBOIT Pre's one Graphics packet, or a later local bridge could overwrite the retained
    // output between the compiler-owned UAV and VertexBuffer phases.
    const bool avboitAccumulationSharedComputeEmulationMerged = [&](){
        const usize phaseCount = m_deferredAvboitAccumulationSharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : m_deferredAvboitAccumulationSharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(
            !ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount)
            || m_deferredAvboitAccumulationComputeEmulationTask.valid()
            || !m_deferredAvboitAccumulationStreamTask.valid()
            || !taskIsCompiled(m_deferredAvboitAccumulationTask)
            || !m_deferredAvboitAccumulationFinalizeTask.valid()
            || !taskIsCompiled(m_deferredAvboitPreTask)
            || !avboitAccumulationSharedComputeEmulationQueue
            || !avboitAccumulationQueue
            || !avboitPreQueue
            || avboitUsesAsyncCompute
            || avboitAccumulationSharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
            || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
            || m_deferredAvboitAccumulationTask
                != m_deferredAvboitAccumulationSharedComputeEmulationTasks[phaseCount - 1u]
            || !m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredAvboitPreTask,
                m_deferredAvboitAccumulationSharedComputeEmulationTasks[0u]
            )
            || !m_deferredLightingCompiledGraph.taskPrecedesOrSharesPacket(
                m_deferredAvboitAccumulationStreamTask,
                m_deferredAvboitAccumulationSharedComputeEmulationTasks[0u]
            )
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = m_deferredAvboitAccumulationSharedComputeEmulationTasks[phaseIndex];
            if(!task.valid())
                return false;
            const Core::GpuPhysicalQueueInfo* const queue = m_deferredLightingCompiledGraph.queueInfoForTask(task);
            if(
                !queue
                || queue->queueClass != Core::CommandQueue::Graphics
                || !m_deferredLightingCompiledGraph.tasksSharePacket(
                    m_deferredAvboitAccumulationSharedComputeEmulationTasks[0u],
                    task
                )
            )
                return false;
        }
        for(usize phaseIndex = phaseCount;
            phaseIndex < LengthOf(m_deferredAvboitAccumulationSharedComputeEmulationTasks);
            ++phaseIndex
        ){
            if(m_deferredAvboitAccumulationSharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        const Core::GpuTaskId finalizerBoundary[] = {
            m_deferredAvboitAccumulationSharedComputeEmulationTasks[phaseCount - 1u],
            m_deferredAvboitAccumulationFinalizeTask,
        };
        return m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            m_deferredAvboitAccumulationSharedComputeEmulationTasks,
            phaseCount
        ) && m_deferredLightingCompiledGraph.tasksFormContiguousPacketSequence(
            finalizerBoundary,
            LengthOf(finalizerBoundary)
        ) && m_deferredLightingCompiledGraph.taskPrecedesInSamePacket(
            m_deferredAvboitPreTask,
            m_deferredAvboitAccumulationSharedComputeEmulationTasks[0u]
        ) && optionalTaskPrecedesInSharedPacket(
            m_deferredAvboitAccumulationStreamTask,
            m_deferredAvboitAccumulationSharedComputeEmulationTasks[0u]
        );
    }();
    const bool avboitAccumulationAllComputeEmulationMerged =
        avboitAccumulationComputeEmulationMerged
        && avboitAccumulationSharedComputeEmulationMerged
    ;
    const Core::GpuTaskId causticsTask = hardwareShadowSupported
        ? m_deferredHardwareCausticsTask
        : m_deferredSoftwareCausticsTask
    ;
    // Deferred Lighting's submission range starts at the Lighting packet. A fresh history-selector upload must
    // compile into that exact packet, or it would be recorded but omitted from its external wait and acceptance.
    const bool laggedLightingHistorySlotsUploadMergedIntoLightingPacket =
        !m_deferredLaggedLightingHistorySlotsUploadTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredLightingTask,
            m_deferredLaggedLightingHistorySlotsUploadTask
        )
    ;
    // Presentation is the sole renderer policy that needs the exact compiler packet: it owns the binary swap-chain
    // signal. Resolve every signal/hook/token/queue decision through the compiler-owned endpoint instead of
    // mirroring the FrameTimingEnd task's packet identity in renderer policy.
    const Core::GpuCompiledPresentEndpoint* const presentationEndpoint =
        m_deferredLightingCompiledGraph.presentEndpoint();
    const Core::GpuTaskId terminalPresentationTask = presentationEndpoint
        ? presentationEndpoint->producer
        : Core::GpuTaskId{}
    ;
    const Core::GpuSubmissionPacketId terminalPresentationPacket = presentationEndpoint
        ? presentationEndpoint->packet
        : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuPhysicalQueueInfo* const deferredLightingQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredLightingTask);
    const Core::GpuPhysicalQueueInfo* const deferredCompositeQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredCompositeTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredPresentTask);
    const Core::GpuPhysicalQueueInfo* const deferredPresentationOverlayQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredPresentationOverlayTask);
    const Core::GpuPhysicalQueueInfo* const terminalPresentationQueue = presentationEndpoint
        ? m_deferredLightingCompiledGraph.queueInfo(presentationEndpoint->queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const deferredLaggedLightingHistoryQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredLaggedLightingHistoryTask);
    const Core::GpuPhysicalQueueInfo* const deferredFrameRecoveryQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredFrameRecoveryTask);
    const Core::GpuPhysicalQueueInfo* const hardwareCausticsQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredHardwareCausticsTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredSurfelGiTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiPreparationQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredSurfelGiPreparationTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiSnapshotCopyQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredSurfelGiSnapshotCopyTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiCounterReadbackQueue =
        m_deferredLightingCompiledGraph.queueInfoForTask(m_deferredSurfelGiCounterReadbackTask);
    const bool surfelGiRunsOnCompute = surfelGiQueue && surfelGiQueue->queueClass == Core::CommandQueue::Compute;
    // The clear must remain in GI's semantic packet. If it split, the standard effects range would either gain a
    // hidden submission or record an output write outside the acceptance/timing endpoint it protects.
    const bool surfelGiOutputClearMergedIntoGiPacket =
        m_deferredSurfelGiIrradianceClearTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredSurfelGiIrradianceClearTask,
            m_deferredSurfelGiTask
        )
    ;
    // A prepared Surfel GI frame splits age/free, its per-frame cell-head reset, hash build, Spawn, trace-build-
    // args, trace, and resolve from the final upsample. Every callback must still share the semantic GI packet,
    // including one async timing interval and the existing effects acceptance endpoint. A missing prefix denotes
    // compatibility callback.
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
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiAgeFreeTask,
                m_deferredSurfelGiTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiCellHeadClearTask,
                m_deferredSurfelGiTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiHashBuildTask,
                m_deferredSurfelGiTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiSpawnTask,
                m_deferredSurfelGiTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiTraceBuildArgsTask,
                m_deferredSurfelGiTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiTraceTask,
                m_deferredSurfelGiTask
            )
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiResolveTask,
                m_deferredSurfelGiTask
            )
        )
    ;
    // Persistent first-use initialization has four typed clear tasks followed by a resource-free lifecycle task.
    // The range/state anchor stays on the first clear, so require the lifecycle tail to share its packet before
    // accepting the normal graph path; otherwise an accepted clear could be omitted from that semantic prefix.
    const bool surfelGiInitializationLifecycleMergedIntoPreparationPacket =
        !m_deferredSurfelGiInitializationLifecycleTask.valid()
        || (
            m_deferredSurfelGiPreparationTask.valid()
            && m_deferredLightingCompiledGraph.tasksSharePacket(
                m_deferredSurfelGiPreparationTask,
                m_deferredSurfelGiInitializationLifecycleTask
            )
        )
    ;
    // Both caustic routes retain a black output on a no-producer frame. Keep the typed clear with the selected
    // semantic producer task so effects timing, acceptance, and the lagged-history wait still protect the first
    // write without mirroring its compiler packet.
    // Photon, geometry, resolve prepare, five wavelets, upsample, and timing close are distinct callbacks so the
    // compiler can lower their immutable and ping-pong UAV-to-SRV handoffs. They remain one semantic
    // submission: clear acceptance, timing, and all dependent effects keep the established packet endpoint.
    const bool causticPhotonMergedIntoCausticsPacket =
        m_deferredCausticPhotonTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticPhotonTask,
            causticsTask
        )
    ;
    const bool causticGeometryMergedIntoCausticsPacket =
        m_deferredCausticGeometryTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticGeometryTask,
            causticsTask
        )
    ;
    const bool causticResolvePrepareMergedIntoCausticsPacket =
        m_deferredCausticResolvePrepareTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticResolvePrepareTask,
            causticsTask
        )
    ;
    const bool causticResolveWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveWaveletTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticResolveWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveSecondWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveSecondWaveletTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticResolveSecondWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveThirdWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveThirdWaveletTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticResolveThirdWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveFourthWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveFourthWaveletTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticResolveFourthWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveFifthWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveFifthWaveletTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticResolveFifthWaveletTask,
            causticsTask
        )
    ;
    const bool causticResolveUpsampleMergedIntoCausticsPacket =
        m_deferredCausticResolveUpsampleTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticResolveUpsampleTask,
            causticsTask
        )
    ;
    const bool causticIrradianceClearMergedIntoCausticsPacket =
        m_deferredCausticIrradianceClearTask.valid()
        && m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticIrradianceClearTask,
            causticsTask
        )
    ;
    // Non-temporal accumulation resets every frame through a typed graph clear before its selected producer. The
    // producer commits the matching CPU reset only after that shared packet accepts.
    const bool causticAccumulatorNonTemporalClearMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorNonTemporalClearTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticAccumulatorNonTemporalClearTask,
            causticsTask
        )
    ;
    // A fresh temporal accumulator is zeroed by a typed graph clear before its selected producer. Like the
    // irradiance clear, it must remain in that producer packet so the accepted callback is the sole publisher of
    // the initialized mirror and no hidden submission can write the accumulator.
    const bool causticAccumulatorBootstrapClearMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorBootstrapClearTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticAccumulatorBootstrapClearTask,
            causticsTask
        )
    ;
    // A warm temporal accumulator decays in the same selected caustics packet.  If it split, the photon timing
    // scope could span an unsubmitted packet and the compiler-owned UAV dependency would no longer protect the
    // following atomic producer.
    const bool causticAccumulatorDecayMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorDecayTask.valid()
        || m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredCausticAccumulatorDecayTask,
            causticsTask
        )
    ;
    // Keep every recording and submission span derived from semantic task endpoints. Compiler packet identities
    // remain below for validation, accepted-token lookup, and the terminal presentation signal only.
    const Core::GpuSubmissionPacketRange shadowPreparePacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(m_deferredShadowPrepareTask, m_deferredShadowPrepareTask);
    const Core::GpuSubmissionPacketRange graphicsPrefixWorkPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(
            m_graphicsPrefixMeshViewSetupTask,
            m_graphicsPrefixTask
        );
    const Core::GpuSubmissionPacketRange graphicsPrefixPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(m_graphicsPrefixTask, m_graphicsPrefixTask);
    const Core::GpuSubmissionPacketRange shadowPrepareThroughPrefixPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(m_deferredShadowPrepareTask, m_graphicsPrefixTask);
    const Core::GpuSubmissionPacketRange shadowEffectsPacketRange = m_deferredLightingCompiledGraph.packetRangeForTasks(
        m_deferredShadowVisibilityTask,
        hardwareShadowSupported ? m_deferredShadowVisibilityTask : m_deferredSoftwareCausticsTask
    );
    const Core::GpuTaskId surfelGiFirstTask = m_deferredSurfelGiPreparationTask.valid()
        ? m_deferredSurfelGiPreparationTask
        : m_deferredSurfelGiTask
    ;
    const Core::GpuSubmissionPacketRange surfelGiPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(surfelGiFirstTask, m_deferredSurfelGiTask);
    const usize expectedSurfelGiPacketCount = m_deferredSurfelGiSnapshotCopyTask.valid()
        ? (m_deferredLightingCompiledGraph.tasksSharePacket(
            m_deferredSurfelGiPreparationTask,
            m_deferredSurfelGiSnapshotCopyTask
        )
            ? __hidden_renderer_system::s_SurfelGiMergedPreparationAndCopyPacketCount
            : __hidden_renderer_system::s_SurfelGiSeparatePreparationAndCopyPacketCount)
        : __hidden_renderer_system::s_SinglePacketCount
    ;
    const Core::GpuSubmissionPacketRange hardwareCausticsPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(
            m_deferredHardwareCausticsTask,
            m_deferredHardwareCausticsTask
        );
    const Core::GpuTaskId avboitUnsplitFinalTask = hasTransparentRenderers
        ? m_deferredAvboitAccumulationFinalizeTask
        : m_deferredAvboitPreTask
    ;
    const Core::GpuSubmissionPacketRange avboitPacketRange = m_deferredLightingCompiledGraph.packetRangeForTasks(
        m_deferredAvboitPreTask,
        avboitUsesAsyncCompute ? m_deferredAvboitAccumulationFinalizeTask : avboitUnsplitFinalTask
    );
    const Core::GpuSubmissionPacketRange deferredLightingCompositePacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(m_deferredLightingTask, m_deferredCompositeTask);
    const Core::GpuSubmissionPacketRange deferredPresentPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(m_deferredPresentTask, m_deferredPresentTask);
    const Core::GpuSubmissionPacketRange terminalPresentationPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(m_deferredPresentTask, terminalPresentationTask);
    const Core::GpuSubmissionPacketRange effectsThroughPresentationPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(
            m_deferredShadowVisibilityTask,
            terminalPresentationTask
        );
    const Core::GpuSubmissionPacketRange deferredNormalPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(m_deferredShadowPrepareTask, terminalPresentationTask);
    const Core::GpuTaskId deferredTailFirstTask = m_deferredSurfelGiCounterReadbackTask.valid()
        ? m_deferredSurfelGiCounterReadbackTask
        : (captureLaggedLightingHistory ? m_deferredLaggedLightingHistoryTask : m_deferredFrameRecoveryTask)
    ;
    const Core::GpuSubmissionPacketRange deferredTailPacketRange = m_deferredLightingCompiledGraph.packetRangeForTasks(
        deferredTailFirstTask,
        m_deferredFrameRecoveryTask
    );
    const Core::GpuSubmissionPacketRange deferredFullPacketRange =
        m_deferredLightingCompiledGraph.packetRangeForTasks(
            m_deferredShadowPrepareTask,
            m_deferredFrameRecoveryTask
        );
    const usize expectedAvboitPacketCount = avboitUsesAsyncCompute
        ? __hidden_renderer_system::s_AvboitAsyncComputePacketCount
        : __hidden_renderer_system::s_SinglePacketCount;
    const usize expectedDeferredTailPacketCount = __hidden_renderer_system::s_SinglePacketCount
        + (m_deferredSurfelGiCounterReadbackTask.valid()
            ? __hidden_renderer_system::s_SinglePacketCount
            : 0u)
        + (captureLaggedLightingHistory ? __hidden_renderer_system::s_SinglePacketCount : 0u)
    ;
    // A presentation contributor may declare graph-owned setup uploads between Deferred Present and its terminal
    // Graphics overlay. The renderer requires the scene Present endpoint, a separate final timing/signal endpoint,
    // and (when requested) one overlay packet, while the compiler owns intervening upload packet routing.
    const usize minimumTerminalPresentationPacketCount = __hidden_renderer_system::s_SinglePacketCount
        + __hidden_renderer_system::s_SinglePacketCount
        + (m_deferredPresentationOverlayRequired ? __hidden_renderer_system::s_PresentationOverlayPacketCount : 0u);
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
        || !m_graphicsPrefixDeferredClearFirstTask.valid()
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
        || !taskIsCompiled(m_graphicsPrefixDeferredClearFirstTask)
        || !taskIsCompiled(m_graphicsPrefixDeferredClearTask)
        || !taskIsCompiled(m_graphicsPrefixGbufferTask)
        || (hasOpaqueCsgFrameWork && (
            !m_deferredLightingCompiledGraph.findTask(m_graphicsPrefixCsgReceiverSpanTask)
            || !m_deferredLightingCompiledGraph.findTask(m_graphicsPrefixCsgIntervalCombineTask)
            || !m_deferredLightingCompiledGraph.findTask(m_graphicsPrefixCsgIntervalSampleTask)
        ))
        || !taskIsCompiled(m_graphicsPrefixTask)
        || !graphicsPrefixTimingBindingsValid
        || !graphicsPrefixPacketsAreGraphics
        || !graphicsPrefixDeferredClearBundleMerged
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
        || (hardwareShadowSupported && (
            !m_deferredHardwareCausticsTask.valid()
            || !taskIsCompiled(m_deferredHardwareCausticsTask)
            || !hardwareCausticsQueue
            || hardwareCausticsQueue->queueClass != Core::CommandQueue::Graphics
        ))
        || !m_deferredAvboitPreTask.valid()
        || !m_deferredAvboitOccupancyTask.valid()
        || !taskIsCompiled(m_deferredAvboitPreTask)
        || !avboitPrePacketContainsClear
        || !avboitPrePacketContainsTransparentCsgClear
        || !avboitPrePacketContainsCsgReceiverSpan
        || !avboitPrePacketContainsCsgIntervalCombine
        || !avboitPrePacketContainsOccupancy
        || !avboitOccupancyAllComputeEmulationMerged
        || !avboitExtinctionPacketContainsStreams
        || !avboitExtinctionAllComputeEmulationMerged
        || !avboitUnsplitIntegrationTailMerged
        || !avboitAccumulationAllComputeEmulationMerged
        || !avboitAccumulationPacketContainsStreams
        || !avboitAccumulationPacketContainsFinalizer
        || (hasTransparentRenderers && (
            !m_deferredAvboitExtinctionTask.valid()
            || !m_deferredAvboitAccumulationTask.valid()
            || !m_deferredAvboitAccumulationFinalizeTask.valid()
            || !taskIsCompiled(m_deferredAvboitExtinctionTask)
            || !taskIsCompiled(m_deferredAvboitAccumulationTask)
            || !m_deferredLightingCompiledGraph.findTask(m_deferredAvboitAccumulationFinalizeTask)
            || (!avboitUsesAsyncCompute && (
                !avboitUnsplitPrePacketContainsExtinction
                || !avboitUnsplitPrePacketContainsIntegration
                || !avboitUnsplitPrePacketContainsAccumulation
            ))
        ))
        || !avboitPreQueue
        || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
        || (avboitUsesAsyncCompute && (
            !m_deferredAvboitDepthWarpTask.valid()
            || !m_deferredAvboitExtinctionTask.valid()
            || !m_deferredAvboitIntegrationTask.valid()
            || !m_deferredAvboitAccumulationTask.valid()
            || !taskIsCompiled(m_deferredAvboitDepthWarpTask)
            || !taskIsCompiled(m_deferredAvboitExtinctionTask)
            || !taskIsCompiled(m_deferredAvboitIntegrationTask)
            || !taskIsCompiled(m_deferredAvboitAccumulationTask)
            || !avboitDepthWarpQueue
            || !avboitExtinctionQueue
            || !avboitIntegrationQueue
            || !avboitAccumulationQueue
            || avboitDepthWarpQueue->queueClass != Core::CommandQueue::Compute
            || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
            || avboitIntegrationQueue->queueClass != Core::CommandQueue::Compute
            || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
        ))
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
        || (laggedLightingHistoryCompletionRequired && !m_deferredLightingHistoryCompletion.valid())
        || !taskIsCompiled(m_deferredLightingTask)
        || !laggedLightingHistorySlotsUploadMergedIntoLightingPacket
        || !taskIsCompiled(m_deferredCompositeTask)
        || !taskIsCompiled(m_deferredPresentTask)
        || !taskIsCompiled(m_deferredFrameTimingEndTask)
        || (m_deferredPresentationOverlayRequired && !taskIsCompiled(m_deferredPresentationOverlayTask))
        || !presentationEndpoint
        || !presentationEndpoint->valid()
        || presentationEndpoint->producer != m_deferredFrameTimingEndTask
        || !m_deferredLightingTaskGraph.validResource(presentationEndpoint->backBuffer)
        || !terminalPresentationPacket.valid()
        || !taskIsCompiled(m_deferredFrameRecoveryTask)
        || !deferredLightingQueue
        || !deferredCompositeQueue
        || !deferredPresentQueue
        || (m_deferredPresentationOverlayRequired && !deferredPresentationOverlayQueue)
        || !terminalPresentationQueue
        || !deferredFrameRecoveryQueue
        // The acquired swap-chain semaphore is attached to the primary physical Graphics transport. The task
        // graph may use an explicitly opted-in auxiliary Graphics queue for ordinary work, but it must not route
        // a backbuffer writer or its final presentation signal without an acquired-image/share contract for it.
        || !primaryGraphicsQueue.valid()
        || presentationEndpoint->queue != primaryGraphicsQueue
        || deferredPresentQueue->id != primaryGraphicsQueue
        || (m_deferredPresentationOverlayRequired
            && deferredPresentationOverlayQueue->id != primaryGraphicsQueue)
        || terminalPresentationQueue->id != primaryGraphicsQueue
        || !shadowPreparePacketRange.valid()
        || shadowPreparePacketRange.packetCount != __hidden_renderer_system::s_SinglePacketCount
        || !graphicsPrefixWorkPacketRange.valid()
        || graphicsPrefixWorkPacketRange.packetCount < graphicsPrefixUniquePacketCount
        || !graphicsPrefixPacketRange.valid()
        || graphicsPrefixPacketRange.packetCount != __hidden_renderer_system::s_SinglePacketCount
        || !shadowPrepareThroughPrefixPacketRange.valid()
        || shadowPrepareThroughPrefixPacketRange.packetCount
            != shadowPreparePacketRange.packetCount + graphicsPrefixWorkPacketRange.packetCount
        || !shadowEffectsPacketRange.valid()
        || shadowEffectsPacketRange.packetCount != (hardwareShadowSupported
            ? __hidden_renderer_system::s_SinglePacketCount
            : __hidden_renderer_system::s_SoftwareShadowEffectsPacketCount)
        || !surfelGiPacketRange.valid()
        || surfelGiPacketRange.packetCount != expectedSurfelGiPacketCount
        || (hardwareShadowSupported && (
            !hardwareCausticsPacketRange.valid()
            || hardwareCausticsPacketRange.packetCount != __hidden_renderer_system::s_SinglePacketCount
        ))
        || !avboitPacketRange.valid()
        || avboitPacketRange.packetCount != expectedAvboitPacketCount
        || !deferredLightingCompositePacketRange.valid()
        || deferredLightingCompositePacketRange.packetCount
            != __hidden_renderer_system::s_DeferredLightingCompositePacketCount
        || !deferredPresentPacketRange.valid()
        || deferredPresentPacketRange.packetCount != __hidden_renderer_system::s_SinglePacketCount
        || !terminalPresentationPacketRange.valid()
        || terminalPresentationPacketRange.packetCount < minimumTerminalPresentationPacketCount
        || !effectsThroughPresentationPacketRange.valid()
        || !deferredNormalPacketRange.valid()
        || deferredNormalPacketRange.packetCount
            != shadowPrepareThroughPrefixPacketRange.packetCount + effectsThroughPresentationPacketRange.packetCount
        || !deferredTailPacketRange.valid()
        || deferredTailPacketRange.packetCount != expectedDeferredTailPacketCount
        || !deferredFullPacketRange.valid()
        || deferredFullPacketRange.packetCount != m_deferredLightingCompiledGraph.packetCount()
        || deferredFullPacketRange.packetCount
            != deferredNormalPacketRange.packetCount + deferredTailPacketRange.packetCount
        || (laggedAsyncLightingSchedule && deferredLightingQueue->queueClass != Core::CommandQueue::Compute)
        || (laggedAsyncLightingSchedule && deferredCompositeQueue->queueClass != Core::CommandQueue::Graphics)
        || deferredPresentQueue->queueClass != Core::CommandQueue::Graphics
        || (m_deferredPresentationOverlayRequired
            && deferredPresentationOverlayQueue->queueClass != Core::CommandQueue::Graphics)
        || terminalPresentationQueue->queueClass != Core::CommandQueue::Graphics
        || deferredFrameRecoveryQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned prefix/effects/deferred packet chain was unavailable"));
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
        // Immutable scene-light preparation happens during graph declaration so it can be copied into graph-owned
        // blobs. No packet has been accepted on this early path; restore its CPU-only classification exactly.
        m_rayTracingState.m_shadowSlotCount = postGbufferPacketCpuState.shadowSlotCount;
        m_rayTracingState.m_softShadowSlotMask = postGbufferPacketCpuState.softShadowSlotMask;
        m_rayTracingState.m_causticLightCount = postGbufferPacketCpuState.causticLightCount;
        m_rayTracingState.m_causticEmissionGateLogged = postGbufferPacketCpuState.causticEmissionGateLogged;
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
        if(asyncPrefixTiming){
            asyncPrefixTiming->discardTiming();
            asyncPrefixTiming.reset();
        }
        if(deferredClearTiming){
            deferredClearTiming->discardTiming();
            deferredClearTiming.reset();
        }
        if(asyncFinalTiming){
            asyncFinalTiming->discardTiming();
            asyncFinalTiming.reset();
        }
        if(causticPhotonTiming){
            causticPhotonTiming->discardTiming();
            causticPhotonTiming.reset();
        }
        if(causticResolveTiming){
            causticResolveTiming->discardTiming();
            causticResolveTiming.reset();
        }
        if(transparentCsgIntervalsTiming){
            transparentCsgIntervalsTiming->discardTiming();
            transparentCsgIntervalsTiming.reset();
        }
        if(avboitOccupancyComputeEmulationTiming){
            avboitOccupancyComputeEmulationTiming->discardTiming();
            avboitOccupancyComputeEmulationTiming.reset();
        }
        if(avboitExtinctionComputeEmulationTiming){
            avboitExtinctionComputeEmulationTiming->discardTiming();
            avboitExtinctionComputeEmulationTiming.reset();
        }
        if(avboitAccumulationComputeEmulationTiming){
            avboitAccumulationComputeEmulationTiming->discardTiming();
            avboitAccumulationComputeEmulationTiming.reset();
        }
        if(opaqueRegularSharedComputeEmulationTiming){
            opaqueRegularSharedComputeEmulationTiming->discardTiming();
            opaqueRegularSharedComputeEmulationTiming.reset();
        }
        if(opaqueCsgIntervalSampleComputeEmulationTiming){
            opaqueCsgIntervalSampleComputeEmulationTiming->discardTiming();
            opaqueCsgIntervalSampleComputeEmulationTiming.reset();
        }
        frameTimingTransaction.discard();
        discardTimingTickets();
        if(!discardUnacceptedGraphPackets()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred graph cancellation overlapped active native work; requesting device recreation"));
            m_graphics.requestDeviceRecreation();
        }
        const bool shadowPrepareAccepted = taskIsCompiled(m_deferredShadowPrepareTask)
            && m_deferredLightingSubmissionTransaction.taskToken(
                m_deferredLightingCompiledGraph,
                m_deferredShadowPrepareTask
            ).valid()
        ;
        restorePostGbufferPacketCpuState(!shadowPrepareAccepted);
        m_raytracingSystem.discardSoftShadowTemporalHistory();
        resetAbandonedFrameStateHandoffs();
    };

    const auto appendDeclaredStateSource = [](
        Core::GpuExternalPacketStateSource* const sources,
        const usize capacity,
        usize& sourceCount,
        const Core::CommandListResourceStateHandoff* const states
    ){
        if(!states || !states->valid() || sourceCount >= capacity)
            return false;
        sources[sourceCount++] = Core::GpuExternalPacketStateSource{
            .states = states,
        };
        return true;
    };
    const auto appendTaskPacketStateBinding = [](
        Core::GpuTaskPacketStateBinding* const bindings,
        const usize capacity,
        usize& bindingCount,
        const Core::GpuTaskId task,
        const Core::GpuExternalPacketStateSource* const sources,
        const usize sourceCount
    ){
        if(
            !task.valid()
            || !sources
            || sourceCount == 0u
            || bindingCount >= capacity
        )
            return false;
        bindings[bindingCount++] = Core::GpuTaskPacketStateBinding{
            .task = task,
            .externalStateSources = sources,
            .externalStateSourceCount = sourceCount,
        };
        return true;
    };

    // Record preparation and prefix through the graph's ready-frontier path. These renderer payloads intentionally
    // retain the serial default, while graph-owned upload packets later in the frame may opt into worker recording.
    const Core::GpuNativePacketRecorder deferredRecorder(device);
    Core::Alloc::ScratchArena shadowPrepareStateScratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> shadowPrepareLiveStateBuffers{ shadowPrepareStateScratchArena };
    const auto appendShadowPrepareStateBuffer = [&](const Core::BufferHandle& buffer){
        if(!buffer)
            return;
        for(const Core::BufferHandle& existing : shadowPrepareLiveStateBuffers){
            if(existing.get() == buffer.get())
                return;
        }
        shadowPrepareLiveStateBuffers.push_back(buffer);
    };
    if(m_rayTracingState.m_tlas)
        appendShadowPrepareStateBuffer(m_rayTracingState.m_tlas->getBackingBufferHandle());
    bool shadowPrepareStateCandidateRequired = static_cast<bool>(m_rayTracingState.m_tlas)
        || m_raytracingSystem.preparedMeshSwBvhBuildsReady()
        || m_raytracingSystem.shadowVisibilitySoftwareResourcesPreflighted()
    ;
    // A normalized trace-geometry stream may be imported as ShaderResource on the next frame even when its native
    // BufferDesc starts at Common. Keep that accepted graph state with the preparation handoff; otherwise the
    // following Prefix packet has a compiler state seed with no native producer entry to import. Include retained
    // invisible streams too: preflight deliberately keeps their accepted normalization until the mesh is removed.
    for(const Core::BufferHandle& acceptedBuffer : m_raytracingSystem.acceptedShadowTraceGeometryBuffers()){
        appendShadowPrepareStateBuffer(acceptedBuffer);
        shadowPrepareStateCandidateRequired = true;
    }
    const PreparedShadowTraceGeometryBufferVector& preparedTraceGeometry =
        m_raytracingSystem.preparedShadowTraceGeometryBuffers()
    ;
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        appendShadowPrepareStateBuffer(preparedBuffer.buffer);
        shadowPrepareStateCandidateRequired = true;
    }
    for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        // A frozen BLAS plan can fall back to the native current-mesh build when runtime geometry changes between
        // preflight and recording. That bridge leaves its position/index streams in AccelStructBuildInput, even
        // after a static first-build clears blasBuildPending. Retain every live pair, not only the frozen inputs,
        // so a later descriptor import never seeds Common over that accepted native state.
        if(mesh.blas){
            appendShadowPrepareStateBuffer(mesh.positionBuffer);
            appendShadowPrepareStateBuffer(mesh.triangleIndexBuffer);
            appendShadowPrepareStateBuffer(mesh.blas->getBackingBufferHandle());
            shadowPrepareStateCandidateRequired = true;
        }
        // Preserve both mesh-local SW state buffers across route changes. A hybrid native build may leave these UAVs
        // before the next frame switches to software-only frozen recording.
        appendShadowPrepareStateBuffer(mesh.swBvhNodeBuffer);
        appendShadowPrepareStateBuffer(mesh.swBvhParentBuffer);
    }
    appendShadowPrepareStateBuffer(m_rayTracingState.m_bvhSortKeysBuffer);
    appendShadowPrepareStateBuffer(m_rayTracingState.m_bvhSortPayloadBuffer);
    appendShadowPrepareStateBuffer(m_rayTracingState.m_bvhVisitCounterBuffer);

    // Filter the prior accepted cache into a typed recording source, but do not alter the cache yet: a rejected
    // Shadow Preparation packet must leave both its state and resource lifetimes exactly as the last acceptance.
    Core::GpuPersistentResourceStateCache::Candidate shadowPreparePriorStateCandidate(m_arena);
    bool shadowPreparePriorStateReady = true;
    if(m_shadowPreparePersistentState.valid()){
        const Core::CommandListResourceStateHandoff* const previousStates = m_shadowPreparePersistentState.source();
        shadowPreparePriorStateReady = previousStates
            && m_shadowPreparePersistentState.buildFilteredBufferSubset(
                shadowPreparePriorStateCandidate,
                *previousStates,
                shadowPrepareLiveStateBuffers.data(),
                shadowPrepareLiveStateBuffers.size()
            )
        ;
    }

    Core::GpuExternalPacketStateSource shadowPrepareStateSources[
        __hidden_renderer_system::s_SingleStateSourceCapacity
    ] = {};
    usize shadowPrepareStateSourceCount = 0u;
    if(shadowPreparePriorStateReady && shadowPreparePriorStateCandidate.valid()){
        if(!appendDeclaredStateSource(
            shadowPrepareStateSources,
            LengthOf(shadowPrepareStateSources),
            shadowPrepareStateSourceCount,
            shadowPreparePriorStateCandidate.source()
        )){
            shadowPrepareStateSourceCount = 0u;
        }
    }
    Core::GpuTaskPacketStateBinding shadowPrepareStateBindings[
        __hidden_renderer_system::s_SinglePacketStateBindingCapacity
    ] = {};
    usize shadowPrepareStateBindingCount = 0u;
    const bool shadowPrepareStateBindingsReady = shadowPrepareStateSourceCount == 0u
        || appendTaskPacketStateBinding(
            shadowPrepareStateBindings,
            LengthOf(shadowPrepareStateBindings),
            shadowPrepareStateBindingCount,
            m_deferredShadowPrepareTask,
            shadowPrepareStateSources,
            shadowPrepareStateSourceCount
        )
    ;
    const bool graphicsPrefixRecorded =
        m_deferredLightingTaskGraphValid
        && m_graphicsPrefixMeshViewSetupTask.valid()
        && m_graphicsPrefixSceneShadingSetupTask.valid()
        && m_graphicsPrefixDeferredClearFirstTask.valid()
        && m_graphicsPrefixDeferredClearTask.valid()
        && m_graphicsPrefixGbufferTask.valid()
        && (!hasOpaqueCsgFrameWork || (
            m_graphicsPrefixCsgReceiverSpanTask.valid()
            && m_graphicsPrefixCsgIntervalCombineTask.valid()
            && m_graphicsPrefixCsgIntervalSampleTask.valid()
        ))
        && m_graphicsPrefixTask.valid()
        && m_deferredShadowPrepareTask.valid()
        && taskIsCompiled(m_deferredShadowPrepareTask)
        && shadowPrepareSoftwareBvhBuildsMerged
        && shadowPrepareHybridSoftwareTailMerged
        && shadowPrepareAccelStructFinalizeMerged
        && taskIsCompiled(m_graphicsPrefixTask)
        && graphicsPrefixDeferredClearBundleMerged
        && graphicsPrefixOpaqueComputeEmulationMerged
        && graphicsPrefixOpaqueSharedComputeEmulationMerged
        && graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged
        && graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged
        && shadowPrepareStateBindingsReady
        && deferredRecorder.recordTaskRangeInReadyFrontiers(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            m_deferredShadowPrepareTask,
            m_graphicsPrefixTask,
            nullptr,
            0u,
            m_deferredLightingRecordedGraph,
            m_world.taskPool(),
            nullptr,
            nullptr,
            shadowPrepareStateBindingCount != 0u ? shadowPrepareStateBindings : nullptr,
            shadowPrepareStateBindingCount
        )
    ;
    const Core::CommandListResourceStateHandoff* const shadowPrepareFinalStateSeed = graphicsPrefixRecorded
        ? m_deferredLightingRecordedGraph.taskFinalStateSeed(
            m_deferredLightingCompiledGraph,
            m_deferredShadowPrepareTask
        )
        : nullptr
    ;
    const Core::CommandListResourceStateHandoff* const graphicsPrefixFinalStateSeed = graphicsPrefixRecorded
        ? m_deferredLightingRecordedGraph.taskFinalStateSeed(
            m_deferredLightingCompiledGraph,
            m_graphicsPrefixTask
        )
        : nullptr
    ;
    // Build the sparse AS/software-BVH state candidate before submission, but let the accepted packet callback
    // adopt it. The cache folds the prior accepted source with the recorded final state and owns the typed backings.
    Core::GpuPersistentResourceStateCache::Candidate shadowPrepareAcceptedStateCandidate(m_arena);
    bool shadowPrepareAcceptedStateCandidateReady = true;
    if(!shadowPrepareLiveStateBuffers.empty()){
        shadowPrepareAcceptedStateCandidateReady = shadowPrepareFinalStateSeed
            && m_shadowPreparePersistentState.buildMergedBufferSubset(
                shadowPrepareAcceptedStateCandidate,
                *shadowPrepareFinalStateSeed,
                shadowPrepareLiveStateBuffers.data(),
                shadowPrepareLiveStateBuffers.size()
            )
        ;
        if(shadowPrepareAcceptedStateCandidate.valid())
            shadowPrepareAcceptedStateCandidateReady = shadowPrepareAcceptedStateCandidateReady
                && !shadowPrepareAcceptedStateCandidate.empty()
            ;
    }
    const bool shadowPrepareStateCandidatePresent =
        shadowPrepareAcceptedStateCandidate.valid()
        && !shadowPrepareAcceptedStateCandidate.empty()
    ;
    if(shadowPrepareStateCandidateRequired && !shadowPrepareStateCandidatePresent)
        shadowPrepareAcceptedStateCandidateReady = false;
    if(
        !graphicsPrefixRecorded
        || !shadowPrepareFinalStateSeed
        || !graphicsPrefixFinalStateSeed
        || !shadowPrepareAcceptedStateCandidateReady
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to retain graph-owned shadow-preparation/prefix state"));
        discardRenderPackets();
        return;
    }

    Core::GpuExternalPacketStateSource shadowVisibilityStateSources[
        __hidden_renderer_system::s_ShadowVisibilityStateSourceCapacity
    ] = {};
    usize shadowVisibilityStateSourceCount = 0u;
    bool shadowVisibilityStateSourcesReady = appendDeclaredStateSource(
        shadowVisibilityStateSources,
        LengthOf(shadowVisibilityStateSources),
        shadowVisibilityStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    if(m_shadowComputePersistentState.valid()){
        shadowVisibilityStateSourcesReady = shadowVisibilityStateSourcesReady
            && appendDeclaredStateSource(
                shadowVisibilityStateSources,
                LengthOf(shadowVisibilityStateSources),
                shadowVisibilityStateSourceCount,
                m_shadowComputePersistentState.source()
            )
        ;
    }
    if(shadowVisibilityRunsOnCompute && m_shadowVisibilityReturnState.valid()){
        shadowVisibilityStateSourcesReady = shadowVisibilityStateSourcesReady
            && appendDeclaredStateSource(
                shadowVisibilityStateSources,
                LengthOf(shadowVisibilityStateSources),
                shadowVisibilityStateSourceCount,
                m_shadowVisibilityReturnState.source()
            )
        ;
    }

    Core::GpuExternalPacketStateSource softwareCausticsStateSources[
        __hidden_renderer_system::s_SoftwareCausticsStateSourceCapacity
    ] = {};
    usize softwareCausticsStateSourceCount = 0u;
    bool softwareCausticsStateSourcesReady = appendDeclaredStateSource(
        softwareCausticsStateSources,
        LengthOf(softwareCausticsStateSources),
        softwareCausticsStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    if(!hardwareShadowSupported){
        if(softwareCausticsRunsOnCompute && m_causticsComputePersistentState.valid()){
            softwareCausticsStateSourcesReady = softwareCausticsStateSourcesReady
                && appendDeclaredStateSource(
                    softwareCausticsStateSources,
                    LengthOf(softwareCausticsStateSources),
                    softwareCausticsStateSourceCount,
                    m_causticsComputePersistentState.source()
                )
            ;
        }
        if(softwareCausticsRunsOnCompute && m_causticIrradianceReturnState.valid()){
            softwareCausticsStateSourcesReady = softwareCausticsStateSourcesReady
                && appendDeclaredStateSource(
                    softwareCausticsStateSources,
                    LengthOf(softwareCausticsStateSources),
                    softwareCausticsStateSourceCount,
                    m_causticIrradianceReturnState.source()
                )
            ;
        }
    }

    // Shadow Visibility, optional Software Caustics, and each Surfel-GI packet record after Prefix in compiler
    // order. The snapshot packet needs the same persistent source as the final compute task, while compiler
    // prologue seeds replace only the regions produced by an in-graph initialization or copy packet.
    Core::GpuExternalPacketStateSource surfelGiStateSources[
        __hidden_renderer_system::s_SurfelGiStateSourceCapacity
    ] = {};
    usize surfelGiStateSourceCount = 0u;
    bool surfelGiStateSourcesReady = appendDeclaredStateSource(
        surfelGiStateSources,
        LengthOf(surfelGiStateSources),
        surfelGiStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    if(surfelGiRunsOnCompute && m_surfelGiComputePersistentState.valid()){
        surfelGiStateSourcesReady = surfelGiStateSourcesReady
            && appendDeclaredStateSource(
                surfelGiStateSources,
                LengthOf(surfelGiStateSources),
                surfelGiStateSourceCount,
                m_surfelGiComputePersistentState.source()
            )
        ;
    }
    if(m_surfelGiCounterPersistentState.valid()){
        surfelGiStateSourcesReady = surfelGiStateSourcesReady
            && appendDeclaredStateSource(
                surfelGiStateSources,
                LengthOf(surfelGiStateSources),
                surfelGiStateSourceCount,
                m_surfelGiCounterPersistentState.source()
            )
        ;
    }
    if(surfelGiRunsOnCompute && m_surfelIrradianceReturnState.valid()){
        surfelGiStateSourcesReady = surfelGiStateSourcesReady
            && appendDeclaredStateSource(
                surfelGiStateSources,
                LengthOf(surfelGiStateSources),
                surfelGiStateSourceCount,
                m_surfelIrradianceReturnState.source()
            )
        ;
    }

    // AVBOIT and lagged Lighting retain the filtered Prefix source for their independent common reads. Hardware
    // Caustics additionally imports its accepted Graphics accumulator state when a warm temporal decay reads it.
    // Their packet ordering remains internal to this compiled graph.
    m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
    const Core::GpuExternalPacketStateSource avboitPreStateSources[] = {
        Core::GpuExternalPacketStateSource{
            .states = graphicsPrefixFinalStateSeed,
        },
    };
    Core::GpuExternalPacketStateSource deferredLightingStateSources[
        __hidden_renderer_system::s_SingleStateSourceCapacity
    ] = {};
    usize deferredLightingStateSourceCount = 0u;
    const bool deferredLightingStateSourcesReady = appendDeclaredStateSource(
        deferredLightingStateSources,
        LengthOf(deferredLightingStateSources),
        deferredLightingStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    Core::GpuExternalPacketStateSource hardwareCausticsStateSources[
        __hidden_renderer_system::s_HardwareCausticsStateSourceCapacity
    ] = {};
    usize hardwareCausticsStateSourceCount = 0u;
    bool hardwareCausticsStateSourcesReady = true;
    if(hardwareShadowSupported){
        hardwareCausticsStateSourcesReady = appendDeclaredStateSource(
            hardwareCausticsStateSources,
            LengthOf(hardwareCausticsStateSources),
            hardwareCausticsStateSourceCount,
            graphicsPrefixFinalStateSeed
        );
        if(m_hardwareCausticAccumulatorPersistentState.valid()){
            hardwareCausticsStateSourcesReady = hardwareCausticsStateSourcesReady
                && appendDeclaredStateSource(
                    hardwareCausticsStateSources,
                    LengthOf(hardwareCausticsStateSources),
                    hardwareCausticsStateSourceCount,
                    m_hardwareCausticAccumulatorPersistentState.source()
                )
            ;
        }
    }
    Core::GpuExternalPacketStateSource deferredCompositeStateSources[
        __hidden_renderer_system::s_SingleStateSourceCapacity
    ] = {};
    usize deferredCompositeStateSourceCount = 0u;
    const bool deferredCompositeStateSourcesReady = appendDeclaredStateSource(
        deferredCompositeStateSources,
        LengthOf(deferredCompositeStateSources),
        deferredCompositeStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    // The accepted Prefix source only exists after the first range records. Bind every late source to its semantic
    // graph task rather than the packet selected by this compilation; the recorder resolves the current packet and
    // retains the established packet-wide resource filtering. This lets packetization evolve without rebuilding a
    // renderer-owned packet override table.
    Core::GpuTaskPacketStateBinding deferredStateBindings[
        __hidden_renderer_system::s_DeferredPacketStateBindingCapacity
    ] = {};
    usize deferredStateBindingCount = 0u;
    bool deferredStateBindingsReady = appendTaskPacketStateBinding(
        deferredStateBindings,
        LengthOf(deferredStateBindings),
        deferredStateBindingCount,
        m_deferredShadowVisibilityTask,
        shadowVisibilityStateSources,
        shadowVisibilityStateSourceCount
    );
    if(!hardwareShadowSupported){
        deferredStateBindingsReady = deferredStateBindingsReady
            && appendTaskPacketStateBinding(
                deferredStateBindings,
                LengthOf(deferredStateBindings),
                deferredStateBindingCount,
                m_deferredSoftwareCausticsTask,
                softwareCausticsStateSources,
                softwareCausticsStateSourceCount
            )
        ;
    }
    if(m_deferredSurfelGiPreparationTask.valid()){
        deferredStateBindingsReady = deferredStateBindingsReady
            && appendTaskPacketStateBinding(
                deferredStateBindings,
                LengthOf(deferredStateBindings),
                deferredStateBindingCount,
                m_deferredSurfelGiPreparationTask,
                surfelGiStateSources,
                surfelGiStateSourceCount
            )
        ;
    }
    if(
        m_deferredSurfelGiSnapshotCopyTask.valid()
        && m_deferredSurfelGiSnapshotCopyTask != m_deferredSurfelGiPreparationTask
    ){
        deferredStateBindingsReady = deferredStateBindingsReady
            && appendTaskPacketStateBinding(
                deferredStateBindings,
                LengthOf(deferredStateBindings),
                deferredStateBindingCount,
                m_deferredSurfelGiSnapshotCopyTask,
                surfelGiStateSources,
                surfelGiStateSourceCount
            )
        ;
    }
    deferredStateBindingsReady = deferredStateBindingsReady
        && appendTaskPacketStateBinding(
            deferredStateBindings,
            LengthOf(deferredStateBindings),
            deferredStateBindingCount,
            m_deferredSurfelGiTask,
            surfelGiStateSources,
            surfelGiStateSourceCount
        )
    ;
    if(hardwareShadowSupported){
        deferredStateBindingsReady = deferredStateBindingsReady
            && appendTaskPacketStateBinding(
                deferredStateBindings,
                LengthOf(deferredStateBindings),
                deferredStateBindingCount,
                m_deferredHardwareCausticsTask,
                hardwareCausticsStateSources,
                hardwareCausticsStateSourceCount
            )
        ;
    }
    deferredStateBindingsReady = deferredStateBindingsReady
        && appendTaskPacketStateBinding(
            deferredStateBindings,
            LengthOf(deferredStateBindings),
            deferredStateBindingCount,
            m_deferredAvboitPreTask,
            avboitPreStateSources,
            LengthOf(avboitPreStateSources)
        )
        && appendTaskPacketStateBinding(
            deferredStateBindings,
            LengthOf(deferredStateBindings),
            deferredStateBindingCount,
            m_deferredLightingTask,
            deferredLightingStateSources,
            deferredLightingStateSourceCount
        )
        && appendTaskPacketStateBinding(
            deferredStateBindings,
            LengthOf(deferredStateBindings),
            deferredStateBindingCount,
            m_deferredCompositeTask,
            deferredCompositeStateSources,
            deferredCompositeStateSourceCount
        )
    ;
    // The optional history tail and independent recovery tail record only after their runtime prerequisites exist.
    // Record the normal compile-order prefix now; both late packets join this recorded graph and transaction without
    // renderer-side completion bridges.
    bool deferredPacketsRecorded =
        hardwareCausticsStateSourcesReady
        && shadowVisibilityStateSourcesReady
        && softwareCausticsStateSourcesReady
        && surfelGiStateSourcesReady
        && deferredLightingStateSourcesReady
        && deferredCompositeStateSourcesReady
        && deferredStateBindingsReady
        && m_deferredLightingTaskGraphValid
        && m_graphicsPrefixMeshViewSetupTask.valid()
        && m_graphicsPrefixSceneShadingSetupTask.valid()
        && m_graphicsPrefixDeferredClearFirstTask.valid()
        && m_graphicsPrefixDeferredClearTask.valid()
        && m_graphicsPrefixGbufferTask.valid()
        && (!hasOpaqueCsgFrameWork || (
            m_graphicsPrefixCsgReceiverSpanTask.valid()
            && m_graphicsPrefixCsgIntervalCombineTask.valid()
            && m_graphicsPrefixCsgIntervalSampleTask.valid()
        ))
        && m_graphicsPrefixTask.valid()
        && taskIsCompiled(m_graphicsPrefixTask)
        && graphicsPrefixDeferredClearBundleMerged
        && graphicsPrefixOpaqueComputeEmulationMerged
        && graphicsPrefixOpaqueSharedComputeEmulationMerged
        && graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged
        && graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged
        && m_deferredShadowVisibilityTask.valid()
        && taskIsCompiled(m_deferredShadowVisibilityTask)
        && shadowVisibilityPreparedTasksMerged
        && shadowVisibilityAllLitClearMerged
        && shadowVisibilityAdaptivePrimitivesMerged
        && (hardwareShadowSupported || (
            m_deferredSoftwareCausticsTask.valid()
            && taskIsCompiled(m_deferredSoftwareCausticsTask)
        ))
        && m_deferredSurfelGiTask.valid()
        && taskIsCompiled(m_deferredSurfelGiTask)
        && (!m_deferredSurfelGiSnapshotCopyTask.valid() || (
            m_deferredSurfelGiPreparationTask.valid()
            && taskIsCompiled(m_deferredSurfelGiPreparationTask)
            && taskIsCompiled(m_deferredSurfelGiSnapshotCopyTask)
        ))
        && (!hardwareShadowSupported || (
            m_deferredHardwareCausticsTask.valid()
            && taskIsCompiled(m_deferredHardwareCausticsTask)
        ))
        && m_deferredAvboitPreTask.valid()
        && m_deferredAvboitOccupancyTask.valid()
        && taskIsCompiled(m_deferredAvboitPreTask)
        && avboitPrePacketContainsClear
        && avboitPrePacketContainsCsgReceiverSpan
        && avboitPrePacketContainsCsgIntervalCombine
        && avboitPrePacketContainsOccupancy
        && avboitOccupancyAllComputeEmulationMerged
        && avboitExtinctionPacketContainsStreams
        && avboitExtinctionAllComputeEmulationMerged
        && avboitUnsplitIntegrationTailMerged
        && avboitAccumulationAllComputeEmulationMerged
        && avboitAccumulationPacketContainsStreams
        && avboitAccumulationPacketContainsFinalizer
        && (!hasTransparentRenderers || (
            m_deferredAvboitExtinctionTask.valid()
            && m_deferredAvboitAccumulationTask.valid()
            && m_deferredAvboitAccumulationFinalizeTask.valid()
            && taskIsCompiled(m_deferredAvboitExtinctionTask)
            && taskIsCompiled(m_deferredAvboitAccumulationTask)
            && m_deferredLightingCompiledGraph.findTask(m_deferredAvboitAccumulationFinalizeTask)
            && (avboitUsesAsyncCompute || (
                avboitUnsplitPrePacketContainsExtinction
                && avboitUnsplitPrePacketContainsIntegration
                && avboitUnsplitPrePacketContainsAccumulation
            ))
        ))
        && (!avboitUsesAsyncCompute || (
            m_deferredAvboitDepthWarpTask.valid()
            && m_deferredAvboitExtinctionTask.valid()
            && m_deferredAvboitIntegrationTask.valid()
            && m_deferredAvboitAccumulationTask.valid()
            && taskIsCompiled(m_deferredAvboitDepthWarpTask)
            && taskIsCompiled(m_deferredAvboitExtinctionTask)
            && taskIsCompiled(m_deferredAvboitIntegrationTask)
            && taskIsCompiled(m_deferredAvboitAccumulationTask)
        ))
        && m_deferredLightingTask.valid()
        && m_deferredCompositeTask.valid()
        && m_deferredPresentTask.valid()
        && m_deferredFrameTimingEndTask.valid()
        && (!m_deferredPresentationOverlayRequired || (
            m_deferredPresentationOverlayTask.valid()
            && taskIsCompiled(m_deferredPresentationOverlayTask)
        ))
        && m_deferredFrameRecoveryTask.valid()
        && (!captureLaggedLightingHistory || (
            m_deferredLaggedLightingHistoryTask.valid()
            && taskIsCompiled(m_deferredLaggedLightingHistoryTask)
        ))
        && (!laggedLightingHistoryCompletionRequired || m_deferredLightingHistoryCompletion.valid())
        && taskIsCompiled(m_deferredLightingTask)
        && taskIsCompiled(m_deferredCompositeTask)
        && taskIsCompiled(m_deferredPresentTask)
        && taskIsCompiled(m_deferredFrameTimingEndTask)
        && presentationEndpoint
        && presentationEndpoint->valid()
        && presentationEndpoint->producer == m_deferredFrameTimingEndTask
        && terminalPresentationPacket.valid()
        && taskIsCompiled(m_deferredFrameRecoveryTask)
        && deferredFrameRecoveryQueue
        && effectsThroughPresentationPacketRange.valid()
    ;
    if(deferredPacketsRecorded){
        deferredPacketsRecorded = deferredRecorder.recordTaskRangeInReadyFrontiers(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            m_deferredShadowVisibilityTask,
            terminalPresentationTask,
            nullptr,
            0u,
            m_deferredLightingRecordedGraph,
            m_world.taskPool(),
            nullptr,
            nullptr,
            deferredStateBindings,
            deferredStateBindingCount
        );
    }
    if(!deferredPacketsRecorded){
        shadowPrepareTimingTicket.discard();
        discardGraphicsPrefixTimingTickets();
        avboitPreTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        surfelGiTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        deferredLightingTimingTicket.discard();
        deferredCompositeTimingTicket.discard();
        deferredPresentTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned deferred effects/AVBOIT/lighting/composite/present chain"));
        discardRenderPackets();
        return;
    }
    const Core::CommandListResourceStateHandoff* const shadowVisibilityFinalStateSeed =
        m_deferredLightingRecordedGraph.taskFinalStateSeed(
            m_deferredLightingCompiledGraph,
            m_deferredShadowVisibilityTask
        )
    ;
    const Core::CommandListResourceStateHandoff* const softwareCausticsFinalStateSeed = !hardwareShadowSupported
        ? m_deferredLightingRecordedGraph.taskFinalStateSeed(
            m_deferredLightingCompiledGraph,
            m_deferredSoftwareCausticsTask
        )
        : nullptr
    ;
    const Core::CommandListResourceStateHandoff* const causticsFinalStateSeed = softwareCausticsFinalStateSeed;
    const Core::CommandListResourceStateHandoff* const deferredLightingFinalStateSeed =
        m_deferredLightingRecordedGraph.taskFinalStateSeed(
            m_deferredLightingCompiledGraph,
            m_deferredLightingTask
        )
    ;
    const Core::CommandListResourceStateHandoff* const surfelGiFinalStateSeed =
        m_deferredLightingRecordedGraph.taskFinalStateSeed(
            m_deferredLightingCompiledGraph,
            m_deferredSurfelGiTask
        )
    ;
    const Core::CommandListResourceStateHandoff* const hardwareCausticsFinalStateSeed = hardwareShadowSupported
        ? m_deferredLightingRecordedGraph.taskFinalStateSeed(
            m_deferredLightingCompiledGraph,
            m_deferredHardwareCausticsTask
        )
        : nullptr
    ;
    if(
        !graphicsPrefixFinalStateSeed
        || !shadowVisibilityFinalStateSeed
        || (!hardwareShadowSupported && !softwareCausticsFinalStateSeed)
        || !surfelGiFinalStateSeed
        || !deferredLightingFinalStateSeed
        || (hardwareShadowSupported && !hardwareCausticsFinalStateSeed)
    ){
        shadowPrepareTimingTicket.discard();
        discardGraphicsPrefixTimingTickets();
        avboitPreTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        surfelGiTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        deferredLightingTimingTicket.discard();
        deferredCompositeTimingTicket.discard();
        deferredPresentTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred effects/AVBOIT/lighting/composite/present graph did not retain final state"));
        discardRenderPackets();
        return;
    }

    Core::QueueSubmissionToken shadowVisibilitySubmissionToken;
    Core::QueueSubmissionToken hardwareCausticsSubmissionToken;
    Core::QueueSubmissionToken softwareCausticsSubmissionToken;
    Core::QueueSubmissionToken surfelGiSubmissionToken;
    Core::QueueSubmissionToken avboitPreSubmissionToken;
    Core::QueueSubmissionToken avboitDepthWarpSubmissionToken;
    Core::QueueSubmissionToken avboitExtinctionSubmissionToken;
    Core::QueueSubmissionToken avboitIntegrationSubmissionToken;
    Core::QueueSubmissionToken avboitAccumulationSubmissionToken;
    Core::QueueSubmissionToken avboitFinalSubmissionToken;
    Core::QueueSubmissionToken deferredLightingSubmissionToken;
    Core::QueueSubmissionToken deferredCompositeSubmissionToken;
    const auto submitFrameRecoveryPacket = [&]() -> bool {
        // Retire the accepted frame scope after a rejected packet. The transaction supplies one latest token from
        // every other accepted physical queue directly to the graph-marked recovery packet; Graphics order covers
        // its own accepted prefix without a redundant timeline wait.
        if(device.isDeviceLost()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery packet skipped because the graphics device is lost"));
            m_deferredFrameRecoveryArmed = false;
            m_deferredFrameRecoveryRetiresTiming = false;
            frameTimingTransaction.discard();
            return false;
        }
        if(
            !m_deferredLightingTaskGraphValid
            || !m_deferredFrameRecoveryTask.valid()
            || !m_deferredLightingCompiledGraph.findTask(m_deferredFrameRecoveryTask)
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery task was unavailable"));
            m_deferredFrameRecoveryArmed = false;
            m_deferredFrameRecoveryRetiresTiming = false;
            frameTimingTransaction.discard();
            return false;
        }
        const bool retireTiming = frameTimingTransaction.needsRetirement();
        if(retireTiming)
            frameTimingTransaction.prepareForRecovery();
        m_deferredFrameRecoveryArmed = true;
        m_deferredFrameRecoveryRetiresTiming = retireTiming;
        Core::Alloc::ScratchArena recoveryScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter submitter(device);
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
    const auto restoreUnacceptedShadowEffectsCpuState = [&](){
        // Compile-order submission can retain either successor. Keep every accepted CPU mirror intact while
        // restoring only work that never reached a queue.
        if(
            (hardwareShadowSupported && !hardwareCausticsSubmissionToken.valid())
            || (!hardwareShadowSupported && !softwareCausticsSubmissionToken.valid())
        )
            restoreCausticsCpuState();
        if(!surfelGiSubmissionToken.valid())
            restoreSurfelGiCpuState();
        restoreAvboitCpuState();
    };
    const auto recoverPendingFrameSubmission = [&]() -> bool {
        return (m_deferredLightingSubmissionTransaction.hasAcceptedPackets() || frameTimingTransaction.needsRetirement())
            ? submitFrameRecoveryPacket()
            : true
        ;
    };
    const auto recoverPendingFrameThenDiscardUnaccepted = [&]() -> bool {
        const bool recovered = recoverPendingFrameSubmission();
        // Recovery must record and submit while this packet remains Declared. Once it has accepted (or its own
        // rejection discarded the timing transaction), reject every remaining normal packet in the shared graph.
        return discardUnacceptedGraphPackets() && recovered;
    };
    const auto failFrameRenderRecovery = [&](){
        if(m_frameRenderRecoveryFailed)
            return;
        m_frameRenderRecoveryFailed = true;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: cannot safely continue after an unresolved frame recovery submission; requesting device recreation"));
        // Defer device recreation until accepted work cannot be invalidated.
        m_graphics.requestDeviceRecreation();
    };

    const auto submitAvboitLightingAndComposite = [&]() -> bool {
        Core::Alloc::ScratchArena deferredScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter deferredSubmitter(device);
        Core::GpuTaskGraphTaskTimingTicket avboitTimingTickets[
            __hidden_renderer_system::s_AvboitTimingTicketCapacity
        ] = {};
        usize avboitTimingTicketCount = 0u;
        const auto appendAvboitTimingTicket = [&avboitTimingTickets, &avboitTimingTicketCount](
            const Core::GpuTaskId task,
            Core::GpuTimingSubmissionTicket* const timingTicket
        ){
            NWB_ASSERT(avboitTimingTicketCount < LengthOf(avboitTimingTickets));
            avboitTimingTickets[avboitTimingTicketCount++] = Core::GpuTaskGraphTaskTimingTicket{
                .task = task,
                .timingTicket = timingTicket,
            };
        };
        appendAvboitTimingTicket(m_deferredAvboitPreTask, &avboitPreTimingTicket);
        if(avboitUsesAsyncCompute){
            appendAvboitTimingTicket(m_deferredAvboitDepthWarpTask, &avboitDepthWarpTimingTicket);
            if(m_deferredAvboitExtinctionComputeEmulationTask.valid()){
                // Both callbacks resolve to one packet and deliberately share the one Extinction timing ticket.
                appendAvboitTimingTicket(
                    m_deferredAvboitExtinctionComputeEmulationTask,
                    &avboitExtinctionTimingTicket
                );
            }
            appendAvboitTimingTicket(m_deferredAvboitExtinctionTask, &avboitExtinctionTimingTicket);
            appendAvboitTimingTicket(m_deferredAvboitIntegrationTask, &avboitIntegrationTimingTicket);
            if(m_deferredAvboitAccumulationComputeEmulationTask.valid()){
                // Both callbacks resolve to one packet and deliberately share the one Accumulation timing ticket.
                appendAvboitTimingTicket(
                    m_deferredAvboitAccumulationComputeEmulationTask,
                    &avboitAccumulationTimingTicket
                );
            }
            appendAvboitTimingTicket(m_deferredAvboitAccumulationTask, &avboitAccumulationTimingTicket);
        }
        const usize avboitPacketExpectedCount = avboitUsesAsyncCompute
            ? __hidden_renderer_system::s_AvboitAsyncComputePacketCount
            : __hidden_renderer_system::s_SinglePacketCount;
        const bool avboitPacketsAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredAvboitPreTask.valid()
            && m_deferredAvboitOccupancyTask.valid()
            && taskIsCompiled(m_deferredAvboitPreTask)
            && avboitPrePacketContainsClear
            && avboitPrePacketContainsCsgReceiverSpan
            && avboitPrePacketContainsCsgIntervalCombine
            && avboitPrePacketContainsOccupancy
            && avboitOccupancyAllComputeEmulationMerged
            && avboitExtinctionPacketContainsStreams
            && avboitExtinctionAllComputeEmulationMerged
            && avboitUnsplitIntegrationTailMerged
            && avboitAccumulationAllComputeEmulationMerged
            && avboitAccumulationPacketContainsStreams
            && avboitAccumulationPacketContainsFinalizer
            && (!hasTransparentRenderers || (
                m_deferredAvboitExtinctionTask.valid()
                && m_deferredAvboitAccumulationTask.valid()
                && m_deferredAvboitAccumulationFinalizeTask.valid()
                && taskIsCompiled(m_deferredAvboitExtinctionTask)
                && taskIsCompiled(m_deferredAvboitAccumulationTask)
                && m_deferredLightingCompiledGraph.findTask(m_deferredAvboitAccumulationFinalizeTask)
                && (avboitUsesAsyncCompute || (
                    avboitUnsplitPrePacketContainsExtinction
                    && avboitUnsplitPrePacketContainsIntegration
                    && avboitUnsplitPrePacketContainsAccumulation
                ))
            ))
            && avboitPacketRange.valid()
            && avboitPacketRange.packetCount == avboitPacketExpectedCount
            && (!avboitUsesAsyncCompute || (
                m_deferredAvboitDepthWarpTask.valid()
                && m_deferredAvboitExtinctionTask.valid()
                && m_deferredAvboitIntegrationTask.valid()
                && m_deferredAvboitAccumulationTask.valid()
                && taskIsCompiled(m_deferredAvboitDepthWarpTask)
                && taskIsCompiled(m_deferredAvboitExtinctionTask)
                && taskIsCompiled(m_deferredAvboitIntegrationTask)
                && taskIsCompiled(m_deferredAvboitAccumulationTask)
            ))
            && deferredSubmitter.submitTaskRangeInCompileOrderFromTasks(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                m_deferredAvboitPreTask,
                avboitUsesAsyncCompute ? m_deferredAvboitAccumulationFinalizeTask : avboitUnsplitFinalTask,
                nullptr,
                0u,
                avboitTimingTickets,
                avboitTimingTicketCount,
                m_deferredLightingSubmissionTransaction,
                deferredScratchArena
            )
        ;
        avboitPreSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredAvboitPreTask
        );
        avboitDepthWarpSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredAvboitDepthWarpTask
        );
        avboitExtinctionSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredAvboitExtinctionTask
        );
        avboitIntegrationSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredAvboitIntegrationTask
        );
        avboitAccumulationSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredAvboitAccumulationTask
        );
        avboitFinalSubmissionToken = (avboitUsesAsyncCompute || hasTransparentRenderers)
            ? avboitAccumulationSubmissionToken
            : avboitPreSubmissionToken
        ;
        if(!avboitPacketsAccepted || !avboitPreSubmissionToken.valid() || !avboitFinalSubmissionToken.valid()){
            const bool avboitPreWasRejected = !avboitPreSubmissionToken.valid();
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            if(avboitPreWasRejected)
                restoreAvboitCpuState();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred AVBOIT packet chain was rejected"));
            if(!recovered)
                failFrameRenderRecovery();
            return false;
        }

        // Prefix and all current-frame producers are internal. Active lagged Lighting alone imports prior history.
        Core::GpuTaskGraphExternalCompletionToken deferredLightingCompletionTokens[
            __hidden_renderer_system::s_SingleExternalCompletionTokenCapacity
        ] = {};
        usize deferredLightingCompletionCount = 0u;
        if(laggedAsyncLightingSchedule){
            deferredLightingCompletionTokens[deferredLightingCompletionCount++] = {
                .completion = m_deferredLightingHistoryCompletion,
                .token = priorLaggedLightingHistoryToken,
            };
        }
        struct DeferredLightingAcceptanceContext{
            RendererSystem* renderer = nullptr;
            DeferredFrameTargets* targets = nullptr;
            const Core::CommandListResourceStateHandoff* finalState = nullptr;
            bool runsOnCompute = false;
            bool usesLaggedHistory = false;
            bool returnStatesReady = true;
        };
        DeferredLightingAcceptanceContext deferredLightingAcceptance{
            .renderer = this,
            .targets = &deferredTargets,
            .finalState = deferredLightingFinalStateSeed,
            .runsOnCompute = deferredLightingRunsOnCompute,
            .usesLaggedHistory = laggedAsyncLightingSchedule,
        };
        const auto acceptDeferredLightingTask = [](
            void* const rawContext,
            const Core::QueueSubmissionToken& token
        ) -> bool {
            static_cast<void>(token);
            DeferredLightingAcceptanceContext* const context =
                static_cast<DeferredLightingAcceptanceContext*>(rawContext)
            ;
            if(!context || !context->renderer || !context->targets || !context->finalState)
                return false;

            RendererSystem& renderer = *context->renderer;
            if(context->usesLaggedHistory)
                context->targets->laggedLightingHistory.slotsUploaded = true;
            context->returnStatesReady = !context->runsOnCompute || context->usesLaggedHistory || (
                renderer.m_shadowVisibilityReturnState.replaceTextureSubset(
                    *context->finalState,
                    context->targets->shadowVisibility
                )
                // Bootstrap uses live caustics; active lagged mode uses the producer image directly.
                && renderer.m_causticIrradianceReturnState.replaceTextureSubset(
                    *context->finalState,
                    context->targets->causticIrradiance
                )
                && renderer.m_surfelIrradianceReturnState.replaceTextureSubset(
                    *context->finalState,
                    context->targets->surfelIrradiance
                )
            );
            if(!context->returnStatesReady)
                return false;
            if(context->usesLaggedHistory){
                renderer.reportLaggedLightingTransition(
                    LaggedLightingReport::ActiveHistoryAccepted,
                    context->targets->laggedLightingHistory.generation
                );
            }
            return true;
        };
        const Core::GpuTaskGraphTaskAcceptedCallback deferredLightingAcceptedCallback{
            .task = m_deferredLightingTask,
            .context = &deferredLightingAcceptance,
            .invoke = acceptDeferredLightingTask,
        };
        const Core::GpuTaskGraphTaskTimingTicket deferredLightingCompositeTimingTickets[] = {
            Core::GpuTaskGraphTaskTimingTicket{
                .task = m_deferredLightingTask,
                .timingTicket = &deferredLightingTimingTicket,
            },
            Core::GpuTaskGraphTaskTimingTicket{
                .task = m_deferredCompositeTask,
                .timingTicket = &deferredCompositeTimingTicket,
            },
        };
        const bool deferredLightingCompositeAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredShadowVisibilityTask.valid()
            && (hardwareShadowSupported || (
                m_deferredSoftwareCausticsTask.valid()
            ))
            && m_deferredSurfelGiTask.valid()
            && (!hardwareShadowSupported || (
                m_deferredHardwareCausticsTask.valid()
            ))
            && m_deferredLightingTask.valid()
            && m_deferredCompositeTask.valid()
            && (!laggedAsyncLightingSchedule || m_deferredLightingHistoryCompletion.valid())
            && laggedLightingHistorySlotsUploadMergedIntoLightingPacket
            && taskIsCompiled(m_deferredLightingTask)
            && taskIsCompiled(m_deferredCompositeTask)
            && deferredLightingCompositePacketRange.valid()
            && deferredLightingCompositePacketRange.packetCount == LengthOf(deferredLightingCompositeTimingTickets)
            && deferredSubmitter.submitTaskRangeInCompileOrderFromTasks(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                m_deferredLightingTask,
                m_deferredCompositeTask,
                deferredLightingCompletionTokens,
                deferredLightingCompletionCount,
                deferredLightingCompositeTimingTickets,
                LengthOf(deferredLightingCompositeTimingTickets),
                m_deferredLightingSubmissionTransaction,
                deferredScratchArena,
                nullptr,
                nullptr,
                nullptr,
                0u,
                &deferredLightingAcceptedCallback,
                1u
            )
        ;
        deferredLightingSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredLightingTask
        );
        deferredCompositeSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredCompositeTask
        );
        if(!deferredLightingAcceptance.returnStatesReady){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            if(!recovered)
                failFrameRenderRecovery();
            // Lost post-lighting state leaves no safe producer layout.
            failFrameRenderRecovery();
            return false;
        }
        if(
            !deferredLightingCompositeAccepted
            || !deferredLightingSubmissionToken.valid()
            || !deferredCompositeSubmissionToken.valid()
        ){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred lighting/composite packet chain was rejected"));
            if(!recovered)
                failFrameRenderRecovery();
            return false;
        }
        return true;
    };

    Core::QueueSubmissionToken finalPresentationSubmissionToken;
    const auto submitDeferredPresent = [&]() -> bool {
        Core::Alloc::ScratchArena presentScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter deferredPresentSubmitter(device);
        const Core::GpuTaskGraphTaskTimingTicket deferredPresentTimingTickets[] = {
            Core::GpuTaskGraphTaskTimingTicket{
                .task = m_deferredPresentTask,
                .timingTicket = &deferredPresentTimingTicket,
            },
        };
        const bool terminalPresentationReady =
            m_deferredLightingTaskGraphValid
            && m_deferredPresentTask.valid()
            && m_deferredFrameTimingEndTask.valid()
            && m_deferredSurfelGiTask.valid()
            && m_deferredCompositeTask.valid()
            && (!m_deferredPresentationOverlayRequired || m_deferredPresentationOverlayTask.valid())
            && taskIsCompiled(m_deferredPresentTask)
            && taskIsCompiled(m_deferredFrameTimingEndTask)
            && presentationEndpoint
            && presentationEndpoint->valid()
            && presentationEndpoint->producer == m_deferredFrameTimingEndTask
            && terminalPresentationPacket.valid()
            && terminalPresentationPacketRange.valid()
            && terminalPresentationPacketRange.packetCount >= LengthOf(deferredPresentTimingTickets)
        ;
        // BackendContext turns this into a submission-local binary signal only when a swap-chain image is active.
        // Empty hooks retain the compatibility present() path for non-windowed/direct render callers.
        const Core::QueueSubmissionPreSubmitHook framePresentationSignal = terminalPresentationReady
            ? m_graphics.claimFramePresentationSignal()
            : Core::QueueSubmissionPreSubmitHook{}
        ;
        const Core::GpuTaskGraphTaskSubmissionHook terminalPresentationSubmissionHooks[] = {
            Core::GpuTaskGraphTaskSubmissionHook{
                .task = terminalPresentationTask,
                .hook = framePresentationSignal,
            },
        };
        const bool deferredPresentationAccepted =
            terminalPresentationReady
            && deferredPresentSubmitter.submitTaskRangeInCompileOrderFromTasks(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                m_deferredPresentTask,
                terminalPresentationTask,
                nullptr,
                0u,
                deferredPresentTimingTickets,
                LengthOf(deferredPresentTimingTickets),
                m_deferredLightingSubmissionTransaction,
                presentScratchArena,
                nullptr,
                nullptr,
                nullptr,
                0u,
                nullptr,
                0u,
                framePresentationSignal.valid() ? terminalPresentationSubmissionHooks : nullptr,
                framePresentationSignal.valid() ? LengthOf(terminalPresentationSubmissionHooks) : 0u
            )
        ;
        finalPresentationSubmissionToken = deferredPresentationAccepted
            ? m_deferredLightingSubmissionTransaction.taskToken(
                m_deferredLightingCompiledGraph,
                terminalPresentationTask
            )
            : Core::QueueSubmissionToken{}
        ;
        if(!finalPresentationSubmissionToken.valid()){
            if(framePresentationSignal.valid())
                m_graphics.cancelFramePresentationSignal();
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred presentation submission was rejected"));
            if(!recovered)
                failFrameRenderRecovery();
            return false;
        }
        if(
            framePresentationSignal.valid()
            && !m_graphics.confirmFramePresentationSignal(finalPresentationSubmissionToken)
        ){
            // A terminal packet reached a queue but its native present signal did not retain matching physical
            // identity. Do not let present() fall back to a potentially unordered broad-Graphics submit.
            m_graphics.cancelFramePresentationSignal();
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: terminal graph presentation signal confirmation failed"));
            if(!recovered)
                failFrameRenderRecovery();
            failFrameRenderRecovery();
            return false;
        }
        if(!frameTimingTransaction.confirmEndSubmission(true)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm frame critical-path timing"));
            frameTimingTransaction.discard();
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
        return true;
    };

    const auto submitDeferredSurfelGi = [&]() -> bool {
        Core::Alloc::ScratchArena surfelGiScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter surfelGiSubmitter(device);
        Core::GpuTaskGraphExternalCompletionToken surfelGiCompletionTokens[
            __hidden_renderer_system::s_SingleExternalCompletionTokenCapacity
        ] = {};
        usize surfelGiCompletionTokenCount = 0u;
        if(m_deferredSurfelGiCounterReadbackCompletion.valid()){
            surfelGiCompletionTokens[surfelGiCompletionTokenCount++] = {
                .completion = m_deferredSurfelGiCounterReadbackCompletion,
                .token = surfelCounterReadbackCompletionToken,
            };
        }
        struct SurfelGiAcceptanceContext{
            RendererSystem* renderer = nullptr;
            DeferredFrameTargets* targets = nullptr;
            const Core::CommandListResourceStateHandoff* finalState = nullptr;
            bool runsOnCompute = false;
            bool stateReady = true;
        };
        SurfelGiAcceptanceContext surfelGiAcceptance{
            .renderer = this,
            .targets = &deferredTargets,
            .finalState = surfelGiFinalStateSeed,
            .runsOnCompute = surfelGiRunsOnCompute,
        };
        const auto acceptSurfelGiTask = [](
            void* const rawContext,
            const Core::QueueSubmissionToken& token
        ) -> bool {
            static_cast<void>(token);
            SurfelGiAcceptanceContext* const context =
                static_cast<SurfelGiAcceptanceContext*>(rawContext)
            ;
            if(!context)
                return false;
            if(!context->renderer || !context->targets || !context->finalState){
                context->stateReady = false;
                return false;
            }

            RendererSystem& renderer = *context->renderer;
            DeferredFrameTargets& targets = *context->targets;
            context->stateReady = renderer.m_surfelIrradianceReturnState.replaceTextureSubset(
                *context->finalState,
                targets.surfelIrradiance
            );
            const Core::BufferHandle surfelGiCounterBuffers[] = {
                renderer.m_rayTracingState.m_surfelCounterBuffer,
            };
            context->stateReady = context->stateReady
                && renderer.m_surfelGiCounterPersistentState.replaceBufferSubset(
                    *context->finalState,
                    surfelGiCounterBuffers,
                    LengthOf(surfelGiCounterBuffers)
                )
            ;
            if(!context->stateReady || !context->runsOnCompute)
                return context->stateReady;

            const Core::TextureHandle surfelGiComputeScratchTextures[] = {
                targets.surfelIrradianceHalf,
            };
            const Core::BufferHandle surfelGiComputeScratchBuffers[] = {
                renderer.m_rayTracingState.m_surfelPoolBuffer,
                renderer.m_rayTracingState.m_surfelCellHeadBuffer,
                renderer.m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
                renderer.m_rayTracingState.m_surfelFreeListBuffer,
                renderer.m_rayTracingState.m_surfelPoolSnapshotBuffer,
                renderer.m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
            };
            context->stateReady = renderer.m_surfelGiComputePersistentState.replaceResourceSubset(
                *context->finalState,
                surfelGiComputeScratchTextures,
                LengthOf(surfelGiComputeScratchTextures),
                surfelGiComputeScratchBuffers,
                LengthOf(surfelGiComputeScratchBuffers)
            );
            return context->stateReady;
        };
        const Core::GpuTaskGraphTaskAcceptedCallback surfelGiAcceptedCallback{
            .task = m_deferredSurfelGiTask,
            .context = &surfelGiAcceptance,
            .invoke = acceptSurfelGiTask,
        };
        const Core::GpuTaskGraphTaskTimingTicket surfelGiTimingTickets[] = {
            Core::GpuTaskGraphTaskTimingTicket{
                .task = m_deferredSurfelGiTask,
                .timingTicket = &surfelGiTimingTicket,
            },
        };
        const bool surfelGiAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredSurfelGiTask.valid()
            && (!m_deferredSurfelGiCounterReadbackCompletion.valid()
                || surfelCounterReadbackCompletionToken.valid())
            && taskIsCompiled(m_deferredSurfelGiTask)
            && (!m_deferredSurfelGiSnapshotCopyTask.valid() || (
                m_deferredSurfelGiPreparationTask.valid()
                && taskIsCompiled(m_deferredSurfelGiPreparationTask)
                && taskIsCompiled(m_deferredSurfelGiSnapshotCopyTask)
            ))
            && surfelGiPacketRange.valid()
            && surfelGiPacketRange.packetCount == expectedSurfelGiPacketCount
            && surfelGiSubmitter.submitTaskRangeInCompileOrderFromTasks(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                surfelGiFirstTask,
                m_deferredSurfelGiTask,
                surfelGiCompletionTokens,
                surfelGiCompletionTokenCount,
                surfelGiTimingTickets,
                LengthOf(surfelGiTimingTickets),
                m_deferredLightingSubmissionTransaction,
                surfelGiScratchArena,
                nullptr,
                nullptr,
                nullptr,
                0u,
                &surfelGiAcceptedCallback,
                1u
            )
        ;
        surfelGiSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredSurfelGiTask
        );
        if(!surfelGiAcceptance.stateReady){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            if(!recovered)
                failFrameRenderRecovery();
            // An accepted graph producer without a retained state cannot safely feed a later frame.
            failFrameRenderRecovery();
            return false;
        }
        if(!surfelGiAccepted || !surfelGiSubmissionToken.valid()){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            restoreUnacceptedShadowEffectsCpuState();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred surfel-GI packet was rejected"));
            if(!recovered)
                failFrameRenderRecovery();
            return false;
        }
        return true;
    };

    // Preparation and the compiler-derived graphics-prefix chain start the shared transaction. A frontier-safe
    // prefix may include additional built-in immutable-upload packets; timing remains attached to the semantic
    // packet anchors while the graph submits every packet in compiler order.
    {
        Core::Alloc::ScratchArena shadowPreparePrefixScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter shadowPreparePrefixSubmitter(device);
        Core::GpuTaskGraphTaskTimingTicket shadowPreparePrefixTimingTickets[
            1u + graphicsPrefixTimingTicketCount
        ] = {};
        usize shadowPreparePrefixTimingTicketCount = 0u;
        shadowPreparePrefixTimingTickets[shadowPreparePrefixTimingTicketCount++] =
            Core::GpuTaskGraphTaskTimingTicket{
                .task = m_deferredShadowPrepareTask,
                .timingTicket = &shadowPrepareTimingTicket,
            }
        ;
        bool shadowPreparePrefixTimingTicketsValid = graphicsPrefixTimingBindingsValid;
        for(usize prefixTaskIndex = 0u;
            shadowPreparePrefixTimingTicketsValid && prefixTaskIndex < graphicsPrefixTimingTicketCount;
            ++prefixTaskIndex
        ){
            const Core::GpuTaskId task = graphicsPrefixTimingTasks[prefixTaskIndex];
            bool packetAlreadyTimed = false;
            for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
                if(m_deferredLightingCompiledGraph.tasksSharePacket(
                    task,
                    graphicsPrefixTimingTasks[earlierTaskIndex]
                )){
                    packetAlreadyTimed = true;
                    break;
                }
            }
            if(packetAlreadyTimed)
                continue;
            if(
                !m_deferredLightingCompiledGraph.findTask(task)
                || !graphicsPrefixTimingTickets[prefixTaskIndex]
            ){
                shadowPreparePrefixTimingTicketsValid = false;
                break;
            }
            shadowPreparePrefixTimingTickets[shadowPreparePrefixTimingTicketCount++] =
                Core::GpuTaskGraphTaskTimingTicket{
                    .task = task,
                    .timingTicket = graphicsPrefixTimingTickets[prefixTaskIndex],
                }
            ;
        }
        struct PrefixTimingAcceptanceContext{
            Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
            RendererSystem* renderer = nullptr;
            Core::GpuPersistentResourceStateCache::Candidate* shadowPrepareStateCandidate = nullptr;
            bool shadowPrepareHasLiveStateBuffers = false;
            bool shadowPrepareStateCandidateRequired = false;
            bool shadowPrepareStateReady = true;
            // A required state-source loss follows accepted native work, so rendering must stop until device
            // recreation rather than retrying the backing generation with its stale Common descriptor seed.
            bool shadowPrepareStateLostAfterAcceptance = false;
        };
        PrefixTimingAcceptanceContext prefixTimingAcceptance{
            .frameTimingTransaction = &frameTimingTransaction,
            .renderer = this,
            .shadowPrepareStateCandidate = &shadowPrepareAcceptedStateCandidate,
            .shadowPrepareHasLiveStateBuffers = !shadowPrepareLiveStateBuffers.empty(),
            .shadowPrepareStateCandidateRequired = shadowPrepareStateCandidateRequired,
        };
        const auto acceptShadowPrepareTask = [](
            void* const rawContext,
            const Core::QueueSubmissionToken& token
        ) -> bool {
            static_cast<void>(token);
            PrefixTimingAcceptanceContext* const context = static_cast<PrefixTimingAcceptanceContext*>(rawContext);
            if(!context)
                return false;
            if(!context->frameTimingTransaction || !context->renderer){
                context->shadowPrepareStateReady = false;
                return false;
            }

            // Shadow Preparation is the first accepted normal-frame packet, including graph-owned setup uploads
            // merged into that packet. Confirm this endpoint before any later accepted-state validation can reject.
            context->frameTimingTransaction->confirmBeginSubmission();

            RendererSystem& renderer = *context->renderer;
            if(!context->shadowPrepareHasLiveStateBuffers){
                renderer.m_shadowPreparePersistentState.reset();
            }
            else if(
                !context->shadowPrepareStateCandidate
                || !context->shadowPrepareStateCandidate->valid()
                || context->shadowPrepareStateCandidate->empty()
            ){
                if(!context->shadowPrepareStateCandidateRequired){
                    renderer.m_shadowPreparePersistentState.reset();
                    return true;
                }
                // The packet is already accepted, so its native state cannot safely fall back to the previous
                // generation. The outer submission path must stop until device recreation.
                context->shadowPrepareStateLostAfterAcceptance = true;
                renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
                context->shadowPrepareStateReady = false;
                return false;
            }
            else{
                context->shadowPrepareStateReady = renderer.m_shadowPreparePersistentState.commit(
                    *context->shadowPrepareStateCandidate
                );
                if(context->shadowPrepareStateReady){
                    // Task acceptance has retained the backing handoff. Commit the frozen AS cache/refit state
                    // only after that graph-owned source exists for the next frame.
                    renderer.m_raytracingSystem.confirmPreparedSceneTlasBuild();
                    renderer.m_raytracingSystem.confirmPreparedMeshBlasBuilds();
                    renderer.m_raytracingSystem.confirmAcceptedShadowPrepareAccelStructStateHandoffs();
                    renderer.m_raytracingSystem.confirmPreparedMeshSwBvhBuilds();
                }
                else{
                    context->shadowPrepareStateLostAfterAcceptance = true;
                    renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
                    context->shadowPrepareStateReady = false;
                    return false;
                }
            }
            return true;
        };
        const Core::GpuTaskGraphTaskAcceptedCallback shadowPreparePrefixAcceptedCallbacks[] = {
            Core::GpuTaskGraphTaskAcceptedCallback{
                .task = m_deferredShadowPrepareTask,
                .context = &prefixTimingAcceptance,
                .invoke = acceptShadowPrepareTask,
            },
        };
        const bool shadowPreparePrefixAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredShadowPrepareTask.valid()
            && shadowPrepareHybridSoftwareTailMerged
            && shadowPrepareAccelStructFinalizeMerged
            && m_graphicsPrefixMeshViewSetupTask.valid()
            && m_graphicsPrefixSceneShadingSetupTask.valid()
            && m_graphicsPrefixDeferredClearFirstTask.valid()
            && m_graphicsPrefixDeferredClearTask.valid()
            && m_graphicsPrefixGbufferTask.valid()
            && (!hasOpaqueCsgFrameWork || (
                m_graphicsPrefixCsgReceiverSpanTask.valid()
                && m_graphicsPrefixCsgIntervalCombineTask.valid()
                && m_graphicsPrefixCsgIntervalSampleTask.valid()
            ))
            && m_graphicsPrefixTask.valid()
            && taskIsCompiled(m_deferredShadowPrepareTask)
            && deferredBindlessSlotsUploadMergedIntoShadowPreparePacket
            && rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket
            && causticEmissionTargetsUploadMergedIntoShadowPreparePacket
            && surfelFrameConstantsUploadMergedIntoShadowPreparePacket
            && shadowMaterialContextUploadsMergedIntoShadowPreparePacket
            && sceneBvhUploadsMergedIntoShadowPreparePacket
            && taskIsCompiled(m_graphicsPrefixTask)
            && graphicsPrefixDeferredClearBundleMerged
            && graphicsPrefixOpaqueComputeEmulationMerged
            && graphicsPrefixOpaqueSharedComputeEmulationMerged
            && graphicsPrefixOpaqueCsgReceiverComputeEmulationMerged
            && graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationMerged
            && graphicsPrefixWorkPacketRange.valid()
            && shadowPrepareThroughPrefixPacketRange.valid()
            && shadowPreparePrefixTimingTicketsValid
            && prefixTimingAcceptance.shadowPrepareStateReady
            && shadowPreparePrefixTimingTicketCount == 1u + graphicsPrefixUniquePacketCount
            && shadowPrepareThroughPrefixPacketRange.packetCount >= shadowPreparePrefixTimingTicketCount
            && shadowPreparePrefixSubmitter.submitTaskRangeInCompileOrderFromTasks(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                m_deferredShadowPrepareTask,
                m_graphicsPrefixTask,
                nullptr,
                0u,
                shadowPreparePrefixTimingTickets,
                shadowPreparePrefixTimingTicketCount,
                m_deferredLightingSubmissionTransaction,
                shadowPreparePrefixScratchArena,
                nullptr,
                nullptr,
                nullptr,
                0u,
                shadowPreparePrefixAcceptedCallbacks,
                LengthOf(shadowPreparePrefixAcceptedCallbacks)
            )
        ;
        if(
            !shadowPreparePrefixAccepted
            || !m_deferredLightingSubmissionTransaction.taskToken(
                m_deferredLightingCompiledGraph,
                m_deferredShadowPrepareTask
            ).valid()
            || !m_deferredLightingSubmissionTransaction.taskToken(
                m_deferredLightingCompiledGraph,
                m_graphicsPrefixTask
            ).valid()
        ){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardRenderPackets();
            if(!recovered || prefixTimingAcceptance.shadowPrepareStateLostAfterAcceptance)
                failFrameRenderRecovery();
            return;
        }
        Core::Alloc::ScratchArena shadowEffectsScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter shadowEffectsSubmitter(device);
        const Core::GpuTaskGraphTaskTimingTicket shadowEffectsTimingTickets[] = {
            Core::GpuTaskGraphTaskTimingTicket{
                .task = m_deferredShadowVisibilityTask,
                .timingTicket = &shadowVisibilityTimingTicket,
            },
            Core::GpuTaskGraphTaskTimingTicket{
                .task = m_deferredSoftwareCausticsTask,
                .timingTicket = &softwareCausticsTimingTicket,
            },
        };
        const usize shadowEffectsTimingTicketCount = hardwareShadowSupported
            ? 1u
            : LengthOf(shadowEffectsTimingTickets)
        ;
        Core::GpuTaskGraphExternalCompletionToken shadowEffectsCompletionTokens[
            __hidden_renderer_system::s_SingleExternalCompletionTokenCapacity
        ] = {};
        usize shadowEffectsCompletionCount = 0u;
        if(laggedLightingHistoryWriterWaitPending){
            shadowEffectsCompletionTokens[shadowEffectsCompletionCount++] = {
                .completion = m_deferredLightingHistoryCompletion,
                .token = laggedLightingHistoryWriterCompletionToken,
            };
        }
        const bool shadowEffectsSubmitted =
            m_deferredLightingTaskGraphValid
            && m_deferredShadowVisibilityTask.valid()
            && taskIsCompiled(m_deferredShadowVisibilityTask)
            && (!laggedLightingHistoryWriterWaitPending || (
                m_deferredLightingHistoryCompletion.valid()
                && laggedLightingHistoryWriterCompletionToken.valid()
            ))
            && shadowVisibilityPreparedTasksMerged
            && shadowVisibilityAllLitClearMerged
            && shadowVisibilityAdaptivePrimitivesMerged
            && shadowEffectsPacketRange.valid()
            && shadowEffectsPacketRange.packetCount == shadowEffectsTimingTicketCount
            && (hardwareShadowSupported || (
                m_deferredSoftwareCausticsTask.valid()
                && taskIsCompiled(m_deferredSoftwareCausticsTask)
            ))
            && shadowEffectsSubmitter.submitTaskRangeInCompileOrderFromTasks(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                m_deferredShadowVisibilityTask,
                hardwareShadowSupported ? m_deferredShadowVisibilityTask : m_deferredSoftwareCausticsTask,
                shadowEffectsCompletionTokens,
                shadowEffectsCompletionCount,
                shadowEffectsTimingTickets,
                shadowEffectsTimingTicketCount,
                m_deferredLightingSubmissionTransaction,
                shadowEffectsScratchArena
            )
        ;
        shadowVisibilitySubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
            m_deferredLightingCompiledGraph,
            m_deferredShadowVisibilityTask
        );
        softwareCausticsSubmissionToken = !hardwareShadowSupported
            ? m_deferredLightingSubmissionTransaction.taskToken(
                m_deferredLightingCompiledGraph,
                m_deferredSoftwareCausticsTask
            )
            : Core::QueueSubmissionToken{}
        ;
        if(!shadowVisibilitySubmissionToken.valid()){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            restoreShadowCpuState();
            restoreUnacceptedShadowEffectsCpuState();
            m_raytracingSystem.discardSoftShadowTemporalHistory();
            resetRejectedShadowVisibilityStateHandoffs();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred shadow-visibility packet was rejected"));
            if(!recovered)
                failFrameRenderRecovery();
            return;
        }

        const bool softwareCausticsAccepted = hardwareShadowSupported || (
            shadowEffectsSubmitted && softwareCausticsSubmissionToken.valid()
        );

        if(shadowVisibilityRunsOnCompute){
            // Retain only the cross-queue return state; shared inputs come from next frame's prefix.
            // Retain accepted producer state for recovery after later rejection.
            const bool producerReturnStatesReady =
                m_shadowVisibilityReturnState.replaceTextureSubset(
                    *shadowVisibilityFinalStateSeed,
                    deferredTargets.shadowVisibility
                )
            ;
            if(!producerReturnStatesReady){
                const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
                discardTimingTickets();
                restoreUnacceptedShadowEffectsCpuState();
                m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
                if(!recovered)
                    failFrameRenderRecovery();
                // Missing accepted producer state leaves no safe next layout.
                failFrameRenderRecovery();
                return;
            }
        }
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
            m_rayTracingState.m_swShadowEdgeStatsBuffer,
            m_rayTracingState.m_swShadowEdgeStatsReadback,
            m_rayTracingState.m_swShadowEdgeCounterBuffer,
            m_rayTracingState.m_swShadowEdgeListBuffer,
            m_rayTracingState.m_swShadowIndirectArgsBuffer,
        };
        if(!m_shadowComputePersistentState.replaceResourceSubset(
            *shadowVisibilityFinalStateSeed,
            shadowComputeScratchTextures,
            LengthOf(shadowComputeScratchTextures),
            shadowComputeScratchBuffers,
            LengthOf(shadowComputeScratchBuffers)
        )){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            restoreUnacceptedShadowEffectsCpuState();
            m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
            if(!recovered)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to recover an accepted deferred packet before abandoning shadow scratch state"));
            // Missing private shadow scratch leaves no safe layout restoration.
            failFrameRenderRecovery();
            return;
        }

        m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
        if(!hardwareShadowSupported && (!softwareCausticsAccepted || !softwareCausticsSubmissionToken.valid())){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            restoreUnacceptedShadowEffectsCpuState();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred software-caustics packet was rejected"));
            if(!recovered)
                failFrameRenderRecovery();
            return;
        }
        if(!submitDeferredSurfelGi())
            return;

        if(hardwareShadowSupported){
            Core::GpuTaskGraphExternalCompletionToken hardwareCausticsCompletionTokens[
                __hidden_renderer_system::s_SingleExternalCompletionTokenCapacity
            ] = {};
            usize hardwareCausticsCompletionCount = 0u;
            if(laggedLightingHistoryWriterWaitPending){
                hardwareCausticsCompletionTokens[hardwareCausticsCompletionCount++] = {
                    .completion = m_deferredLightingHistoryCompletion,
                    .token = laggedLightingHistoryWriterCompletionToken,
                };
            }
            Core::Alloc::ScratchArena hardwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
            const Core::GpuTaskGraphSubmitter hardwareCausticsSubmitter(device);
            struct HardwareCausticsAcceptanceContext{
                RendererSystem* renderer = nullptr;
                DeferredFrameTargets* targets = nullptr;
                const Core::CommandListResourceStateHandoff* finalState = nullptr;
                bool usesLaggedHistory = false;
                bool stateReady = true;
            };
            HardwareCausticsAcceptanceContext hardwareCausticsAcceptance{
                .renderer = this,
                .targets = &deferredTargets,
                .finalState = hardwareCausticsFinalStateSeed,
                .usesLaggedHistory = laggedAsyncLightingSchedule,
            };
            const auto acceptHardwareCausticsTask = [](
                void* const rawContext,
                const Core::QueueSubmissionToken& token
            ) -> bool {
                static_cast<void>(token);
                HardwareCausticsAcceptanceContext* const context =
                    static_cast<HardwareCausticsAcceptanceContext*>(rawContext)
                ;
                if(!context)
                    return false;
                if(!context->renderer || !context->targets || !context->finalState){
                    context->stateReady = false;
                    return false;
                }
                // The accumulator is the only hardware-caustics scratch read on a warm frame. Retain it only after
                // the producer packet accepts so a rejected record keeps the prior native state source intact.
                context->stateReady = context->renderer->m_hardwareCausticAccumulatorPersistentState.replaceTextureSubset(
                    *context->finalState,
                    context->targets->causticAccumulator
                );
                if(context->stateReady && context->usesLaggedHistory){
                    context->stateReady = context->renderer->m_causticIrradianceLightingState.replaceTextureSubset(
                        *context->finalState,
                        context->targets->causticIrradiance
                    );
                }
                return context->stateReady;
            };
            const Core::GpuTaskGraphTaskAcceptedCallback hardwareCausticsAcceptedCallback{
                .task = m_deferredHardwareCausticsTask,
                .context = &hardwareCausticsAcceptance,
                .invoke = acceptHardwareCausticsTask,
            };
            const Core::GpuTaskGraphTaskTimingTicket hardwareCausticsTimingTickets[] = {
                Core::GpuTaskGraphTaskTimingTicket{
                    .task = m_deferredHardwareCausticsTask,
                    .timingTicket = &hardwareCausticsTimingTicket,
                },
            };
            const bool hardwareCausticsAccepted =
                m_deferredLightingTaskGraphValid
                && m_deferredHardwareCausticsTask.valid()
                && (!laggedLightingHistoryWriterWaitPending || (
                    m_deferredLightingHistoryCompletion.valid()
                    && laggedLightingHistoryWriterCompletionToken.valid()
                ))
                && taskIsCompiled(m_deferredHardwareCausticsTask)
                && hardwareCausticsPacketRange.valid()
                && hardwareCausticsPacketRange.packetCount == LengthOf(hardwareCausticsTimingTickets)
                && hardwareCausticsSubmitter.submitTaskRangeInCompileOrderFromTasks(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph,
                    m_deferredHardwareCausticsTask,
                    m_deferredHardwareCausticsTask,
                    hardwareCausticsCompletionTokens,
                    hardwareCausticsCompletionCount,
                    hardwareCausticsTimingTickets,
                    LengthOf(hardwareCausticsTimingTickets),
                    m_deferredLightingSubmissionTransaction,
                    hardwareCausticsScratchArena,
                    nullptr,
                    nullptr,
                    nullptr,
                    0u,
                    &hardwareCausticsAcceptedCallback,
                    1u
                )
            ;
            hardwareCausticsSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(
                m_deferredLightingCompiledGraph,
                m_deferredHardwareCausticsTask
            );
            const bool hardwareCausticsStateReady =
                hardwareCausticsAccepted
                &&
                hardwareCausticsSubmissionToken.valid()
                && hardwareCausticsAcceptance.stateReady
            ;
            if(!hardwareCausticsStateReady){
                const bool hardwareCausticsWasAccepted = hardwareCausticsSubmissionToken.valid();
                const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
                discardTimingTickets();
                if(!hardwareCausticsWasAccepted)
                    restoreCausticsCpuState();
                restoreAvboitCpuState();
                resetAbandonedFrameStateHandoffs();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred hardware caustics was unavailable"));
                if(!recovered)
                    failFrameRenderRecovery();
                if(hardwareCausticsWasAccepted)
                    failFrameRenderRecovery();
                return;
            }
        }
        if(!hardwareShadowSupported && laggedAsyncLightingSchedule){
            if(!m_causticIrradianceLightingState.replaceTextureSubset(
                *causticsFinalStateSeed,
                deferredTargets.causticIrradiance
            )){
                const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
                discardTimingTickets();
                restoreAvboitCpuState();
                if(!recovered)
                    failFrameRenderRecovery();
                failFrameRenderRecovery();
                return;
            }
        }
        if(!hardwareShadowSupported && softwareCausticsRunsOnCompute){
            const bool softwareCausticsStateReady =
                m_causticIrradianceReturnState.replaceTextureSubset(
                    *causticsFinalStateSeed,
                    deferredTargets.causticIrradiance
                )
            ;
            if(!softwareCausticsStateReady){
                const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
                discardTimingTickets();
                restoreAvboitCpuState();
                if(!recovered)
                    failFrameRenderRecovery();
                // An accepted Compute producer without retained output state cannot safely span frames.
                failFrameRenderRecovery();
                return;
            }

            const Core::TextureHandle causticsComputeScratchTextures[] = {
                deferredTargets.causticAccumulator,
                deferredTargets.causticHistory,
                deferredTargets.causticResolveHalf,
                deferredTargets.causticResolveGeometry,
            };
            if(!m_causticsComputePersistentState.replaceResourceSubset(
                *causticsFinalStateSeed,
                causticsComputeScratchTextures,
                LengthOf(causticsComputeScratchTextures),
                nullptr,
                0u
            )){
                const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
                discardTimingTickets();
                restoreAvboitCpuState();
                if(!recovered)
                    failFrameRenderRecovery();
                failFrameRenderRecovery();
                return;
            }
        }
        if(!submitAvboitLightingAndComposite() || !submitDeferredPresent())
            return;
    }

    if(m_deferredSurfelGiCounterReadbackTask.valid()){
        // The readback has no normal-frame consumer, so record it only after Present. Its Counter source imports the
        // compiler-retained Surfel-GI packet state through the declared dependency; recording needs no manual state
        // seed or synthetic queue wait, while the resulting tail state is retained for the next frame.
        const bool readbackTailAvailable =
            finalPresentationSubmissionToken.valid()
            && m_deferredLightingTaskGraphValid
            && m_deferredLightingCompiledGraph.findTask(m_deferredSurfelGiCounterReadbackTask)
            && surfelGiCounterReadbackQueue
            && (static_cast<u8>(surfelGiCounterReadbackQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) != 0u
        ;
        if(!readbackTailAvailable){
            m_deferredLightingSubmissionTransaction.rejectTask(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredSurfelGiCounterReadbackTask
            );
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback tail was unavailable"));
        }
        else{
            Core::GpuPersistentResourceStateCache::Candidate readbackCounterStateCandidate(m_arena);
            const Core::BufferHandle readbackCounterBuffers[] = {
                m_rayTracingState.m_surfelCounterBuffer,
            };
            // Keep the filtered final-state candidate private until the Transfer task accepts, so a rejected
            // readback cannot replace the last accepted Surfel-GI counter state.
            struct SurfelCounterReadbackRecordContext{
                RendererSystem* renderer = nullptr;
                Core::GpuPersistentResourceStateCache::Candidate* candidate = nullptr;
                const Core::BufferHandle* buffers = nullptr;
                usize bufferCount = 0u;
                bool finalStateReady = false;
            } readbackRecordContext{
                .renderer = this,
                .candidate = &readbackCounterStateCandidate,
                .buffers = readbackCounterBuffers,
                .bufferCount = LengthOf(readbackCounterBuffers),
            };
            const auto prepareReadbackFinalState = [](
                void* const rawContext,
                const Core::CommandListResourceStateHandoff* const finalState
            ) -> bool {
                SurfelCounterReadbackRecordContext* const context =
                    static_cast<SurfelCounterReadbackRecordContext*>(rawContext)
                ;
                if(!context || !context->renderer || !context->candidate || !context->buffers)
                    return false;
                context->finalStateReady = finalState
                    && context->renderer->m_surfelGiCounterPersistentState.buildFilteredBufferSubset(
                        *context->candidate,
                        *finalState,
                        context->buffers,
                        context->bufferCount
                    )
                ;
                return context->finalStateReady;
            };
            const Core::GpuTaskGraphTaskRecordedCallback readbackRecordedCallback{
                .context = &readbackRecordContext,
                .invoke = prepareReadbackFinalState,
            };
            Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
            const Core::GpuNativePacketRecorder recorder(device);
            const Core::GpuTaskGraphSubmitter submitter(device);
            const bool readbackAccepted = submitter.recordAndSubmitTask(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                recorder,
                m_deferredLightingRecordedGraph,
                m_deferredSurfelGiCounterReadbackTask,
                nullptr,
                0u,
                &readbackRecordedCallback,
                m_deferredLightingSubmissionTransaction,
                scratchArena
            );
            const Core::QueueSubmissionToken readbackSubmissionToken = readbackAccepted
                ? m_deferredLightingSubmissionTransaction.taskToken(
                    m_deferredLightingCompiledGraph,
                    m_deferredSurfelGiCounterReadbackTask
                )
                : Core::QueueSubmissionToken{}
            ;
            if(!readbackSubmissionToken.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback record/submission was rejected"));
            }
            else{
                if(!m_surfelGiCounterPersistentState.commit(readbackCounterStateCandidate)){
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: accepted surfel counter-readback tail lost its retained state"));
                    failFrameRenderRecovery();
                    return;
                }
                // addCopyBufferTask publishes only on accepted submission, keeping CPU polling tied to the
                // selected physical transport rather than the preceding Surfel-GI packet.
                NWB_ASSERT(
                    m_rayTracingState.m_surfelCountReadbackSubmissionToken.queue == readbackSubmissionToken.queue
                    && m_rayTracingState.m_surfelCountReadbackSubmissionToken.value == readbackSubmissionToken.value
                    && m_rayTracingState.m_surfelCountReadbackSubmissionToken.matchesPhysicalQueue(
                        readbackSubmissionToken.physicalQueueIndex,
                        readbackSubmissionToken.deviceGeneration
                    )
                );
            }
        }
    }

    if(requestsLaggedLightingHistoryCapture && !captureLaggedLightingHistory){
        // The optional tail could not be built, but Present already completed through the durable deferred graph.
        // Match the former standalone-copy fallback by forcing the next frame through bootstrap.
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting"));
        invalidateLaggedLightingHistorySubmission();
    }
    else if(captureLaggedLightingHistory){
        // The deferred graph's terminal history-copy task depends on Present internally. Its retained producer
        // snapshots are only available after those producers accept, so this remains a late record into the shared
        // recorded graph rather than part of the initial packet prefix; its source binding stays task-anchored.
        const Core::CommandListResourceStateHandoff* const causticHistoryCopySource = laggedAsyncLightingSchedule
            ? m_causticIrradianceLightingState.source()
            : m_causticIrradianceReturnState.source()
        ;
        Core::GpuExternalPacketStateSource historyCopyStateSources[
            __hidden_renderer_system::s_LaggedLightingHistoryStateSourceCapacity
        ] = {};
        usize historyCopyStateSourceCount = 0u;
        const bool historyCopyStateSourcesReady =
            appendDeclaredStateSource(
                historyCopyStateSources,
                LengthOf(historyCopyStateSources),
                historyCopyStateSourceCount,
                m_shadowVisibilityReturnState.source()
            )
            && appendDeclaredStateSource(
                historyCopyStateSources,
                LengthOf(historyCopyStateSources),
                historyCopyStateSourceCount,
                causticHistoryCopySource
            )
            && appendDeclaredStateSource(
                historyCopyStateSources,
                LengthOf(historyCopyStateSources),
                historyCopyStateSourceCount,
                m_surfelIrradianceReturnState.source()
            )
        ;
        if(!historyCopyStateSourcesReady){
            if(!m_deferredLightingSubmissionTransaction.discardUnaccepted(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph.recordingAttemptGeneration()
            ))
                m_graphics.requestDeviceRecreation();
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history capture skipped because its source state was unavailable"));
            invalidateLaggedLightingHistorySubmission();
        }
        else if(
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
            const Core::GpuTaskPacketStateBinding historyCopyStateBindings[] = {
                Core::GpuTaskPacketStateBinding{
                    .task = m_deferredLaggedLightingHistoryTask,
                    .externalStateSources = historyCopyStateSources,
                    .externalStateSourceCount = historyCopyStateSourceCount,
                },
            };
            struct HistoryCopyRecordContext{
                const Core::CommandListResourceStateHandoff* finalState = nullptr;
            } historyCopyRecordContext;
            const auto retainHistoryCopyFinalState = [](
                void* const rawContext,
                const Core::CommandListResourceStateHandoff* const finalState
            ) -> bool {
                HistoryCopyRecordContext* const context = static_cast<HistoryCopyRecordContext*>(rawContext);
                if(!context || !finalState)
                    return false;
                context->finalState = finalState;
                return true;
            };
            const Core::GpuTaskGraphTaskRecordedCallback historyCopyRecordedCallback{
                .context = &historyCopyRecordContext,
                .invoke = retainHistoryCopyFinalState,
            };
            Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
            const Core::GpuNativePacketRecorder recorder(device);
            const Core::GpuTaskGraphSubmitter submitter(device);
            const bool historyCopyAccepted = submitter.recordAndSubmitTask(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                recorder,
                m_deferredLightingRecordedGraph,
                m_deferredLaggedLightingHistoryTask,
                historyCopyStateBindings,
                LengthOf(historyCopyStateBindings),
                &historyCopyRecordedCallback,
                m_deferredLightingSubmissionTransaction,
                scratchArena
            );
            const Core::CommandListResourceStateHandoff* const historyCopyFinalStateSeed =
                historyCopyRecordContext.finalState
            ;
            if(!historyCopyAccepted || !historyCopyFinalStateSeed){
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
                const Core::QueueSubmissionToken historyCopySubmissionToken =
                    m_deferredLightingSubmissionTransaction.taskToken(
                        m_deferredLightingCompiledGraph,
                        m_deferredLaggedLightingHistoryTask
                    )
                ;
                if(!historyCopySubmissionToken.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned lagged lighting-history capture submission was rejected; reverting to current-frame lighting"));
                    invalidateLaggedLightingHistorySubmission();
                }
                else{
                    const bool historyCopyReturnStatesReady =
                        m_shadowVisibilityReturnState.replaceTextureSubset(
                            *historyCopyFinalStateSeed,
                            deferredTargets.shadowVisibility
                        )
                        && m_causticIrradianceReturnState.replaceTextureSubset(
                            *historyCopyFinalStateSeed,
                            deferredTargets.causticIrradiance
                        )
                        && m_surfelIrradianceReturnState.replaceTextureSubset(
                            *historyCopyFinalStateSeed,
                            deferredTargets.surfelIrradiance
                        )
                    ;
                    if(!historyCopyReturnStatesReady){
                        if(!submitFrameRecoveryPacket())
                            failFrameRenderRecovery();
                        // Lost retained copy state leaves no safe producer layout.
                        failFrameRenderRecovery();
                        return;
                    }

                    // The task's accepted hook publishes this exact token.  Keep the assertion close to the handoff
                    // so a future lifecycle change cannot accidentally reintroduce a renderer-side publication path.
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

