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
    , m_deferredLightingTaskGraph(arena)
    , m_deferredLightingTaskGraphAnalysis(arena)
    , m_deferredLightingTaskGraphQueueAssignments(arena)
    , m_deferredLightingCompiledGraph(arena)
    , m_deferredLightingRecordedGraph(arena)
    , m_deferredLightingSubmissionTransaction(arena)
    , m_meshState(arena)
    , m_materialState(arena)
    , m_rayTracingState(arena)
    , m_shadowComputePersistentStateHandoff(arena)
    , m_shadowVisibilityReturnStateHandoff(arena)
    , m_shadowPreparePersistentStateHandoff(arena)
    , m_shadowPreparePersistentStateBuffers(arena)
    , m_causticsComputePersistentStateHandoff(arena)
    , m_causticIrradianceLightingStateHandoff(arena)
    , m_causticIrradianceReturnStateHandoff(arena)
    , m_surfelGiComputePersistentStateHandoff(arena)
    , m_surfelGiCounterPersistentStateHandoff(arena)
    , m_surfelIrradianceReturnStateHandoff(arena)
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
}
RendererSystem::~RendererSystem(){}


#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
void RendererSystem::forceHybridSceneTraversalFallbackForTesting()noexcept{
    m_raytracingSystem.forceHybridSceneTraversalFallbackForTesting();
}

void RendererSystem::forceHybridSceneTraversalFallbackEveryFrameForTesting()noexcept{
    m_raytracingSystem.forceHybridSceneTraversalFallbackEveryFrameForTesting();
}

void RendererSystem::forceHybridHardwareFallbackSnapshotStaleForTesting()noexcept{
    m_raytracingSystem.forceHybridHardwareFallbackSnapshotStaleForTesting();
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

void RendererSystem::resetLaggedLightingHistoryTracking()noexcept{
    invalidateLaggedLightingHistorySubmission();
    m_laggedLightingHistoryGeneration = 0u;
}

void RendererSystem::resetTargetGenerationStateHandoffs()noexcept{
    // Replaced targets invalidate retained compute-local state.
    m_shadowComputePersistentStateHandoff.reset();
    m_shadowVisibilityReturnStateHandoff.reset();
    m_causticsComputePersistentStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_causticIrradianceReturnStateHandoff.reset();
    m_surfelGiComputePersistentStateHandoff.reset();
    m_surfelGiCounterPersistentStateHandoff.reset();
    m_surfelIrradianceReturnStateHandoff.reset();
}

void RendererSystem::resetInvalidatedResourceStateHandoffs()noexcept{
    // The shadow-preparation packet owns its serial export; only retained cross-frame state is reset here.
    resetTargetGenerationStateHandoffs();
    m_shadowPreparePersistentStateHandoff.reset();
    m_shadowPreparePersistentStateBuffers.clear();
}

void RendererSystem::resetFrameRecordingStateHandoffs()noexcept{
    // Preserve accepted compute-local state across fresh recordings.
    m_causticIrradianceLightingStateHandoff.reset();
}

void RendererSystem::resetAbandonedFrameStateHandoffs()noexcept{
    // Rejected frames keep only cross-frame state from earlier accepted producers.
    m_causticIrradianceLightingStateHandoff.reset();
}

void RendererSystem::resetRejectedShadowVisibilityStateHandoffs()noexcept{
    // Preserve accepted Graphics prefix state when graph-owned shadow visibility rejects.
    m_causticIrradianceLightingStateHandoff.reset();
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
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearFirstTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredLightingTaskGraphValid = false;
    m_deferredShadowPrepareTask = {};
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
    m_deferredCausticProducerDispatched = false;
    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitClearTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitOccupancyTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionStreamTask = {};
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationStreamTask = {};
    m_deferredAvboitAccumulationTask = {};
    m_deferredAvboitAccumulationFinalizeTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentationOverlayTask = {};
    m_deferredPresentTask = {};
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
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredLightingTaskGraphValid = false;
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
    m_deferredCausticProducerDispatched = false;
    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitClearTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitOccupancyTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionStreamTask = {};
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationStreamTask = {};
    m_deferredAvboitAccumulationTask = {};
    m_deferredAvboitAccumulationFinalizeTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentationOverlayTask = {};
    m_deferredPresentTask = {};
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

    if(laggedAsyncLightingRequested){
        const u64 historyGeneration = deferredTargets.laggedLightingHistory.generation;
        if(m_laggedLightingHistoryGeneration != historyGeneration){
            // Target generations prevent history from using recycled slots after resize.
            invalidateLaggedLightingHistorySubmission();
            m_laggedLightingHistoryGeneration = historyGeneration;
        }
    }
    else{
        resetLaggedLightingHistoryTracking();
    }
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    const bool laggedAsyncLightingSchedule =
        laggedAsyncLightingRequested
        && laggedLightingHistoryResourcesReady
        && m_laggedLightingHistorySubmissionToken.valid()
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
        .laggedLightingHistoryAccepted = m_laggedLightingHistorySubmissionToken.valid(),
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
        &graphicsPrefixCsgIntervalSampleTimingTicket,
        &graphicsPrefixNormalizeTimingTicket,
    };
    Core::GpuTimingSubmissionTicket* const graphicsPrefixOwnedTimingTickets[graphicsPrefixTimingTicketCount] = {
        &graphicsPrefixMeshViewSetupTimingTicket,
        &graphicsPrefixSceneShadingSetupTimingTicket,
        &graphicsPrefixDeferredClearTimingTicket,
        &graphicsPrefixGbufferTimingTicket,
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
    Core::GpuTimingSubmissionTicket shadowVisibilityTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket softwareCausticsTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket surfelGiTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket hardwareCausticsTimingTicket(m_graphics.gpuTiming());
    // A warm temporal decay starts this interval in its graph task and the selected photon producer closes it.
    Optional<Core::GpuTimingMeasure> causticPhotonTiming;
    // Geometry downsample begins the resolve interval and wavelet resolve closes it in the same selected packet.
    Optional<Core::GpuTimingMeasure> causticResolveTiming;
    const bool clearAvboitTargets = hasTransparentRenderers || m_avboitState.m_targetsNeedClear;
    Core::GpuTimingSubmissionTicket avboitPreTimingTicket(m_graphics.gpuTiming());
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
        shadowPrepareTimingTicket,
        graphicsPrefixTimingTickets,
        &asyncPrefixTimingSpansOnePacket,
        asyncFinalTiming,
        avboitPreTimingTicket,
        avboitDepthWarpTimingTicket,
        avboitExtinctionTimingTicket,
        avboitIntegrationTimingTicket,
        avboitAccumulationTimingTicket,
        shadowVisibilityTimingTicket,
        softwareCausticsTimingTicket,
        surfelGiTimingTicket,
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
            shadowPrepareTimingTicket,
            graphicsPrefixTimingTickets,
            &asyncPrefixTimingSpansOnePacket,
            asyncFinalTiming,
            avboitPreTimingTicket,
            avboitDepthWarpTimingTicket,
            avboitExtinctionTimingTicket,
            avboitIntegrationTimingTicket,
            avboitAccumulationTimingTicket,
            shadowVisibilityTimingTicket,
            softwareCausticsTimingTicket,
            surfelGiTimingTicket,
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
    const Core::GpuSubmissionPacketId shadowPreparePacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredShadowPrepareTask
    );
    const Core::GpuSubmissionPacketId deferredBindlessSlotsUploadPacket =
        m_deferredBindlessSlotsUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_deferredBindlessSlotsUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    // The prefix submission range is anchored at Shadow Preparation. A selector upload is safe only when the
    // compiler keeps it in that exact packet; otherwise it would be recorded but omitted from the accepted range.
    const bool deferredBindlessSlotsUploadMergedIntoShadowPreparePacket =
        !m_deferredBindlessSlotsUploadTask.valid()
        || (
            shadowPreparePacket.valid()
            && deferredBindlessSlotsUploadPacket.valid()
            && deferredBindlessSlotsUploadPacket == shadowPreparePacket
        )
    ;
    const Core::GpuSubmissionPacketId rayTraceMaterialContextSlotsUploadPacket =
        m_rayTraceMaterialContextSlotsUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_rayTraceMaterialContextSlotsUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    // This selector is consumed by later Compute trace tasks, but the prefix submission range starts at Shadow
    // Preparation. Keep the immutable upload in that exact first packet so Shadow Preparation becomes the handoff.
    const bool rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket =
        !m_rayTraceMaterialContextSlotsUploadTask.valid()
        || (
            shadowPreparePacket.valid()
            && rayTraceMaterialContextSlotsUploadPacket.valid()
            && rayTraceMaterialContextSlotsUploadPacket == shadowPreparePacket
        )
    ;
    const Core::GpuSubmissionPacketId causticEmissionTargetsUploadPacket =
        m_causticEmissionTargetsUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_causticEmissionTargetsUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    // The caustic input upload is optional for an empty refractive set, but a nonempty payload must live in the
    // exact Shadow Preparation packet that owns the following ShaderResource handoff to later Compute consumers.
    const bool causticEmissionTargetsUploadMergedIntoShadowPreparePacket =
        !m_causticEmissionTargetsUploadTask.valid()
        || (
            shadowPreparePacket.valid()
            && causticEmissionTargetsUploadPacket.valid()
            && causticEmissionTargetsUploadPacket == shadowPreparePacket
        )
    ;
    const Core::GpuSubmissionPacketId surfelFrameConstantsUploadPacket =
        m_surfelFrameConstantsUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_surfelFrameConstantsUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    // Active surfel frames freeze a fresh constant payload before compilation. It must share Shadow Preparation's
    // first Graphics packet so that task remains the handoff producer for the asynchronous Surfel-GI consumer.
    const bool surfelFrameConstantsUploadMergedIntoShadowPreparePacket =
        !m_surfelFrameConstantsUploadTask.valid()
        || (
            shadowPreparePacket.valid()
            && surfelFrameConstantsUploadPacket.valid()
            && surfelFrameConstantsUploadPacket == shadowPreparePacket
        )
    ;
    const Core::GpuSubmissionPacketId shadowInstanceMaterialUploadPacket =
        m_shadowInstanceMaterialUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_shadowInstanceMaterialUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId shadowInstanceUploadPacket =
        m_shadowInstanceUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_shadowInstanceUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId shadowMaterialTypedUploadPacket =
        m_shadowMaterialTypedUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_shadowMaterialTypedUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    // This ABI-coupled triple must remain in the first accepted Graphics packet. Shadow Preparation supersedes the
    // upload writers as the ShaderResource producer observed by later asynchronous trace passes.
    const bool shadowMaterialContextUploadsMergedIntoShadowPreparePacket =
        (!m_shadowInstanceMaterialUploadTask.valid() || (
            shadowPreparePacket.valid()
            && shadowInstanceMaterialUploadPacket.valid()
            && shadowInstanceMaterialUploadPacket == shadowPreparePacket
        ))
        && (!m_shadowInstanceUploadTask.valid() || (
            shadowPreparePacket.valid()
            && shadowInstanceUploadPacket.valid()
            && shadowInstanceUploadPacket == shadowPreparePacket
        ))
        && (!m_shadowMaterialTypedUploadTask.valid() || (
            shadowPreparePacket.valid()
            && shadowMaterialTypedUploadPacket.valid()
            && shadowMaterialTypedUploadPacket == shadowPreparePacket
        ))
    ;
    const Core::GpuSubmissionPacketId sceneBvhNodesUploadPacket =
        m_sceneBvhNodesUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_sceneBvhNodesUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId sceneBvhInstancesUploadPacket =
        m_sceneBvhInstancesUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_sceneBvhInstancesUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    // Scene-BVH nodes index the companion leaf-instance stream. Keep the whole immutable pair in the accepting
    // Shadow Preparation packet so it becomes the only ShaderResource producer exposed to later Compute work.
    const bool sceneBvhUploadsMergedIntoShadowPreparePacket =
        m_sceneBvhNodesUploadTask.valid() == m_sceneBvhInstancesUploadTask.valid()
        && (!m_sceneBvhNodesUploadTask.valid() || (
            shadowPreparePacket.valid()
            && sceneBvhNodesUploadPacket.valid()
            && sceneBvhInstancesUploadPacket.valid()
            && sceneBvhNodesUploadPacket == shadowPreparePacket
            && sceneBvhInstancesUploadPacket == shadowPreparePacket
        ))
    ;
    const Core::GpuSubmissionPacketId graphicsPrefixPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_graphicsPrefixTask
    );
    const Core::GpuSubmissionPacketId graphicsPrefixMeshViewSetupPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_graphicsPrefixMeshViewSetupTask);
    const Core::GpuSubmissionPacketId graphicsPrefixSceneShadingSetupPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_graphicsPrefixSceneShadingSetupTask);
    const Core::GpuSubmissionPacketId graphicsPrefixDeferredClearPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_graphicsPrefixDeferredClearTask);
    const Core::GpuSubmissionPacketId graphicsPrefixDeferredClearFirstPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_graphicsPrefixDeferredClearFirstTask);
    const Core::GpuSubmissionPacketId graphicsPrefixGbufferPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_graphicsPrefixGbufferTask);
    // The opaque CSG sample task exists only when this frame has semantic CSG work. In ordinary frames its timing
    // slot aliases G-buffer, so the fixed prefix ticket table still has one ordered anchor per semantic position.
    const Core::GpuSubmissionPacketId graphicsPrefixCsgIntervalSamplePacket =
        m_graphicsPrefixCsgIntervalSampleTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_graphicsPrefixCsgIntervalSampleTask)
            : graphicsPrefixGbufferPacket
    ;
    const Core::GpuSubmissionPacketId graphicsPrefixTaskPackets[graphicsPrefixTimingTicketCount] = {
        graphicsPrefixMeshViewSetupPacket,
        graphicsPrefixSceneShadingSetupPacket,
        graphicsPrefixDeferredClearPacket,
        graphicsPrefixGbufferPacket,
        graphicsPrefixCsgIntervalSamplePacket,
        graphicsPrefixPacket,
    };
    bool graphicsPrefixTimingBindingsValid = true;
    usize graphicsPrefixUniquePacketCount = 0u;
    for(usize prefixTaskIndex = 0u; prefixTaskIndex < graphicsPrefixTimingTicketCount; ++prefixTaskIndex){
        const Core::GpuSubmissionPacketId packet = graphicsPrefixTaskPackets[prefixTaskIndex];
        if(
            !packet.valid()
            || (prefixTaskIndex != 0u && packet.index < graphicsPrefixTaskPackets[prefixTaskIndex - 1u].index)
        ){
            graphicsPrefixTimingBindingsValid = false;
            break;
        }
        bool sharesPacketWithEarlierTask = false;
        for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
            if(packet != graphicsPrefixTaskPackets[earlierTaskIndex])
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
        && graphicsPrefixMeshViewSetupPacket == graphicsPrefixPacket
    ;
    const Core::GpuSubmissionPacketId shadowVisibilityPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredShadowVisibilityTask
    );
    const Core::GpuSubmissionPacketId softwareCausticsPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredSoftwareCausticsTask
    );
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = shadowVisibilityPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(shadowVisibilityPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixQueue = graphicsPrefixPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(graphicsPrefixPacket).queue
        )
        : nullptr
    ;
    bool graphicsPrefixPacketsAreGraphics = graphicsPrefixTimingBindingsValid;
    for(usize prefixTaskIndex = 0u;
        graphicsPrefixPacketsAreGraphics && prefixTaskIndex < graphicsPrefixTimingTicketCount;
        ++prefixTaskIndex
    ){
        const Core::GpuSubmissionPacketId packet = graphicsPrefixTaskPackets[prefixTaskIndex];
        const Core::GpuPhysicalQueueInfo* const queue = packet.valid()
            ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(packet).queue)
            : nullptr
        ;
        graphicsPrefixPacketsAreGraphics = queue && queue->queueClass == Core::CommandQueue::Graphics;
    }
    // The deferred-clear measure begins and ends inside the first and terminal typed clear tasks.  The terminal
    // clear owns the later asynchronous handoff, so do not record unless the entire bracket is one Graphics packet.
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixDeferredClearQueue =
        graphicsPrefixDeferredClearPacket.valid()
            ? m_deferredLightingCompiledGraph.queueInfo(
                m_deferredLightingCompiledGraph.packet(graphicsPrefixDeferredClearPacket).queue
            )
            : nullptr
    ;
    const bool graphicsPrefixDeferredClearBundleMerged =
        graphicsPrefixDeferredClearFirstPacket.valid()
        && graphicsPrefixDeferredClearPacket.valid()
        && graphicsPrefixDeferredClearFirstPacket == graphicsPrefixDeferredClearPacket
        && graphicsPrefixDeferredClearQueue
        && graphicsPrefixDeferredClearQueue->queueClass == Core::CommandQueue::Graphics
    ;
    const Core::GpuPhysicalQueueInfo* const shadowPrepareQueue = shadowPreparePacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(shadowPreparePacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const softwareCausticsQueue = softwareCausticsPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(softwareCausticsPacket).queue
        )
        : nullptr
    ;
    const bool shadowVisibilityRunsOnCompute = shadowVisibilityQueue
        && shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute
    ;
    const bool softwareCausticsRunsOnCompute = !hardwareShadowSupported
        && softwareCausticsQueue
        && softwareCausticsQueue->queueClass == Core::CommandQueue::Compute
    ;
    const Core::GpuSubmissionPacketId avboitPrePacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitPreTask
    );
    const Core::GpuSubmissionPacketId avboitClearPacket = m_deferredAvboitClearTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredAvboitClearTask)
        : avboitPrePacket
    ;
    const Core::GpuSubmissionPacketId avboitOccupancyPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitOccupancyTask
    );
    const bool avboitPrePacketContainsClear = !clearAvboitTargets || (
        avboitClearPacket.valid()
        && avboitPrePacket.valid()
        && avboitClearPacket == avboitPrePacket
    );
    const bool avboitPrePacketContainsOccupancy = avboitPrePacket.valid()
        && avboitOccupancyPacket.valid()
        && avboitPrePacket == avboitOccupancyPacket
    ;
    const Core::GpuPhysicalQueueInfo* const avboitPreQueue = avboitPrePacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(avboitPrePacket).queue)
        : nullptr
    ;
    const bool avboitUsesAsyncCompute = m_deferredAvboitDepthWarpTask.valid();
    const Core::GpuSubmissionPacketId avboitDepthWarpPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitDepthWarpTask
    );
    const Core::GpuSubmissionPacketId avboitExtinctionPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitExtinctionTask
    );
    const Core::GpuSubmissionPacketId avboitExtinctionStreamPacket =
        m_deferredAvboitExtinctionStreamTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredAvboitExtinctionStreamTask)
        : Core::GpuSubmissionPacketId{}
    ;
    const bool avboitExtinctionPacketContainsStreams = !m_deferredAvboitExtinctionStreamTask.valid()
        || (
            avboitExtinctionPacket.valid()
            && avboitExtinctionStreamPacket.valid()
            && avboitExtinctionPacket == avboitExtinctionStreamPacket
        )
    ;
    const bool avboitUnsplitPrePacketContainsExtinction = !hasTransparentRenderers
        || (
            avboitPrePacket.valid()
            && avboitExtinctionPacket.valid()
            && avboitPrePacket == avboitExtinctionPacket
        )
    ;
    const Core::GpuSubmissionPacketId avboitIntegrationPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitIntegrationTask
    );
    const Core::GpuSubmissionPacketId avboitAccumulationPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitAccumulationTask
    );
    const Core::GpuSubmissionPacketId avboitAccumulationFinalizePacket =
        m_deferredAvboitAccumulationFinalizeTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredAvboitAccumulationFinalizeTask)
        : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId avboitAccumulationStreamPacket =
        m_deferredAvboitAccumulationStreamTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredAvboitAccumulationStreamTask)
        : Core::GpuSubmissionPacketId{}
    ;
    const bool avboitAccumulationPacketContainsStreams = !m_deferredAvboitAccumulationStreamTask.valid()
        || (
            avboitAccumulationPacket.valid()
            && avboitAccumulationStreamPacket.valid()
            && avboitAccumulationPacket == avboitAccumulationStreamPacket
        )
    ;
    // The graph-only finalizer lowers the final attachment transition, so it is part of accumulation's accepted
    // Graphics packet and its timing/submission endpoint. A split here would let Composite bypass that handoff.
    const bool avboitAccumulationPacketContainsFinalizer = !hasTransparentRenderers
        || (
            avboitAccumulationPacket.valid()
            && avboitAccumulationFinalizePacket.valid()
            && avboitAccumulationPacket == avboitAccumulationFinalizePacket
        )
    ;
    const bool avboitUnsplitPrePacketContainsAccumulation = !hasTransparentRenderers
        || (
            avboitPrePacket.valid()
            && avboitAccumulationPacket.valid()
            && avboitPrePacket == avboitAccumulationPacket
        )
    ;
    const Core::GpuPhysicalQueueInfo* const avboitDepthWarpQueue = avboitDepthWarpPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(avboitDepthWarpPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionQueue = avboitExtinctionPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(avboitExtinctionPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitIntegrationQueue = avboitIntegrationPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(avboitIntegrationPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationQueue = avboitAccumulationPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(avboitAccumulationPacket).queue
        )
        : nullptr
    ;
    const Core::GpuSubmissionPacketId hardwareCausticsPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredHardwareCausticsTask
    );
    const Core::GpuSubmissionPacketId causticPhotonPacket = m_deferredCausticPhotonTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredCausticPhotonTask)
        : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId causticGeometryPacket = m_deferredCausticGeometryTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredCausticGeometryTask)
        : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId causticResolvePreparePacket = m_deferredCausticResolvePrepareTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredCausticResolvePrepareTask)
        : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId causticResolveWaveletPacket = m_deferredCausticResolveWaveletTask.valid()
        ? m_deferredLightingCompiledGraph.packetForTask(m_deferredCausticResolveWaveletTask)
        : Core::GpuSubmissionPacketId{}
    ;
    const Core::GpuSubmissionPacketId causticIrradianceClearPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredCausticIrradianceClearTask
    );
    const Core::GpuSubmissionPacketId surfelGiPreparationPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredSurfelGiPreparationTask
    );
    const Core::GpuSubmissionPacketId surfelGiSnapshotCopyPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredSurfelGiSnapshotCopyTask
    );
    const Core::GpuSubmissionPacketId surfelGiIrradianceClearPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredSurfelGiIrradianceClearTask
    );
    const Core::GpuSubmissionPacketId surfelGiPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredSurfelGiTask
    );
    const Core::GpuSubmissionPacketId surfelGiCounterReadbackPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_deferredSurfelGiCounterReadbackTask);
    const Core::GpuSubmissionPacketId deferredLightingPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredLightingTask
    );
    const Core::GpuSubmissionPacketId laggedLightingHistorySlotsUploadPacket =
        m_deferredLaggedLightingHistorySlotsUploadTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_deferredLaggedLightingHistorySlotsUploadTask)
            : Core::GpuSubmissionPacketId{}
    ;
    // Deferred Lighting's submission range starts at the Lighting packet. A fresh history-selector upload must
    // compile into that exact packet, or it would be recorded but omitted from its external wait and acceptance.
    const bool laggedLightingHistorySlotsUploadMergedIntoLightingPacket =
        !m_deferredLaggedLightingHistorySlotsUploadTask.valid()
        || (
            deferredLightingPacket.valid()
            && laggedLightingHistorySlotsUploadPacket.valid()
            && laggedLightingHistorySlotsUploadPacket == deferredLightingPacket
        )
    ;
    const Core::GpuSubmissionPacketId deferredCompositePacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredCompositeTask
    );
    const Core::GpuSubmissionPacketId deferredPresentPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredPresentTask
    );
    const Core::GpuSubmissionPacketId deferredPresentationOverlayPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_deferredPresentationOverlayTask);
    const Core::GpuSubmissionPacketId terminalPresentationPacket = deferredPresentationOverlayPacket.valid()
        ? deferredPresentationOverlayPacket
        : deferredPresentPacket
    ;
    const Core::GpuSubmissionPacketId deferredLaggedLightingHistoryPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_deferredLaggedLightingHistoryTask);
    const Core::GpuSubmissionPacketId deferredFrameRecoveryPacket =
        m_deferredLightingCompiledGraph.packetForTask(m_deferredFrameRecoveryTask);
    const Core::GpuPhysicalQueueInfo* const deferredLightingQueue = deferredLightingPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(deferredLightingPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const deferredCompositeQueue = deferredCompositePacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(deferredCompositePacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const deferredPresentQueue = deferredPresentPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(deferredPresentPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const terminalPresentationQueue = terminalPresentationPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(terminalPresentationPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const deferredLaggedLightingHistoryQueue =
        deferredLaggedLightingHistoryPacket.valid()
            ? m_deferredLightingCompiledGraph.queueInfo(
                m_deferredLightingCompiledGraph.packet(deferredLaggedLightingHistoryPacket).queue
            )
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const deferredFrameRecoveryQueue =
        deferredFrameRecoveryPacket.valid()
            ? m_deferredLightingCompiledGraph.queueInfo(
                m_deferredLightingCompiledGraph.packet(deferredFrameRecoveryPacket).queue
            )
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const hardwareCausticsQueue = hardwareCausticsPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(hardwareCausticsPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const surfelGiQueue = surfelGiPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(surfelGiPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const surfelGiPreparationQueue = surfelGiPreparationPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(surfelGiPreparationPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const surfelGiSnapshotCopyQueue = surfelGiSnapshotCopyPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(surfelGiSnapshotCopyPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const surfelGiCounterReadbackQueue = surfelGiCounterReadbackPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(
            m_deferredLightingCompiledGraph.packet(surfelGiCounterReadbackPacket).queue
        )
        : nullptr
    ;
    const bool surfelGiRunsOnCompute = surfelGiQueue && surfelGiQueue->queueClass == Core::CommandQueue::Compute;
    // The clear must remain in GI's semantic packet. If it split, the standard effects range would either gain a
    // hidden submission or record an output write outside the acceptance/timing endpoint it protects.
    const bool surfelGiOutputClearMergedIntoGiPacket =
        m_deferredSurfelGiIrradianceClearTask.valid()
        && surfelGiIrradianceClearPacket.valid()
        && surfelGiPacket.valid()
        && surfelGiIrradianceClearPacket == surfelGiPacket
    ;
    // Both caustic routes retain a black output on a no-producer frame. Keep the typed clear in the selected
    // producer packet so effects timing, acceptance, and the lagged-history wait still protect the first write.
    const Core::GpuSubmissionPacketId causticsPacket = hardwareShadowSupported
        ? hardwareCausticsPacket
        : softwareCausticsPacket
    ;
    // Photon, geometry, resolve prepare, first wavelet, and the timed tail are distinct callbacks so the compiler
    // can lower their immutable and first ping-pong UAV-to-SRV handoffs. They remain one semantic submission: clear
    // acceptance, timing, and all dependent effects keep the established packet endpoint.
    const bool causticPhotonMergedIntoCausticsPacket =
        m_deferredCausticPhotonTask.valid()
        && causticPhotonPacket.valid()
        && causticsPacket.valid()
        && causticPhotonPacket == causticsPacket
    ;
    const bool causticGeometryMergedIntoCausticsPacket =
        m_deferredCausticGeometryTask.valid()
        && causticGeometryPacket.valid()
        && causticsPacket.valid()
        && causticGeometryPacket == causticsPacket
    ;
    const bool causticResolvePrepareMergedIntoCausticsPacket =
        m_deferredCausticResolvePrepareTask.valid()
        && causticResolvePreparePacket.valid()
        && causticsPacket.valid()
        && causticResolvePreparePacket == causticsPacket
    ;
    const bool causticResolveWaveletMergedIntoCausticsPacket =
        m_deferredCausticResolveWaveletTask.valid()
        && causticResolveWaveletPacket.valid()
        && causticsPacket.valid()
        && causticResolveWaveletPacket == causticsPacket
    ;
    const bool causticIrradianceClearMergedIntoCausticsPacket =
        m_deferredCausticIrradianceClearTask.valid()
        && causticIrradianceClearPacket.valid()
        && causticsPacket.valid()
        && causticIrradianceClearPacket == causticsPacket
    ;
    // Non-temporal accumulation resets every frame through a typed graph clear before its selected producer. The
    // producer commits the matching CPU reset only after that shared packet accepts.
    const Core::GpuSubmissionPacketId causticAccumulatorNonTemporalClearPacket =
        m_deferredCausticAccumulatorNonTemporalClearTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_deferredCausticAccumulatorNonTemporalClearTask)
            : Core::GpuSubmissionPacketId{}
    ;
    const bool causticAccumulatorNonTemporalClearMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorNonTemporalClearTask.valid()
        || (
            causticAccumulatorNonTemporalClearPacket.valid()
            && causticsPacket.valid()
            && causticAccumulatorNonTemporalClearPacket == causticsPacket
        )
    ;
    // A fresh temporal accumulator is zeroed by a typed graph clear before its selected producer. Like the
    // irradiance clear, it must remain in that producer packet so the accepted callback is the sole publisher of
    // the initialized mirror and no hidden submission can write the accumulator.
    const Core::GpuSubmissionPacketId causticAccumulatorBootstrapClearPacket =
        m_deferredCausticAccumulatorBootstrapClearTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_deferredCausticAccumulatorBootstrapClearTask)
            : Core::GpuSubmissionPacketId{}
    ;
    const bool causticAccumulatorBootstrapClearMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorBootstrapClearTask.valid()
        || (
            causticAccumulatorBootstrapClearPacket.valid()
            && causticsPacket.valid()
            && causticAccumulatorBootstrapClearPacket == causticsPacket
        )
    ;
    // A warm temporal accumulator decays in the same selected caustics packet.  If it split, the photon timing
    // scope could span an unsubmitted packet and the compiler-owned UAV dependency would no longer protect the
    // following atomic producer.
    const Core::GpuSubmissionPacketId causticAccumulatorDecayPacket =
        m_deferredCausticAccumulatorDecayTask.valid()
            ? m_deferredLightingCompiledGraph.packetForTask(m_deferredCausticAccumulatorDecayTask)
            : Core::GpuSubmissionPacketId{}
    ;
    const bool causticAccumulatorDecayMergedIntoCausticsPacket =
        !m_deferredCausticAccumulatorDecayTask.valid()
        || (
            causticAccumulatorDecayPacket.valid()
            && causticsPacket.valid()
            && causticAccumulatorDecayPacket == causticsPacket
        )
    ;
    // Keep every recording and submission span derived from compiled packet handles. The renderer names semantic
    // endpoints only; raw compiler-order indices remain inside the task-graph runtime.
    const Core::GpuSubmissionPacketRange shadowPreparePacketRange =
        m_deferredLightingCompiledGraph.packetRange(shadowPreparePacket, shadowPreparePacket);
    const Core::GpuSubmissionPacketRange graphicsPrefixWorkPacketRange =
        m_deferredLightingCompiledGraph.packetRange(graphicsPrefixMeshViewSetupPacket, graphicsPrefixPacket);
    const Core::GpuSubmissionPacketRange graphicsPrefixPacketRange =
        m_deferredLightingCompiledGraph.packetRange(graphicsPrefixPacket, graphicsPrefixPacket);
    const Core::GpuSubmissionPacketRange shadowPrepareThroughPrefixPacketRange =
        m_deferredLightingCompiledGraph.packetRange(shadowPreparePacket, graphicsPrefixPacket);
    const Core::GpuSubmissionPacketRange shadowEffectsPacketRange = m_deferredLightingCompiledGraph.packetRange(
        shadowVisibilityPacket,
        hardwareShadowSupported ? shadowVisibilityPacket : softwareCausticsPacket
    );
    const Core::GpuSubmissionPacketId surfelGiFirstPacket = surfelGiPreparationPacket.valid()
        ? surfelGiPreparationPacket
        : surfelGiPacket
    ;
    const Core::GpuSubmissionPacketRange surfelGiPacketRange =
        m_deferredLightingCompiledGraph.packetRange(surfelGiFirstPacket, surfelGiPacket);
    const usize expectedSurfelGiPacketCount = m_deferredSurfelGiSnapshotCopyTask.valid()
        ? (surfelGiPreparationPacket == surfelGiSnapshotCopyPacket ? 2u : 3u)
        : 1u
    ;
    const Core::GpuSubmissionPacketRange hardwareCausticsPacketRange =
        m_deferredLightingCompiledGraph.packetRange(hardwareCausticsPacket, hardwareCausticsPacket);
    const Core::GpuSubmissionPacketId avboitUnsplitFinalPacket = hasTransparentRenderers
        ? avboitAccumulationFinalizePacket
        : avboitPrePacket
    ;
    const Core::GpuSubmissionPacketRange avboitPacketRange = m_deferredLightingCompiledGraph.packetRange(
        avboitPrePacket,
        avboitUsesAsyncCompute ? avboitAccumulationFinalizePacket : avboitUnsplitFinalPacket
    );
    const Core::GpuSubmissionPacketRange deferredLightingCompositePacketRange =
        m_deferredLightingCompiledGraph.packetRange(deferredLightingPacket, deferredCompositePacket);
    const Core::GpuSubmissionPacketRange deferredPresentPacketRange =
        m_deferredLightingCompiledGraph.packetRange(deferredPresentPacket, deferredPresentPacket);
    const Core::GpuSubmissionPacketRange terminalPresentationPacketRange =
        m_deferredLightingCompiledGraph.packetRange(deferredPresentPacket, terminalPresentationPacket);
    const Core::GpuSubmissionPacketRange deferredLaggedLightingHistoryPacketRange =
        m_deferredLightingCompiledGraph.packetRange(
            deferredLaggedLightingHistoryPacket,
            deferredLaggedLightingHistoryPacket
        )
    ;
    const Core::GpuSubmissionPacketRange surfelGiCounterReadbackPacketRange =
        m_deferredLightingCompiledGraph.packetRange(
            surfelGiCounterReadbackPacket,
            surfelGiCounterReadbackPacket
        );
    const Core::GpuSubmissionPacketRange deferredFrameRecoveryPacketRange =
        m_deferredLightingCompiledGraph.packetRange(deferredFrameRecoveryPacket, deferredFrameRecoveryPacket);
    const Core::GpuSubmissionPacketRange effectsThroughPresentationPacketRange =
        m_deferredLightingCompiledGraph.packetRange(shadowVisibilityPacket, terminalPresentationPacket);
    const Core::GpuSubmissionPacketRange deferredNormalPacketRange =
        m_deferredLightingCompiledGraph.packetRange(shadowPreparePacket, terminalPresentationPacket);
    const Core::GpuSubmissionPacketId deferredTailFirstPacket = m_deferredSurfelGiCounterReadbackTask.valid()
        ? surfelGiCounterReadbackPacket
        : (captureLaggedLightingHistory ? deferredLaggedLightingHistoryPacket : deferredFrameRecoveryPacket)
    ;
    const Core::GpuSubmissionPacketRange deferredTailPacketRange = m_deferredLightingCompiledGraph.packetRange(
        deferredTailFirstPacket,
        deferredFrameRecoveryPacket
    );
    const Core::GpuSubmissionPacketRange deferredFullPacketRange =
        m_deferredLightingCompiledGraph.packetRange(shadowPreparePacket, deferredFrameRecoveryPacket);
    const usize expectedAvboitPacketCount = avboitUsesAsyncCompute ? 5u : 1u;
    const usize expectedDeferredTailPacketCount = 1u
        + (m_deferredSurfelGiCounterReadbackTask.valid() ? 1u : 0u)
        + (captureLaggedLightingHistory ? 1u : 0u)
    ;
    // A presentation contributor may declare graph-owned setup uploads between Deferred Present and its terminal
    // Graphics overlay.  The renderer still requires the scene Present endpoint and (when requested) one final
    // overlay packet, while the compiler owns the exact number and routing of the intervening uploads.
    const usize minimumTerminalPresentationPacketCount = m_deferredPresentationOverlayRequired ? 2u : 1u;
    const auto discardGraphicsPrefixTimingTickets = [&graphicsPrefixOwnedTimingTickets](){
        for(Core::GpuTimingSubmissionTicket* const timingTicket : graphicsPrefixOwnedTimingTickets)
            timingTicket->discard();
    };
    if(
        !m_deferredLightingTaskGraphValid
        || !m_deferredShadowPrepareTask.valid()
        || !shadowPreparePacket.valid()
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
        || (hasOpaqueCsgFrameWork && !m_graphicsPrefixCsgIntervalSampleTask.valid())
        || !m_graphicsPrefixTask.valid()
        || !graphicsPrefixMeshViewSetupPacket.valid()
        || !graphicsPrefixSceneShadingSetupPacket.valid()
        || !graphicsPrefixDeferredClearFirstPacket.valid()
        || !graphicsPrefixDeferredClearPacket.valid()
        || !graphicsPrefixGbufferPacket.valid()
        || !graphicsPrefixCsgIntervalSamplePacket.valid()
        || !graphicsPrefixPacket.valid()
        || !graphicsPrefixTimingBindingsValid
        || !graphicsPrefixPacketsAreGraphics
        || !graphicsPrefixDeferredClearBundleMerged
        || !graphicsPrefixQueue
        || graphicsPrefixQueue->queueClass != Core::CommandQueue::Graphics
        || !m_deferredShadowVisibilityTask.valid()
        || !shadowVisibilityPacket.valid()
        || !shadowVisibilityQueue
        || (!hardwareShadowSupported && (
            !m_deferredSoftwareCausticsTask.valid()
            || !softwareCausticsPacket.valid()
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
        || !causticIrradianceClearMergedIntoCausticsPacket
        || !causticAccumulatorNonTemporalClearMergedIntoCausticsPacket
        || !causticAccumulatorBootstrapClearMergedIntoCausticsPacket
        || !causticAccumulatorDecayMergedIntoCausticsPacket
        || !m_deferredSurfelGiTask.valid()
        || !m_deferredSurfelGiIrradianceClearTask.valid()
        || !surfelGiOutputClearMergedIntoGiPacket
        || !surfelGiPacket.valid()
        || !surfelGiQueue
        || (m_deferredSurfelGiSnapshotCopyTask.valid() && (
            !m_deferredSurfelGiPreparationTask.valid()
            || !surfelGiPreparationPacket.valid()
            || !surfelGiPreparationQueue
            || !surfelGiSnapshotCopyPacket.valid()
            || !surfelGiSnapshotCopyQueue
            || (static_cast<u8>(surfelGiSnapshotCopyQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || (m_deferredSurfelGiCounterReadbackTask.valid() && (
            !surfelGiCounterReadbackPacket.valid()
            || !surfelGiCounterReadbackQueue
            || (static_cast<u8>(surfelGiCounterReadbackQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || (hardwareShadowSupported && (
            !m_deferredHardwareCausticsTask.valid()
            || !hardwareCausticsPacket.valid()
            || !hardwareCausticsQueue
            || hardwareCausticsQueue->queueClass != Core::CommandQueue::Graphics
        ))
        || !m_deferredAvboitPreTask.valid()
        || !m_deferredAvboitOccupancyTask.valid()
        || !avboitPrePacket.valid()
        || !avboitPrePacketContainsClear
        || !avboitPrePacketContainsOccupancy
        || !avboitExtinctionPacketContainsStreams
        || !avboitAccumulationPacketContainsStreams
        || !avboitAccumulationPacketContainsFinalizer
        || (hasTransparentRenderers && (
            !m_deferredAvboitExtinctionTask.valid()
            || !m_deferredAvboitAccumulationTask.valid()
            || !m_deferredAvboitAccumulationFinalizeTask.valid()
            || !avboitExtinctionPacket.valid()
            || !avboitAccumulationPacket.valid()
            || !avboitAccumulationFinalizePacket.valid()
            || (!avboitUsesAsyncCompute && (
                !avboitUnsplitPrePacketContainsExtinction
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
            || !avboitDepthWarpPacket.valid()
            || !avboitExtinctionPacket.valid()
            || !avboitIntegrationPacket.valid()
            || !avboitAccumulationPacket.valid()
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
        || (m_deferredPresentationOverlayRequired != m_deferredPresentationOverlayTask.valid())
        || !m_deferredFrameRecoveryTask.valid()
        || (m_deferredSurfelGiCounterReadbackCompletion.valid()
            && !surfelCounterReadbackCompletionToken.valid())
        || (captureLaggedLightingHistory && (
            !m_deferredLaggedLightingHistoryTask.valid()
            || !deferredLaggedLightingHistoryPacket.valid()
            || !deferredLaggedLightingHistoryQueue
            || (static_cast<u8>(deferredLaggedLightingHistoryQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
        ))
        || (laggedAsyncLightingSchedule && !m_deferredLightingHistoryCompletion.valid())
        || !deferredLightingPacket.valid()
        || !laggedLightingHistorySlotsUploadMergedIntoLightingPacket
        || !deferredCompositePacket.valid()
        || !deferredPresentPacket.valid()
        || (m_deferredPresentationOverlayRequired && !deferredPresentationOverlayPacket.valid())
        || !terminalPresentationPacket.valid()
        || !deferredFrameRecoveryPacket.valid()
        || !deferredLightingQueue
        || !deferredCompositeQueue
        || !deferredPresentQueue
        || !terminalPresentationQueue
        || !deferredFrameRecoveryQueue
        || !shadowPreparePacketRange.valid()
        || shadowPreparePacketRange.packetCount != 1u
        || !graphicsPrefixWorkPacketRange.valid()
        || graphicsPrefixWorkPacketRange.packetCount < graphicsPrefixUniquePacketCount
        || !graphicsPrefixPacketRange.valid()
        || graphicsPrefixPacketRange.packetCount != 1u
        || !shadowPrepareThroughPrefixPacketRange.valid()
        || shadowPrepareThroughPrefixPacketRange.packetCount
            != shadowPreparePacketRange.packetCount + graphicsPrefixWorkPacketRange.packetCount
        || !shadowEffectsPacketRange.valid()
        || shadowEffectsPacketRange.packetCount != (hardwareShadowSupported ? 1u : 2u)
        || !surfelGiPacketRange.valid()
        || surfelGiPacketRange.packetCount != expectedSurfelGiPacketCount
        || (hardwareShadowSupported && (
            !hardwareCausticsPacketRange.valid()
            || hardwareCausticsPacketRange.packetCount != 1u
        ))
        || !avboitPacketRange.valid()
        || avboitPacketRange.packetCount != expectedAvboitPacketCount
        || !deferredLightingCompositePacketRange.valid()
        || deferredLightingCompositePacketRange.packetCount != 2u
        || !deferredPresentPacketRange.valid()
        || deferredPresentPacketRange.packetCount != 1u
        || !terminalPresentationPacketRange.valid()
        || terminalPresentationPacketRange.packetCount < minimumTerminalPresentationPacketCount
        || (m_deferredSurfelGiCounterReadbackTask.valid() && (
            !surfelGiCounterReadbackPacketRange.valid()
            || surfelGiCounterReadbackPacketRange.packetCount != 1u
        ))
        || (captureLaggedLightingHistory && (
            !deferredLaggedLightingHistoryPacketRange.valid()
            || deferredLaggedLightingHistoryPacketRange.packetCount != 1u
        ))
        || !deferredFrameRecoveryPacketRange.valid()
        || deferredFrameRecoveryPacketRange.packetCount != 1u
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
    const auto discardUnacceptedGraphPackets = [&](){
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
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
        frameTimingTransaction.discard();
        discardTimingTickets();
        discardUnacceptedGraphPackets();
        const bool shadowPrepareAccepted = shadowPreparePacket.valid()
            && m_deferredLightingSubmissionTransaction.packetToken(shadowPreparePacket).valid()
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

    // Record preparation and prefix through the graph's ready-frontier path. These renderer payloads intentionally
    // retain the serial default, while graph-owned upload packets later in the frame may opt into worker recording.
    const Core::GpuNativePacketRecorder deferredRecorder(device);
    Core::Alloc::ScratchArena shadowPrepareStateScratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> shadowPrepareLiveStateBuffers{ shadowPrepareStateScratchArena };
    Vector<Core::Buffer*, Core::Alloc::ScratchArena> shadowPrepareLiveStatePointers{ shadowPrepareStateScratchArena };
    const auto appendShadowPrepareStateBuffer = [&](const Core::BufferHandle& buffer){
        if(!buffer)
            return;
        for(const Core::BufferHandle& existing : shadowPrepareLiveStateBuffers){
            if(existing.get() == buffer.get())
                return;
        }
        shadowPrepareLiveStateBuffers.push_back(buffer);
        shadowPrepareLiveStatePointers.push_back(buffer.get());
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
        if(mesh.blas){
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

    // The handoff stores raw RHI pointers. Filter it into a local source for every current AS/software-BVH and
    // trace-geometry state buffer before recording, but do not publish that filtered set yet: a rejected packet
    // must leave the prior accepted seed untouched. The local strong handles remain alive through the accepted
    // packet callback below.
    Core::CommandListResourceStateHandoff shadowPrepareFilteredPriorState(m_arena);
    bool shadowPrepareFilteredPriorStateReady = false;
    if(m_shadowPreparePersistentStateHandoff.valid()){
        shadowPrepareFilteredPriorStateReady = shadowPrepareFilteredPriorState.buildResourceSubset(
                m_shadowPreparePersistentStateHandoff,
                nullptr,
                0u,
                shadowPrepareLiveStatePointers.data(),
                shadowPrepareLiveStatePointers.size()
            )
        ;
    }

    Core::GpuExternalPacketStateSource shadowPrepareStateSources[1] = {};
    usize shadowPrepareStateSourceCount = 0u;
    if(shadowPrepareFilteredPriorStateReady){
        if(!appendDeclaredStateSource(
            shadowPrepareStateSources,
            LengthOf(shadowPrepareStateSources),
            shadowPrepareStateSourceCount,
            &shadowPrepareFilteredPriorState
        )){
            shadowPrepareStateSourceCount = 0u;
        }
    }
    const Core::GpuNativePacketRecordDesc shadowPrepareRecordDescs[] = {
        Core::GpuNativePacketRecordDesc{
            .packet = shadowPreparePacket,
            .externalStateSources = shadowPrepareStateSources,
            .externalStateSourceCount = shadowPrepareStateSourceCount,
        },
    };
    const bool graphicsPrefixRecorded =
        m_deferredLightingTaskGraphValid
        && m_graphicsPrefixMeshViewSetupTask.valid()
        && m_graphicsPrefixSceneShadingSetupTask.valid()
        && m_graphicsPrefixDeferredClearFirstTask.valid()
        && m_graphicsPrefixDeferredClearTask.valid()
        && m_graphicsPrefixGbufferTask.valid()
        && (!hasOpaqueCsgFrameWork || m_graphicsPrefixCsgIntervalSampleTask.valid())
        && m_graphicsPrefixTask.valid()
        && m_deferredShadowPrepareTask.valid()
        && shadowPreparePacket.valid()
        && graphicsPrefixPacket.valid()
        && graphicsPrefixDeferredClearBundleMerged
        && deferredRecorder.recordPacketRangeInReadyFrontiers(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            shadowPrepareThroughPrefixPacketRange,
            shadowPrepareStateSourceCount != 0u ? shadowPrepareRecordDescs : nullptr,
            shadowPrepareStateSourceCount != 0u ? LengthOf(shadowPrepareRecordDescs) : 0u,
            m_deferredLightingRecordedGraph,
            m_world.taskPool()
        )
    ;
    const Core::CommandListResourceStateHandoff* const shadowPrepareFinalStateSeed = graphicsPrefixRecorded
        ? m_deferredLightingRecordedGraph.packetFinalStateSeed(shadowPreparePacket)
        : nullptr
    ;
    const Core::CommandListResourceStateHandoff* const graphicsPrefixFinalStateSeed = graphicsPrefixRecorded
        ? m_deferredLightingRecordedGraph.packetFinalStateSeed(graphicsPrefixPacket)
        : nullptr
    ;
    // Build the sparse AS/software-BVH state candidate before submission, but publish it only from the accepted
    // packet callback below. Fan in the prior accepted states because a frame may leave static or route-inactive
    // scratch untouched.
    Core::CommandListResourceStateHandoff shadowPrepareAcceptedStateCandidate(m_arena);
    bool shadowPrepareAcceptedStateCandidateReady = true;
    if(!shadowPrepareLiveStatePointers.empty()){
        Core::CommandListResourceStateHandoff shadowPrepareFinalState(m_arena);
        shadowPrepareAcceptedStateCandidateReady = shadowPrepareFinalStateSeed
            && shadowPrepareFinalState.buildResourceSubset(
                *shadowPrepareFinalStateSeed,
                nullptr,
                0u,
                shadowPrepareLiveStatePointers.data(),
                shadowPrepareLiveStatePointers.size()
            )
        ;
        if(shadowPrepareAcceptedStateCandidateReady){
            if(!shadowPrepareFinalState.empty()){
                const Core::CommandListResourceStateHandoff* const stateBranches[] = { &shadowPrepareFinalState };
                shadowPrepareAcceptedStateCandidateReady = shadowPrepareFilteredPriorStateReady
                    ? shadowPrepareAcceptedStateCandidate.buildFanIn(
                        shadowPrepareFilteredPriorState,
                        stateBranches,
                        LengthOf(stateBranches)
                    )
                    : shadowPrepareAcceptedStateCandidate.copyFrom(shadowPrepareFinalState)
                ;
            }
            else if(shadowPrepareFilteredPriorStateReady)
                shadowPrepareAcceptedStateCandidateReady = shadowPrepareAcceptedStateCandidate.copyFrom(shadowPrepareFilteredPriorState);
        }
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

    Core::GpuExternalPacketStateSource shadowVisibilityStateSources[3] = {};
    usize shadowVisibilityStateSourceCount = 0u;
    bool shadowVisibilityStateSourcesReady = appendDeclaredStateSource(
        shadowVisibilityStateSources,
        LengthOf(shadowVisibilityStateSources),
        shadowVisibilityStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    if(shadowVisibilityRunsOnCompute && m_shadowComputePersistentStateHandoff.valid()){
        shadowVisibilityStateSourcesReady = shadowVisibilityStateSourcesReady
            && appendDeclaredStateSource(
                shadowVisibilityStateSources,
                LengthOf(shadowVisibilityStateSources),
                shadowVisibilityStateSourceCount,
                &m_shadowComputePersistentStateHandoff
            )
        ;
    }
    if(shadowVisibilityRunsOnCompute && m_shadowVisibilityReturnStateHandoff.valid()){
        shadowVisibilityStateSourcesReady = shadowVisibilityStateSourcesReady
            && appendDeclaredStateSource(
                shadowVisibilityStateSources,
                LengthOf(shadowVisibilityStateSources),
                shadowVisibilityStateSourceCount,
                &m_shadowVisibilityReturnStateHandoff
            )
        ;
    }

    Core::GpuExternalPacketStateSource softwareCausticsStateSources[3] = {};
    usize softwareCausticsStateSourceCount = 0u;
    bool softwareCausticsStateSourcesReady = appendDeclaredStateSource(
        softwareCausticsStateSources,
        LengthOf(softwareCausticsStateSources),
        softwareCausticsStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    if(!hardwareShadowSupported){
        if(softwareCausticsRunsOnCompute && m_causticsComputePersistentStateHandoff.valid()){
            softwareCausticsStateSourcesReady = softwareCausticsStateSourcesReady
                && appendDeclaredStateSource(
                    softwareCausticsStateSources,
                    LengthOf(softwareCausticsStateSources),
                    softwareCausticsStateSourceCount,
                    &m_causticsComputePersistentStateHandoff
                )
            ;
        }
        if(softwareCausticsRunsOnCompute && m_causticIrradianceReturnStateHandoff.valid()){
            softwareCausticsStateSourcesReady = softwareCausticsStateSourcesReady
                && appendDeclaredStateSource(
                    softwareCausticsStateSources,
                    LengthOf(softwareCausticsStateSources),
                    softwareCausticsStateSourceCount,
                    &m_causticIrradianceReturnStateHandoff
                )
            ;
        }
    }

    // Shadow Visibility, optional Software Caustics, and each Surfel-GI packet record after Prefix in compiler
    // order. The snapshot packet needs the same persistent source as the final compute task, while compiler
    // prologue seeds replace only the regions produced by an in-graph initialization or copy packet.
    Core::GpuExternalPacketStateSource surfelGiStateSources[4] = {};
    usize surfelGiStateSourceCount = 0u;
    bool surfelGiStateSourcesReady = appendDeclaredStateSource(
        surfelGiStateSources,
        LengthOf(surfelGiStateSources),
        surfelGiStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    if(surfelGiRunsOnCompute && m_surfelGiComputePersistentStateHandoff.valid()){
        surfelGiStateSourcesReady = surfelGiStateSourcesReady
            && appendDeclaredStateSource(
                surfelGiStateSources,
                LengthOf(surfelGiStateSources),
                surfelGiStateSourceCount,
                &m_surfelGiComputePersistentStateHandoff
            )
        ;
    }
    if(m_surfelGiCounterPersistentStateHandoff.valid()){
        surfelGiStateSourcesReady = surfelGiStateSourcesReady
            && appendDeclaredStateSource(
                surfelGiStateSources,
                LengthOf(surfelGiStateSources),
                surfelGiStateSourceCount,
                &m_surfelGiCounterPersistentStateHandoff
            )
        ;
    }
    if(surfelGiRunsOnCompute && m_surfelIrradianceReturnStateHandoff.valid()){
        surfelGiStateSourcesReady = surfelGiStateSourcesReady
            && appendDeclaredStateSource(
                surfelGiStateSources,
                LengthOf(surfelGiStateSources),
                surfelGiStateSourceCount,
                &m_surfelIrradianceReturnStateHandoff
            )
        ;
    }

    // AVBOIT, Hardware Caustics, and lagged Lighting each retain the same filtered Prefix source for their
    // independent common reads.  Their packet ordering remains internal to this compiled graph.
    m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
    const Core::GpuExternalPacketStateSource avboitPreStateSources[] = {
        Core::GpuExternalPacketStateSource{
            .states = graphicsPrefixFinalStateSeed,
        },
    };
    Core::GpuExternalPacketStateSource deferredLightingStateSources[1] = {};
    usize deferredLightingStateSourceCount = 0u;
    const bool deferredLightingStateSourcesReady = appendDeclaredStateSource(
        deferredLightingStateSources,
        LengthOf(deferredLightingStateSources),
        deferredLightingStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    Core::GpuExternalPacketStateSource hardwareCausticsStateSources[1] = {};
    usize hardwareCausticsStateSourceCount = 0u;
    const bool hardwareCausticsStateSourcesReady =
        !hardwareShadowSupported
        || appendDeclaredStateSource(
            hardwareCausticsStateSources,
            LengthOf(hardwareCausticsStateSources),
            hardwareCausticsStateSourceCount,
            graphicsPrefixFinalStateSeed
        )
    ;
    Core::GpuExternalPacketStateSource deferredCompositeStateSources[1] = {};
    usize deferredCompositeStateSourceCount = 0u;
    const bool deferredCompositeStateSourcesReady = appendDeclaredStateSource(
        deferredCompositeStateSources,
        LengthOf(deferredCompositeStateSources),
        deferredCompositeStateSourceCount,
        graphicsPrefixFinalStateSeed
    );
    // Only the packets that import accepted external state need renderer-provided overrides. All remaining packets
    // receive the compiler-derived default descriptor during range traversal.
    Core::GpuNativePacketRecordDesc deferredRecordDescs[8] = {};
    usize deferredRecordDescCount = 0u;
    deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
        .packet = shadowVisibilityPacket,
        .externalStateSources = shadowVisibilityStateSources,
        .externalStateSourceCount = shadowVisibilityStateSourceCount,
    };
    if(!hardwareShadowSupported){
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = softwareCausticsPacket,
            .externalStateSources = softwareCausticsStateSources,
            .externalStateSourceCount = softwareCausticsStateSourceCount,
        };
    }
    if(surfelGiPreparationPacket.valid() && surfelGiPreparationPacket != surfelGiPacket){
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = surfelGiPreparationPacket,
            .externalStateSources = surfelGiStateSources,
            .externalStateSourceCount = surfelGiStateSourceCount,
        };
    }
    if(
        surfelGiSnapshotCopyPacket.valid()
        && surfelGiSnapshotCopyPacket != surfelGiPreparationPacket
        && surfelGiSnapshotCopyPacket != surfelGiPacket
    ){
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = surfelGiSnapshotCopyPacket,
            .externalStateSources = surfelGiStateSources,
            .externalStateSourceCount = surfelGiStateSourceCount,
        };
    }
    deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
        .packet = surfelGiPacket,
        .externalStateSources = surfelGiStateSources,
        .externalStateSourceCount = surfelGiStateSourceCount,
    };
    if(hardwareShadowSupported){
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = hardwareCausticsPacket,
            .externalStateSources = hardwareCausticsStateSources,
            .externalStateSourceCount = hardwareCausticsStateSourceCount,
        };
    }
    deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
        .packet = avboitPrePacket,
        .externalStateSources = avboitPreStateSources,
        .externalStateSourceCount = LengthOf(avboitPreStateSources),
    };
    deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
        .packet = deferredLightingPacket,
        .externalStateSources = deferredLightingStateSources,
        .externalStateSourceCount = deferredLightingStateSourceCount,
    };
    deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
        .packet = deferredCompositePacket,
        .externalStateSources = deferredCompositeStateSources,
        .externalStateSourceCount = deferredCompositeStateSourceCount,
    };
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
        && m_deferredLightingTaskGraphValid
        && m_graphicsPrefixMeshViewSetupTask.valid()
        && m_graphicsPrefixSceneShadingSetupTask.valid()
        && m_graphicsPrefixDeferredClearFirstTask.valid()
        && m_graphicsPrefixDeferredClearTask.valid()
        && m_graphicsPrefixGbufferTask.valid()
        && (!hasOpaqueCsgFrameWork || m_graphicsPrefixCsgIntervalSampleTask.valid())
        && m_graphicsPrefixTask.valid()
        && graphicsPrefixPacket.valid()
        && graphicsPrefixDeferredClearBundleMerged
        && m_deferredShadowVisibilityTask.valid()
        && shadowVisibilityPacket.valid()
        && (hardwareShadowSupported || (
            m_deferredSoftwareCausticsTask.valid()
            && softwareCausticsPacket.valid()
        ))
        && m_deferredSurfelGiTask.valid()
        && surfelGiPacket.valid()
        && (!m_deferredSurfelGiSnapshotCopyTask.valid() || (
            m_deferredSurfelGiPreparationTask.valid()
            && surfelGiPreparationPacket.valid()
            && surfelGiSnapshotCopyPacket.valid()
        ))
        && (!hardwareShadowSupported || (
            m_deferredHardwareCausticsTask.valid()
            && hardwareCausticsPacket.valid()
        ))
        && m_deferredAvboitPreTask.valid()
        && m_deferredAvboitOccupancyTask.valid()
        && avboitPrePacket.valid()
        && avboitPrePacketContainsClear
        && avboitPrePacketContainsOccupancy
        && avboitExtinctionPacketContainsStreams
        && avboitAccumulationPacketContainsStreams
        && avboitAccumulationPacketContainsFinalizer
        && (!hasTransparentRenderers || (
            m_deferredAvboitExtinctionTask.valid()
            && m_deferredAvboitAccumulationTask.valid()
            && m_deferredAvboitAccumulationFinalizeTask.valid()
            && avboitExtinctionPacket.valid()
            && avboitAccumulationPacket.valid()
            && avboitAccumulationFinalizePacket.valid()
            && (avboitUsesAsyncCompute || (
                avboitUnsplitPrePacketContainsExtinction
                && avboitUnsplitPrePacketContainsAccumulation
            ))
        ))
        && (!avboitUsesAsyncCompute || (
            m_deferredAvboitDepthWarpTask.valid()
            && m_deferredAvboitExtinctionTask.valid()
            && m_deferredAvboitIntegrationTask.valid()
            && m_deferredAvboitAccumulationTask.valid()
            && avboitDepthWarpPacket.valid()
            && avboitExtinctionPacket.valid()
            && avboitIntegrationPacket.valid()
            && avboitAccumulationPacket.valid()
        ))
        && m_deferredLightingTask.valid()
        && m_deferredCompositeTask.valid()
        && m_deferredPresentTask.valid()
        && (!m_deferredPresentationOverlayRequired || (
            m_deferredPresentationOverlayTask.valid()
            && deferredPresentationOverlayPacket.valid()
        ))
        && m_deferredFrameRecoveryTask.valid()
        && (!captureLaggedLightingHistory || (
            m_deferredLaggedLightingHistoryTask.valid()
            && deferredLaggedLightingHistoryPacket.valid()
        ))
        && (!laggedAsyncLightingSchedule || m_deferredLightingHistoryCompletion.valid())
        && deferredLightingPacket.valid()
        && deferredCompositePacket.valid()
        && deferredPresentPacket.valid()
        && terminalPresentationPacket.valid()
        && deferredFrameRecoveryPacket.valid()
        && deferredFrameRecoveryQueue
        && effectsThroughPresentationPacketRange.valid()
    ;
    if(deferredPacketsRecorded){
        deferredPacketsRecorded = deferredRecorder.recordPacketRangeInReadyFrontiers(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            effectsThroughPresentationPacketRange,
            deferredRecordDescs,
            deferredRecordDescCount,
            m_deferredLightingRecordedGraph,
            m_world.taskPool()
        );
    }
    if(!deferredPacketsRecorded){
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
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
        m_deferredLightingRecordedGraph.packetFinalStateSeed(shadowVisibilityPacket)
    ;
    const Core::CommandListResourceStateHandoff* const softwareCausticsFinalStateSeed = !hardwareShadowSupported
        ? m_deferredLightingRecordedGraph.packetFinalStateSeed(softwareCausticsPacket)
        : nullptr
    ;
    const Core::CommandListResourceStateHandoff* const causticsFinalStateSeed = softwareCausticsFinalStateSeed;
    const Core::CommandListResourceStateHandoff* const deferredLightingFinalStateSeed =
        m_deferredLightingRecordedGraph.packetFinalStateSeed(deferredLightingPacket)
    ;
    const Core::CommandListResourceStateHandoff* const surfelGiFinalStateSeed =
        m_deferredLightingRecordedGraph.packetFinalStateSeed(surfelGiPacket)
    ;
    const Core::CommandListResourceStateHandoff* const hardwareCausticsFinalStateSeed = hardwareShadowSupported
        ? m_deferredLightingRecordedGraph.packetFinalStateSeed(hardwareCausticsPacket)
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
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
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
        const bool retireTiming = frameTimingTransaction.needsRetirement();
        if(retireTiming)
            frameTimingTransaction.prepareForRecovery();
        m_deferredFrameRecoveryArmed = true;
        m_deferredFrameRecoveryRetiresTiming = retireTiming;
        const auto discardFrameRecovery = [&](){
            if(
                m_deferredLightingTaskGraphValid
                && deferredFrameRecoveryPacket.valid()
            ){
                m_deferredLightingSubmissionTransaction.rejectPacket(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    deferredFrameRecoveryPacket
                );
            }
            else{
                m_deferredFrameRecoveryArmed = false;
                m_deferredFrameRecoveryRetiresTiming = false;
                frameTimingTransaction.discard();
            }
        };
        if(
            !m_deferredLightingTaskGraphValid
            || !m_deferredFrameRecoveryTask.valid()
            || !deferredFrameRecoveryPacket.valid()
            || !deferredFrameRecoveryPacketRange.valid()
            || !deferredFrameRecoveryQueue
            || deferredFrameRecoveryQueue->queueClass != Core::CommandQueue::Graphics
            || !m_deferredLightingCompiledGraph.packet(deferredFrameRecoveryPacket).joinsAcceptedQueueFrontier
            || !m_deferredLightingSubmissionTransaction.hasAcceptedPackets()
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery packet was unavailable"));
            discardFrameRecovery();
            return false;
        }

        const bool recoveryRecorded = deferredRecorder.recordPacketRangeInCompileOrder(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            deferredFrameRecoveryPacketRange,
            nullptr,
            0u,
            m_deferredLightingRecordedGraph
        );
        if(!recoveryRecorded){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to late-record deferred frame recovery packet"));
            discardFrameRecovery();
            return false;
        }

        Core::Alloc::ScratchArena recoveryScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter submitter(device);
        const bool recoveryAccepted = submitter.submitPacketRangeInCompileOrder(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            m_deferredLightingRecordedGraph,
            deferredFrameRecoveryPacketRange,
            nullptr,
            0u,
            nullptr,
            0u,
            m_deferredLightingSubmissionTransaction,
            recoveryScratchArena
        );
        if(!recoveryAccepted){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery submission was rejected"));
            discardFrameRecovery();
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
        discardUnacceptedGraphPackets();
        return recovered;
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
        const Core::GpuTaskGraphPacketTimingTicket avboitTimingTickets[] = {
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = avboitPrePacket,
                .timingTicket = &avboitPreTimingTicket,
            },
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = avboitDepthWarpPacket,
                .timingTicket = &avboitDepthWarpTimingTicket,
            },
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = avboitExtinctionPacket,
                .timingTicket = &avboitExtinctionTimingTicket,
            },
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = avboitIntegrationPacket,
                .timingTicket = &avboitIntegrationTimingTicket,
            },
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = avboitAccumulationPacket,
                .timingTicket = &avboitAccumulationTimingTicket,
            },
        };
        const usize avboitTimingTicketCount = avboitUsesAsyncCompute ? LengthOf(avboitTimingTickets) : 1u;
        const bool avboitPacketsAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredAvboitPreTask.valid()
            && m_deferredAvboitOccupancyTask.valid()
            && avboitPrePacket.valid()
            && avboitPrePacketContainsClear
            && avboitPrePacketContainsOccupancy
            && avboitExtinctionPacketContainsStreams
            && avboitAccumulationPacketContainsStreams
            && avboitAccumulationPacketContainsFinalizer
            && (!hasTransparentRenderers || (
                m_deferredAvboitExtinctionTask.valid()
                && m_deferredAvboitAccumulationTask.valid()
                && m_deferredAvboitAccumulationFinalizeTask.valid()
                && avboitExtinctionPacket.valid()
                && avboitAccumulationPacket.valid()
                && avboitAccumulationFinalizePacket.valid()
                && (avboitUsesAsyncCompute || (
                    avboitUnsplitPrePacketContainsExtinction
                    && avboitUnsplitPrePacketContainsAccumulation
                ))
            ))
            && avboitPacketRange.valid()
            && avboitPacketRange.packetCount == avboitTimingTicketCount
            && (!avboitUsesAsyncCompute || (
                m_deferredAvboitDepthWarpTask.valid()
                && m_deferredAvboitExtinctionTask.valid()
                && m_deferredAvboitIntegrationTask.valid()
                && m_deferredAvboitAccumulationTask.valid()
                && avboitDepthWarpPacket.valid()
                && avboitExtinctionPacket.valid()
                && avboitIntegrationPacket.valid()
                && avboitAccumulationPacket.valid()
            ))
            && deferredSubmitter.submitPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                avboitPacketRange,
                nullptr,
                0u,
                avboitTimingTickets,
                avboitTimingTicketCount,
                m_deferredLightingSubmissionTransaction,
                deferredScratchArena
            )
        ;
        avboitPreSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(avboitPrePacket);
        avboitDepthWarpSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(avboitDepthWarpPacket);
        avboitExtinctionSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(avboitExtinctionPacket);
        avboitIntegrationSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(avboitIntegrationPacket);
        avboitAccumulationSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(
            avboitAccumulationPacket
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
        Core::GpuTaskGraphExternalCompletionToken deferredLightingCompletionTokens[1] = {};
        usize deferredLightingCompletionCount = 0u;
        if(laggedAsyncLightingSchedule){
            deferredLightingCompletionTokens[deferredLightingCompletionCount++] = {
                .completion = m_deferredLightingHistoryCompletion,
                .token = m_laggedLightingHistorySubmissionToken,
            };
        }
        struct DeferredLightingAcceptanceContext{
            RendererSystem* renderer = nullptr;
            DeferredFrameTargets* targets = nullptr;
            const Core::CommandListResourceStateHandoff* finalState = nullptr;
            Core::GpuSubmissionPacketId lightingPacket;
            bool runsOnCompute = false;
            bool usesLaggedHistory = false;
            bool returnStatesReady = true;
        };
        DeferredLightingAcceptanceContext deferredLightingAcceptance{
            .renderer = this,
            .targets = &deferredTargets,
            .finalState = deferredLightingFinalStateSeed,
            .lightingPacket = deferredLightingPacket,
            .runsOnCompute = deferredLightingRunsOnCompute,
            .usesLaggedHistory = laggedAsyncLightingSchedule,
        };
        const auto acceptDeferredLightingPacket = [](
            void* const rawContext,
            const Core::GpuSubmissionPacketId& packet,
            const Core::QueueSubmissionToken& token
        ) -> bool {
            static_cast<void>(token);
            DeferredLightingAcceptanceContext* const context =
                static_cast<DeferredLightingAcceptanceContext*>(rawContext)
            ;
            if(!context || !context->renderer || !context->targets || !context->finalState)
                return false;
            if(packet != context->lightingPacket)
                return true;

            RendererSystem& renderer = *context->renderer;
            if(context->usesLaggedHistory)
                context->targets->laggedLightingHistory.slotsUploaded = true;
            context->returnStatesReady = !context->runsOnCompute || context->usesLaggedHistory || (
                renderer.m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                    *context->finalState,
                    context->targets->shadowVisibility.get()
                )
                // Bootstrap uses live caustics; active lagged mode uses the producer image directly.
                && renderer.m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                    *context->finalState,
                    context->targets->causticIrradiance.get()
                )
                && renderer.m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                    *context->finalState,
                    context->targets->surfelIrradiance.get()
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
        const Core::GpuTaskGraphPacketAcceptedCallback deferredLightingAcceptedCallback{
            .context = &deferredLightingAcceptance,
            .invoke = acceptDeferredLightingPacket,
        };
        const Core::GpuTaskGraphPacketTimingTicket deferredLightingCompositeTimingTickets[] = {
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = deferredLightingPacket,
                .timingTicket = &deferredLightingTimingTicket,
            },
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = deferredCompositePacket,
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
            && deferredLightingPacket.valid()
            && deferredCompositePacket.valid()
            && deferredLightingCompositePacketRange.valid()
            && deferredLightingCompositePacketRange.packetCount == LengthOf(deferredLightingCompositeTimingTickets)
            && deferredSubmitter.submitPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                deferredLightingCompositePacketRange,
                deferredLightingCompletionTokens,
                deferredLightingCompletionCount,
                deferredLightingCompositeTimingTickets,
                LengthOf(deferredLightingCompositeTimingTickets),
                m_deferredLightingSubmissionTransaction,
                deferredScratchArena,
                nullptr,
                &deferredLightingAcceptedCallback
            )
        ;
        deferredLightingSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(deferredLightingPacket);
        deferredCompositeSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(deferredCompositePacket);
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
        const Core::GpuTaskGraphPacketTimingTicket deferredPresentTimingTickets[] = {
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = deferredPresentPacket,
                .timingTicket = &deferredPresentTimingTicket,
            },
        };
        const bool terminalPresentationReady =
            m_deferredLightingTaskGraphValid
            && m_deferredPresentTask.valid()
            && m_deferredSurfelGiTask.valid()
            && m_deferredCompositeTask.valid()
            && (!m_deferredPresentationOverlayRequired || m_deferredPresentationOverlayTask.valid())
            && deferredPresentPacket.valid()
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
        const Core::GpuTaskGraphPacketSubmissionHook terminalPresentationSubmissionHooks[] = {
            Core::GpuTaskGraphPacketSubmissionHook{
                .packet = terminalPresentationPacket,
                .hook = framePresentationSignal,
            },
        };
        const bool deferredPresentationAccepted =
            terminalPresentationReady
            && deferredPresentSubmitter.submitPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                terminalPresentationPacketRange,
                nullptr,
                0u,
                deferredPresentTimingTickets,
                LengthOf(deferredPresentTimingTickets),
                m_deferredLightingSubmissionTransaction,
                presentScratchArena,
                nullptr,
                nullptr,
                framePresentationSignal.valid() ? terminalPresentationSubmissionHooks : nullptr,
                framePresentationSignal.valid() ? LengthOf(terminalPresentationSubmissionHooks) : 0u
            )
        ;
        finalPresentationSubmissionToken = deferredPresentationAccepted
            ? m_deferredLightingSubmissionTransaction.packetToken(terminalPresentationPacket)
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
        Core::GpuTaskGraphExternalCompletionToken surfelGiCompletionTokens[1] = {};
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
            Core::GpuSubmissionPacketId surfelGiPacket;
            bool runsOnCompute = false;
            bool stateReady = true;
        };
        SurfelGiAcceptanceContext surfelGiAcceptance{
            .renderer = this,
            .targets = &deferredTargets,
            .finalState = surfelGiFinalStateSeed,
            .surfelGiPacket = surfelGiPacket,
            .runsOnCompute = surfelGiRunsOnCompute,
        };
        const auto acceptSurfelGiPacket = [](
            void* const rawContext,
            const Core::GpuSubmissionPacketId& packet,
            const Core::QueueSubmissionToken& token
        ) -> bool {
            static_cast<void>(token);
            SurfelGiAcceptanceContext* const context =
                static_cast<SurfelGiAcceptanceContext*>(rawContext)
            ;
            if(!context)
                return false;
            if(packet != context->surfelGiPacket)
                return true;
            if(!context->renderer || !context->targets || !context->finalState){
                context->stateReady = false;
                return false;
            }

            RendererSystem& renderer = *context->renderer;
            DeferredFrameTargets& targets = *context->targets;
            context->stateReady = renderer.m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                *context->finalState,
                targets.surfelIrradiance.get()
            );
            Core::Buffer* const surfelGiCounterBuffers[] = {
                renderer.m_rayTracingState.m_surfelCounterBuffer.get(),
            };
            context->stateReady = context->stateReady
                && renderer.m_surfelGiCounterPersistentStateHandoff.buildResourceSubset(
                    *context->finalState,
                    nullptr,
                    0u,
                    surfelGiCounterBuffers,
                    LengthOf(surfelGiCounterBuffers)
                )
            ;
            if(!context->stateReady || !context->runsOnCompute)
                return context->stateReady;

            Core::Texture* const surfelGiComputeScratchTextures[] = {
                targets.surfelIrradianceHalf.get(),
            };
            Core::Buffer* const surfelGiComputeScratchBuffers[] = {
                renderer.m_rayTracingState.m_surfelPoolBuffer.get(),
                renderer.m_rayTracingState.m_surfelCellHeadBuffer.get(),
                renderer.m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(),
                renderer.m_rayTracingState.m_surfelFreeListBuffer.get(),
                renderer.m_rayTracingState.m_surfelPoolSnapshotBuffer.get(),
                renderer.m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(),
            };
            context->stateReady = renderer.m_surfelGiComputePersistentStateHandoff.buildResourceSubset(
                *context->finalState,
                surfelGiComputeScratchTextures,
                LengthOf(surfelGiComputeScratchTextures),
                surfelGiComputeScratchBuffers,
                LengthOf(surfelGiComputeScratchBuffers)
            );
            return context->stateReady;
        };
        const Core::GpuTaskGraphPacketAcceptedCallback surfelGiAcceptedCallback{
            .context = &surfelGiAcceptance,
            .invoke = acceptSurfelGiPacket,
        };
        const Core::GpuTaskGraphPacketTimingTicket surfelGiTimingTickets[] = {
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = surfelGiPacket,
                .timingTicket = &surfelGiTimingTicket,
            },
        };
        const bool surfelGiAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredSurfelGiTask.valid()
            && (!m_deferredSurfelGiCounterReadbackCompletion.valid()
                || surfelCounterReadbackCompletionToken.valid())
            && surfelGiPacket.valid()
            && (!m_deferredSurfelGiSnapshotCopyTask.valid() || (
                m_deferredSurfelGiPreparationTask.valid()
                && surfelGiPreparationPacket.valid()
                && surfelGiSnapshotCopyPacket.valid()
            ))
            && surfelGiPacketRange.valid()
            && surfelGiPacketRange.packetCount == expectedSurfelGiPacketCount
            && surfelGiSubmitter.submitPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                surfelGiPacketRange,
                surfelGiCompletionTokens,
                surfelGiCompletionTokenCount,
                surfelGiTimingTickets,
                LengthOf(surfelGiTimingTickets),
                m_deferredLightingSubmissionTransaction,
                surfelGiScratchArena,
                nullptr,
                &surfelGiAcceptedCallback
            )
        ;
        surfelGiSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(surfelGiPacket);
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
        Core::GpuTaskGraphPacketTimingTicket shadowPreparePrefixTimingTickets[
            1u + graphicsPrefixTimingTicketCount
        ] = {};
        usize shadowPreparePrefixTimingTicketCount = 0u;
        shadowPreparePrefixTimingTickets[shadowPreparePrefixTimingTicketCount++] =
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = shadowPreparePacket,
                .timingTicket = &shadowPrepareTimingTicket,
            }
        ;
        bool shadowPreparePrefixTimingTicketsValid = graphicsPrefixTimingBindingsValid;
        for(usize prefixTaskIndex = 0u;
            shadowPreparePrefixTimingTicketsValid && prefixTaskIndex < graphicsPrefixTimingTicketCount;
            ++prefixTaskIndex
        ){
            const Core::GpuSubmissionPacketId packet = graphicsPrefixTaskPackets[prefixTaskIndex];
            bool packetAlreadyTimed = false;
            for(usize earlierTaskIndex = 0u; earlierTaskIndex < prefixTaskIndex; ++earlierTaskIndex){
                if(packet == graphicsPrefixTaskPackets[earlierTaskIndex]){
                    packetAlreadyTimed = true;
                    break;
                }
            }
            if(packetAlreadyTimed)
                continue;
            if(!packet.valid() || !graphicsPrefixTimingTickets[prefixTaskIndex]){
                shadowPreparePrefixTimingTicketsValid = false;
                break;
            }
            shadowPreparePrefixTimingTickets[shadowPreparePrefixTimingTicketCount++] =
                Core::GpuTaskGraphPacketTimingTicket{
                    .packet = packet,
                    .timingTicket = graphicsPrefixTimingTickets[prefixTaskIndex],
                }
            ;
        }
        struct PrefixTimingAcceptanceContext{
            Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
            Core::GpuSubmissionPacketId frameBeginPacket;
            RendererSystem* renderer = nullptr;
            Core::GpuSubmissionPacketId shadowPreparePacket;
            const Core::CommandListResourceStateHandoff* shadowPrepareStateCandidate = nullptr;
            const Vector<Core::BufferHandle, Core::Alloc::ScratchArena>* shadowPrepareStateBackings = nullptr;
            bool shadowPrepareStateCandidateRequired = false;
            bool shadowPrepareStateReady = true;
        };
        PrefixTimingAcceptanceContext prefixTimingAcceptance{
            .frameTimingTransaction = &frameTimingTransaction,
            .frameBeginPacket = graphicsPrefixMeshViewSetupPacket,
            .renderer = this,
            .shadowPreparePacket = shadowPreparePacket,
            .shadowPrepareStateCandidate = &shadowPrepareAcceptedStateCandidate,
            .shadowPrepareStateBackings = &shadowPrepareLiveStateBuffers,
            .shadowPrepareStateCandidateRequired = shadowPrepareStateCandidateRequired,
        };
        const auto acceptPrefixTimingPacket = [](
            void* const rawContext,
            const Core::GpuSubmissionPacketId& packet,
            const Core::QueueSubmissionToken& token
        ) -> bool {
            static_cast<void>(token);
            PrefixTimingAcceptanceContext* const context = static_cast<PrefixTimingAcceptanceContext*>(rawContext);
            if(!context || !context->frameTimingTransaction || !context->frameBeginPacket.valid())
                return false;
            if(packet == context->shadowPreparePacket){
                if(!context->renderer){
                    context->shadowPrepareStateReady = false;
                    return false;
                }

                RendererSystem& renderer = *context->renderer;
                if(
                    !context->shadowPrepareStateBackings
                    || context->shadowPrepareStateBackings->empty()
                ){
                    renderer.m_shadowPreparePersistentStateHandoff.reset();
                    renderer.m_shadowPreparePersistentStateBuffers.clear();
                }
                else if(
                    !context->shadowPrepareStateCandidate
                    || !context->shadowPrepareStateCandidate->valid()
                    || context->shadowPrepareStateCandidate->empty()
                ){
                    if(!context->shadowPrepareStateCandidateRequired){
                        renderer.m_shadowPreparePersistentStateHandoff.reset();
                        renderer.m_shadowPreparePersistentStateBuffers.clear();
                        return true;
                    }
                    // The packet is already accepted, so preserve the last accepted source rather than replacing it
                    // with a partial generation. Re-pend the semantic plan for a conservative rebuild next frame.
                    renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
                    context->shadowPrepareStateReady = false;
                    return false;
                }
                else{
                    context->shadowPrepareStateReady = renderer.m_shadowPreparePersistentStateHandoff.copyFrom(
                        *context->shadowPrepareStateCandidate
                    );
                    if(context->shadowPrepareStateReady){
                        renderer.m_shadowPreparePersistentStateBuffers.clear();
                        for(const Core::BufferHandle& backing : *context->shadowPrepareStateBackings)
                            renderer.m_shadowPreparePersistentStateBuffers.push_back(backing);
                        // Task acceptance runs before this outer packet callback. Commit frozen AS cache/refit state
                        // only after the accepted backing handoff has been retained successfully.
                        renderer.m_raytracingSystem.confirmPreparedSceneTlasBuild();
                        renderer.m_raytracingSystem.confirmPreparedMeshBlasBuilds();
                        renderer.m_raytracingSystem.confirmPreparedMeshSwBvhBuilds();
                    }
                    else{
                        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
                        context->shadowPrepareStateReady = false;
                        return false;
                    }
                }
            }
            if(packet == context->frameBeginPacket)
                context->frameTimingTransaction->confirmBeginSubmission();
            return true;
        };
        const Core::GpuTaskGraphPacketAcceptedCallback shadowPreparePrefixAcceptedCallback{
            .context = &prefixTimingAcceptance,
            .invoke = acceptPrefixTimingPacket,
        };
        const bool shadowPreparePrefixAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredShadowPrepareTask.valid()
            && m_graphicsPrefixMeshViewSetupTask.valid()
            && m_graphicsPrefixSceneShadingSetupTask.valid()
            && m_graphicsPrefixDeferredClearFirstTask.valid()
            && m_graphicsPrefixDeferredClearTask.valid()
            && m_graphicsPrefixGbufferTask.valid()
            && (!hasOpaqueCsgFrameWork || m_graphicsPrefixCsgIntervalSampleTask.valid())
            && m_graphicsPrefixTask.valid()
            && shadowPreparePacket.valid()
            && deferredBindlessSlotsUploadMergedIntoShadowPreparePacket
            && rayTraceMaterialContextSlotsUploadMergedIntoShadowPreparePacket
            && causticEmissionTargetsUploadMergedIntoShadowPreparePacket
            && surfelFrameConstantsUploadMergedIntoShadowPreparePacket
            && shadowMaterialContextUploadsMergedIntoShadowPreparePacket
            && sceneBvhUploadsMergedIntoShadowPreparePacket
            && graphicsPrefixPacket.valid()
            && graphicsPrefixDeferredClearBundleMerged
            && graphicsPrefixWorkPacketRange.valid()
            && shadowPrepareThroughPrefixPacketRange.valid()
            && shadowPreparePrefixTimingTicketsValid
            && prefixTimingAcceptance.shadowPrepareStateReady
            && shadowPreparePrefixTimingTicketCount == 1u + graphicsPrefixUniquePacketCount
            && shadowPrepareThroughPrefixPacketRange.packetCount >= shadowPreparePrefixTimingTicketCount
            && shadowPreparePrefixSubmitter.submitPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                shadowPrepareThroughPrefixPacketRange,
                nullptr,
                0u,
                shadowPreparePrefixTimingTickets,
                shadowPreparePrefixTimingTicketCount,
                m_deferredLightingSubmissionTransaction,
                shadowPreparePrefixScratchArena,
                nullptr,
                &shadowPreparePrefixAcceptedCallback
            )
        ;
        if(
            !shadowPreparePrefixAccepted
            || !m_deferredLightingSubmissionTransaction.packetToken(shadowPreparePacket).valid()
            || !m_deferredLightingSubmissionTransaction.packetToken(graphicsPrefixPacket).valid()
        ){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardRenderPackets();
            if(!recovered)
                failFrameRenderRecovery();
            return;
        }
        Core::Alloc::ScratchArena shadowEffectsScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter shadowEffectsSubmitter(device);
        const Core::GpuTaskGraphPacketTimingTicket shadowEffectsTimingTickets[] = {
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = shadowVisibilityPacket,
                .timingTicket = &shadowVisibilityTimingTicket,
            },
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = softwareCausticsPacket,
                .timingTicket = &softwareCausticsTimingTicket,
            },
        };
        const usize shadowEffectsTimingTicketCount = hardwareShadowSupported
            ? 1u
            : LengthOf(shadowEffectsTimingTickets)
        ;
        const bool shadowEffectsSubmitted =
            m_deferredLightingTaskGraphValid
            && m_deferredShadowVisibilityTask.valid()
            && shadowVisibilityPacket.valid()
            && shadowEffectsPacketRange.valid()
            && shadowEffectsPacketRange.packetCount == shadowEffectsTimingTicketCount
            && (hardwareShadowSupported || (
                m_deferredSoftwareCausticsTask.valid()
                && softwareCausticsPacket.valid()
            ))
            && shadowEffectsSubmitter.submitPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                shadowEffectsPacketRange,
                nullptr,
                0u,
                shadowEffectsTimingTickets,
                shadowEffectsTimingTicketCount,
                m_deferredLightingSubmissionTransaction,
                shadowEffectsScratchArena
            )
        ;
        shadowVisibilitySubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(
            shadowVisibilityPacket
        );
        softwareCausticsSubmissionToken = !hardwareShadowSupported
            ? m_deferredLightingSubmissionTransaction.packetToken(softwareCausticsPacket)
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
            // Retain only private compute scratch; shared inputs come from next frame's prefix.
            // Retain accepted producer state for recovery after later rejection.
            const bool producerReturnStatesReady =
                m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                    *shadowVisibilityFinalStateSeed,
                    deferredTargets.shadowVisibility.get()
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
            Core::Texture* const shadowComputeScratchTextures[] = {
                deferredTargets.shadowCoarseTransmittance.get(),
                deferredTargets.shadowSoftHalfA.get(),
                deferredTargets.shadowSoftHalfB.get(),
                deferredTargets.shadowSoftGeometry.get(),
                deferredTargets.shadowSoftGeometryPrev.get(),
                deferredTargets.shadowHistA.get(),
                deferredTargets.shadowHistB.get(),
                deferredTargets.shadowMomentsA.get(),
                deferredTargets.shadowMomentsB.get(),
                deferredTargets.transparentSoftHalf.get(),
                deferredTargets.transparentHistA.get(),
                deferredTargets.transparentHistB.get(),
                deferredTargets.transparentMomentsA.get(),
                deferredTargets.transparentMomentsB.get(),
            };
            Core::Buffer* const shadowComputeScratchBuffers[] = {
                m_rayTracingState.m_swShadowEdgeStatsBuffer.get(),
                m_rayTracingState.m_swShadowEdgeStatsReadback.get(),
                m_rayTracingState.m_swShadowEdgeCounterBuffer.get(),
                m_rayTracingState.m_swShadowEdgeListBuffer.get(),
                m_rayTracingState.m_swShadowIndirectArgsBuffer.get(),
            };
            if(!m_shadowComputePersistentStateHandoff.buildResourceSubset(
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
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to recover an accepted deferred packet before abandoning shadow-compute state"));
                // Missing compute scratch leaves no safe layout restoration.
                failFrameRenderRecovery();
                return;
            }
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
            Core::GpuTaskGraphExternalCompletionToken hardwareCausticsCompletionTokens[1] = {};
            usize hardwareCausticsCompletionCount = 0u;
            if(laggedAsyncLightingSchedule){
                hardwareCausticsCompletionTokens[hardwareCausticsCompletionCount++] = {
                    .completion = m_deferredLightingHistoryCompletion,
                    .token = m_laggedLightingHistorySubmissionToken,
                };
            }
            Core::Alloc::ScratchArena hardwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
            const Core::GpuTaskGraphSubmitter hardwareCausticsSubmitter(device);
            struct HardwareCausticsAcceptanceContext{
                RendererSystem* renderer = nullptr;
                DeferredFrameTargets* targets = nullptr;
                const Core::CommandListResourceStateHandoff* finalState = nullptr;
                Core::GpuSubmissionPacketId hardwareCausticsPacket;
                bool usesLaggedHistory = false;
                bool stateReady = true;
            };
            HardwareCausticsAcceptanceContext hardwareCausticsAcceptance{
                .renderer = this,
                .targets = &deferredTargets,
                .finalState = hardwareCausticsFinalStateSeed,
                .hardwareCausticsPacket = hardwareCausticsPacket,
                .usesLaggedHistory = laggedAsyncLightingSchedule,
            };
            const auto acceptHardwareCausticsPacket = [](
                void* const rawContext,
                const Core::GpuSubmissionPacketId& packet,
                const Core::QueueSubmissionToken& token
            ) -> bool {
                static_cast<void>(token);
                HardwareCausticsAcceptanceContext* const context =
                    static_cast<HardwareCausticsAcceptanceContext*>(rawContext)
                ;
                if(!context)
                    return false;
                if(packet != context->hardwareCausticsPacket)
                    return true;
                if(!context->usesLaggedHistory)
                    return true;
                if(!context->renderer || !context->targets || !context->finalState){
                    context->stateReady = false;
                    return false;
                }
                context->stateReady = context->renderer->m_causticIrradianceLightingStateHandoff.buildTextureSubset(
                    *context->finalState,
                    context->targets->causticIrradiance.get()
                );
                return context->stateReady;
            };
            const Core::GpuTaskGraphPacketAcceptedCallback hardwareCausticsAcceptedCallback{
                .context = &hardwareCausticsAcceptance,
                .invoke = acceptHardwareCausticsPacket,
            };
            const Core::GpuTaskGraphPacketTimingTicket hardwareCausticsTimingTickets[] = {
                Core::GpuTaskGraphPacketTimingTicket{
                    .packet = hardwareCausticsPacket,
                    .timingTicket = &hardwareCausticsTimingTicket,
                },
            };
            const bool hardwareCausticsAccepted =
                m_deferredLightingTaskGraphValid
                && m_deferredHardwareCausticsTask.valid()
                && (!laggedAsyncLightingSchedule || m_deferredLightingHistoryCompletion.valid())
                && hardwareCausticsPacket.valid()
                && hardwareCausticsPacketRange.valid()
                && hardwareCausticsPacketRange.packetCount == LengthOf(hardwareCausticsTimingTickets)
                && hardwareCausticsSubmitter.submitPacketRangeInCompileOrder(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph,
                    hardwareCausticsPacketRange,
                    hardwareCausticsCompletionTokens,
                    hardwareCausticsCompletionCount,
                    hardwareCausticsTimingTickets,
                    LengthOf(hardwareCausticsTimingTickets),
                    m_deferredLightingSubmissionTransaction,
                    hardwareCausticsScratchArena,
                    nullptr,
                    &hardwareCausticsAcceptedCallback
                )
            ;
            hardwareCausticsSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(
                hardwareCausticsPacket
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
            if(!m_causticIrradianceLightingStateHandoff.buildTextureSubset(
                *causticsFinalStateSeed,
                deferredTargets.causticIrradiance.get()
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
                m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                    *causticsFinalStateSeed,
                    deferredTargets.causticIrradiance.get()
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

            Core::Texture* const causticsComputeScratchTextures[] = {
                deferredTargets.causticAccumulator.get(),
                deferredTargets.causticHistory.get(),
                deferredTargets.causticResolveHalf.get(),
                deferredTargets.causticResolveGeometry.get(),
            };
            if(!m_causticsComputePersistentStateHandoff.buildResourceSubset(
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
            && surfelGiCounterReadbackPacket.valid()
            && surfelGiCounterReadbackQueue
            && (static_cast<u8>(surfelGiCounterReadbackQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) != 0u
            && surfelGiCounterReadbackPacketRange.valid()
            && surfelGiCounterReadbackPacketRange.packetCount == 1u
        ;
        if(!readbackTailAvailable){
            m_deferredLightingSubmissionTransaction.rejectPacket(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                surfelGiCounterReadbackPacket
            );
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback tail was unavailable"));
        }
        else{
            Core::GpuNativePacketRecorder recorder(device);
            const bool readbackRecorded = recorder.recordPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                surfelGiCounterReadbackPacketRange,
                nullptr,
                0u,
                m_deferredLightingRecordedGraph
            );
            const Core::CommandListResourceStateHandoff* const readbackFinalStateSeed = readbackRecorded
                ? m_deferredLightingRecordedGraph.packetFinalStateSeed(surfelGiCounterReadbackPacket)
                : nullptr
            ;
            Core::CommandListResourceStateHandoff readbackCounterFinalState(m_arena);
            Core::Buffer* const readbackCounterBuffers[] = {
                m_rayTracingState.m_surfelCounterBuffer.get(),
            };
            const bool readbackFinalStateReady = readbackFinalStateSeed
                && readbackCounterFinalState.buildResourceSubset(
                    *readbackFinalStateSeed,
                    nullptr,
                    0u,
                    readbackCounterBuffers,
                    LengthOf(readbackCounterBuffers)
                )
            ;
            if(!readbackRecorded || !readbackFinalStateReady){
                m_deferredLightingSubmissionTransaction.rejectPacket(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    surfelGiCounterReadbackPacket
                );
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retain late deferred surfel counter-readback state"));
            }
            else{
                Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
                const Core::GpuTaskGraphSubmitter submitter(device);
                const bool readbackAccepted = submitter.submitPacketRangeInCompileOrder(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph,
                    surfelGiCounterReadbackPacketRange,
                    nullptr,
                    0u,
                    nullptr,
                    0u,
                    m_deferredLightingSubmissionTransaction,
                    scratchArena
                );
                const Core::QueueSubmissionToken readbackSubmissionToken = readbackAccepted
                    ? m_deferredLightingSubmissionTransaction.packetToken(surfelGiCounterReadbackPacket)
                    : Core::QueueSubmissionToken{}
                ;
                if(!readbackSubmissionToken.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred surfel counter-readback submission was rejected"));
                }
                else{
                    if(!m_surfelGiCounterPersistentStateHandoff.copyFrom(readbackCounterFinalState)){
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
    }

    if(requestsLaggedLightingHistoryCapture && !captureLaggedLightingHistory){
        // The optional tail could not be built, but Present already completed through the durable deferred graph.
        // Match the former standalone-copy fallback by forcing the next frame through bootstrap.
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting"));
        invalidateLaggedLightingHistorySubmission();
    }
    else if(captureLaggedLightingHistory){
        // The deferred graph's terminal history-copy task depends on Present internally. Its declared source uses
        // select retained producer snapshots only after those producers accepted, so this is intentionally a late
        // record into the shared recorded graph rather than part of the initial packet prefix.
        const Core::CommandListResourceStateHandoff* const causticHistoryCopySource = laggedAsyncLightingSchedule
            ? &m_causticIrradianceLightingStateHandoff
            : &m_causticIrradianceReturnStateHandoff
        ;
        Core::GpuExternalPacketStateSource historyCopyStateSources[3] = {};
        usize historyCopyStateSourceCount = 0u;
        const bool historyCopyStateSourcesReady =
            appendDeclaredStateSource(
                historyCopyStateSources,
                LengthOf(historyCopyStateSources),
                historyCopyStateSourceCount,
                &m_shadowVisibilityReturnStateHandoff
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
                &m_surfelIrradianceReturnStateHandoff
            )
        ;
        if(!historyCopyStateSourcesReady){
            m_deferredLightingSubmissionTransaction.discardUnaccepted(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph
            );
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history capture skipped because its source state was unavailable"));
            invalidateLaggedLightingHistorySubmission();
        }
        else if(
            !finalPresentationSubmissionToken.valid()
            || !m_deferredLightingTaskGraphValid
            || !m_deferredLaggedLightingHistoryTask.valid()
            || !deferredLaggedLightingHistoryPacket.valid()
            || !deferredLaggedLightingHistoryQueue
            || (static_cast<u8>(deferredLaggedLightingHistoryQueue->capabilities)
                & static_cast<u8>(Core::GpuQueueCapability::Transfer)) == 0u
            || !deferredLaggedLightingHistoryPacketRange.valid()
            || deferredLaggedLightingHistoryPacketRange.packetCount != 1u
        ){
            if(m_deferredLightingTaskGraphValid){
                m_deferredLightingSubmissionTransaction.discardUnaccepted(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph
                );
            }
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting"));
            invalidateLaggedLightingHistorySubmission();
        }
        else{
            Core::GpuNativePacketRecorder recorder(device);
            const Core::GpuNativePacketRecordDesc recordDescs[] = {
                Core::GpuNativePacketRecordDesc{
                    .packet = deferredLaggedLightingHistoryPacket,
                    .externalStateSources = historyCopyStateSources,
                    .externalStateSourceCount = historyCopyStateSourceCount,
                },
            };
            const bool historyCopyRecorded = recorder.recordPacketRangeInCompileOrder(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    deferredLaggedLightingHistoryPacketRange,
                    recordDescs,
                    LengthOf(recordDescs),
                    m_deferredLightingRecordedGraph
                )
            ;
            const Core::CommandListResourceStateHandoff* const historyCopyFinalStateSeed = historyCopyRecorded
                ? m_deferredLightingRecordedGraph.packetFinalStateSeed(deferredLaggedLightingHistoryPacket)
                : nullptr
            ;
            if(!historyCopyRecorded || !historyCopyFinalStateSeed){
                m_deferredLightingSubmissionTransaction.discardUnaccepted(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph
                );
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to late-record graph-owned lagged lighting-history capture; reverting to current-frame lighting"));
                invalidateLaggedLightingHistorySubmission();
            }
            else{
                Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
                const Core::GpuTaskGraphSubmitter submitter(device);
                const bool historyCopyAccepted = submitter.submitPacketRangeInCompileOrder(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph,
                    deferredLaggedLightingHistoryPacketRange,
                    nullptr,
                    0u,
                    nullptr,
                    0u,
                    m_deferredLightingSubmissionTransaction,
                    scratchArena
                );
                const Core::QueueSubmissionToken historyCopySubmissionToken = historyCopyAccepted
                    ? m_deferredLightingSubmissionTransaction.packetToken(deferredLaggedLightingHistoryPacket)
                    : Core::QueueSubmissionToken{}
                ;
                if(!historyCopySubmissionToken.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned lagged lighting-history capture submission was rejected; reverting to current-frame lighting"));
                    invalidateLaggedLightingHistorySubmission();
                }
                else{
                    const bool historyCopyReturnStatesReady =
                        m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                            *historyCopyFinalStateSeed,
                            deferredTargets.shadowVisibility.get()
                        )
                        && m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                            *historyCopyFinalStateSeed,
                            deferredTargets.causticIrradiance.get()
                        )
                        && m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                            *historyCopyFinalStateSeed,
                            deferredTargets.surfelIrradiance.get()
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

