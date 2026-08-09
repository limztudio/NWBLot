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
    , m_frameRecoveryTaskGraph(arena)
    , m_frameRecoveryTaskGraphAnalysis(arena)
    , m_frameRecoveryTaskGraphQueueAssignments(arena)
    , m_frameRecoveryCompiledGraph(arena)
    , m_frameRecoveryRecordedGraph(arena)
    , m_frameRecoverySubmissionTransaction(arena)
    , m_shadowPrepareTaskGraph(arena)
    , m_shadowPrepareTaskGraphAnalysis(arena)
    , m_shadowPrepareTaskGraphQueueAssignments(arena)
    , m_shadowPrepareCompiledGraph(arena)
    , m_shadowPrepareRecordedGraph(arena)
    , m_shadowPrepareSubmissionTransaction(arena)
    , m_graphicsPrefixTaskGraph(arena)
    , m_graphicsPrefixTaskGraphAnalysis(arena)
    , m_graphicsPrefixTaskGraphQueueAssignments(arena)
    , m_graphicsPrefixCompiledGraph(arena)
    , m_graphicsPrefixRecordedGraph(arena)
    , m_graphicsPrefixSubmissionTransaction(arena)
    , m_laggedLightingHistoryTaskGraph(arena)
    , m_laggedLightingHistoryTaskGraphAnalysis(arena)
    , m_laggedLightingHistoryTaskGraphQueueAssignments(arena)
    , m_laggedLightingHistoryCompiledGraph(arena)
    , m_laggedLightingHistoryRecordedGraph(arena)
    , m_laggedLightingHistorySubmissionTransaction(arena)
    , m_deferredPresentTaskGraph(arena)
    , m_deferredPresentTaskGraphAnalysis(arena)
    , m_deferredPresentTaskGraphQueueAssignments(arena)
    , m_deferredPresentCompiledGraph(arena)
    , m_deferredPresentRecordedGraph(arena)
    , m_deferredPresentSubmissionTransaction(arena)
    , m_shadowVisibilityTaskGraph(arena)
    , m_shadowVisibilityTaskGraphAnalysis(arena)
    , m_shadowVisibilityTaskGraphQueueAssignments(arena)
    , m_shadowVisibilityCompiledGraph(arena)
    , m_shadowVisibilityRecordedGraph(arena)
    , m_shadowVisibilitySubmissionTransaction(arena)
    , m_hardwareCausticsTaskGraph(arena)
    , m_hardwareCausticsTaskGraphAnalysis(arena)
    , m_hardwareCausticsTaskGraphQueueAssignments(arena)
    , m_hardwareCausticsCompiledGraph(arena)
    , m_hardwareCausticsRecordedGraph(arena)
    , m_hardwareCausticsSubmissionTransaction(arena)
    , m_avboitTaskGraph(arena)
    , m_avboitTaskGraphAnalysis(arena)
    , m_avboitTaskGraphQueueAssignments(arena)
    , m_avboitCompiledGraph(arena)
    , m_avboitRecordedGraph(arena)
    , m_avboitSubmissionTransaction(arena)
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
    , m_causticsComputePersistentStateHandoff(arena)
    , m_causticIrradianceLightingStateHandoff(arena)
    , m_causticIrradianceReturnStateHandoff(arena)
    , m_surfelGiComputePersistentStateHandoff(arena)
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
    m_surfelIrradianceReturnStateHandoff.reset();
}

void RendererSystem::resetInvalidatedResourceStateHandoffs()noexcept{
    // The shadow-preparation packet owns its serial export; only retained cross-frame state is reset here.
    resetTargetGenerationStateHandoffs();
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
    m_preparedShadowVisibilityReady = false;
    m_frameRecoveryTaskGraphValid = false;
    m_frameRecoveryTask = {};
    m_frameRecoveryAsyncCompletion = {};
    m_frameRecoveryTaskGraph.reset();
    m_frameRecoveryTaskGraphAnalysis.reset();
    m_frameRecoveryTaskGraphQueueAssignments.reset();
    m_frameRecoveryCompiledGraph.reset();
    m_frameRecoveryRecordedGraph.reset(m_frameRecoveryCompiledGraph);
    m_frameRecoverySubmissionTransaction.reset(m_frameRecoveryCompiledGraph);
    m_shadowPrepareTaskGraphValid = false;
    m_shadowPrepareTask = {};
    m_shadowPrepareTaskGraph.reset();
    m_shadowPrepareTaskGraphAnalysis.reset();
    m_shadowPrepareTaskGraphQueueAssignments.reset();
    m_shadowPrepareCompiledGraph.reset();
    m_shadowPrepareRecordedGraph.reset(m_shadowPrepareCompiledGraph);
    m_shadowPrepareSubmissionTransaction.reset(m_shadowPrepareCompiledGraph);
    m_graphicsPrefixTaskGraphValid = false;
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_graphicsPrefixTaskGraph.reset();
    m_graphicsPrefixTaskGraphAnalysis.reset();
    m_graphicsPrefixTaskGraphQueueAssignments.reset();
    m_graphicsPrefixCompiledGraph.reset();
    m_graphicsPrefixRecordedGraph.reset(m_graphicsPrefixCompiledGraph);
    m_graphicsPrefixSubmissionTransaction.reset(m_graphicsPrefixCompiledGraph);
    m_laggedLightingHistoryTaskGraphValid = false;
    m_laggedLightingHistoryTask = {};
    m_laggedLightingPresentationCompletion = {};
    m_laggedLightingHistoryTaskGraph.reset();
    m_laggedLightingHistoryTaskGraphAnalysis.reset();
    m_laggedLightingHistoryTaskGraphQueueAssignments.reset();
    m_laggedLightingHistoryCompiledGraph.reset();
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);
    m_deferredPresentTaskGraphValid = false;
    m_deferredPresentTask = {};
    m_deferredPresentCompositeCompletion = {};
    m_deferredPresentSurfelGiCompletion = {};
    m_deferredPresentTaskGraph.reset();
    m_deferredPresentTaskGraphAnalysis.reset();
    m_deferredPresentTaskGraphQueueAssignments.reset();
    m_deferredPresentCompiledGraph.reset();
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);
    m_shadowVisibilityTaskGraphValid = false;
    m_shadowVisibilityTask = {};
    m_softwareCausticsTask = {};
    m_surfelGiTask = {};
    m_shadowVisibilityPrefixCompletion = {};
    m_shadowVisibilityTaskGraph.reset();
    m_shadowVisibilityTaskGraphAnalysis.reset();
    m_shadowVisibilityTaskGraphQueueAssignments.reset();
    m_shadowVisibilityCompiledGraph.reset();
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);
    m_hardwareCausticsTaskGraphValid = false;
    m_hardwareCausticsTask = {};
    m_hardwareCausticsPrefixCompletion = {};
    m_hardwareCausticsLaggedHistoryCompletion = {};
    m_hardwareCausticsTaskGraph.reset();
    m_hardwareCausticsTaskGraphAnalysis.reset();
    m_hardwareCausticsTaskGraphQueueAssignments.reset();
    m_hardwareCausticsCompiledGraph.reset();
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);
    m_avboitTaskGraphValid = false;
    m_avboitPreTask = {};
    m_avboitDepthWarpTask = {};
    m_avboitExtinctionTask = {};
    m_avboitIntegrationTask = {};
    m_avboitAccumulationTask = {};
    m_avboitPrefixCompletion = {};
    m_avboitTaskGraph.reset();
    m_avboitTaskGraphAnalysis.reset();
    m_avboitTaskGraphQueueAssignments.reset();
    m_avboitCompiledGraph.reset();
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);
    m_deferredLightingTaskGraphValid = false;
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredLightingAvboitCompletion = {};
    m_deferredLightingSurfelGiCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    m_taskGraphDeviceGeneration = m_taskGraphDeviceGeneration == Limit<u16>::s_Max
        ? 1u
        : static_cast<u16>(m_taskGraphDeviceGeneration + 1u)
    ;
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
    m_preparedShadowVisibilityReady = false;
    m_preparedHasTransparentRenderers = false;

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

    auto& device = m_graphics.getDevice();

    // Preparation uploads target slots and initializes surfels in an ordered Graphics graph packet before render.
    const bool deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded;
    m_raytracingSystem.discardSurfelResourceInitialization();
    Core::GpuTimingSubmissionTicket shadowPrepareTimingTicket(m_graphics.gpuTiming());
    buildShadowPrepareTaskGraph(deferredTargets, shadowPrepareTimingTicket);
    const Core::GpuSubmissionPacketId shadowPreparePacket = m_shadowPrepareCompiledGraph.packetForTask(
        m_shadowPrepareTask
    );
    const Core::GpuPhysicalQueueInfo* const shadowPrepareQueue = shadowPreparePacket.valid()
        ? m_shadowPrepareCompiledGraph.queueInfo(m_shadowPrepareCompiledGraph.packet(shadowPreparePacket).queue)
        : nullptr
    ;
    const auto discardShadowPrepare = [&](){
        if(m_shadowPrepareTaskGraphValid){
            m_shadowPrepareSubmissionTransaction.discardUnaccepted(
                m_shadowPrepareTaskGraph,
                m_shadowPrepareCompiledGraph
            );
        }
        else{
            // A declaration or compilation failure occurs before the task payload can run its graph discard hook.
            m_rayTracingState.m_tlasStaticSceneHashValid = false;
            m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
            m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
            m_rayTracingState.m_swShadowMaterialContextHashValid = false;
            shadowPrepareTimingTicket.discard();
            m_preparedShadowVisibilityReady = false;
            deferredTargets.bindless.slotsUploaded = deferredBindlessSlotsWereUploaded;
            m_raytracingSystem.discardSurfelResourceInitialization();
        }
        m_shadowPrepareRecordedGraph.reset(m_shadowPrepareCompiledGraph);
    };
    if(
        !m_shadowPrepareTaskGraphValid
        || !m_shadowPrepareTask.valid()
        || !shadowPreparePacket.valid()
        || !shadowPrepareQueue
        || shadowPrepareQueue->queueClass != Core::CommandQueue::Graphics
    ){
        discardShadowPrepare();
        return false;
    }

    bool shadowPrepareRecorded = false;
    const Core::Graphics::JobHandle shadowPrepareJob = m_graphics.scheduleGraphicsJob([
        this,
        &shadowPrepareRecorded,
        shadowPreparePacket
    ](){
        const Core::GpuNativePacketRecorder recorder(m_graphics.getDevice());
        shadowPrepareRecorded = recorder.recordPacket(
            m_shadowPrepareTaskGraph,
            m_shadowPrepareCompiledGraph,
            Core::GpuNativePacketRecordDesc{
                .packet = shadowPreparePacket,
            },
            m_shadowPrepareRecordedGraph
        );
    });
    if(!shadowPrepareJob.valid()){
        discardShadowPrepare();
        return false;
    }

    m_graphics.waitJob(shadowPrepareJob);
    if(!shadowPrepareRecorded){
        discardShadowPrepare();
        return false;
    }

    const Core::CommandListResourceStateHandoff* const shadowPrepareStateSeed =
        m_shadowPrepareRecordedGraph.packetFinalStateSeed(shadowPreparePacket)
    ;
    if(!shadowPrepareStateSeed){
        discardShadowPrepare();
        return false;
    }

    Core::Alloc::ScratchArena submissionScratch(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphSubmitter submitter(device);
    if(!submitter.submitPacket(
        m_shadowPrepareTaskGraph,
        m_shadowPrepareCompiledGraph,
        m_shadowPrepareRecordedGraph,
        shadowPreparePacket,
        nullptr,
        0u,
        m_shadowPrepareSubmissionTransaction,
        submissionScratch,
        &shadowPrepareTimingTicket
    )){
        discardShadowPrepare();
        return false;
    }

    return true;
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    m_frameRecoveryTaskGraphValid = false;
    m_frameRecoveryTask = {};
    m_frameRecoveryAsyncCompletion = {};
    m_frameRecoveryTaskGraph.reset();
    m_frameRecoveryTaskGraphAnalysis.reset();
    m_frameRecoveryTaskGraphQueueAssignments.reset();
    m_frameRecoveryCompiledGraph.reset();
    m_frameRecoveryRecordedGraph.reset(m_frameRecoveryCompiledGraph);
    m_frameRecoverySubmissionTransaction.reset(m_frameRecoveryCompiledGraph);
    m_graphicsPrefixTaskGraphValid = false;
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_graphicsPrefixTaskGraph.reset();
    m_graphicsPrefixTaskGraphAnalysis.reset();
    m_graphicsPrefixTaskGraphQueueAssignments.reset();
    m_graphicsPrefixCompiledGraph.reset();
    m_graphicsPrefixRecordedGraph.reset(m_graphicsPrefixCompiledGraph);
    m_graphicsPrefixSubmissionTransaction.reset(m_graphicsPrefixCompiledGraph);
    m_laggedLightingHistoryTaskGraphValid = false;
    m_laggedLightingHistoryTask = {};
    m_laggedLightingPresentationCompletion = {};
    m_laggedLightingHistoryTaskGraph.reset();
    m_laggedLightingHistoryTaskGraphAnalysis.reset();
    m_laggedLightingHistoryTaskGraphQueueAssignments.reset();
    m_laggedLightingHistoryCompiledGraph.reset();
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);
    m_deferredPresentTaskGraphValid = false;
    m_deferredPresentTask = {};
    m_deferredPresentCompositeCompletion = {};
    m_deferredPresentSurfelGiCompletion = {};
    m_deferredPresentTaskGraph.reset();
    m_deferredPresentTaskGraphAnalysis.reset();
    m_deferredPresentTaskGraphQueueAssignments.reset();
    m_deferredPresentCompiledGraph.reset();
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);
    m_shadowVisibilityTaskGraphValid = false;
    m_shadowVisibilityTask = {};
    m_softwareCausticsTask = {};
    m_surfelGiTask = {};
    m_shadowVisibilityPrefixCompletion = {};
    m_shadowVisibilityTaskGraph.reset();
    m_shadowVisibilityTaskGraphAnalysis.reset();
    m_shadowVisibilityTaskGraphQueueAssignments.reset();
    m_shadowVisibilityCompiledGraph.reset();
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);
    m_hardwareCausticsTaskGraphValid = false;
    m_hardwareCausticsTask = {};
    m_hardwareCausticsPrefixCompletion = {};
    m_hardwareCausticsLaggedHistoryCompletion = {};
    m_hardwareCausticsTaskGraph.reset();
    m_hardwareCausticsTaskGraphAnalysis.reset();
    m_hardwareCausticsTaskGraphQueueAssignments.reset();
    m_hardwareCausticsCompiledGraph.reset();
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);
    m_avboitTaskGraphValid = false;
    m_avboitPreTask = {};
    m_avboitDepthWarpTask = {};
    m_avboitExtinctionTask = {};
    m_avboitIntegrationTask = {};
    m_avboitAccumulationTask = {};
    m_avboitPrefixCompletion = {};
    m_avboitTaskGraph.reset();
    m_avboitTaskGraphAnalysis.reset();
    m_avboitTaskGraphQueueAssignments.reset();
    m_avboitCompiledGraph.reset();
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);
    m_deferredLightingTaskGraphValid = false;
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredLightingAvboitCompletion = {};
    m_deferredLightingSurfelGiCompletion = {};
    m_deferredLightingHistoryCompletion = {};
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
    const Core::GpuSubmissionPacketId shadowPreparePacket = m_shadowPrepareCompiledGraph.packetForTask(
        m_shadowPrepareTask
    );
    const Core::CommandListResourceStateHandoff* const shadowPrepareStateSeed =
        shadowPreparePacket.valid()
            ? m_shadowPrepareRecordedGraph.packetFinalStateSeed(shadowPreparePacket)
            : nullptr
    ;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    NWB_ASSERT(m_shadowPrepareTaskGraphValid && shadowPrepareStateSeed);
    NWB_ASSERT(deferredTargets.bindless.slotsUploaded);
    if(!m_shadowPrepareTaskGraphValid || !shadowPrepareStateSeed){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned shadow preparation was unavailable"));
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
    const bool dedicatedAsyncCompute = device.isRenderLaneDedicated(Core::RenderLane::AsyncCompute);
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
    // transport.
    const bool captureLaggedLightingHistory = laggedAsyncLightingRequested;
    // Software caustics are graph-owned. A distinct Compute family is the only route that may resolve to Compute;
    // otherwise the compiler routes the same task through Graphics without a renderer fallback topology.
    const bool shadowVisibilityExpectedCompute = dedicatedAsyncCompute;
    const bool softwareCausticsExpectedCompute = dedicatedAsyncCompute && !hardwareShadowSupported;
    const bool shadowVisibilityPrepared = m_preparedShadowVisibilityReady;
    // Compile every independent graph before native recording. The graphics prefix records all five ordered tasks
    // natively from mesh-view setup through post-G-buffer normalization.
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput taskGraphInput{
        .dedicatedAsyncCompute = dedicatedAsyncCompute,
        .frameLaggedAsyncLightingEnabled = m_frameLaggedAsyncLightingEnabled,
        .laggedLightingHistoryReady = laggedLightingHistoryResourcesReady,
        .laggedLightingHistoryAccepted = m_laggedLightingHistorySubmissionToken.valid(),
        .hasTransparentRenderers = hasTransparentRenderers,
        .hardwareCaustics = hardwareShadowSupported,
    };
    buildLaggedLightingHistoryTaskGraph(taskGraphInput, deferredTargets);

    resetFrameRecordingStateHandoffs();
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Preserve CPU mirrors so rejected recordings can be retried exactly.
    struct PostGbufferPacketCpuState{
        u64 swShadowEdgeStatsPendingSubmissionID = 0u;
        u64 surfelCountReadbackPendingSubmissionID = 0u;

        u32 softShadowFrameIndex = 0u;
        u32 swShadowEdgeStatsTick = 0u;
        u32 swShadowEdgeStatsPendingTick = 0u;
        u32 causticTemporalReuseFrameCount = 0u;
        u32 swCausticFrameIndex = 0u;
        u32 hwCausticFrameIndex = 0u;
        u32 surfelFrameIndex = 0u;
        u32 surfelCountReadbackFrame = 0u;

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
        bool surfelCountReadbackPending = false;
        bool surfelCountReadbackPendingSubmissionUnconfirmed = false;
        Core::CommandQueue::Enum swShadowEdgeStatsPendingSubmissionQueue = Core::CommandQueue::kCount;
        Core::CommandQueue::Enum surfelCountReadbackPendingSubmissionQueue = Core::CommandQueue::kCount;
    };
    const PostGbufferPacketCpuState postGbufferPacketCpuState{
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionID,
        m_rayTracingState.m_softShadowFrameIndex,
        m_rayTracingState.m_swShadowEdgeStatsTick,
        m_rayTracingState.m_swShadowEdgeStatsPendingTick,
        m_rayTracingState.m_causticTemporalReuseFrameCount,
        m_rayTracingState.m_swCausticFrameIndex,
        m_rayTracingState.m_hwCausticFrameIndex,
        m_rayTracingState.m_surfelFrameIndex,
        m_rayTracingState.m_surfelCountReadbackFrame,
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
        m_rayTracingState.m_surfelCountReadbackPending,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionUnconfirmed,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionQueue,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionQueue,
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
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionQueue = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionQueue;
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
        m_rayTracingState.m_surfelCountReadbackPending = postGbufferPacketCpuState.surfelCountReadbackPending;
        m_rayTracingState.m_surfelCountReadbackFrame = postGbufferPacketCpuState.surfelCountReadbackFrame;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionID = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionID;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionQueue = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionQueue;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionUnconfirmed = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionUnconfirmed;
    };
    const auto restoreAvboitCpuState = [&](){
        m_avboitState.m_targetsNeedClear = postGbufferPacketCpuState.avboitTargetsNeedClear;
    };
    const auto restorePostGbufferEffectsCpuState = [&](){
        restoreCausticsCpuState();
        restoreSurfelGiCpuState();
        restoreAvboitCpuState();
    };
    const auto restorePostGbufferPacketCpuState = [&](){
        deferredTargets.bindless.slotsUploaded = postGbufferPacketCpuState.deferredBindlessSlotsUploaded;
        restorePrefixCpuState();
        restoreShadowCpuState();
        restorePostGbufferEffectsCpuState();
    };

    // The graph-owned prefix packet retains the established timing scope across mesh-view setup,
    // scene-shading setup, deferred clear, G-buffer, and normalization.
    Core::GpuTimingSubmissionTicket graphicsPrefixTimingTicket(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncPrefixTiming;
    Core::GpuTimingSubmissionTicket shadowVisibilityTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket softwareCausticsTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket surfelGiTimingTicket(m_graphics.gpuTiming());
    buildShadowVisibilityTaskGraph(
        taskGraphInput,
        deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        shadowVisibilityTimingTicket,
        softwareCausticsTimingTicket,
        surfelGiTimingTicket
    );
    const Core::GpuSubmissionPacketId shadowVisibilityPacket = m_shadowVisibilityCompiledGraph.packetForTask(
        m_shadowVisibilityTask
    );
    const Core::GpuSubmissionPacketId softwareCausticsPacket = m_shadowVisibilityCompiledGraph.packetForTask(
        m_softwareCausticsTask
    );
    const Core::GpuSubmissionPacketId surfelGiPacket = m_shadowVisibilityCompiledGraph.packetForTask(m_surfelGiTask);
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = shadowVisibilityPacket.valid()
        ? m_shadowVisibilityCompiledGraph.queueInfo(
            m_shadowVisibilityCompiledGraph.packet(shadowVisibilityPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const softwareCausticsQueue = softwareCausticsPacket.valid()
        ? m_shadowVisibilityCompiledGraph.queueInfo(
            m_shadowVisibilityCompiledGraph.packet(softwareCausticsPacket).queue
        )
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const surfelGiQueue = surfelGiPacket.valid()
        ? m_shadowVisibilityCompiledGraph.queueInfo(m_shadowVisibilityCompiledGraph.packet(surfelGiPacket).queue)
        : nullptr
    ;
    const usize shadowEffectsPacketCount = hardwareShadowSupported ? 2u : 3u;
    if(
        !m_shadowVisibilityTaskGraphValid
        || !m_shadowVisibilityTask.valid()
        || !m_surfelGiTask.valid()
        || !m_shadowVisibilityPrefixCompletion.valid()
        || !shadowVisibilityPacket.valid()
        || !surfelGiPacket.valid()
        || m_shadowVisibilityCompiledGraph.packetCount() != shadowEffectsPacketCount
        || !shadowVisibilityQueue
        || !surfelGiQueue
        || m_shadowVisibilityCompiledGraph.packetIdAt(0u) != shadowVisibilityPacket
        || (hardwareShadowSupported
            ? m_shadowVisibilityCompiledGraph.packetIdAt(1u) != surfelGiPacket
            : (
                !m_softwareCausticsTask.valid()
                || !softwareCausticsPacket.valid()
                || !softwareCausticsQueue
                || m_shadowVisibilityCompiledGraph.packetIdAt(1u) != softwareCausticsPacket
                || m_shadowVisibilityCompiledGraph.packetIdAt(2u) != surfelGiPacket
            )
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned shadow effects chain was unavailable"));
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool shadowVisibilityRunsOnCompute = shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
    if(shadowVisibilityRunsOnCompute != shadowVisibilityExpectedCompute){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow-visibility graph queue disagrees with renderer topology"));
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    Core::GpuTimingSubmissionTicket hardwareCausticsTimingTicket(m_graphics.gpuTiming());
    if(hardwareShadowSupported){
        buildHardwareCausticsTaskGraph(
            taskGraphInput,
            deferredTargets,
            shadowVisibilityPrepared,
            laggedAsyncLightingSchedule,
            hardwareCausticsTimingTicket
        );
    }
    const Core::GpuSubmissionPacketId hardwareCausticsPacket = m_hardwareCausticsCompiledGraph.packetForTask(
        m_hardwareCausticsTask
    );
    const Core::GpuPhysicalQueueInfo* const hardwareCausticsQueue = hardwareCausticsPacket.valid()
        ? m_hardwareCausticsCompiledGraph.queueInfo(
            m_hardwareCausticsCompiledGraph.packet(hardwareCausticsPacket).queue
        )
        : nullptr
    ;
    if(
        hardwareShadowSupported
        && (
            !m_hardwareCausticsTaskGraphValid
            || !m_hardwareCausticsTask.valid()
            || !m_hardwareCausticsPrefixCompletion.valid()
            || (laggedAsyncLightingSchedule && !m_hardwareCausticsLaggedHistoryCompletion.valid())
            || !hardwareCausticsQueue
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned hardware caustics were unavailable"));
        hardwareCausticsTimingTicket.discard();
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    if(
        hardwareShadowSupported
        && hardwareCausticsQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware-caustics graph queue disagrees with renderer topology"));
        hardwareCausticsTimingTicket.discard();
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool softwareCausticsRunsOnCompute =
        !hardwareShadowSupported
        && softwareCausticsQueue->queueClass == Core::CommandQueue::Compute
    ;
    if(!hardwareShadowSupported && softwareCausticsRunsOnCompute != softwareCausticsExpectedCompute){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-caustics graph queue disagrees with renderer topology"));
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        surfelGiTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool surfelGiRunsOnCompute = surfelGiQueue->queueClass == Core::CommandQueue::Compute;
    const bool clearAvboitTargets = hasTransparentRenderers || m_avboitState.m_targetsNeedClear;
    Core::GpuTimingSubmissionTicket avboitPreTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitDepthWarpTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitExtinctionTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitIntegrationTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitAccumulationTimingTicket(m_graphics.gpuTiming());
    buildAvboitTaskGraph(
        taskGraphInput,
        deferredTargets,
        csgFrameState,
        clearAvboitTargets,
        hasTransparentRenderers,
        avboitPreTimingTicket,
        avboitDepthWarpTimingTicket,
        avboitExtinctionTimingTicket,
        avboitIntegrationTimingTicket,
        avboitAccumulationTimingTicket
    );
    const Core::GpuSubmissionPacketId avboitPrePacket = m_avboitCompiledGraph.packetForTask(m_avboitPreTask);
    const Core::GpuPhysicalQueueInfo* const avboitPreQueue = avboitPrePacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitPrePacket).queue)
        : nullptr
    ;
    const bool avboitUsesAsyncCompute = m_avboitDepthWarpTask.valid();
    const Core::GpuSubmissionPacketId avboitDepthWarpPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitDepthWarpTask
    );
    const Core::GpuSubmissionPacketId avboitExtinctionPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitExtinctionTask
    );
    const Core::GpuSubmissionPacketId avboitIntegrationPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitIntegrationTask
    );
    const Core::GpuSubmissionPacketId avboitAccumulationPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitAccumulationTask
    );
    const Core::GpuPhysicalQueueInfo* const avboitDepthWarpQueue = avboitDepthWarpPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitDepthWarpPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionQueue = avboitExtinctionPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitExtinctionPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitIntegrationQueue = avboitIntegrationPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitIntegrationPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationQueue = avboitAccumulationPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitAccumulationPacket).queue)
        : nullptr
    ;
    if(
        !m_avboitTaskGraphValid
        || !m_avboitPreTask.valid()
        || !m_avboitPrefixCompletion.valid()
        || !avboitPreQueue
        || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
        || (
            avboitUsesAsyncCompute
            && (
                !m_avboitExtinctionTask.valid()
                || !m_avboitIntegrationTask.valid()
                || !m_avboitAccumulationTask.valid()
                || !avboitDepthWarpQueue
                || !avboitExtinctionQueue
                || !avboitIntegrationQueue
                || !avboitAccumulationQueue
                || avboitDepthWarpQueue->queueClass != Core::CommandQueue::Compute
                || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
                || avboitIntegrationQueue->queueClass != Core::CommandQueue::Compute
                || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
            )
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT was unavailable"));
        avboitPreTimingTicket.discard();
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    Core::GpuTimingSubmissionTicket deferredLightingTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket deferredCompositeTimingTicket(m_graphics.gpuTiming());
    buildDeferredLightingTaskGraph(
        taskGraphInput,
        deferredTargets,
        deferredLightingTimingTicket,
        deferredCompositeTimingTicket
    );
    const Core::GpuSubmissionPacketId deferredLightingPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredLightingTask
    );
    const Core::GpuSubmissionPacketId deferredCompositePacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredCompositeTask
    );
    const Core::GpuPhysicalQueueInfo* const deferredLightingQueue = deferredLightingPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(deferredLightingPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const deferredCompositeQueue = deferredCompositePacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(deferredCompositePacket).queue)
        : nullptr
    ;
    const Core::GpuExternalCompletionId deferredLightingDependentCompletion = laggedAsyncLightingSchedule
        ? m_deferredLightingHistoryCompletion
        : m_deferredLightingSurfelGiCompletion
    ;
    if(
        !m_deferredLightingTaskGraphValid
        || !m_deferredLightingTask.valid()
        || !m_deferredCompositeTask.valid()
        || !m_deferredLightingAvboitCompletion.valid()
        || !deferredLightingDependentCompletion.valid()
        || !deferredLightingPacket.valid()
        || !deferredCompositePacket.valid()
        || m_deferredLightingCompiledGraph.packetCount() != 2u
        || !deferredLightingQueue
        || !deferredCompositeQueue
        || (laggedAsyncLightingSchedule && deferredLightingQueue->queueClass != Core::CommandQueue::Graphics)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred lighting/composite was unavailable"));
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
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool deferredLightingRunsOnCompute = deferredLightingQueue->queueClass == Core::CommandQueue::Compute;
    // Publish the frame endpoint only after the final Graphics packet accepts.  This also covers the serialized
    // Graphics-only route when no dedicated compute family exists.
    Core::GpuTimingFrameTransaction frameTimingTransaction(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncFinalTiming;
    Core::GpuTimingSubmissionTicket deferredPresentTimingTicket(m_graphics.gpuTiming());
    buildDeferredPresentTaskGraph(
        taskGraphInput,
        deferredTargets,
        framebuffer,
        laggedAsyncLightingSchedule,
        shadowVisibilityRunsOnCompute,
        frameTimingTransaction,
        asyncFinalTiming,
        deferredPresentTimingTicket
    );
    const Core::GpuSubmissionPacketId deferredPresentPacket = m_deferredPresentCompiledGraph.packetForTask(
        m_deferredPresentTask
    );
    const Core::GpuPhysicalQueueInfo* const deferredPresentQueue = deferredPresentPacket.valid()
        ? m_deferredPresentCompiledGraph.queueInfo(m_deferredPresentCompiledGraph.packet(deferredPresentPacket).queue)
        : nullptr
    ;
    if(
        !m_deferredPresentTaskGraphValid
        || !m_deferredPresentTask.valid()
        || !m_deferredPresentCompositeCompletion.valid()
        || (laggedAsyncLightingSchedule && !m_deferredPresentSurfelGiCompletion.valid())
        || !deferredPresentQueue
        || deferredPresentQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred present was unavailable"));
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
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const auto discardTimingTickets = [
        &graphicsPrefixTimingTicket,
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
        graphicsPrefixTimingTicket.discard();
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
        m_graphicsPrefixSubmissionTransaction.discardUnaccepted(
            m_graphicsPrefixTaskGraph,
            m_graphicsPrefixCompiledGraph
        );
        m_deferredPresentSubmissionTransaction.discardUnaccepted(
            m_deferredPresentTaskGraph,
            m_deferredPresentCompiledGraph
        );
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
        m_avboitSubmissionTransaction.discardUnaccepted(
            m_avboitTaskGraph,
            m_avboitCompiledGraph
        );
        m_shadowVisibilitySubmissionTransaction.discardUnaccepted(
            m_shadowVisibilityTaskGraph,
            m_shadowVisibilityCompiledGraph
        );
        m_hardwareCausticsSubmissionTransaction.discardUnaccepted(
            m_hardwareCausticsTaskGraph,
            m_hardwareCausticsCompiledGraph
        );
    };
    const auto discardRenderPackets = [&](){
        if(asyncPrefixTiming){
            asyncPrefixTiming->discardTiming();
            asyncPrefixTiming.reset();
        }
        if(asyncFinalTiming){
            asyncFinalTiming->discardTiming();
            asyncFinalTiming.reset();
        }
        frameTimingTransaction.discard();
        discardTimingTickets();
        discardUnacceptedGraphPackets();
        restorePostGbufferPacketCpuState();
        m_raytracingSystem.discardSoftShadowTemporalHistory();
        resetAbandonedFrameStateHandoffs();
    };

    // The native prefix begins from the serial Graphics preparation snapshot and records mesh-view setup, scene
    // setup, clear, opaque G-buffer, and normalization as one graph-owned packet.
    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(deferredTargets.framebuffer->getFramebufferInfo());
    buildGraphicsPrefixTaskGraph(
        taskGraphInput,
        deferredTargets,
        csgFrameState,
        hasOpaqueCsgFrameWork,
        meshViewAspectRatio,
        shadowVisibilityRunsOnCompute,
        surfelGiRunsOnCompute,
        frameTimingTransaction,
        asyncPrefixTiming,
        graphicsPrefixTimingTicket
    );
    const Core::GpuSubmissionPacketId graphicsPrefixPacket = m_graphicsPrefixCompiledGraph.packetForTask(
        m_graphicsPrefixTask
    );
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixQueue = graphicsPrefixPacket.valid()
        ? m_graphicsPrefixCompiledGraph.queueInfo(
            m_graphicsPrefixCompiledGraph.packet(graphicsPrefixPacket).queue
        )
        : nullptr
    ;
    if(
        !m_graphicsPrefixTaskGraphValid
        || !m_graphicsPrefixMeshViewSetupTask.valid()
        || !m_graphicsPrefixSceneShadingSetupTask.valid()
        || !m_graphicsPrefixDeferredClearTask.valid()
        || !m_graphicsPrefixGbufferTask.valid()
        || !m_graphicsPrefixTask.valid()
        || !graphicsPrefixQueue
        || graphicsPrefixQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned graphics prefix was unavailable"));
        discardRenderPackets();
        return;
    }

    Core::GpuNativePacketRecorder graphicsPrefixRecorder(device);
    const Core::GpuNativePacketRecordDesc graphicsPrefixRecordDesc{
        .packet = graphicsPrefixPacket,
        .serialStateSeed = shadowPrepareStateSeed,
    };
    const bool graphicsPrefixRecorded =
        m_graphicsPrefixTaskGraphValid
        && m_graphicsPrefixMeshViewSetupTask.valid()
        && m_graphicsPrefixSceneShadingSetupTask.valid()
        && m_graphicsPrefixDeferredClearTask.valid()
        && m_graphicsPrefixGbufferTask.valid()
        && m_graphicsPrefixTask.valid()
        && graphicsPrefixPacket.valid()
        && graphicsPrefixRecorder.recordPacket(
            m_graphicsPrefixTaskGraph,
            m_graphicsPrefixCompiledGraph,
            graphicsPrefixRecordDesc,
            m_graphicsPrefixRecordedGraph
        )
    ;
    const Core::CommandListResourceStateHandoff* const graphicsPrefixStateSeed = graphicsPrefixRecorded
        ? m_graphicsPrefixRecordedGraph.packetFinalStateSeed(graphicsPrefixPacket)
        : nullptr
    ;
    if(!graphicsPrefixRecorded || !graphicsPrefixStateSeed){
        m_graphicsPrefixSubmissionTransaction.discardUnaccepted(
            m_graphicsPrefixTaskGraph,
            m_graphicsPrefixCompiledGraph
        );
        graphicsPrefixTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to capture graph-owned graphics-prefix state"));
        discardRenderPackets();
        return;
    }

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

    // Native graph packets seed every declared resource from the native prefix export and, on the dedicated
    // Compute route, from accepted cross-frame state. No renderer-side input subset or fan-in is needed.
    Core::GpuExternalPacketStateSource shadowVisibilityStateSources[3] = {};
    usize shadowVisibilityStateSourceCount = 0u;
    bool shadowVisibilityStateSourcesReady = appendDeclaredStateSource(
        shadowVisibilityStateSources,
        LengthOf(shadowVisibilityStateSources),
        shadowVisibilityStateSourceCount,
        graphicsPrefixStateSeed
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
    bool softwareCausticsStateSourcesReady = true;
    if(!hardwareShadowSupported){
        softwareCausticsStateSourcesReady = appendDeclaredStateSource(
            softwareCausticsStateSources,
            LengthOf(softwareCausticsStateSources),
            softwareCausticsStateSourceCount,
            graphicsPrefixStateSeed
        );
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

    Core::GpuExternalPacketStateSource surfelGiStateSources[3] = {};
    usize surfelGiStateSourceCount = 0u;
    bool surfelGiStateSourcesReady = appendDeclaredStateSource(
        surfelGiStateSources,
        LengthOf(surfelGiStateSources),
        surfelGiStateSourceCount,
        graphicsPrefixStateSeed
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

    // Shadow Visibility, its optional Software Caustics successor, and Surfel GI record in compiler order. Each
    // packet retains declaration-filtered prefix/return sources; the selected effects predecessor and all shared
    // state seeds stay graph-internal.
    const Core::GpuNativePacketRecordDesc hardwareShadowEffectsRecordDescs[] = {
        Core::GpuNativePacketRecordDesc{
            .packet = shadowVisibilityPacket,
            .externalStateSources = shadowVisibilityStateSources,
            .externalStateSourceCount = shadowVisibilityStateSourceCount,
        },
        Core::GpuNativePacketRecordDesc{
            .packet = surfelGiPacket,
            .externalStateSources = surfelGiStateSources,
            .externalStateSourceCount = surfelGiStateSourceCount,
        },
    };
    const Core::GpuNativePacketRecordDesc softwareShadowEffectsRecordDescs[] = {
        Core::GpuNativePacketRecordDesc{
            .packet = shadowVisibilityPacket,
            .externalStateSources = shadowVisibilityStateSources,
            .externalStateSourceCount = shadowVisibilityStateSourceCount,
        },
        Core::GpuNativePacketRecordDesc{
            .packet = softwareCausticsPacket,
            .externalStateSources = softwareCausticsStateSources,
            .externalStateSourceCount = softwareCausticsStateSourceCount,
        },
        Core::GpuNativePacketRecordDesc{
            .packet = surfelGiPacket,
            .externalStateSources = surfelGiStateSources,
            .externalStateSourceCount = surfelGiStateSourceCount,
        },
    };
    const Core::GpuNativePacketRecordDesc* const shadowEffectsRecordDescs = hardwareShadowSupported
        ? hardwareShadowEffectsRecordDescs
        : softwareShadowEffectsRecordDescs
    ;
    const usize shadowEffectsRecordDescCount = hardwareShadowSupported
        ? LengthOf(hardwareShadowEffectsRecordDescs)
        : LengthOf(softwareShadowEffectsRecordDescs)
    ;
    const Core::GpuNativePacketRecorder shadowEffectsRecorder(device);
    const bool shadowEffectsRecorded =
        shadowVisibilityStateSourcesReady
        && m_shadowVisibilityTaskGraphValid
        && m_shadowVisibilityTask.valid()
        && m_surfelGiTask.valid()
        && m_shadowVisibilityPrefixCompletion.valid()
        && shadowVisibilityPacket.valid()
        && surfelGiPacket.valid()
        && surfelGiStateSourcesReady
        && m_shadowVisibilityCompiledGraph.packetCount() == shadowEffectsRecordDescCount
        && m_shadowVisibilityCompiledGraph.packetIdAt(0u) == shadowVisibilityPacket
        && (hardwareShadowSupported
            ? m_shadowVisibilityCompiledGraph.packetIdAt(1u) == surfelGiPacket
            : (
                softwareCausticsStateSourcesReady
                && m_softwareCausticsTask.valid()
                && softwareCausticsPacket.valid()
                && m_shadowVisibilityCompiledGraph.packetIdAt(1u) == softwareCausticsPacket
                && m_shadowVisibilityCompiledGraph.packetIdAt(2u) == surfelGiPacket
            )
        )
        && shadowEffectsRecorder.recordPacketsInCompileOrder(
            m_shadowVisibilityTaskGraph,
            m_shadowVisibilityCompiledGraph,
            shadowEffectsRecordDescs,
            shadowEffectsRecordDescCount,
            m_shadowVisibilityRecordedGraph
        )
    ;
    const Core::CommandListResourceStateHandoff* const shadowVisibilityFinalStateSeed = shadowEffectsRecorded
        ? m_shadowVisibilityRecordedGraph.packetFinalStateSeed(shadowVisibilityPacket)
        : nullptr
    ;
    const Core::CommandListResourceStateHandoff* const softwareCausticsFinalStateSeed =
        shadowEffectsRecorded && !hardwareShadowSupported
            ? m_shadowVisibilityRecordedGraph.packetFinalStateSeed(softwareCausticsPacket)
            : nullptr
    ;
    const Core::CommandListResourceStateHandoff* const surfelGiFinalStateSeed = shadowEffectsRecorded
        ? m_shadowVisibilityRecordedGraph.packetFinalStateSeed(surfelGiPacket)
        : nullptr
    ;
    if(
        !shadowEffectsRecorded
        || !shadowVisibilityFinalStateSeed
        || !surfelGiFinalStateSeed
        || (!hardwareShadowSupported && !softwareCausticsFinalStateSeed)
    ){
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned shadow effects chain"));
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* causticsFinalStateSeed = softwareCausticsFinalStateSeed;
    if(hardwareShadowSupported){
        Core::GpuExternalPacketStateSource hardwareCausticsStateSources[1] = {};
        usize hardwareCausticsStateSourceCount = 0u;
        const bool hardwareCausticsStateSourcesReady = appendDeclaredStateSource(
            hardwareCausticsStateSources,
            LengthOf(hardwareCausticsStateSources),
            hardwareCausticsStateSourceCount,
            graphicsPrefixStateSeed
        );
        Core::GpuNativePacketRecorder hardwareCausticsRecorder(device);
        const Core::GpuNativePacketRecordDesc hardwareCausticsRecordDesc{
            .packet = hardwareCausticsPacket,
            .externalStateSources = hardwareCausticsStateSources,
            .externalStateSourceCount = hardwareCausticsStateSourceCount,
        };
        const bool hardwareCausticsRecorded =
            hardwareCausticsStateSourcesReady
            && m_hardwareCausticsTaskGraphValid
            && m_hardwareCausticsTask.valid()
            && m_hardwareCausticsPrefixCompletion.valid()
            && (!laggedAsyncLightingSchedule || m_hardwareCausticsLaggedHistoryCompletion.valid())
            && hardwareCausticsPacket.valid()
            && hardwareCausticsRecorder.recordPacket(
                m_hardwareCausticsTaskGraph,
                m_hardwareCausticsCompiledGraph,
                hardwareCausticsRecordDesc,
                m_hardwareCausticsRecordedGraph
            )
        ;
        causticsFinalStateSeed = hardwareCausticsRecorded
            ? m_hardwareCausticsRecordedGraph.packetFinalStateSeed(hardwareCausticsPacket)
            : nullptr
        ;
        if(!hardwareCausticsRecorded || !causticsFinalStateSeed){
            m_hardwareCausticsSubmissionTransaction.discardUnaccepted(
                m_hardwareCausticsTaskGraph,
                m_hardwareCausticsCompiledGraph
            );
            hardwareCausticsTimingTicket.discard();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned hardware caustics"));
            discardRenderPackets();
            return;
        }
    }

    // AVBOIT's raster entry point imports the prefix packet state; every later stage is recorded in the compiler's
    // dependency order, so its state seeds stay internal to the declared packet chain.
    m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
    Core::GpuNativePacketRecorder avboitRecorder(device);
    const Core::GpuExternalPacketStateSource avboitPreStateSources[] = {
        Core::GpuExternalPacketStateSource{
            .states = graphicsPrefixStateSeed,
        },
    };
    const Core::GpuNativePacketRecordDesc avboitRecordDescs[] = {
        Core::GpuNativePacketRecordDesc{
            .packet = avboitPrePacket,
            .externalStateSources = avboitPreStateSources,
            .externalStateSourceCount = LengthOf(avboitPreStateSources),
        },
        Core::GpuNativePacketRecordDesc{
            .packet = avboitDepthWarpPacket,
        },
        Core::GpuNativePacketRecordDesc{
            .packet = avboitExtinctionPacket,
        },
        Core::GpuNativePacketRecordDesc{
            .packet = avboitIntegrationPacket,
        },
        Core::GpuNativePacketRecordDesc{
            .packet = avboitAccumulationPacket,
        },
    };
    const usize avboitRecordDescCount = avboitUsesAsyncCompute ? LengthOf(avboitRecordDescs) : 1u;
    const bool avboitRecorded =
        m_avboitTaskGraphValid
        && m_avboitPreTask.valid()
        && m_avboitPrefixCompletion.valid()
        && avboitPrePacket.valid()
        && (!avboitUsesAsyncCompute || (
            m_avboitDepthWarpTask.valid()
            && m_avboitExtinctionTask.valid()
            && m_avboitIntegrationTask.valid()
            && m_avboitAccumulationTask.valid()
        ))
        && avboitRecorder.recordPacketsInCompileOrder(
            m_avboitTaskGraph,
            m_avboitCompiledGraph,
            avboitRecordDescs,
            avboitRecordDescCount,
            m_avboitRecordedGraph
        )
    ;
    if(!avboitRecorded){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned AVBOIT packet chain"));
        discardRenderPackets();
        return;
    }

    const Core::GpuSubmissionPacketId avboitFinalPacket = avboitUsesAsyncCompute
        ? avboitAccumulationPacket
        : avboitPrePacket
    ;
    const Core::CommandListResourceStateHandoff* const avboitFinalStateSeed =
        m_avboitRecordedGraph.packetFinalStateSeed(avboitFinalPacket)
    ;
    if(!avboitFinalStateSeed){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT graph did not retain its final state seed"));
        discardRenderPackets();
        return;
    }

    // Lighting imports live effects only on the default path; lagged mode uses accepted history.  Each source is
    // filtered by the lighting task declaration, so the graph owns the formerly hand-written state fan-in.
    Core::GpuExternalPacketStateSource deferredLightingStateSources[5] = {};
    usize deferredLightingStateSourceCount = 0u;
    bool deferredLightingStateSourcesReady = appendDeclaredStateSource(
        deferredLightingStateSources,
        LengthOf(deferredLightingStateSources),
        deferredLightingStateSourceCount,
        graphicsPrefixStateSeed
    );
    if(!laggedAsyncLightingSchedule){
        deferredLightingStateSourcesReady = deferredLightingStateSourcesReady
            && appendDeclaredStateSource(
                deferredLightingStateSources,
                LengthOf(deferredLightingStateSources),
                deferredLightingStateSourceCount,
                shadowVisibilityFinalStateSeed
            )
            && appendDeclaredStateSource(
                deferredLightingStateSources,
                LengthOf(deferredLightingStateSources),
                deferredLightingStateSourceCount,
                causticsFinalStateSeed
            )
            && appendDeclaredStateSource(
                deferredLightingStateSources,
                LengthOf(deferredLightingStateSources),
                deferredLightingStateSourceCount,
                surfelGiFinalStateSeed
            )
        ;
    }
    deferredLightingStateSourcesReady = deferredLightingStateSourcesReady
        && appendDeclaredStateSource(
            deferredLightingStateSources,
            LengthOf(deferredLightingStateSources),
            deferredLightingStateSourceCount,
            avboitFinalStateSeed
        )
    ;

    // Lighting and composite share one graph. The latter receives the former's opaque result as a compiler-produced
    // state seed, while its AVBOIT and current-prefix inputs remain declaration-filtered external sources.
    Core::GpuExternalPacketStateSource deferredCompositeStateSources[2] = {};
    usize deferredCompositeStateSourceCount = 0u;
    const bool deferredCompositeStateSourcesReady =
        appendDeclaredStateSource(
            deferredCompositeStateSources,
            LengthOf(deferredCompositeStateSources),
            deferredCompositeStateSourceCount,
            graphicsPrefixStateSeed
        )
        && appendDeclaredStateSource(
            deferredCompositeStateSources,
            LengthOf(deferredCompositeStateSources),
            deferredCompositeStateSourceCount,
            avboitFinalStateSeed
        )
    ;
    const Core::GpuNativePacketRecordDesc deferredRecordDescs[] = {
        Core::GpuNativePacketRecordDesc{
            .packet = deferredLightingPacket,
            .externalStateSources = deferredLightingStateSources,
            .externalStateSourceCount = deferredLightingStateSourceCount,
        },
        Core::GpuNativePacketRecordDesc{
            .packet = deferredCompositePacket,
            .externalStateSources = deferredCompositeStateSources,
            .externalStateSourceCount = deferredCompositeStateSourceCount,
        },
    };
    const Core::GpuNativePacketRecorder deferredRecorder(device);
    const bool deferredPacketsRecorded =
        deferredLightingStateSourcesReady
        && deferredCompositeStateSourcesReady
        && m_deferredLightingTaskGraphValid
        && m_deferredLightingTask.valid()
        && m_deferredCompositeTask.valid()
        && m_deferredLightingAvboitCompletion.valid()
        && deferredLightingDependentCompletion.valid()
        && deferredLightingPacket.valid()
        && deferredCompositePacket.valid()
        && m_deferredLightingCompiledGraph.packetCount() == LengthOf(deferredRecordDescs)
        && m_deferredLightingCompiledGraph.packetIdAt(0u) == deferredLightingPacket
        && m_deferredLightingCompiledGraph.packetIdAt(1u) == deferredCompositePacket
        && deferredRecorder.recordPacketsInCompileOrder(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            deferredRecordDescs,
            LengthOf(deferredRecordDescs),
            m_deferredLightingRecordedGraph
        )
    ;
    if(!deferredPacketsRecorded){
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
        deferredLightingTimingTicket.discard();
        deferredCompositeTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned deferred lighting/composite chain"));
        discardRenderPackets();
        return;
    }
    const Core::CommandListResourceStateHandoff* const deferredLightingFinalStateSeed =
        m_deferredLightingRecordedGraph.packetFinalStateSeed(deferredLightingPacket)
    ;
    const Core::CommandListResourceStateHandoff* const deferredCompositeFinalStateSeed =
        m_deferredLightingRecordedGraph.packetFinalStateSeed(deferredCompositePacket)
    ;
    if(!deferredLightingFinalStateSeed || !deferredCompositeFinalStateSeed){
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
        deferredLightingTimingTicket.discard();
        deferredCompositeTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred lighting/composite graph did not retain final state"));
        discardRenderPackets();
        return;
    }

    Core::GpuExternalPacketStateSource deferredPresentStateSources[2] = {};
    usize deferredPresentStateSourceCount = 0u;
    const bool deferredPresentStateSourcesReady =
        appendDeclaredStateSource(
            deferredPresentStateSources,
            LengthOf(deferredPresentStateSources),
            deferredPresentStateSourceCount,
            graphicsPrefixStateSeed
        )
        && appendDeclaredStateSource(
            deferredPresentStateSources,
            LengthOf(deferredPresentStateSources),
            deferredPresentStateSourceCount,
            deferredCompositeFinalStateSeed
        )
    ;
    Core::GpuNativePacketRecorder deferredPresentRecorder(device);
    const Core::GpuNativePacketRecordDesc deferredPresentRecordDesc{
        .packet = deferredPresentPacket,
        .externalStateSources = deferredPresentStateSources,
        .externalStateSourceCount = deferredPresentStateSourceCount,
    };
    const bool deferredPresentRecorded =
        deferredPresentStateSourcesReady
        && m_deferredPresentTaskGraphValid
        && m_deferredPresentTask.valid()
        && m_deferredPresentCompositeCompletion.valid()
        && (!laggedAsyncLightingSchedule || m_deferredPresentSurfelGiCompletion.valid())
        && deferredPresentPacket.valid()
        && deferredPresentRecorder.recordPacket(
            m_deferredPresentTaskGraph,
            m_deferredPresentCompiledGraph,
            deferredPresentRecordDesc,
            m_deferredPresentRecordedGraph
        )
    ;
    if(!deferredPresentRecorded){
        m_deferredPresentSubmissionTransaction.discardUnaccepted(
            m_deferredPresentTaskGraph,
            m_deferredPresentCompiledGraph
        );
        deferredPresentTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned deferred present"));
        discardRenderPackets();
        return;
    }


    const auto submitFrameRecoveryPacket = [&](const Core::QueueSubmissionToken* asyncWaitToken) -> bool {
        // Retire the accepted frame scope after a rejected packet and, when applicable, join accepted async work
        // without queue-family ownership repair.
        if(device.isDeviceLost()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery packet skipped because the graphics device is lost"));
            frameTimingTransaction.discard();
            return false;
        }
        NWB_ASSERT(!asyncWaitToken || asyncWaitToken->valid());

        const bool retireTiming = frameTimingTransaction.needsRetirement();
        if(retireTiming)
            frameTimingTransaction.prepareForRecovery();
        buildFrameRecoveryTaskGraph(frameTimingTransaction, retireTiming, asyncWaitToken != nullptr);
        const Core::GpuSubmissionPacketId recoveryPacket = m_frameRecoveryCompiledGraph.packetForTask(
            m_frameRecoveryTask
        );
        const Core::GpuPhysicalQueueInfo* const recoveryQueue = recoveryPacket.valid()
            ? m_frameRecoveryCompiledGraph.queueInfo(m_frameRecoveryCompiledGraph.packet(recoveryPacket).queue)
            : nullptr
        ;
        const auto discardFrameRecovery = [&](){
            if(m_frameRecoveryTaskGraphValid){
                m_frameRecoverySubmissionTransaction.discardUnaccepted(
                    m_frameRecoveryTaskGraph,
                    m_frameRecoveryCompiledGraph
                );
            }
            else{
                frameTimingTransaction.discard();
            }
        };
        if(
            !m_frameRecoveryTaskGraphValid
            || !m_frameRecoveryTask.valid()
            || !recoveryPacket.valid()
            || !recoveryQueue
            || recoveryQueue->queueClass != Core::CommandQueue::Graphics
            || (asyncWaitToken && !m_frameRecoveryAsyncCompletion.valid())
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery graph was unavailable"));
            discardFrameRecovery();
            return false;
        }

        const Core::GpuNativePacketRecorder recorder(device);
        const bool recoveryRecorded = recorder.recordPacket(
            m_frameRecoveryTaskGraph,
            m_frameRecoveryCompiledGraph,
            Core::GpuNativePacketRecordDesc{
                .packet = recoveryPacket,
            },
            m_frameRecoveryRecordedGraph
        );
        if(!recoveryRecorded){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to record frame recovery packet"));
            discardFrameRecovery();
            return false;
        }

        Core::GpuTaskGraphExternalCompletionToken recoveryCompletionToken;
        const Core::GpuTaskGraphExternalCompletionToken* recoveryCompletionTokens = nullptr;
        usize recoveryCompletionCount = 0u;
        if(asyncWaitToken){
            recoveryCompletionToken = Core::GpuTaskGraphExternalCompletionToken{
                .completion = m_frameRecoveryAsyncCompletion,
                .token = *asyncWaitToken,
            };
            recoveryCompletionTokens = &recoveryCompletionToken;
            recoveryCompletionCount = 1u;
        }
        Core::Alloc::ScratchArena recoveryScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter submitter(device);
        const bool recoveryAccepted = submitter.submitPacket(
            m_frameRecoveryTaskGraph,
            m_frameRecoveryCompiledGraph,
            m_frameRecoveryRecordedGraph,
            recoveryPacket,
            recoveryCompletionTokens,
            recoveryCompletionCount,
            m_frameRecoverySubmissionTransaction,
            recoveryScratchArena
        );
        if(!recoveryAccepted){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery submission was rejected"));
            discardFrameRecovery();
            return false;
        }
        return true;
    };
    Core::QueueSubmissionToken prefixSubmissionToken;
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
    const auto restoreUnacceptedShadowEffectsCpuState = [&](){
        // Compile-order submission can retain either successor. Keep every accepted CPU mirror intact while
        // restoring only work that never reached a queue.
        if(hardwareShadowSupported || !softwareCausticsSubmissionToken.valid())
            restoreCausticsCpuState();
        if(!surfelGiSubmissionToken.valid())
            restoreSurfelGiCpuState();
        restoreAvboitCpuState();
    };
    const auto recoverPendingFrameSubmission = [&]() -> bool {
        const Core::QueueSubmissionToken* asyncWaitToken = nullptr;
        if(
            shadowVisibilitySubmissionToken.valid()
            && shadowVisibilitySubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &shadowVisibilitySubmissionToken;
        if(
            softwareCausticsSubmissionToken.valid()
            && softwareCausticsSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &softwareCausticsSubmissionToken;
        if(
            surfelGiSubmissionToken.valid()
            && surfelGiSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &surfelGiSubmissionToken;
        if(
            avboitDepthWarpSubmissionToken.valid()
            && avboitDepthWarpSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &avboitDepthWarpSubmissionToken;
        if(
            avboitIntegrationSubmissionToken.valid()
            && avboitIntegrationSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &avboitIntegrationSubmissionToken;
        if(
            deferredLightingSubmissionToken.valid()
            && deferredLightingSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &deferredLightingSubmissionToken;
        if(
            deferredCompositeSubmissionToken.valid()
            && deferredCompositeSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &deferredCompositeSubmissionToken;
        return (asyncWaitToken || frameTimingTransaction.needsRetirement())
            ? submitFrameRecoveryPacket(asyncWaitToken)
            : true
        ;
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
        const Core::GpuTaskGraphExternalCompletionToken avboitPrefixCompletionToken{
            .completion = m_avboitPrefixCompletion,
            .token = prefixSubmissionToken,
        };
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
        Core::Alloc::ScratchArena avboitScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter avboitSubmitter(device);
        const bool avboitPacketsAccepted =
            m_avboitTaskGraphValid
            && m_avboitPreTask.valid()
            && m_avboitPrefixCompletion.valid()
            && avboitPrePacket.valid()
            && avboitSubmitter.submitPacketsInCompileOrder(
                m_avboitTaskGraph,
                m_avboitCompiledGraph,
                m_avboitRecordedGraph,
                &avboitPrefixCompletionToken,
                1u,
                avboitTimingTickets,
                avboitTimingTicketCount,
                m_avboitSubmissionTransaction,
                avboitScratchArena
            )
        ;
        avboitPreSubmissionToken = m_avboitSubmissionTransaction.packetToken(avboitPrePacket);
        avboitDepthWarpSubmissionToken = m_avboitSubmissionTransaction.packetToken(avboitDepthWarpPacket);
        avboitExtinctionSubmissionToken = m_avboitSubmissionTransaction.packetToken(avboitExtinctionPacket);
        avboitIntegrationSubmissionToken = m_avboitSubmissionTransaction.packetToken(avboitIntegrationPacket);
        avboitAccumulationSubmissionToken = m_avboitSubmissionTransaction.packetToken(avboitAccumulationPacket);
        avboitFinalSubmissionToken = m_avboitSubmissionTransaction.packetToken(
            avboitUsesAsyncCompute ? avboitAccumulationPacket : avboitPrePacket
        );
        if(!avboitPacketsAccepted || !avboitPreSubmissionToken.valid() || !avboitFinalSubmissionToken.valid()){
            const bool avboitPreWasRejected = !avboitPreSubmissionToken.valid();
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            if(avboitPreWasRejected)
                restoreAvboitCpuState();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT packet chain was rejected"));
            if(!recoverPendingFrameSubmission())
                failFrameRenderRecovery();
            return false;
        }

        // The deferred suffix imports AVBOIT plus the selected effects producer once. Its internal lighting-to-
        // composite edge, packet states, and submission order are all compiled from task declarations.
        const Core::GpuTaskGraphExternalCompletionToken deferredCompletionTokens[] = {
            Core::GpuTaskGraphExternalCompletionToken{
                .completion = m_deferredLightingAvboitCompletion,
                .token = avboitFinalSubmissionToken,
            },
            Core::GpuTaskGraphExternalCompletionToken{
                .completion = deferredLightingDependentCompletion,
                .token = laggedAsyncLightingSchedule
                    ? m_laggedLightingHistorySubmissionToken
                    : surfelGiSubmissionToken,
            },
        };
        const Core::GpuTaskGraphPacketTimingTicket deferredTimingTickets[] = {
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = deferredLightingPacket,
                .timingTicket = &deferredLightingTimingTicket,
            },
            Core::GpuTaskGraphPacketTimingTicket{
                .packet = deferredCompositePacket,
                .timingTicket = &deferredCompositeTimingTicket,
            },
        };
        Core::Alloc::ScratchArena deferredScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter deferredSubmitter(device);
        const bool deferredPacketsAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredLightingTask.valid()
            && m_deferredCompositeTask.valid()
            && m_deferredLightingAvboitCompletion.valid()
            && deferredLightingDependentCompletion.valid()
            && deferredLightingPacket.valid()
            && deferredCompositePacket.valid()
            && m_deferredLightingCompiledGraph.packetCount() == LengthOf(deferredTimingTickets)
            && deferredSubmitter.submitPacketsInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                deferredCompletionTokens,
                LengthOf(deferredCompletionTokens),
                deferredTimingTickets,
                LengthOf(deferredTimingTickets),
                m_deferredLightingSubmissionTransaction,
                deferredScratchArena
            )
        ;
        deferredLightingSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(deferredLightingPacket);
        deferredCompositeSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(deferredCompositePacket);
        if(deferredLightingSubmissionToken.valid()){
            if(laggedAsyncLightingSchedule)
                deferredTargets.laggedLightingHistory.slotsUploaded = true;
            const bool lightingReturnStatesReady = !deferredLightingRunsOnCompute || laggedAsyncLightingSchedule || (
                m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                    *deferredLightingFinalStateSeed,
                    deferredTargets.shadowVisibility.get()
                )
                // Bootstrap uses live caustics; active lagged mode uses the producer image directly.
                && m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                    *deferredLightingFinalStateSeed,
                    deferredTargets.causticIrradiance.get()
                )
                && m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                    *deferredLightingFinalStateSeed,
                    deferredTargets.surfelIrradiance.get()
                )
            );
            if(!lightingReturnStatesReady){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                // Lost post-lighting state leaves no safe producer layout.
                failFrameRenderRecovery();
                return false;
            }
            if(laggedAsyncLightingSchedule){
                reportLaggedLightingTransition(
                    LaggedLightingReport::ActiveHistoryAccepted,
                    deferredTargets.laggedLightingHistory.generation
                );
            }
        }
        if(
            !deferredPacketsAccepted
            || !deferredLightingSubmissionToken.valid()
            || !deferredCompositeSubmissionToken.valid()
        ){
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred lighting/composite packet chain was rejected"));
            if(!recoverPendingFrameSubmission())
                failFrameRenderRecovery();
            return false;
        }
        return true;
    };

    Core::QueueSubmissionToken finalPresentationSubmissionToken;
    const auto submitDeferredPresent = [&]() -> bool {
        Core::GpuTaskGraphExternalCompletionToken presentCompletionTokens[2] = {
            Core::GpuTaskGraphExternalCompletionToken{
                .completion = m_deferredPresentCompositeCompletion,
                .token = deferredCompositeSubmissionToken,
            },
        };
        usize presentCompletionCount = 1u;
        if(laggedAsyncLightingSchedule){
            presentCompletionTokens[presentCompletionCount++] = {
                .completion = m_deferredPresentSurfelGiCompletion,
                .token = surfelGiSubmissionToken,
            };
        }
        Core::Alloc::ScratchArena presentScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter deferredPresentSubmitter(device);
        const bool deferredPresentAccepted =
            m_deferredPresentTaskGraphValid
            && m_deferredPresentTask.valid()
            && m_deferredPresentCompositeCompletion.valid()
            && (!laggedAsyncLightingSchedule || m_deferredPresentSurfelGiCompletion.valid())
            && deferredPresentSubmitter.submitPacket(
                m_deferredPresentTaskGraph,
                m_deferredPresentCompiledGraph,
                m_deferredPresentRecordedGraph,
                deferredPresentPacket,
                presentCompletionTokens,
                presentCompletionCount,
                m_deferredPresentSubmissionTransaction,
                presentScratchArena,
                &deferredPresentTimingTicket
            )
        ;
        finalPresentationSubmissionToken = deferredPresentAccepted
            ? m_deferredPresentSubmissionTransaction.packetToken(deferredPresentPacket)
            : Core::QueueSubmissionToken{}
        ;
        if(!finalPresentationSubmissionToken.valid()){
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred present submission was rejected"));
            if(!recoverPendingFrameSubmission())
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

    // The graph owns the prefix packet and publishes the completion consumed by every subsequent producer.
    {
        Core::Alloc::ScratchArena graphicsPrefixScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter graphicsPrefixSubmitter(device);
        const bool graphicsPrefixAccepted =
            m_graphicsPrefixTaskGraphValid
            && m_graphicsPrefixMeshViewSetupTask.valid()
            && m_graphicsPrefixSceneShadingSetupTask.valid()
            && m_graphicsPrefixDeferredClearTask.valid()
            && m_graphicsPrefixGbufferTask.valid()
            && m_graphicsPrefixTask.valid()
            && graphicsPrefixSubmitter.submitPacket(
                m_graphicsPrefixTaskGraph,
                m_graphicsPrefixCompiledGraph,
                m_graphicsPrefixRecordedGraph,
                graphicsPrefixPacket,
                nullptr,
                0u,
                m_graphicsPrefixSubmissionTransaction,
                graphicsPrefixScratchArena,
                &graphicsPrefixTimingTicket
            )
        ;
        prefixSubmissionToken = graphicsPrefixAccepted
            ? m_graphicsPrefixSubmissionTransaction.packetToken(graphicsPrefixPacket)
            : Core::QueueSubmissionToken{}
        ;
        if(!prefixSubmissionToken.valid()){
            discardRenderPackets();
            return;
        }
        frameTimingTransaction.confirmBeginSubmission();
        // The ordered shadow-effects graph consumes the accepted prefix token once. Software Caustics is optional,
        // and Surfel GI is the compiled successor of the selected effects producer rather than a renderer-submitted
        // external completion.
        {
            const Core::GpuTaskGraphExternalCompletionToken prefixCompletionToken{
                .completion = m_shadowVisibilityPrefixCompletion,
                .token = prefixSubmissionToken,
            };
            const Core::GpuTaskGraphPacketTimingTicket hardwareShadowEffectsTimingTickets[] = {
                Core::GpuTaskGraphPacketTimingTicket{
                    .packet = shadowVisibilityPacket,
                    .timingTicket = &shadowVisibilityTimingTicket,
                },
                Core::GpuTaskGraphPacketTimingTicket{
                    .packet = surfelGiPacket,
                    .timingTicket = &surfelGiTimingTicket,
                },
            };
            const Core::GpuTaskGraphPacketTimingTicket softwareShadowEffectsTimingTickets[] = {
                Core::GpuTaskGraphPacketTimingTicket{
                    .packet = shadowVisibilityPacket,
                    .timingTicket = &shadowVisibilityTimingTicket,
                },
                Core::GpuTaskGraphPacketTimingTicket{
                    .packet = softwareCausticsPacket,
                    .timingTicket = &softwareCausticsTimingTicket,
                },
                Core::GpuTaskGraphPacketTimingTicket{
                    .packet = surfelGiPacket,
                    .timingTicket = &surfelGiTimingTicket,
                },
            };
            const Core::GpuTaskGraphPacketTimingTicket* const shadowEffectsTimingTickets = hardwareShadowSupported
                ? hardwareShadowEffectsTimingTickets
                : softwareShadowEffectsTimingTickets
            ;
            const usize shadowEffectsTimingTicketCount = hardwareShadowSupported
                ? LengthOf(hardwareShadowEffectsTimingTickets)
                : LengthOf(softwareShadowEffectsTimingTickets)
            ;
            Core::Alloc::ScratchArena shadowEffectsScratchArena(RendererArenaScope::s_TaskGraphArena);
            const Core::GpuTaskGraphSubmitter shadowEffectsSubmitter(device);
            const bool shadowEffectsAccepted =
                m_shadowVisibilityTaskGraphValid
                && m_shadowVisibilityTask.valid()
                && m_surfelGiTask.valid()
                && m_shadowVisibilityPrefixCompletion.valid()
                && shadowVisibilityPacket.valid()
                && surfelGiPacket.valid()
                && m_shadowVisibilityCompiledGraph.packetCount() == shadowEffectsTimingTicketCount
                && m_shadowVisibilityCompiledGraph.packetIdAt(0u) == shadowVisibilityPacket
                && (hardwareShadowSupported
                    ? m_shadowVisibilityCompiledGraph.packetIdAt(1u) == surfelGiPacket
                    : (
                        m_softwareCausticsTask.valid()
                        && softwareCausticsPacket.valid()
                        && m_shadowVisibilityCompiledGraph.packetIdAt(1u) == softwareCausticsPacket
                        && m_shadowVisibilityCompiledGraph.packetIdAt(2u) == surfelGiPacket
                    )
                )
                && shadowEffectsSubmitter.submitPacketsInCompileOrder(
                    m_shadowVisibilityTaskGraph,
                    m_shadowVisibilityCompiledGraph,
                    m_shadowVisibilityRecordedGraph,
                    &prefixCompletionToken,
                    1u,
                    shadowEffectsTimingTickets,
                    shadowEffectsTimingTicketCount,
                    m_shadowVisibilitySubmissionTransaction,
                    shadowEffectsScratchArena
                )
            ;
            shadowVisibilitySubmissionToken = m_shadowVisibilitySubmissionTransaction.packetToken(shadowVisibilityPacket);
            softwareCausticsSubmissionToken = m_shadowVisibilitySubmissionTransaction.packetToken(softwareCausticsPacket);
            surfelGiSubmissionToken = m_shadowVisibilitySubmissionTransaction.packetToken(surfelGiPacket);
            if(!shadowVisibilitySubmissionToken.valid()){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                restoreShadowCpuState();
                restoreUnacceptedShadowEffectsCpuState();
                m_raytracingSystem.discardSoftShadowTemporalHistory();
                resetRejectedShadowVisibilityStateHandoffs();
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return;
            }

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
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    restoreUnacceptedShadowEffectsCpuState();
                    m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
                    if(!recoverPendingFrameSubmission())
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
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    restoreUnacceptedShadowEffectsCpuState();
                    m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
                    recoverPendingFrameSubmission();
                    // Missing compute scratch leaves no safe layout restoration.
                    failFrameRenderRecovery();
                    return;
                }
            }

            m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
            if(
                !shadowEffectsAccepted
                || (!hardwareShadowSupported && !softwareCausticsSubmissionToken.valid())
                || !surfelGiSubmissionToken.valid()
            ){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                restoreUnacceptedShadowEffectsCpuState();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned shadow effects packet chain was rejected"));
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return;
            }
            if(!m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                *surfelGiFinalStateSeed,
                deferredTargets.surfelIrradiance.get()
            )){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                // An accepted graph producer without a retained state cannot safely feed a later frame.
                failFrameRenderRecovery();
                return;
            }

            if(surfelGiRunsOnCompute){
                Core::Texture* const surfelGiComputeScratchTextures[] = {
                    deferredTargets.surfelIrradianceHalf.get(),
                };
                Core::Buffer* const surfelGiComputeScratchBuffers[] = {
                    m_rayTracingState.m_surfelPoolBuffer.get(),
                    m_rayTracingState.m_surfelCellHeadBuffer.get(),
                    m_rayTracingState.m_surfelCounterBuffer.get(),
                    m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(),
                    m_rayTracingState.m_surfelFreeListBuffer.get(),
                    m_rayTracingState.m_surfelPoolSnapshotBuffer.get(),
                    m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(),
                    m_rayTracingState.m_surfelCounterReadback.get(),
                };
                if(!m_surfelGiComputePersistentStateHandoff.buildResourceSubset(
                    *surfelGiFinalStateSeed,
                    surfelGiComputeScratchTextures,
                    LengthOf(surfelGiComputeScratchTextures),
                    surfelGiComputeScratchBuffers,
                    LengthOf(surfelGiComputeScratchBuffers)
                )){
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    if(!recoverPendingFrameSubmission())
                        failFrameRenderRecovery();
                    failFrameRenderRecovery();
                    return;
                }
            }
            if(hardwareShadowSupported){
                Core::GpuTaskGraphExternalCompletionToken hardwareCausticsCompletionTokens[2] = {
                    Core::GpuTaskGraphExternalCompletionToken{
                        .completion = m_hardwareCausticsPrefixCompletion,
                        .token = prefixSubmissionToken,
                    },
                };
                usize hardwareCausticsCompletionCount = 1u;
                if(laggedAsyncLightingSchedule){
                    hardwareCausticsCompletionTokens[hardwareCausticsCompletionCount++] = {
                        .completion = m_hardwareCausticsLaggedHistoryCompletion,
                        .token = m_laggedLightingHistorySubmissionToken,
                    };
                }
                Core::Alloc::ScratchArena hardwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
                const Core::GpuTaskGraphSubmitter hardwareCausticsSubmitter(device);
                const bool hardwareCausticsAccepted =
                    m_hardwareCausticsTaskGraphValid
                    && m_hardwareCausticsTask.valid()
                    && m_hardwareCausticsPrefixCompletion.valid()
                    && (!laggedAsyncLightingSchedule || m_hardwareCausticsLaggedHistoryCompletion.valid())
                    && hardwareCausticsSubmitter.submitPacket(
                        m_hardwareCausticsTaskGraph,
                        m_hardwareCausticsCompiledGraph,
                        m_hardwareCausticsRecordedGraph,
                        hardwareCausticsPacket,
                        hardwareCausticsCompletionTokens,
                        hardwareCausticsCompletionCount,
                        m_hardwareCausticsSubmissionTransaction,
                        hardwareCausticsScratchArena,
                        &hardwareCausticsTimingTicket
                    )
                ;
                hardwareCausticsSubmissionToken = hardwareCausticsAccepted
                    ? m_hardwareCausticsSubmissionTransaction.packetToken(hardwareCausticsPacket)
                    : Core::QueueSubmissionToken{}
                ;
                if(!hardwareCausticsSubmissionToken.valid()){
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    restoreCausticsCpuState();
                    restoreAvboitCpuState();
                    resetAbandonedFrameStateHandoffs();
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned hardware caustics submission was rejected"));
                    if(!recoverPendingFrameSubmission())
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
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    restoreAvboitCpuState();
                    if(!recoverPendingFrameSubmission())
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
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    restoreAvboitCpuState();
                    if(!recoverPendingFrameSubmission())
                        failFrameRenderRecovery();
                    failFrameRenderRecovery();
                    return;
                }
            }

        }
        if(!submitAvboitLightingAndComposite() || !submitDeferredPresent())
            return;
    }

    if(captureLaggedLightingHistory){
        // The graph task waits on the accepted final presentation token. Its declared source uses select the
        // retained producer snapshots directly; the recorder owns all current-frame state fan-in and barriers.
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
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history capture skipped because its source state was unavailable"));
            invalidateLaggedLightingHistorySubmission();
        }
        else if(
            !m_laggedLightingHistoryTaskGraphValid
            || !finalPresentationSubmissionToken.valid()
            || !m_laggedLightingHistoryTask.valid()
            || !m_laggedLightingPresentationCompletion.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history graph was unavailable; reverting to current-frame lighting"));
            invalidateLaggedLightingHistorySubmission();
        }
        else{
            const Core::GpuSubmissionPacketId historyPacket = m_laggedLightingHistoryCompiledGraph.packetForTask(
                m_laggedLightingHistoryTask
            );
            Core::GpuNativePacketRecorder recorder(device);
            const Core::GpuNativePacketRecordDesc recordDesc{
                .packet = historyPacket,
                .externalStateSources = historyCopyStateSources,
                .externalStateSourceCount = historyCopyStateSourceCount,
            };
            const bool historyCopyRecorded = historyPacket.valid()
                && recorder.recordPacket(
                    m_laggedLightingHistoryTaskGraph,
                    m_laggedLightingHistoryCompiledGraph,
                    recordDesc,
                    m_laggedLightingHistoryRecordedGraph
                )
            ;
            const Core::CommandListResourceStateHandoff* const historyCopyFinalStateSeed = historyCopyRecorded
                ? m_laggedLightingHistoryRecordedGraph.packetFinalStateSeed(historyPacket)
                : nullptr
            ;
            if(!historyCopyRecorded || !historyCopyFinalStateSeed){
                m_laggedLightingHistorySubmissionTransaction.discardUnaccepted(
                    m_laggedLightingHistoryTaskGraph,
                    m_laggedLightingHistoryCompiledGraph
                );
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record graph-owned lagged lighting-history capture; reverting to current-frame lighting"));
                invalidateLaggedLightingHistorySubmission();
            }
            else{
                Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
                const Core::GpuTaskGraphExternalCompletionToken completionToken{
                    .completion = m_laggedLightingPresentationCompletion,
                    .token = finalPresentationSubmissionToken,
                };
                const Core::GpuTaskGraphSubmitter submitter(device);
                const bool historyCopyAccepted = submitter.submitPacket(
                    m_laggedLightingHistoryTaskGraph,
                    m_laggedLightingHistoryCompiledGraph,
                    m_laggedLightingHistoryRecordedGraph,
                    historyPacket,
                    &completionToken,
                    1u,
                    m_laggedLightingHistorySubmissionTransaction,
                    scratchArena
                );
                const Core::QueueSubmissionToken historyCopySubmissionToken = historyCopyAccepted
                    ? m_laggedLightingHistorySubmissionTransaction.packetToken(historyPacket)
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
                        if(!submitFrameRecoveryPacket(&historyCopySubmissionToken))
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

