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
    m_avboitSystem.resetTaskGraphStage();
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

