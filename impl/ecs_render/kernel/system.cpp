// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/ecs_scene/components.h>


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
    , m_meshState(arena)
    , m_materialState(arena)
    , m_rayTracingState(arena)
    , m_shadowPrepareStateHandoff(arena)
    , m_gbufferStateHandoff(arena)
    , m_postGbufferNormalizedStateHandoff(arena)
    , m_shadowVisibilityStateHandoff(arena)
    , m_causticsSurfelGiStateHandoff(arena)
    , m_postGbufferFanInStateHandoff(arena)
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

bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    m_raytracingSystem.logCapabilityOnce();
    if(width == 0 || height == 0)
        return true;

    if(!ensureFrameCommandLists())
        return false;

    if(!prepareGpuTimingScopes())
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU timing scope preparation failed; timing samples may be skipped"));

    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;
    bool targetsReady = deferredTargets.valid() && deferredTargets.width == width && deferredTargets.height == height;
    if(!targetsReady)
        targetsReady = m_deferredSystem.createDeferredFrameTargets(width, height);
    if(!targetsReady)
        return false;

    if(Core::Framebuffer* presentationFramebuffer = m_graphics.getCurrentFramebuffer()){
        if(!m_deferredSystem.createDeferredCompositePipeline(presentationFramebuffer))
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
    m_shadowPrepareStateHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_causticsSurfelGiStateHandoff.reset();
    m_postGbufferFanInStateHandoff.reset();
    m_gbufferCommandList.reset();
    m_postGbufferNormalizeCommandList.reset();
    m_shadowVisibilityCommandList.reset();
    m_causticsSurfelGiCommandList.reset();
    m_postGbufferCommandList.reset();
    m_shadowPrepareCommandList.reset();
    // The descriptor-buffer TLAS descriptor owns a retained acceleration-structure handle until its in-flight-frame
    // quarantine matures. Retire it before RendererRayTracingState releases the current TLAS so resource invalidation
    // cannot strand a descriptor-buffer block (or its retained AS) until device shutdown.
    if(m_rayTracingState.m_tlasHeapHandle.valid()){
        auto& device = *m_graphics.getDevice();
        device.getDescriptorHeap().free(m_rayTracingState.m_tlasHeapHandle);
    }
    // The persistent caustic-emission and trace material-context heap descriptors retain their backing buffers just
    // like the TLAS descriptor. Retire them while the device heap is still live, before RendererRayTracingState
    // releases those buffers below.
    m_raytracingSystem.releaseCausticEmissionTargetHeapHandle();
    m_raytracingSystem.releaseRayTraceMaterialContextHeapHandles();
    m_raytracingSystem.releaseSwBvhScratchHeapHandles();
    m_raytracingSystem.releaseSurfelGiHeapHandles();
    // Deferred target generations own ordinary image/sampler heap slots. Release those handles while both the target
    // resources and the device heap are still live; RendererDeferredState then drops the remaining resource handles.
    m_deferredSystem.resetDeferredFrameTargets();
    // Mesh geometry heap descriptors retain their backing buffers independently of the MeshResources cache. Retire
    // them before clearing that cache so descriptor slots are eventually recycled after the in-flight quarantine.
    m_meshSystem.releaseAllMeshGeometryHeapHandles();
    m_meshSystem.releaseMeshFrameHeapHandles();
    m_meshState.invalidateResources();
    m_materialState.invalidateResources();
    m_drawState.invalidateResources();
    // CSG's persistent clip descriptors retain their receiver/cutter buffers, so retire them before the CSG state
    // releases those buffers and its slot cbuffer.
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

bool RendererSystem::ensureFrameCommandLists(){
    auto& device = *m_graphics.getDevice();

    if(!m_gbufferCommandList){
        m_gbufferCommandList = device.createCommandList();
        if(!m_gbufferCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create G-buffer command list"));
            return false;
        }
    }

    if(!m_postGbufferNormalizeCommandList){
        m_postGbufferNormalizeCommandList = device.createCommandList();
        if(!m_postGbufferNormalizeCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create post-G-buffer normalization command list"));
            return false;
        }
    }

    if(!m_shadowVisibilityCommandList){
        m_shadowVisibilityCommandList = device.createCommandList();
        if(!m_shadowVisibilityCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow-visibility command list"));
            return false;
        }
    }

    if(!m_causticsSurfelGiCommandList){
        m_causticsSurfelGiCommandList = device.createCommandList();
        if(!m_causticsSurfelGiCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustics/surfel-GI command list"));
            return false;
        }
    }

    if(!m_postGbufferCommandList){
        m_postGbufferCommandList = device.createCommandList();
        if(!m_postGbufferCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create post-G-buffer command list"));
            return false;
        }
    }

    if(!m_shadowPrepareCommandList){
        m_shadowPrepareCommandList = device.createCommandList();
        if(!m_shadowPrepareCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow preparation command list"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::prepareGpuTimingScopes(){
    auto& device = *m_graphics.getDevice();

    struct ScopeReservation{
        const Core::GpuTimingScopeDefinition* scope;
        u32 queryCount;
    };
    const ScopeReservation scopeReservations[] = {
        { &RendererGpuTimingScope::s_MeshDispatch, 128u },
        { &RendererGpuTimingScope::s_Raster, 128u },
        { &RendererGpuTimingScope::s_Frame, 2u },
        { &RendererGpuTimingScope::s_DeferredClear, 2u },
        { &RendererGpuTimingScope::s_ShadowVisibility, 2u },
        { &RendererGpuTimingScope::s_SwBvhSort, 4u },
        { &RendererGpuTimingScope::s_CausticPhotons, 2u },
        { &RendererGpuTimingScope::s_CausticResolve, 2u },
        { &RendererGpuTimingScope::s_DeferredLighting, 2u },
        { &RendererGpuTimingScope::s_DeferredComposite, 2u },
        { &RendererGpuTimingScope::s_MaterialUpload, 2u },
        { &RendererGpuTimingScope::s_OpaqueRegular, 2u },
        { &RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, 2u },
        { &RendererGpuTimingScope::s_OpaqueCsg, 2u },
        { &RendererGpuTimingScope::s_CsgUpload, 2u },
        { &RendererGpuTimingScope::s_CsgSampleStateUpload, 2u },
        { &RendererGpuTimingScope::s_CsgIntervalClear, 4u },
        { &RendererGpuTimingScope::s_CsgIntervalPeel, 2u },
        { &RendererGpuTimingScope::s_CsgReceiverSpanBuild, 2u },
        { &RendererGpuTimingScope::s_CsgIntervalCombine, 2u },
        { &RendererGpuTimingScope::s_CsgCapFill, 2u },
        { &RendererGpuTimingScope::s_TransparentCsgIntervals, 2u },
        { &RendererGpuTimingScope::s_AvboitClear, 2u },
        { &RendererGpuTimingScope::s_AvboitOccupancy, 2u },
        { &RendererGpuTimingScope::s_AvboitDepthWarp, 2u },
        { &RendererGpuTimingScope::s_AvboitExtinction, 2u },
        { &RendererGpuTimingScope::s_AvboitIntegration, 2u },
        { &RendererGpuTimingScope::s_AvboitAccumulate, 2u },
        { &RendererGpuTimingScope::s_SurfelSpawn, 2u },
        { &RendererGpuTimingScope::s_SurfelAgeFree, 2u },
        { &RendererGpuTimingScope::s_SurfelHashBuild, 2u },
        { &RendererGpuTimingScope::s_SurfelTrace, 2u },
        { &RendererGpuTimingScope::s_SurfelResolve, 2u },
        { &RendererGpuTimingScope::s_SurfelUpsample, 2u },
    };

    for(const ScopeReservation& reservation : scopeReservations){
        if(!m_graphics.gpuTiming().prepareScopeQueries(reservation.scope->identity, &device, reservation.queryCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare GPU timing scope '{}'"), reservation.scope->identity.c_str());
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

    // Transparent preparation can grow shared instance and material buffers, which invalidates all mesh
    // heap registrations. Refresh opaque descriptors after the final possible grow so render only consumes prepared
    // resources.
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

    NWB_ASSERT(m_gbufferCommandList);
    NWB_ASSERT(m_postGbufferNormalizeCommandList);
    NWB_ASSERT(m_shadowVisibilityCommandList);
    NWB_ASSERT(m_causticsSurfelGiCommandList);
    NWB_ASSERT(m_postGbufferCommandList);
    NWB_ASSERT(m_shadowPrepareCommandList);

    auto& device = *m_graphics.getDevice();

    // The preparation list owns the first upload of this target generation's descriptor-slot payload and may also
    // contain the one-shot clear for a newly-created surfel pool. Neither CPU marker may commit before submission.
    const bool deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded;
    m_raytracingSystem.discardSurfelResourceInitialization();
    Core::GpuTimingSubmissionTicket shadowPrepareTimingTicket(m_graphics.gpuTiming());
    const auto discardShadowPrepare = [&](){
        shadowPrepareTimingTicket.discard();
        m_shadowPrepareStateHandoff.reset();
        m_preparedShadowVisibilityReady = false;
        deferredTargets.bindless.slotsUploaded = deferredBindlessSlotsWereUploaded;
        m_raytracingSystem.discardSurfelResourceInitialization();
    };
    bool shadowPrepareRecorded = false;
    const Core::Graphics::JobHandle shadowPrepareJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        &shadowPrepareRecorded,
        &shadowPrepareTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(shadowPrepareTimingTicket);
        shadowPrepareRecorded = recordShadowPrepareCommandList(deferredTargets);
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

    Core::CommandList* shadowPrepareCommandLists[] = { m_shadowPrepareCommandList.get() };
    if(!shadowPrepareTimingTicket.submit(device, shadowPrepareCommandLists, 1u)){
        discardShadowPrepare();
        return false;
    }
    m_raytracingSystem.finalizeSurfelResourceInitialization();

    return true;
}

bool RendererSystem::recordShadowPrepareCommandList(DeferredFrameTargets& deferredTargets){
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);

    m_shadowPrepareCommandList->open();
    // The software-shadow trace selects its G-buffer heap descriptors through the target-generation slot cbuffer. It
    // runs before deferred lighting, so make the cbuffer resident on the ordered shadow-preparation command list first.
    const bool deferredBindlessResourcesUploaded = m_deferredSystem.uploadDeferredBindlessFrameResources(
        *m_shadowPrepareCommandList,
        deferredTargets
    );
    // Surfel GI resources are prepared inside prepareShadowVisibilityResources, after the ray-tracing scene
    // structures are resident, so the producer can run on the same frame without startup latency.
    const bool shadowResourcesPrepared = deferredBindlessResourcesUploaded
        && m_raytracingSystem.prepareShadowVisibilityResources(
            *m_shadowPrepareCommandList,
            deferredTargets,
            scratchArena,
            m_preparedShadowVisibilityReady
        )
    ;
    // Scene/material gathers can replace a capacity-grown buffer and therefore its heap slot. Upload the completed
    // five-slot indirection only after the full shadow/GI/caustic preparation path has settled on this frame's
    // resource generations, but before the command list that will dispatch their heap-selected passes is submitted.
    const bool traceMaterialContextUploaded = shadowResourcesPrepared
        && m_raytracingSystem.uploadRayTraceMaterialContextSlots(*m_shadowPrepareCommandList)
    ;
    // This submission precedes render on Graphics. Preserve its final resource state so the first render
    // command buffer never falls back to Unknown/UNDEFINED for preparation outputs.
    m_shadowPrepareCommandList->close(&m_shadowPrepareStateHandoff);
    return traceMaterialContextUploaded && m_shadowPrepareStateHandoff.valid();
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return;

    if(!m_deferredState.m_targets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    NWB_ASSERT(m_shadowPrepareStateHandoff.valid());
    if(!m_shadowPrepareStateHandoff.valid())
        return;

    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto& device = *m_graphics.getDevice();
    Core::CommandList* gbufferCommandList = m_gbufferCommandList.get();
    Core::CommandList* postGbufferNormalizeCommandList = m_postGbufferNormalizeCommandList.get();
    Core::CommandList* shadowVisibilityCommandList = m_shadowVisibilityCommandList.get();
    Core::CommandList* causticsSurfelGiCommandList = m_causticsSurfelGiCommandList.get();
    Core::CommandList* postGbufferCommandList = m_postGbufferCommandList.get();
    NWB_ASSERT(gbufferCommandList);
    NWB_ASSERT(postGbufferNormalizeCommandList);
    NWB_ASSERT(shadowVisibilityCommandList);
    NWB_ASSERT(causticsSurfelGiCommandList);
    NWB_ASSERT(postGbufferCommandList);
    if(!gbufferCommandList || !postGbufferNormalizeCommandList || !shadowVisibilityCommandList || !causticsSurfelGiCommandList || !postGbufferCommandList)
        return;

    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_causticsSurfelGiStateHandoff.reset();
    m_postGbufferFanInStateHandoff.reset();
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Recording a packet can advance CPU mirrors (temporal phases, readback cadence, and the AVBOIT clear latch)
    // before the five command buffers have reached the Graphics queue. Preserve them so every rejection path below can
    // make the following frame record the same GPU work again instead of treating an abandoned packet as completed.
    struct PostGbufferPacketCpuState{
        bool avboitTargetsNeedClear = true;
        bool deferredBindlessSlotsUploaded = false;
        u32 softShadowFrameIndex = 0u;
        u32 swShadowEdgeStatsTick = 0u;
        bool swShadowEdgeStatsPending = false;
        u32 swShadowEdgeStatsPendingTick = 0u;
        bool swShadowDispatchLogged = false;
        bool causticAccumulatorInitialized = false;
        u32 swCausticFrameIndex = 0u;
        u32 hwCausticFrameIndex = 0u;
        bool swCausticDispatchLogged = false;
        bool hwCausticDispatchLogged = false;
        bool causticEmissionGateLogged = false;
        u32 surfelFrameIndex = 0u;
        bool surfelSeeded = false;
        bool surfelCountReadbackPending = false;
        u32 surfelCountReadbackFrame = 0u;
    };
    const PostGbufferPacketCpuState postGbufferPacketCpuState{
        m_avboitState.m_targetsNeedClear,
        deferredTargets.bindless.slotsUploaded,
        m_rayTracingState.m_softShadowFrameIndex,
        m_rayTracingState.m_swShadowEdgeStatsTick,
        m_rayTracingState.m_swShadowEdgeStatsPending,
        m_rayTracingState.m_swShadowEdgeStatsPendingTick,
        m_rayTracingState.m_swShadowDispatchLogged,
        m_rayTracingState.m_causticAccumulatorInitialized,
        m_rayTracingState.m_swCausticFrameIndex,
        m_rayTracingState.m_hwCausticFrameIndex,
        m_rayTracingState.m_swCausticDispatchLogged,
        m_rayTracingState.m_hwCausticDispatchLogged,
        m_rayTracingState.m_causticEmissionGateLogged,
        m_rayTracingState.m_surfelFrameIndex,
        m_rayTracingState.m_surfelSeeded,
        m_rayTracingState.m_surfelCountReadbackPending,
        m_rayTracingState.m_surfelCountReadbackFrame,
    };
    const auto restorePostGbufferPacketCpuState = [&](){
        m_avboitState.m_targetsNeedClear = postGbufferPacketCpuState.avboitTargetsNeedClear;
        deferredTargets.bindless.slotsUploaded = postGbufferPacketCpuState.deferredBindlessSlotsUploaded;
        // The G-buffer writer updates its CPU upload mirrors while recording. Its writes did not reach the device if
        // this packet batch is abandoned, so force both uploads on the retry rather than restoring stale byte caches.
        m_drawState.m_meshViewGpuDataValid = false;
        m_deferredState.m_sceneShadingGpuDataValid = false;

        m_rayTracingState.m_softShadowFrameIndex = postGbufferPacketCpuState.softShadowFrameIndex;
        m_rayTracingState.m_swShadowEdgeStatsTick = postGbufferPacketCpuState.swShadowEdgeStatsTick;
        m_rayTracingState.m_swShadowEdgeStatsPending = postGbufferPacketCpuState.swShadowEdgeStatsPending;
        m_rayTracingState.m_swShadowEdgeStatsPendingTick = postGbufferPacketCpuState.swShadowEdgeStatsPendingTick;
        m_rayTracingState.m_swShadowDispatchLogged = postGbufferPacketCpuState.swShadowDispatchLogged;
        m_rayTracingState.m_causticAccumulatorInitialized = postGbufferPacketCpuState.causticAccumulatorInitialized;
        m_rayTracingState.m_swCausticFrameIndex = postGbufferPacketCpuState.swCausticFrameIndex;
        m_rayTracingState.m_hwCausticFrameIndex = postGbufferPacketCpuState.hwCausticFrameIndex;
        m_rayTracingState.m_swCausticDispatchLogged = postGbufferPacketCpuState.swCausticDispatchLogged;
        m_rayTracingState.m_hwCausticDispatchLogged = postGbufferPacketCpuState.hwCausticDispatchLogged;
        m_rayTracingState.m_causticEmissionGateLogged = postGbufferPacketCpuState.causticEmissionGateLogged;
        m_rayTracingState.m_surfelFrameIndex = postGbufferPacketCpuState.surfelFrameIndex;
        m_rayTracingState.m_surfelSeeded = postGbufferPacketCpuState.surfelSeeded;
        m_rayTracingState.m_surfelCountReadbackPending = postGbufferPacketCpuState.surfelCountReadbackPending;
        m_rayTracingState.m_surfelCountReadbackFrame = postGbufferPacketCpuState.surfelCountReadbackFrame;
    };

    Core::GpuTimingSubmissionTicket renderTimingTicket(m_graphics.gpuTiming());
    // This scope crosses two synchronously-waited graphics jobs, so it cannot live in either worker's scratch
    // arena. Keep its small state in the caller's frame stack instead of allocating it from the renderer's
    // persistent object arena.
    Optional<Core::GpuTimingMeasure> frameTiming;
    const auto discardRenderPackets = [&](){
        if(frameTiming){
            frameTiming->discardTiming();
            frameTiming.reset();
        }
        renderTimingTicket.discard();
        restorePostGbufferPacketCpuState();
        m_raytracingSystem.discardSoftShadowTemporalHistory();
        m_gbufferStateHandoff.reset();
        m_postGbufferNormalizedStateHandoff.reset();
        m_shadowVisibilityStateHandoff.reset();
        m_causticsSurfelGiStateHandoff.reset();
        m_postGbufferFanInStateHandoff.reset();
    };

    bool gbufferCommandListReady = false;
    const Core::Graphics::JobHandle gbufferRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        csgFrameState,
        hasOpaqueCsgFrameWork,
        &device,
        gbufferCommandList,
        &frameTiming,
        &gbufferCommandListReady,
        &renderTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(renderTimingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        Core::CommandList* commandList = gbufferCommandList;
        commandList->open(&m_shadowPrepareStateHandoff);
        if(!commandList->hasCommandBuffer())
            return;

        // Graphics has already reset every timer-query pool in the frame preamble, before shadow preparation and
        // every other render pass. This list can therefore begin the renderer frame scope without invalidating an
        // earlier pass's current-frame timestamps.
        frameTiming.emplace(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_Frame,
            &device,
            *commandList
        );
        if(!frameTiming)
            return;

        MaterialPassDrawItemPartitions opaqueDrawItems{scratchArena};
        InstanceGpuDataVector instanceData{scratchArena};
        CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{scratchArena};
#endif
        MaterialTypedByteDataVector materialTypedBytes{scratchArena};

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

        const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(deferredTargets.framebuffer->getFramebufferInfo());
        const bool meshViewReady = m_meshSystem.updateMeshViewBuffer(*commandList, meshViewAspectRatio);
        const bool sceneShadingReady = m_deferredSystem.updateSceneShadingBuffer(*commandList, meshViewAspectRatio);
        if(meshViewReady && sceneShadingReady){
            m_materialSystem.gatherMaterialPassDrawItems(
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                false,
                csgFrameState,
                opaqueDrawItems,
                instanceData,
                csgFrameData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes,
                RendererResourceLookupMode::PreparedOnly
            );
        }

        const Core::Rect opaqueCsgClearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height);
        m_deferredSystem.clearDeferredTargets(*commandList, deferredTargets, hasOpaqueCsgFrameWork, opaqueCsgClearRect);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && m_materialSystem.materialPassDrawBuffersReady(instanceData, materialTypedBytes)
        ;
        const bool regularDrawResourcesReady =
            deferredResourcesReady
            && m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.regular)
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && (opaqueDrawItems.csg.empty() || m_csgSystem.csgFrameBuffersReady(csgFrameData))
        ;
        const bool csgDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csg.empty() || m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty() || m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        const bool deferredUploadReady =
            deferredResourcesReady
            && m_materialSystem.uploadMaterialPassDrawBuffers(
                *commandList,
                instanceData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes
            )
        ;
        if(deferredUploadReady){
            const bool csgUploadReady = csgResourcesReady && (opaqueDrawItems.csg.empty() || m_csgSystem.uploadCsgFrameBuffers(*commandList, csgFrameData));
            const bool csgSampleStateReady =
                csgUploadReady
                && (!csgFrameData.hasWork() || m_csgSystem.uploadCsgIntervalSampleState(*commandList, deferredTargets, csgFrameData))
            ;
            if(csgSampleStateReady && csgFrameData.hasWork())
                m_csgSystem.dispatchCsgIntervalPeels(*commandList, deferredTargets, csgFrameData);
            const MaterialPassDrawContext opaqueDrawContext{
                *commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                nullptr,
                deferredViewportState
            };
            if(regularDrawResourcesReady && !opaqueDrawItems.regular.empty()){
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueRegular, &device, *commandList);

                m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.regular);
            }

            Core::ViewportState csgIntervalViewportState;
            csgIntervalViewportState
                .addViewport(deferredTargets.framebuffer->getFramebufferInfo().getViewport())
                .addScissorRect(csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height))
            ;
            const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
                *commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::CsgReceiverSurface,
                nullptr,
                csgIntervalViewportState
            };
            if(csgSampleStateReady && csgReceiverSurfaceDrawResourcesReady && !opaqueDrawItems.csgReceiverSurface.empty()){
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, &device, *commandList);

                m_materialSystem.renderMaterialPassDrawItems(csgReceiverSurfaceDrawContext, opaqueDrawItems.csgReceiverSurface);
            }
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgReceiverSpanBuild(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgIntervalCombine(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgDrawResourcesReady){
                if(!opaqueDrawItems.csg.empty()){
                    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, &device, *commandList);

                    m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
                }
                if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                    m_csgSystem.renderCsgIntervalCaps(*commandList, deferredTargets, csgFrameData);
            }
        }
        commandList->endRenderPass();

        // The opaque producer exports its final tracked state after close-time keepInitialState restores. The next
        // packet is a normalization prelude; it imports this snapshot before the two independent workers record.
        frameTiming->finishMarker();
        commandList->close(&m_gbufferStateHandoff);
        if(!m_gbufferStateHandoff.valid()){
            frameTiming->discardTiming();
            frameTiming.reset();
            return;
        }
        gbufferCommandListReady = commandList->hasCommandBuffer();
    });
    if(!gbufferRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(gbufferRecordingJob);
    if(!gbufferCommandListReady || !frameTiming){
        discardRenderPackets();
        return;
    }

    // Normalize every G-buffer/trace input shared by the sibling packets once. The two workers import the exact
    // same snapshot below, so neither can record a stale transition from the opaque producer's final state.
    bool postGbufferNormalizeCommandListReady = false;
    const Core::Graphics::JobHandle postGbufferNormalizeRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        postGbufferNormalizeCommandList,
        &postGbufferNormalizeCommandListReady
    ](){
        postGbufferNormalizeCommandList->open(&m_gbufferStateHandoff);
        if(!postGbufferNormalizeCommandList->hasCommandBuffer())
            return;

        m_raytracingSystem.normalizePostGbufferPacketResources(*postGbufferNormalizeCommandList, deferredTargets);
        postGbufferNormalizeCommandList->close(&m_postGbufferNormalizedStateHandoff);
        postGbufferNormalizeCommandListReady =
            m_postGbufferNormalizedStateHandoff.valid()
            && postGbufferNormalizeCommandList->hasCommandBuffer()
        ;
    });
    if(!postGbufferNormalizeRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(postGbufferNormalizeRecordingJob);
    if(!postGbufferNormalizeCommandListReady){
        discardRenderPackets();
        return;
    }

    const bool shadowVisibilityPrepared = m_preparedShadowVisibilityReady;
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;

    // The shadow packet owns only visibility/soft-shadow outputs. The caustics+GI packet owns only its irradiance
    // and persistent surfel outputs; all shared inputs already have their common read state in the prelude.
    bool shadowVisibilityCommandListReady = false;
    bool causticsSurfelGiCommandListReady = false;
    const Core::Graphics::JobHandle shadowVisibilityRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        shadowVisibilityCommandList,
        &shadowVisibilityCommandListReady,
        &renderTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(renderTimingTicket);
        shadowVisibilityCommandList->open(&m_postGbufferNormalizedStateHandoff);
        if(!shadowVisibilityCommandList->hasCommandBuffer())
            return;

        bool shadowVisibilityWritten = false;
        if(shadowVisibilityPrepared && hardwareShadowSupported){
            // The HW opaque trace feeds the soft denoise chain when available. Transparent shadow stays on the
            // software path, with a hybrid multiply fallback when the colored soft fold was not prepared.
            shadowVisibilityWritten = m_raytracingSystem.renderShadowVisibility(*shadowVisibilityCommandList, deferredTargets);
            if(!shadowVisibilityWritten)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
            else if(!m_raytracingSystem.softTransparentShadowReady() && m_raytracingSystem.hybridTransparentShadowReady()){
                if(!m_raytracingSystem.renderGpuBvhShadowVisibility(*shadowVisibilityCommandList, deferredTargets, true))
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow pass failed"));
            }
        }
        else if(shadowVisibilityPrepared){
            shadowVisibilityWritten = m_raytracingSystem.renderGpuBvhShadowVisibility(*shadowVisibilityCommandList, deferredTargets);
        }
        // Deferred lighting samples visibility every frame, so retain the all-lit fallback whenever neither backend
        // emitted it rather than exposing the previous frame's contents.
        if(!shadowVisibilityWritten)
            m_raytracingSystem.clearShadowVisibility(*shadowVisibilityCommandList, deferredTargets);

        shadowVisibilityCommandList->close(&m_shadowVisibilityStateHandoff);
        shadowVisibilityCommandListReady =
            m_shadowVisibilityStateHandoff.valid()
            && shadowVisibilityCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle causticsSurfelGiRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        causticsSurfelGiCommandList,
        &causticsSurfelGiCommandListReady,
        &renderTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(renderTimingTicket);
        causticsSurfelGiCommandList->open(&m_postGbufferNormalizedStateHandoff);
        if(!causticsSurfelGiCommandList->hasCommandBuffer())
            return;

        // Black is the additive identity for caustics. Keep that valid no-op input even when no refractive scene
        // work was prepared or the selected producer fails to record.
        m_raytracingSystem.clearCausticTargets(*causticsSurfelGiCommandList, deferredTargets);
        if(shadowVisibilityPrepared){
            if(hardwareShadowSupported){
                const bool causticsDispatched = m_raytracingSystem.renderHwCaustics(*causticsSurfelGiCommandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasHwCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic render pass failed"));
            }
            else{
                const bool causticsDispatched = m_raytracingSystem.renderGpuBvhCaustics(*causticsSurfelGiCommandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic render pass failed"));
            }
        }

        // Spawn -> hash build -> trace -> resolve remains one ordered packet, so its persistent surfel buffers never
        // become visible to another worker halfway through their per-frame update.
        if(!m_raytracingSystem.renderSurfelGi(*causticsSurfelGiCommandList, deferredTargets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI render pass failed"));

        causticsSurfelGiCommandList->close(&m_causticsSurfelGiStateHandoff);
        causticsSurfelGiCommandListReady =
            m_causticsSurfelGiStateHandoff.valid()
            && causticsSurfelGiCommandList->hasCommandBuffer()
        ;
    });
    if(!shadowVisibilityRecordingJob.valid() || !causticsSurfelGiRecordingJob.valid()){
        if(shadowVisibilityRecordingJob.valid())
            m_graphics.waitJob(shadowVisibilityRecordingJob);
        if(causticsSurfelGiRecordingJob.valid())
            m_graphics.waitJob(causticsSurfelGiRecordingJob);
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(shadowVisibilityRecordingJob);
    m_graphics.waitJob(causticsSurfelGiRecordingJob);
    if(!shadowVisibilityCommandListReady || !causticsSurfelGiCommandListReady){
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* postGbufferBranchStates[] = {
        &m_shadowVisibilityStateHandoff,
        &m_causticsSurfelGiStateHandoff,
    };
    if(!m_postGbufferFanInStateHandoff.buildFanIn(
        m_postGbufferNormalizedStateHandoff,
        postGbufferBranchStates,
        2u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: post-G-buffer packet state fan-in failed"));
        discardRenderPackets();
        return;
    }

    const bool hasTransparentRenderers = m_preparedHasTransparentRenderers;
    bool postGbufferCommandListReady = false;
    const Core::Graphics::JobHandle postGbufferRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        framebuffer,
        &deferredTargets,
        csgFrameState,
        hasTransparentRenderers,
        postGbufferCommandList,
        &frameTiming,
        &postGbufferCommandListReady,
        &renderTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(renderTimingTicket);
        postGbufferCommandList->open(&m_postGbufferFanInStateHandoff);
        if(!postGbufferCommandList->hasCommandBuffer())
            return;

        bool commandListReady = m_deferredSystem.renderDeferredLighting(*postGbufferCommandList, deferredTargets);
        if(commandListReady){
            if(hasTransparentRenderers || m_avboitState.m_targetsNeedClear){
                m_avboitSystem.clearAvboitTargets(*postGbufferCommandList, deferredTargets.avboit);
                m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
            }
            if(hasTransparentRenderers)
                m_avboitSystem.renderAvboitPasses(*postGbufferCommandList, deferredTargets, csgFrameState);

            commandListReady = m_deferredSystem.renderDeferredComposite(*postGbufferCommandList, deferredTargets, framebuffer);
        }

        // This endpoint completes the frame scope opened on the G-buffer command buffer. All five command buffers are
        // submitted in the fixed Graphics-queue order below, so the timestamps remain one contiguous frame metric.
        frameTiming->finishTiming(*postGbufferCommandList);
        postGbufferCommandList->close();
        postGbufferCommandListReady = commandListReady && postGbufferCommandList->hasCommandBuffer();
    });
    if(!postGbufferRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(postGbufferRecordingJob);
    if(!postGbufferCommandListReady){
        discardRenderPackets();
        return;
    }

    // Workers only record. The GPU still receives one ordered Graphics submission: opaque producer -> normalized
    // prelude -> shadow visibility -> caustics/surfel GI -> lighting/transparency/composite consumer.
    Core::CommandList* commandLists[] = {
        gbufferCommandList,
        postGbufferNormalizeCommandList,
        shadowVisibilityCommandList,
        causticsSurfelGiCommandList,
        postGbufferCommandList,
    };
    if(!renderTimingTicket.submit(device, commandLists, 5u)){
        discardRenderPackets();
        return;
    }

    frameTiming.reset();
    m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

