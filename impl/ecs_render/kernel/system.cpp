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
    , m_shadowPrepareTaskGraph(arena)
    , m_shadowPrepareTaskGraphAnalysis(arena)
    , m_shadowPrepareTaskGraphQueueAssignments(arena)
    , m_shadowPrepareCompiledGraph(arena)
    , m_shadowPrepareRecordedGraph(arena)
    , m_shadowPrepareSubmissionTransaction(arena)
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
    m_shadowPrepareTaskGraphValid = false;
    m_shadowPrepareTask = {};
    m_shadowPrepareTaskGraph.reset();
    m_shadowPrepareTaskGraphAnalysis.reset();
    m_shadowPrepareTaskGraphQueueAssignments.reset();
    m_shadowPrepareCompiledGraph.reset();
    m_shadowPrepareRecordedGraph.reset(m_shadowPrepareCompiledGraph);
    m_shadowPrepareSubmissionTransaction.reset(m_shadowPrepareCompiledGraph);
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredLightingTaskGraphValid = false;
    m_deferredShadowVisibilityTask = {};
    m_deferredSoftwareCausticsTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredFrameRecoveryTask = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredFrameRecoveryCompletion = {};
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
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
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredLightingTaskGraphValid = false;
    m_deferredShadowVisibilityTask = {};
    m_deferredSoftwareCausticsTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredFrameRecoveryTask = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredFrameRecoveryCompletion = {};
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
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
    // transport. Its tail is optional: a failed tail build must leave the current frame's deferred path intact.
    const bool requestsLaggedLightingHistoryCapture = laggedAsyncLightingRequested;
    // Software caustics are graph-owned. A distinct Compute family is the only route that may resolve to Compute;
    // otherwise the compiler routes the same task through Graphics without a renderer fallback topology.
    const bool shadowVisibilityExpectedCompute = dedicatedAsyncCompute;
    const bool softwareCausticsExpectedCompute = dedicatedAsyncCompute && !hardwareShadowSupported;
    const bool surfelGiExpectedCompute = dedicatedAsyncCompute;
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
    Core::GpuTimingSubmissionTicket hardwareCausticsTimingTicket(m_graphics.gpuTiming());
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
        taskGraphInput,
        deferredTargets,
        csgFrameState,
        clearAvboitTargets,
        hasTransparentRenderers,
        shadowVisibilityPrepared,
        hasOpaqueCsgFrameWork,
        meshViewAspectRatio,
        framebuffer,
        shadowVisibilityExpectedCompute,
        surfelGiExpectedCompute,
        frameTimingTransaction,
        asyncPrefixTiming,
        graphicsPrefixTimingTicket,
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
        deferredLightingTimingTicket,
        deferredCompositeTimingTicket,
        deferredPresentTimingTicket,
        requestsLaggedLightingHistoryCapture
    );
    if(requestsLaggedLightingHistoryCapture && !m_deferredLightingTaskGraphValid){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph build with optional lagged lighting-history capture failed; retrying without the tail"));
        buildDeferredLightingTaskGraph(
            taskGraphInput,
            deferredTargets,
            csgFrameState,
            clearAvboitTargets,
            hasTransparentRenderers,
            shadowVisibilityPrepared,
            hasOpaqueCsgFrameWork,
            meshViewAspectRatio,
            framebuffer,
            shadowVisibilityExpectedCompute,
            surfelGiExpectedCompute,
            frameTimingTransaction,
            asyncPrefixTiming,
            graphicsPrefixTimingTicket,
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
            deferredLightingTimingTicket,
            deferredCompositeTimingTicket,
            deferredPresentTimingTicket,
            false
        );
    }
    const bool captureLaggedLightingHistory = m_deferredLaggedLightingHistoryTask.valid();
    const Core::GpuSubmissionPacketId graphicsPrefixPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_graphicsPrefixTask
    );
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
    const Core::GpuSubmissionPacketId avboitIntegrationPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitIntegrationTask
    );
    const Core::GpuSubmissionPacketId avboitAccumulationPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredAvboitAccumulationTask
    );
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
    const Core::GpuSubmissionPacketId surfelGiPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredSurfelGiTask
    );
    const Core::GpuSubmissionPacketId deferredLightingPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredLightingTask
    );
    const Core::GpuSubmissionPacketId deferredCompositePacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredCompositeTask
    );
    const Core::GpuSubmissionPacketId deferredPresentPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredPresentTask
    );
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
    const bool surfelGiRunsOnCompute = surfelGiQueue && surfelGiQueue->queueClass == Core::CommandQueue::Compute;
    const usize graphicsPrefixPacketIndex = 0u;
    const usize deferredShadowVisibilityPacketIndex = graphicsPrefixPacketIndex + 1u;
    const usize deferredSoftwareCausticsPacketCount = hardwareShadowSupported ? 0u : 1u;
    const usize deferredSoftwareCausticsPacketIndex = deferredShadowVisibilityPacketIndex + 1u;
    const usize deferredSurfelGiPacketIndex = deferredSoftwareCausticsPacketIndex + deferredSoftwareCausticsPacketCount;
    const usize deferredHardwarePacketCount = hardwareShadowSupported ? 1u : 0u;
    const usize deferredHardwareCausticsPacketIndex = deferredSurfelGiPacketIndex + 1u;
    const usize deferredAvboitPacketCount = avboitUsesAsyncCompute ? 5u : 1u;
    const usize deferredAvboitPrePacketIndex = deferredHardwareCausticsPacketIndex + deferredHardwarePacketCount;
    const usize deferredAvboitDepthWarpPacketIndex = deferredAvboitPrePacketIndex + 1u;
    const usize deferredAvboitExtinctionPacketIndex = deferredAvboitDepthWarpPacketIndex + 1u;
    const usize deferredAvboitIntegrationPacketIndex = deferredAvboitExtinctionPacketIndex + 1u;
    const usize deferredAvboitAccumulationPacketIndex = deferredAvboitIntegrationPacketIndex + 1u;
    const usize deferredLightingPacketIndex = deferredAvboitPrePacketIndex + deferredAvboitPacketCount;
    const usize deferredCompositePacketIndex = deferredLightingPacketIndex + 1u;
    const usize deferredPresentPacketIndex = deferredCompositePacketIndex + 1u;
    const usize deferredInitialPacketCount = deferredPresentPacketIndex + 1u;
    const usize deferredLaggedLightingHistoryPacketIndex = deferredInitialPacketCount;
    const usize deferredFrameRecoveryPacketIndex = deferredLaggedLightingHistoryPacketIndex
        + (captureLaggedLightingHistory ? 1u : 0u)
    ;
    const usize deferredPacketCount = deferredFrameRecoveryPacketIndex + 1u;
    if(
        !m_deferredLightingTaskGraphValid
        || !m_graphicsPrefixMeshViewSetupTask.valid()
        || !m_graphicsPrefixSceneShadingSetupTask.valid()
        || !m_graphicsPrefixDeferredClearTask.valid()
        || !m_graphicsPrefixGbufferTask.valid()
        || !m_graphicsPrefixTask.valid()
        || !graphicsPrefixPacket.valid()
        || !graphicsPrefixQueue
        || graphicsPrefixQueue->queueClass != Core::CommandQueue::Graphics
        || !m_deferredShadowVisibilityTask.valid()
        || !shadowVisibilityPacket.valid()
        || !shadowVisibilityQueue
        || shadowVisibilityRunsOnCompute != shadowVisibilityExpectedCompute
        || (!hardwareShadowSupported && (
            !m_deferredSoftwareCausticsTask.valid()
            || !softwareCausticsPacket.valid()
            || !softwareCausticsQueue
            || softwareCausticsRunsOnCompute != softwareCausticsExpectedCompute
        ))
        || !m_deferredSurfelGiTask.valid()
        || !surfelGiPacket.valid()
        || !surfelGiQueue
        || surfelGiRunsOnCompute != surfelGiExpectedCompute
        || (hardwareShadowSupported && (
            !m_deferredHardwareCausticsTask.valid()
            || !hardwareCausticsPacket.valid()
            || !hardwareCausticsQueue
            || hardwareCausticsQueue->queueClass != Core::CommandQueue::Graphics
        ))
        || !m_deferredAvboitPreTask.valid()
        || !avboitPrePacket.valid()
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
        || !m_deferredFrameRecoveryTask.valid()
        || !m_deferredFrameRecoveryCompletion.valid()
        || (captureLaggedLightingHistory && (
            !m_deferredLaggedLightingHistoryTask.valid()
            || !deferredLaggedLightingHistoryPacket.valid()
            || !deferredLaggedLightingHistoryQueue
        ))
        || (laggedAsyncLightingSchedule && !m_deferredLightingHistoryCompletion.valid())
        || !deferredLightingPacket.valid()
        || !deferredCompositePacket.valid()
        || !deferredPresentPacket.valid()
        || !deferredFrameRecoveryPacket.valid()
        || m_deferredLightingCompiledGraph.packetCount() != deferredPacketCount
        || !deferredLightingQueue
        || !deferredCompositeQueue
        || !deferredPresentQueue
        || !deferredFrameRecoveryQueue
        || m_deferredLightingCompiledGraph.packetIdAt(graphicsPrefixPacketIndex) != graphicsPrefixPacket
        || m_deferredLightingCompiledGraph.packetIdAt(deferredShadowVisibilityPacketIndex) != shadowVisibilityPacket
        || (!hardwareShadowSupported && (
            m_deferredLightingCompiledGraph.packetIdAt(deferredSoftwareCausticsPacketIndex) != softwareCausticsPacket
        ))
        || m_deferredLightingCompiledGraph.packetIdAt(deferredSurfelGiPacketIndex) != surfelGiPacket
        || (hardwareShadowSupported && (
            m_deferredLightingCompiledGraph.packetIdAt(deferredHardwareCausticsPacketIndex) != hardwareCausticsPacket
        ))
        || m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitPrePacketIndex) != avboitPrePacket
        || (avboitUsesAsyncCompute && (
            m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitDepthWarpPacketIndex) != avboitDepthWarpPacket
            || m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitExtinctionPacketIndex) != avboitExtinctionPacket
            || m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitIntegrationPacketIndex) != avboitIntegrationPacket
            || m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitAccumulationPacketIndex) != avboitAccumulationPacket
        ))
        || m_deferredLightingCompiledGraph.packetIdAt(deferredLightingPacketIndex) != deferredLightingPacket
        || m_deferredLightingCompiledGraph.packetIdAt(deferredCompositePacketIndex) != deferredCompositePacket
        || m_deferredLightingCompiledGraph.packetIdAt(deferredPresentPacketIndex) != deferredPresentPacket
        || (captureLaggedLightingHistory && (
            m_deferredLightingCompiledGraph.packetIdAt(deferredLaggedLightingHistoryPacketIndex)
                != deferredLaggedLightingHistoryPacket
        ))
        || m_deferredLightingCompiledGraph.packetIdAt(deferredFrameRecoveryPacketIndex)
            != deferredFrameRecoveryPacket
        || (laggedAsyncLightingSchedule && deferredLightingQueue->queueClass != Core::CommandQueue::Compute)
        || (laggedAsyncLightingSchedule && deferredCompositeQueue->queueClass != Core::CommandQueue::Graphics)
        || deferredPresentQueue->queueClass != Core::CommandQueue::Graphics
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
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool deferredLightingRunsOnCompute = deferredLightingQueue->queueClass == Core::CommandQueue::Compute;
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

    // Record the ordered Prefix packet first.  Its complete snapshot remains a local recording source for the
    // declared read-only paths that intentionally omit an internal state seed to preserve Graphics/Compute overlap.
    // This never crosses a graph or a submission transaction: all consumers still receive compiler-owned packet
    // dependencies, while the filtered source gives their command lists the established common prefix state.
    const Core::GpuNativePacketRecorder deferredRecorder(device);
    const bool graphicsPrefixRecorded =
        m_deferredLightingTaskGraphValid
        && m_graphicsPrefixMeshViewSetupTask.valid()
        && m_graphicsPrefixSceneShadingSetupTask.valid()
        && m_graphicsPrefixDeferredClearTask.valid()
        && m_graphicsPrefixGbufferTask.valid()
        && m_graphicsPrefixTask.valid()
        && graphicsPrefixPacket.valid()
        && deferredRecorder.recordPacket(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            Core::GpuNativePacketRecordDesc{
                .packet = graphicsPrefixPacket,
                .serialStateSeed = shadowPrepareStateSeed,
            },
            m_deferredLightingRecordedGraph
        )
    ;
    const Core::CommandListResourceStateHandoff* const graphicsPrefixFinalStateSeed = graphicsPrefixRecorded
        ? m_deferredLightingRecordedGraph.packetFinalStateSeed(graphicsPrefixPacket)
        : nullptr
    ;
    if(!graphicsPrefixRecorded || !graphicsPrefixFinalStateSeed){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to retain the graph-owned graphics-prefix state"));
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

    // Shadow Visibility, optional Software Caustics, and Surfel GI record after Prefix in compiler order.
    Core::GpuExternalPacketStateSource surfelGiStateSources[3] = {};
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
    Core::GpuNativePacketRecordDesc deferredRecordDescs[11] = {};
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
    if(avboitUsesAsyncCompute){
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = avboitDepthWarpPacket,
        };
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = avboitExtinctionPacket,
        };
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = avboitIntegrationPacket,
        };
        deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
            .packet = avboitAccumulationPacket,
        };
    }
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
    deferredRecordDescs[deferredRecordDescCount++] = Core::GpuNativePacketRecordDesc{
        .packet = deferredPresentPacket,
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
        && m_graphicsPrefixDeferredClearTask.valid()
        && m_graphicsPrefixGbufferTask.valid()
        && m_graphicsPrefixTask.valid()
        && graphicsPrefixPacket.valid()
        && m_deferredShadowVisibilityTask.valid()
        && shadowVisibilityPacket.valid()
        && (hardwareShadowSupported || (
            m_deferredSoftwareCausticsTask.valid()
            && softwareCausticsPacket.valid()
        ))
        && m_deferredSurfelGiTask.valid()
        && surfelGiPacket.valid()
        && (!hardwareShadowSupported || (
            m_deferredHardwareCausticsTask.valid()
            && hardwareCausticsPacket.valid()
        ))
        && m_deferredAvboitPreTask.valid()
        && avboitPrePacket.valid()
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
        && m_deferredFrameRecoveryTask.valid()
        && m_deferredFrameRecoveryCompletion.valid()
        && (!captureLaggedLightingHistory || (
            m_deferredLaggedLightingHistoryTask.valid()
            && deferredLaggedLightingHistoryPacket.valid()
        ))
        && (!laggedAsyncLightingSchedule || m_deferredLightingHistoryCompletion.valid())
        && deferredLightingPacket.valid()
        && deferredCompositePacket.valid()
        && deferredPresentPacket.valid()
        && deferredFrameRecoveryPacket.valid()
        && deferredFrameRecoveryQueue
        && m_deferredLightingCompiledGraph.packetCount() == deferredPacketCount
        && deferredRecordDescCount == deferredInitialPacketCount - 1u
        && m_deferredLightingCompiledGraph.packetIdAt(graphicsPrefixPacketIndex) == graphicsPrefixPacket
        && m_deferredLightingCompiledGraph.packetIdAt(deferredShadowVisibilityPacketIndex) == shadowVisibilityPacket
        && (hardwareShadowSupported || (
            m_deferredLightingCompiledGraph.packetIdAt(deferredSoftwareCausticsPacketIndex) == softwareCausticsPacket
        ))
        && m_deferredLightingCompiledGraph.packetIdAt(deferredSurfelGiPacketIndex) == surfelGiPacket
        && (!hardwareShadowSupported || (
            m_deferredLightingCompiledGraph.packetIdAt(deferredHardwareCausticsPacketIndex) == hardwareCausticsPacket
        ))
        && m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitPrePacketIndex) == avboitPrePacket
        && (!avboitUsesAsyncCompute || (
            m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitDepthWarpPacketIndex) == avboitDepthWarpPacket
            && m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitExtinctionPacketIndex) == avboitExtinctionPacket
            && m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitIntegrationPacketIndex) == avboitIntegrationPacket
            && m_deferredLightingCompiledGraph.packetIdAt(deferredAvboitAccumulationPacketIndex) == avboitAccumulationPacket
        ))
        && m_deferredLightingCompiledGraph.packetIdAt(deferredLightingPacketIndex) == deferredLightingPacket
        && m_deferredLightingCompiledGraph.packetIdAt(deferredCompositePacketIndex) == deferredCompositePacket
        && m_deferredLightingCompiledGraph.packetIdAt(deferredPresentPacketIndex) == deferredPresentPacket
        && (!captureLaggedLightingHistory || (
            m_deferredLightingCompiledGraph.packetIdAt(deferredLaggedLightingHistoryPacketIndex)
                == deferredLaggedLightingHistoryPacket
        ))
        && m_deferredLightingCompiledGraph.packetIdAt(deferredFrameRecoveryPacketIndex)
            == deferredFrameRecoveryPacket
    ;
    if(deferredPacketsRecorded){
        deferredPacketsRecorded = deferredRecorder.recordPacketRangeInCompileOrder(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            graphicsPrefixPacketIndex + 1u,
            deferredRecordDescs,
            deferredRecordDescCount,
            m_deferredLightingRecordedGraph
        );
    }
    if(!deferredPacketsRecorded){
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
        graphicsPrefixTimingTicket.discard();
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
        graphicsPrefixTimingTicket.discard();
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
    const auto submitFrameRecoveryPacket = [&](const Core::QueueSubmissionToken* const asyncWaitToken) -> bool {
        // Retire the accepted frame scope after a rejected packet and join the latest accepted non-Graphics producer.
        // The Prefix Graphics token is the durable fallback completion; same-queue waits collapse to queue ordering.
        if(device.isDeviceLost()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery packet skipped because the graphics device is lost"));
            m_deferredFrameRecoveryArmed = false;
            m_deferredFrameRecoveryRetiresTiming = false;
            frameTimingTransaction.discard();
            return false;
        }
        NWB_ASSERT(!asyncWaitToken || asyncWaitToken->valid());
        const Core::QueueSubmissionToken& recoveryPredecessorToken = asyncWaitToken
            ? *asyncWaitToken
            : prefixSubmissionToken
        ;
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
            || !m_deferredFrameRecoveryCompletion.valid()
            || !deferredFrameRecoveryPacket.valid()
            || !deferredFrameRecoveryQueue
            || deferredFrameRecoveryQueue->queueClass != Core::CommandQueue::Graphics
            || !recoveryPredecessorToken.valid()
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred frame recovery packet was unavailable"));
            discardFrameRecovery();
            return false;
        }

        const bool recoveryRecorded = deferredRecorder.recordPacket(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            Core::GpuNativePacketRecordDesc{
                .packet = deferredFrameRecoveryPacket,
            },
            m_deferredLightingRecordedGraph
        );
        if(!recoveryRecorded){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to late-record deferred frame recovery packet"));
            discardFrameRecovery();
            return false;
        }

        const Core::GpuTaskGraphExternalCompletionToken recoveryCompletionToken{
            .completion = m_deferredFrameRecoveryCompletion,
            .token = recoveryPredecessorToken,
        };
        Core::Alloc::ScratchArena recoveryScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter submitter(device);
        const bool recoveryAccepted = submitter.submitPacket(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            m_deferredLightingRecordedGraph,
            deferredFrameRecoveryPacket,
            &recoveryCompletionToken,
            1u,
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
        const usize avboitPacketCount = avboitUsesAsyncCompute ? LengthOf(avboitTimingTickets) : 1u;
        const bool avboitPacketsAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredAvboitPreTask.valid()
            && avboitPrePacket.valid()
            && m_deferredLightingCompiledGraph.packetCount() == deferredPacketCount
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
                deferredAvboitPrePacketIndex,
                avboitPacketCount,
                nullptr,
                0u,
                avboitTimingTickets,
                avboitPacketCount,
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
        avboitFinalSubmissionToken = avboitUsesAsyncCompute
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
        const bool deferredLightingAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredShadowVisibilityTask.valid()
            && shadowVisibilitySubmissionToken.valid()
            && (hardwareShadowSupported || (
                m_deferredSoftwareCausticsTask.valid()
                && softwareCausticsSubmissionToken.valid()
            ))
            && m_deferredSurfelGiTask.valid()
            && surfelGiSubmissionToken.valid()
            && (!hardwareShadowSupported || (
                m_deferredHardwareCausticsTask.valid()
                && hardwareCausticsSubmissionToken.valid()
            ))
            && m_deferredLightingTask.valid()
            && m_deferredCompositeTask.valid()
            && (!laggedAsyncLightingSchedule || m_deferredLightingHistoryCompletion.valid())
            && deferredLightingPacket.valid()
            && deferredCompositePacket.valid()
            && m_deferredLightingCompiledGraph.packetCount() == deferredPacketCount
            && deferredSubmitter.submitPacket(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                deferredLightingPacket,
                deferredLightingCompletionTokens,
                deferredLightingCompletionCount,
                m_deferredLightingSubmissionTransaction,
                deferredScratchArena,
                &deferredLightingTimingTicket
            )
        ;
        deferredLightingSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(deferredLightingPacket);
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
                const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
                discardTimingTickets();
                if(!recovered)
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
        const bool deferredCompositeAccepted =
            deferredLightingAccepted
            && m_deferredCompositeTask.valid()
            && deferredCompositePacket.valid()
            && deferredSubmitter.submitPacket(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                deferredCompositePacket,
                nullptr,
                0u,
                m_deferredLightingSubmissionTransaction,
                deferredScratchArena,
                &deferredCompositeTimingTicket
            )
        ;
        deferredCompositeSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(deferredCompositePacket);
        if(
            !deferredLightingAccepted
            || !deferredCompositeAccepted
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
        const bool deferredPresentAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredPresentTask.valid()
            && m_deferredSurfelGiTask.valid()
            && surfelGiSubmissionToken.valid()
            && deferredCompositeSubmissionToken.valid()
            && deferredPresentPacket.valid()
            && deferredPresentSubmitter.submitPacket(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                deferredPresentPacket,
                nullptr,
                0u,
                m_deferredLightingSubmissionTransaction,
                presentScratchArena,
                &deferredPresentTimingTicket
            )
        ;
        finalPresentationSubmissionToken = deferredPresentAccepted
            ? m_deferredLightingSubmissionTransaction.packetToken(deferredPresentPacket)
            : Core::QueueSubmissionToken{}
        ;
        if(!finalPresentationSubmissionToken.valid()){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred present submission was rejected"));
            if(!recovered)
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
        const Core::QueueSubmissionToken& effectsSubmissionToken = hardwareShadowSupported
            ? shadowVisibilitySubmissionToken
            : softwareCausticsSubmissionToken
        ;
        Core::Alloc::ScratchArena surfelGiScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter surfelGiSubmitter(device);
        const bool surfelGiAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredSurfelGiTask.valid()
            && effectsSubmissionToken.valid()
            && surfelGiPacket.valid()
            && m_deferredLightingCompiledGraph.packetCount() == deferredPacketCount
            && surfelGiSubmitter.submitPacket(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                surfelGiPacket,
                nullptr,
                0u,
                m_deferredLightingSubmissionTransaction,
                surfelGiScratchArena,
                &surfelGiTimingTicket
            )
        ;
        surfelGiSubmissionToken = m_deferredLightingSubmissionTransaction.packetToken(surfelGiPacket);
        if(!surfelGiAccepted || !surfelGiSubmissionToken.valid()){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            restoreUnacceptedShadowEffectsCpuState();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred surfel-GI packet was rejected"));
            if(!recovered)
                failFrameRenderRecovery();
            return false;
        }
        if(!m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
            *surfelGiFinalStateSeed,
            deferredTargets.surfelIrradiance.get()
        )){
            const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
            discardTimingTickets();
            if(!recovered)
                failFrameRenderRecovery();
            // An accepted graph producer without a retained state cannot safely feed a later frame.
            failFrameRenderRecovery();
            return false;
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
                const bool recovered = recoverPendingFrameThenDiscardUnaccepted();
                discardTimingTickets();
                if(!recovered)
                    failFrameRenderRecovery();
                failFrameRenderRecovery();
                return false;
            }
        }
        return true;
    };

    // Prefix is the first packet in the shared transaction. Every following task resolves its compiler-owned
    // dependency token rather than crossing a renderer-side completion bridge.
    {
        Core::Alloc::ScratchArena graphicsPrefixScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter graphicsPrefixSubmitter(device);
        const bool graphicsPrefixAccepted =
            m_deferredLightingTaskGraphValid
            && m_graphicsPrefixMeshViewSetupTask.valid()
            && m_graphicsPrefixSceneShadingSetupTask.valid()
            && m_graphicsPrefixDeferredClearTask.valid()
            && m_graphicsPrefixGbufferTask.valid()
            && m_graphicsPrefixTask.valid()
            && graphicsPrefixPacket.valid()
            && graphicsPrefixSubmitter.submitPacket(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                graphicsPrefixPacket,
                nullptr,
                0u,
                m_deferredLightingSubmissionTransaction,
                graphicsPrefixScratchArena,
                &graphicsPrefixTimingTicket
            )
        ;
        prefixSubmissionToken = graphicsPrefixAccepted
            ? m_deferredLightingSubmissionTransaction.packetToken(graphicsPrefixPacket)
            : Core::QueueSubmissionToken{}
        ;
        if(!prefixSubmissionToken.valid()){
            discardRenderPackets();
            return;
        }
        frameTimingTransaction.confirmBeginSubmission();
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
        const usize shadowEffectsPacketCount = hardwareShadowSupported ? 1u : LengthOf(shadowEffectsTimingTickets);
        const bool shadowEffectsSubmitted =
            m_deferredLightingTaskGraphValid
            && m_deferredShadowVisibilityTask.valid()
            && shadowVisibilityPacket.valid()
            && m_deferredLightingCompiledGraph.packetCount() == deferredPacketCount
            && (hardwareShadowSupported || (
                m_deferredSoftwareCausticsTask.valid()
                && softwareCausticsPacket.valid()
            ))
            && shadowEffectsSubmitter.submitPacketRangeInCompileOrder(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                deferredShadowVisibilityPacketIndex,
                shadowEffectsPacketCount,
                nullptr,
                0u,
                shadowEffectsTimingTickets,
                shadowEffectsPacketCount,
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
                static_cast<void>(recovered);
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
            const bool hardwareCausticsAccepted =
                m_deferredLightingTaskGraphValid
                && m_deferredHardwareCausticsTask.valid()
                && (!laggedAsyncLightingSchedule || m_deferredLightingHistoryCompletion.valid())
                && hardwareCausticsPacket.valid()
                && m_deferredLightingCompiledGraph.packetCount() == deferredPacketCount
                && hardwareCausticsSubmitter.submitPacket(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph,
                    hardwareCausticsPacket,
                    hardwareCausticsCompletionTokens,
                    hardwareCausticsCompletionCount,
                    m_deferredLightingSubmissionTransaction,
                    hardwareCausticsScratchArena,
                    &hardwareCausticsTimingTicket
                )
            ;
            hardwareCausticsSubmissionToken = hardwareCausticsAccepted
                ? m_deferredLightingSubmissionTransaction.packetToken(hardwareCausticsPacket)
                : Core::QueueSubmissionToken{}
            ;
            const bool hardwareCausticsStateReady =
                hardwareCausticsSubmissionToken.valid()
                && (
                    !laggedAsyncLightingSchedule
                    || (
                        hardwareCausticsFinalStateSeed
                        && m_causticIrradianceLightingStateHandoff.buildTextureSubset(
                            *hardwareCausticsFinalStateSeed,
                            deferredTargets.causticIrradiance.get()
                        )
                    )
                )
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
            || m_deferredLightingCompiledGraph.packetCount() != deferredPacketCount
            || m_deferredLightingCompiledGraph.packetIdAt(deferredLaggedLightingHistoryPacketIndex)
                != deferredLaggedLightingHistoryPacket
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
            const Core::GpuNativePacketRecordDesc recordDesc{
                .packet = deferredLaggedLightingHistoryPacket,
                .externalStateSources = historyCopyStateSources,
                .externalStateSourceCount = historyCopyStateSourceCount,
            };
            const bool historyCopyRecorded = recorder.recordPacket(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    recordDesc,
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
                const bool historyCopyAccepted = submitter.submitPacket(
                    m_deferredLightingTaskGraph,
                    m_deferredLightingCompiledGraph,
                    m_deferredLightingRecordedGraph,
                    deferredLaggedLightingHistoryPacket,
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

