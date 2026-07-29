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
    , m_meshViewSetupStateHandoff(arena)
    , m_sceneShadingSetupStateHandoff(arena)
    , m_deferredClearStateHandoff(arena)
    , m_frameSetupStateFanInHandoff(arena)
    , m_gbufferStateHandoff(arena)
    , m_postGbufferNormalizedStateHandoff(arena)
    , m_shadowComputeBaseStateHandoff(arena)
    , m_shadowComputeInputStateHandoff(arena)
    , m_shadowComputePersistentStateHandoff(arena)
    , m_shadowVisibilityStateHandoff(arena)
    , m_shadowVisibilityGraphicsStateHandoff(arena)
    , m_shadowVisibilityReturnStateHandoff(arena)
    , m_shadowOwnershipRecoveryInputStateHandoff(arena)
    , m_shadowOwnershipRecoveryStateHandoff(arena)
    , m_causticsComputeBaseStateHandoff(arena)
    , m_causticsComputeInputStateHandoff(arena)
    , m_causticsComputePersistentStateHandoff(arena)
    , m_causticsStateHandoff(arena)
    , m_causticIrradianceGraphicsStateHandoff(arena)
    , m_causticIrradianceReturnStateHandoff(arena)
    , m_surfelGiStateHandoff(arena)
    , m_postGbufferFanInStateHandoff(arena)
    , m_deferredLightingStateHandoff(arena)
    , m_deferredCompositeStateHandoff(arena)
    , m_avboitStateHandoff(arena)
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
    if(!targetsReady){
        // Target-generation replacement invalidates every retained Compute scratch state and the visibility ownership
        // return. The new image can be claimed by its first Compute use without a stale handoff.
        m_shadowComputeBaseStateHandoff.reset();
        m_shadowComputeInputStateHandoff.reset();
        m_shadowComputePersistentStateHandoff.reset();
        m_shadowVisibilityStateHandoff.reset();
        m_shadowVisibilityGraphicsStateHandoff.reset();
        m_shadowVisibilityReturnStateHandoff.reset();
        m_shadowOwnershipRecoveryInputStateHandoff.reset();
        m_shadowOwnershipRecoveryStateHandoff.reset();
        m_causticsComputeBaseStateHandoff.reset();
        m_causticsComputeInputStateHandoff.reset();
        m_causticsComputePersistentStateHandoff.reset();
        m_causticsStateHandoff.reset();
        m_causticIrradianceGraphicsStateHandoff.reset();
        m_causticIrradianceReturnStateHandoff.reset();
        m_deferredCompositeStateHandoff.reset();
        targetsReady = m_deferredSystem.createDeferredFrameTargets(width, height);
    }
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
    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowComputePersistentStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityGraphicsStateHandoff.reset();
    m_shadowVisibilityReturnStateHandoff.reset();
    m_shadowOwnershipRecoveryInputStateHandoff.reset();
    m_shadowOwnershipRecoveryStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsComputePersistentStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceGraphicsStateHandoff.reset();
    m_causticIrradianceReturnStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_postGbufferFanInStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_avboitStateHandoff.reset();
    m_meshViewSetupCommandList.reset();
    m_sceneShadingSetupCommandList.reset();
    m_deferredClearCommandList.reset();
    m_gbufferCommandList.reset();
    m_postGbufferNormalizeCommandList.reset();
    m_shadowVisibilityCommandList.reset();
    m_shadowOwnershipRecoveryCommandList.reset();
    m_asyncEffectsTimingBeginCommandList.reset();
    m_asyncEffectsTimingEndCommandList.reset();
    m_asyncCausticsCommandList.reset();
    m_causticsCommandList.reset();
    m_surfelGiCommandList.reset();
    m_deferredLightingCommandList.reset();
    m_avboitCommandList.reset();
    m_deferredCompositeCommandList.reset();
    m_shadowPrepareCommandList.reset();
    m_asyncShadowOwnershipRecoveryFailed = false;
    // The descriptor-buffer TLAS descriptor owns a retained acceleration-structure handle until its in-flight-frame
    // quarantine matures. Retire it before RendererRayTracingState releases the current TLAS so resource invalidation
    // cannot strand a descriptor-buffer block (or its retained AS) until device shutdown.
    if(m_rayTracingState.m_tlasHeapHandle.valid()){
        auto& device = m_graphics.getDevice();
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
    auto& device = m_graphics.getDevice();

    if(!m_meshViewSetupCommandList){
        m_meshViewSetupCommandList = device.createCommandList();
        if(!m_meshViewSetupCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh-view setup command list"));
            return false;
        }
    }

    if(!m_sceneShadingSetupCommandList){
        m_sceneShadingSetupCommandList = device.createCommandList();
        if(!m_sceneShadingSetupCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene-shading setup command list"));
            return false;
        }
    }

    if(!m_deferredClearCommandList){
        m_deferredClearCommandList = device.createCommandList();
        if(!m_deferredClearCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-clear command list"));
            return false;
        }
    }

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
        Core::CommandListParameters shadowVisibilityCommandListParameters;
        shadowVisibilityCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
        m_shadowVisibilityCommandList = device.createCommandList(shadowVisibilityCommandListParameters);
        if(!m_shadowVisibilityCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow-visibility command list"));
            return false;
        }
    }

    if(!m_shadowOwnershipRecoveryCommandList){
        m_shadowOwnershipRecoveryCommandList = device.createCommandList();
        if(!m_shadowOwnershipRecoveryCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow ownership-recovery command list"));
            return false;
        }
    }

    if(device.isRenderLaneDedicated(Core::RenderLane::AsyncCompute)){
        if(!m_asyncEffectsTimingBeginCommandList){
            m_asyncEffectsTimingBeginCommandList = device.createCommandList();
            if(!m_asyncEffectsTimingBeginCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async effects timing-begin command list"));
                return false;
            }
        }
        if(!m_asyncEffectsTimingEndCommandList){
            m_asyncEffectsTimingEndCommandList = device.createCommandList();
            if(!m_asyncEffectsTimingEndCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async effects timing-end command list"));
                return false;
            }
        }
        if(!m_asyncCausticsCommandList){
            Core::CommandListParameters asyncCausticsCommandListParameters;
            asyncCausticsCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncCausticsCommandList = device.createCommandList(asyncCausticsCommandListParameters);
            if(!m_asyncCausticsCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async software-caustics command list"));
                return false;
            }
        }
    }

    if(!m_causticsCommandList){
        m_causticsCommandList = device.createCommandList();
        if(!m_causticsCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustics command list"));
            return false;
        }
    }

    if(!m_surfelGiCommandList){
        m_surfelGiCommandList = device.createCommandList();
        if(!m_surfelGiCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel-GI command list"));
            return false;
        }
    }

    if(!m_deferredLightingCommandList){
        m_deferredLightingCommandList = device.createCommandList();
        if(!m_deferredLightingCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-lighting command list"));
            return false;
        }
    }

    if(!m_avboitCommandList){
        m_avboitCommandList = device.createCommandList();
        if(!m_avboitCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT command list"));
            return false;
        }
    }

    if(!m_deferredCompositeCommandList){
        m_deferredCompositeCommandList = device.createCommandList();
        if(!m_deferredCompositeCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-composite command list"));
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
    auto& device = m_graphics.getDevice();

    struct ScopeReservation{
        const Core::GpuTimingScopeDefinition* scope;
        u32 queryCount;
    };
    const ScopeReservation scopeReservations[] = {
        { &RendererGpuTimingScope::s_MeshDispatch, 128u },
        { &RendererGpuTimingScope::s_Raster, 128u },
        { &RendererGpuTimingScope::s_Frame, 2u },
        { &RendererGpuTimingScope::s_AsyncPrefix, 2u },
        { &RendererGpuTimingScope::s_AsyncShadow, 2u },
        { &RendererGpuTimingScope::s_AsyncEffects, 2u },
        { &RendererGpuTimingScope::s_AsyncFinal, 2u },
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
        if(!m_graphics.gpuTiming().prepareScopeQueries(reservation.scope->identity, device, reservation.queryCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare GPU timing scope '{}'"), StringConvert(reservation.scope->identity.c_str()));
            return false;
        }
    }

    if(
        device.supportsGraphicsAndComputeTimestamps()
        && !m_graphics.gpuTiming().prepareOverlapMetric(
            RendererGpuTimingScope::s_AsyncShadow.identity,
            RendererGpuTimingScope::s_AsyncEffects.identity,
            RendererGpuTimingScope::s_AsyncShadowEffectsOverlap.identity
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare async shadow/effects overlap metric"));
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

    NWB_ASSERT(m_meshViewSetupCommandList);
    NWB_ASSERT(m_sceneShadingSetupCommandList);
    NWB_ASSERT(m_deferredClearCommandList);
    NWB_ASSERT(m_gbufferCommandList);
    NWB_ASSERT(m_postGbufferNormalizeCommandList);
    NWB_ASSERT(m_shadowVisibilityCommandList);
    NWB_ASSERT(m_causticsCommandList);
    NWB_ASSERT(m_surfelGiCommandList);
    NWB_ASSERT(m_deferredLightingCommandList);
    NWB_ASSERT(m_avboitCommandList);
    NWB_ASSERT(m_deferredCompositeCommandList);
    NWB_ASSERT(m_shadowPrepareCommandList);

    auto& device = m_graphics.getDevice();

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
    // AVBOIT can record before deferred lighting, so its heap-selected resources must have been uploaded by the
    // ordered shadow-preparation packet rather than relying on deferred lighting's otherwise-idempotent upload.
    NWB_ASSERT(deferredTargets.bindless.slotsUploaded);
    if(!deferredTargets.bindless.slotsUploaded)
        return;

    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto& device = m_graphics.getDevice();
    if(m_asyncShadowOwnershipRecoveryFailed){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: async shadow ownership recovery failed; rendering is suspended until resources are recreated"));
        return;
    }
    const bool asyncShadowSchedule = device.isRenderLaneDedicated(Core::RenderLane::AsyncCompute);
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    // A dedicated compute family is not assumed to expose ray-tracing commands. Keep the HW producer on Graphics;
    // only the ordinary-compute software producer joins the AsyncCompute submission.
    const bool asyncSoftwareCausticsSchedule = asyncShadowSchedule && !hardwareShadowSupported;
    Core::CommandList* meshViewSetupCommandList = m_meshViewSetupCommandList.get();
    Core::CommandList* sceneShadingSetupCommandList = m_sceneShadingSetupCommandList.get();
    Core::CommandList* deferredClearCommandList = m_deferredClearCommandList.get();
    Core::CommandList* gbufferCommandList = m_gbufferCommandList.get();
    Core::CommandList* postGbufferNormalizeCommandList = m_postGbufferNormalizeCommandList.get();
    Core::CommandList* shadowVisibilityCommandList = m_shadowVisibilityCommandList.get();
    Core::CommandList* shadowOwnershipRecoveryCommandList = m_shadowOwnershipRecoveryCommandList.get();
    Core::CommandList* asyncEffectsTimingBeginCommandList = m_asyncEffectsTimingBeginCommandList.get();
    Core::CommandList* asyncEffectsTimingEndCommandList = m_asyncEffectsTimingEndCommandList.get();
    Core::CommandList* graphicsCausticsCommandList = m_causticsCommandList.get();
    Core::CommandList* asyncCausticsCommandList = m_asyncCausticsCommandList.get();
    Core::CommandList* causticsCommandList = asyncSoftwareCausticsSchedule
        ? asyncCausticsCommandList
        : graphicsCausticsCommandList
    ;
    Core::CommandList* surfelGiCommandList = m_surfelGiCommandList.get();
    Core::CommandList* deferredLightingCommandList = m_deferredLightingCommandList.get();
    Core::CommandList* avboitCommandList = m_avboitCommandList.get();
    Core::CommandList* deferredCompositeCommandList = m_deferredCompositeCommandList.get();
    NWB_ASSERT(meshViewSetupCommandList);
    NWB_ASSERT(sceneShadingSetupCommandList);
    NWB_ASSERT(deferredClearCommandList);
    NWB_ASSERT(gbufferCommandList);
    NWB_ASSERT(postGbufferNormalizeCommandList);
    NWB_ASSERT(shadowVisibilityCommandList);
    NWB_ASSERT(shadowOwnershipRecoveryCommandList);
    NWB_ASSERT(!asyncShadowSchedule || asyncEffectsTimingBeginCommandList);
    NWB_ASSERT(!asyncShadowSchedule || asyncEffectsTimingEndCommandList);
    NWB_ASSERT(graphicsCausticsCommandList);
    NWB_ASSERT(!asyncSoftwareCausticsSchedule || asyncCausticsCommandList);
    NWB_ASSERT(causticsCommandList);
    NWB_ASSERT(surfelGiCommandList);
    NWB_ASSERT(deferredLightingCommandList);
    NWB_ASSERT(avboitCommandList);
    NWB_ASSERT(deferredCompositeCommandList);
    if(
        !meshViewSetupCommandList
        || !sceneShadingSetupCommandList
        || !deferredClearCommandList
        || !gbufferCommandList
        || !postGbufferNormalizeCommandList
        || !shadowVisibilityCommandList
        || !shadowOwnershipRecoveryCommandList
        || (asyncShadowSchedule && !asyncEffectsTimingBeginCommandList)
        || (asyncShadowSchedule && !asyncEffectsTimingEndCommandList)
        || !graphicsCausticsCommandList
        || (asyncSoftwareCausticsSchedule && !asyncCausticsCommandList)
        || !causticsCommandList
        || !surfelGiCommandList
        || !deferredLightingCommandList
        || !avboitCommandList
        || !deferredCompositeCommandList
    )
        return;

    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityGraphicsStateHandoff.reset();
    m_shadowOwnershipRecoveryInputStateHandoff.reset();
    m_shadowOwnershipRecoveryStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceGraphicsStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_postGbufferFanInStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_avboitStateHandoff.reset();
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Recording a packet can advance CPU mirrors (temporal phases, readback cadence, and the AVBOIT clear latch)
    // before the eleven command buffers have reached the Graphics queue. Preserve them so every rejection path below can
    // make the following frame record the same GPU work again instead of treating an abandoned packet as completed.
    struct PostGbufferPacketCpuState{
        bool avboitTargetsNeedClear = true;
        bool deferredBindlessSlotsUploaded = false;
        u32 softShadowFrameIndex = 0u;
        u32 swShadowEdgeStatsTick = 0u;
        bool swShadowEdgeStatsPending = false;
        u32 swShadowEdgeStatsPendingTick = 0u;
        u64 swShadowEdgeStatsPendingSubmissionID = 0u;
        Core::CommandQueue::Enum swShadowEdgeStatsPendingSubmissionQueue = Core::CommandQueue::kCount;
        bool swShadowEdgeStatsPendingSubmissionUnconfirmed = false;
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
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionQueue,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionUnconfirmed,
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
    const auto restorePrefixCpuState = [&](){
        // The G-buffer writer updates its CPU upload mirrors while recording. Its writes did not reach the device if
        // this packet batch is abandoned, so force both uploads on the retry rather than restoring stale byte caches.
        m_drawState.m_meshViewGpuDataValid = false;
        m_deferredState.m_sceneShadingGpuDataValid = false;
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
        m_rayTracingState.m_swCausticFrameIndex = postGbufferPacketCpuState.swCausticFrameIndex;
        m_rayTracingState.m_hwCausticFrameIndex = postGbufferPacketCpuState.hwCausticFrameIndex;
        m_rayTracingState.m_swCausticDispatchLogged = postGbufferPacketCpuState.swCausticDispatchLogged;
        m_rayTracingState.m_hwCausticDispatchLogged = postGbufferPacketCpuState.hwCausticDispatchLogged;
        m_rayTracingState.m_causticEmissionGateLogged = postGbufferPacketCpuState.causticEmissionGateLogged;
    };
    const auto restoreGraphicsEffectsCpuState = [&](){
        m_avboitState.m_targetsNeedClear = postGbufferPacketCpuState.avboitTargetsNeedClear;
        m_rayTracingState.m_surfelFrameIndex = postGbufferPacketCpuState.surfelFrameIndex;
        m_rayTracingState.m_surfelSeeded = postGbufferPacketCpuState.surfelSeeded;
        m_rayTracingState.m_surfelCountReadbackPending = postGbufferPacketCpuState.surfelCountReadbackPending;
        m_rayTracingState.m_surfelCountReadbackFrame = postGbufferPacketCpuState.surfelCountReadbackFrame;
    };
    const auto restoreEffectsCpuState = [&](){
        restoreCausticsCpuState();
        restoreGraphicsEffectsCpuState();
    };
    // Once the combined Compute submission accepts, the software-caustic temporal phase belongs to that submission;
    // later Graphics-effects rejection must not rewind it along with unaccepted GI/AVBOIT recording state.
    const auto restoreUnacceptedGraphicsEffectsCpuState = [&](){
        if(asyncSoftwareCausticsSchedule)
            restoreGraphicsEffectsCpuState();
        else
            restoreEffectsCpuState();
    };
    const auto restorePostGbufferPacketCpuState = [&](){
        deferredTargets.bindless.slotsUploaded = postGbufferPacketCpuState.deferredBindlessSlotsUploaded;
        restorePrefixCpuState();
        restoreShadowCpuState();
        restoreEffectsCpuState();
    };

    // The Graphics-only fallback keeps its established one-ticket, one-submission timing transaction. A dedicated
    // AsyncCompute lane instead owns one ticket per accepted packet so no timestamp reservation can straddle queues.
    Core::GpuTimingSubmissionTicket renderTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket prefixTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket shadowTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket effectsTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket finalTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket* const prefixTimingTicketForPacket = asyncShadowSchedule ? &prefixTimingTicket : &renderTimingTicket;
    Core::GpuTimingSubmissionTicket* const shadowTimingTicketForPacket = asyncShadowSchedule ? &shadowTimingTicket : &renderTimingTicket;
    Core::GpuTimingSubmissionTicket* const effectsTimingTicketForPacket = asyncShadowSchedule ? &effectsTimingTicket : &renderTimingTicket;
    // On a dedicated lane, software caustics share the accepted Compute submission with shadow visibility. Hardware
    // caustics remain in the Graphics effects submission because a compute-only family need not support dispatchRays.
    Core::GpuTimingSubmissionTicket* const causticsTimingTicketForPacket = asyncSoftwareCausticsSchedule
        ? shadowTimingTicketForPacket
        : effectsTimingTicketForPacket
    ;
    Core::GpuTimingSubmissionTicket* const finalTimingTicketForPacket = asyncShadowSchedule ? &finalTimingTicket : &renderTimingTicket;
    // This scope crosses two synchronously-waited Graphics jobs, so it cannot live in either worker's scratch
    // arena. The fallback owns it directly; the dedicated schedule uses the acceptance-aware transaction below.
    Optional<Core::GpuTimingMeasure> frameTiming;
    // The dedicated schedule keeps render.frame as an end-to-end Graphics-timeline critical path. Its endpoint is
    // committed only after Graphics final accepts; a rejected later packet records a non-publishing recovery end so
    // the accepted prefix timestamp never leaks into a later frame's query pool.
    Core::GpuTimingFrameTransaction asyncFrameTiming(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncPrefixTiming;
    Optional<Core::GpuTimingMeasure> asyncEffectsTiming;
    Optional<Core::GpuTimingMeasure> asyncFinalTiming;
    const auto discardTimingTickets = [&](){
        renderTimingTicket.discard();
        prefixTimingTicket.discard();
        shadowTimingTicket.discard();
        effectsTimingTicket.discard();
        finalTimingTicket.discard();
    };
    const auto discardRenderPackets = [&](){
        if(frameTiming){
            frameTiming->discardTiming();
            frameTiming.reset();
        }
        if(asyncPrefixTiming){
            asyncPrefixTiming->discardTiming();
            asyncPrefixTiming.reset();
        }
        if(asyncEffectsTiming){
            asyncEffectsTiming->discardTiming();
            asyncEffectsTiming.reset();
        }
        if(asyncFinalTiming){
            asyncFinalTiming->discardTiming();
            asyncFinalTiming.reset();
        }
        asyncFrameTiming.discard();
        discardTimingTickets();
        restorePostGbufferPacketCpuState();
        m_raytracingSystem.discardSoftShadowTemporalHistory();
        m_meshViewSetupStateHandoff.reset();
        m_sceneShadingSetupStateHandoff.reset();
        m_deferredClearStateHandoff.reset();
        m_frameSetupStateFanInHandoff.reset();
        m_gbufferStateHandoff.reset();
        m_postGbufferNormalizedStateHandoff.reset();
        m_shadowComputeBaseStateHandoff.reset();
        m_shadowComputeInputStateHandoff.reset();
        m_shadowVisibilityStateHandoff.reset();
        m_shadowVisibilityGraphicsStateHandoff.reset();
        m_shadowOwnershipRecoveryInputStateHandoff.reset();
        m_shadowOwnershipRecoveryStateHandoff.reset();
        m_causticsStateHandoff.reset();
        m_surfelGiStateHandoff.reset();
        m_postGbufferFanInStateHandoff.reset();
        m_deferredLightingStateHandoff.reset();
        m_deferredCompositeStateHandoff.reset();
        m_avboitStateHandoff.reset();
    };

    // Mesh-view and scene-shading uploads plus the non-CSG deferred-target clear have no shared CPU or GPU outputs.
    // Record them from the completed shadow-preparation snapshot on sibling workers, then merge their disjoint final
    // states before the opaque producer gathers its CSG work region from the freshly cached mesh-view data. The CSG
    // interval clear stays with that producer because its rect is not known until after the gather.
    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(deferredTargets.framebuffer->getFramebufferInfo());
    bool meshViewSetupReady = false;
    bool sceneShadingSetupReady = false;
    bool meshViewSetupCommandListReady = false;
    bool sceneShadingSetupCommandListReady = false;
    bool deferredClearCommandListReady = false;
    const Core::Graphics::JobHandle meshViewSetupRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        meshViewAspectRatio,
        &device,
        meshViewSetupCommandList,
        &frameTiming,
        &asyncFrameTiming,
        &asyncPrefixTiming,
        asyncShadowSchedule,
        &meshViewSetupReady,
        &meshViewSetupCommandListReady,
        prefixTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*prefixTimingTicketForPacket);
        meshViewSetupCommandList->open(&m_shadowPrepareStateHandoff);
        if(!meshViewSetupCommandList->hasCommandBuffer())
            return;

        bool asyncFrameTimingStarted = true;
        if(!asyncShadowSchedule){
            // Graphics has already reset every timer-query pool in the frame preamble, before shadow preparation and
            // every other render pass. The legacy whole-frame scope remains valid only for the one-submit fallback.
            frameTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_Frame,
                device,
                *meshViewSetupCommandList
            );
            if(!frameTiming)
                return;
        }
        else{
            asyncPrefixTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncPrefix,
                device,
                *meshViewSetupCommandList
            );
            asyncPrefixTiming->finishMarker();
            asyncFrameTimingStarted = asyncFrameTiming.begin(
                RendererGpuTimingScope::s_Frame,
                device,
                *meshViewSetupCommandList
            );
        }

        const bool meshViewReady = m_meshSystem.updateMeshViewBuffer(*meshViewSetupCommandList, meshViewAspectRatio);
        // A split timing scope must close its debug marker on the same list that opened it. Its end timestamp is
        // deliberately deferred to the ordered deferred-composite packet below.
        if(frameTiming)
            frameTiming->finishMarker();
        meshViewSetupCommandList->close(&m_meshViewSetupStateHandoff);
        meshViewSetupReady = meshViewReady;
        meshViewSetupCommandListReady =
            asyncFrameTimingStarted
            && m_meshViewSetupStateHandoff.valid()
            && meshViewSetupCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle sceneShadingSetupRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        meshViewAspectRatio,
        sceneShadingSetupCommandList,
        &sceneShadingSetupReady,
        &sceneShadingSetupCommandListReady,
        prefixTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*prefixTimingTicketForPacket);
        sceneShadingSetupCommandList->open(&m_shadowPrepareStateHandoff);
        if(!sceneShadingSetupCommandList->hasCommandBuffer())
            return;

        const bool sceneShadingReady = m_deferredSystem.updateSceneShadingBuffer(*sceneShadingSetupCommandList, meshViewAspectRatio);
        sceneShadingSetupCommandList->close(&m_sceneShadingSetupStateHandoff);
        sceneShadingSetupReady = sceneShadingReady;
        sceneShadingSetupCommandListReady =
            m_sceneShadingSetupStateHandoff.valid()
            && sceneShadingSetupCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle deferredClearRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        deferredClearCommandList,
        &deferredClearCommandListReady,
        prefixTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*prefixTimingTicketForPacket);
        deferredClearCommandList->open(&m_shadowPrepareStateHandoff);
        if(!deferredClearCommandList->hasCommandBuffer())
            return;

        // CSG interval clearing is intentionally deferred to the opaque packet: its work rect is calculated from
        // the freshly gathered CSG receiver data. The remaining deferred targets are independent of both setup
        // uploads and can record here in parallel.
        m_deferredSystem.clearDeferredTargets(*deferredClearCommandList, deferredTargets, false, Core::Rect{});
        deferredClearCommandList->close(&m_deferredClearStateHandoff);
        deferredClearCommandListReady =
            m_deferredClearStateHandoff.valid()
            && deferredClearCommandList->hasCommandBuffer()
        ;
    });
    if(
        !meshViewSetupRecordingJob.valid()
        || !sceneShadingSetupRecordingJob.valid()
        || !deferredClearRecordingJob.valid()
    ){
        if(meshViewSetupRecordingJob.valid())
            m_graphics.waitJob(meshViewSetupRecordingJob);
        if(sceneShadingSetupRecordingJob.valid())
            m_graphics.waitJob(sceneShadingSetupRecordingJob);
        if(deferredClearRecordingJob.valid())
            m_graphics.waitJob(deferredClearRecordingJob);
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(meshViewSetupRecordingJob);
    m_graphics.waitJob(sceneShadingSetupRecordingJob);
    m_graphics.waitJob(deferredClearRecordingJob);
    if(
        !meshViewSetupCommandListReady
        || !sceneShadingSetupCommandListReady
        || !deferredClearCommandListReady
        || (!asyncShadowSchedule && !frameTiming)
    ){
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* frameSetupBranchStates[] = {
        &m_meshViewSetupStateHandoff,
        &m_sceneShadingSetupStateHandoff,
        &m_deferredClearStateHandoff,
    };
    if(!m_frameSetupStateFanInHandoff.buildFanIn(
        m_shadowPrepareStateHandoff,
        frameSetupBranchStates,
        3u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame-setup packet state fan-in failed"));
        discardRenderPackets();
        return;
    }

    const bool frameSetupReady = meshViewSetupReady && sceneShadingSetupReady;
    bool gbufferCommandListReady = false;
    const Core::Graphics::JobHandle gbufferRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        csgFrameState,
        hasOpaqueCsgFrameWork,
        frameSetupReady,
        &device,
        gbufferCommandList,
        &gbufferCommandListReady,
        prefixTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*prefixTimingTicketForPacket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        Core::CommandList* commandList = gbufferCommandList;
        commandList->open(&m_frameSetupStateFanInHandoff);
        if(!commandList->hasCommandBuffer())
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

        if(frameSetupReady){
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
        if(hasOpaqueCsgFrameWork)
            m_deferredSystem.clearCsgIntervalTargets(*commandList, deferredTargets, opaqueCsgClearRect);

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
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueRegular, device, *commandList);

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
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, device, *commandList);

                m_materialSystem.renderMaterialPassDrawItems(csgReceiverSurfaceDrawContext, opaqueDrawItems.csgReceiverSurface);
            }
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgReceiverSpanBuild(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgIntervalCombine(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgDrawResourcesReady){
                if(!opaqueDrawItems.csg.empty()){
                    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, device, *commandList);

                    m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
                }
                if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                    m_csgSystem.renderCsgIntervalCaps(*commandList, deferredTargets, csgFrameData);
            }
        }
        commandList->endRenderPass();

        // The opaque producer exports its final tracked state after close-time keepInitialState restores. The next
        // packet is a normalization prelude; it imports this snapshot before the four independent workers record.
        commandList->close(&m_gbufferStateHandoff);
        if(!m_gbufferStateHandoff.valid())
            return;
        gbufferCommandListReady = commandList->hasCommandBuffer();
    });
    if(!gbufferRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(gbufferRecordingJob);
    if(!gbufferCommandListReady){
        discardRenderPackets();
        return;
    }

    // Normalize every G-buffer/trace input shared by the sibling packets once. The four workers import the exact
    // same snapshot below, so none can record a stale transition from the opaque producer's final state.
    bool postGbufferNormalizeCommandListReady = false;
    const Core::Graphics::JobHandle postGbufferNormalizeRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        postGbufferNormalizeCommandList,
        &postGbufferNormalizeCommandListReady,
        &asyncPrefixTiming,
        asyncShadowSchedule,
        prefixTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*prefixTimingTicketForPacket);
        postGbufferNormalizeCommandList->open(&m_gbufferStateHandoff);
        if(!postGbufferNormalizeCommandList->hasCommandBuffer())
            return;

        m_raytracingSystem.normalizePostGbufferPacketResources(*postGbufferNormalizeCommandList, deferredTargets);
        if(asyncShadowSchedule && asyncPrefixTiming){
            asyncPrefixTiming->finishTiming(*postGbufferNormalizeCommandList);
            asyncPrefixTiming.reset();
        }
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

    // The Compute packet must not import the broad post-G-buffer snapshot: it also carries exclusive Graphics-only
    // caustic/GI/AVBOIT resources. Select only the shader-visible shadow inputs, then merge the Compute-only scratch
    // state and last frame's Graphics -> Compute visibility release.
    if(asyncShadowSchedule){
        Core::Alloc::ScratchArena shadowInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> shadowInputTextures{ shadowInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> shadowInputBuffers{ shadowInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                shadowInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                shadowInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.normal.get());
        appendTexture(deferredTargets.depth.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        if(m_rayTracingState.m_tlas)
            appendBuffer(m_rayTracingState.m_tlas->getBackingBuffer());
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
            appendBuffer(buffer);

        if(!m_shadowComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            shadowInputTextures.data(),
            shadowInputTextures.size(),
            shadowInputBuffers.data(),
            shadowInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async shadow input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* shadowInputBranches[2] = {};
        usize shadowInputBranchCount = 0u;
        if(m_shadowComputePersistentStateHandoff.valid())
            shadowInputBranches[shadowInputBranchCount++] = &m_shadowComputePersistentStateHandoff;
        if(m_shadowVisibilityReturnStateHandoff.valid())
            shadowInputBranches[shadowInputBranchCount++] = &m_shadowVisibilityReturnStateHandoff;
        if(!m_shadowComputeInputStateHandoff.buildFanIn(
            m_shadowComputeBaseStateHandoff,
            shadowInputBranches,
            shadowInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async shadow input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    // Software caustics use the same cross-lane read-only trace inputs as the software shadow, plus their emission
    // target list and camera view. Their accumulator/resolve scratch is private to Compute and their lighting-facing
    // irradiance is deliberately excluded here: it comes from the prior Graphics -> Compute ownership return.
    if(asyncSoftwareCausticsSchedule){
        Core::Alloc::ScratchArena causticsInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> causticsInputTextures{ causticsInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> causticsInputBuffers{ causticsInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                causticsInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                causticsInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.depth.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        appendBuffer(m_rayTracingState.m_causticEmissionTargetBuffer.get());
        appendBuffer(m_drawState.m_meshViewBuffer.get());
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
            appendBuffer(buffer);

        if(!m_causticsComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            causticsInputTextures.data(),
            causticsInputTextures.size(),
            causticsInputBuffers.data(),
            causticsInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async software-caustics input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* causticsInputBranches[2] = {};
        usize causticsInputBranchCount = 0u;
        if(m_causticsComputePersistentStateHandoff.valid())
            causticsInputBranches[causticsInputBranchCount++] = &m_causticsComputePersistentStateHandoff;
        if(m_causticIrradianceReturnStateHandoff.valid())
            causticsInputBranches[causticsInputBranchCount++] = &m_causticIrradianceReturnStateHandoff;
        if(!m_causticsComputeInputStateHandoff.buildFanIn(
            m_causticsComputeBaseStateHandoff,
            causticsInputBranches,
            causticsInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async software-caustics input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    const bool shadowVisibilityPrepared = m_preparedShadowVisibilityReady;
    const bool hasTransparentRenderers = m_preparedHasTransparentRenderers;

    // The shadow, caustics, surfel-GI, and AVBOIT packets own distinct outputs. Every shared input already has its
    // common read state in the prelude, so the workers can record independently from the same snapshot. AVBOIT still
    // executes after the ray-effect packets on Graphics; deferred lighting is its first GPU consumer.
    bool shadowVisibilityCommandListReady = false;
    bool causticsCommandListReady = false;
    bool surfelGiCommandListReady = false;
    bool avboitCommandListReady = false;
    bool asyncEffectsTimingBeginCommandListReady = !asyncShadowSchedule;
    if(asyncShadowSchedule){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*effectsTimingTicketForPacket);
        asyncEffectsTimingBeginCommandList->open();
        if(asyncEffectsTimingBeginCommandList->hasCommandBuffer()){
            asyncEffectsTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncEffects,
                device,
                *asyncEffectsTimingBeginCommandList
            );
            asyncEffectsTiming->finishMarker();
            asyncEffectsTimingBeginCommandList->close();
            asyncEffectsTimingBeginCommandListReady = asyncEffectsTimingBeginCommandList->hasCommandBuffer();
        }
    }
    if(!asyncEffectsTimingBeginCommandListReady){
        discardRenderPackets();
        return;
    }
    const Core::Graphics::JobHandle shadowVisibilityRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        shadowVisibilityCommandList,
        &shadowVisibilityCommandListReady,
        asyncShadowSchedule,
        shadowTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*shadowTimingTicketForPacket);
        shadowVisibilityCommandList->open(
            asyncShadowSchedule
                ? &m_shadowComputeInputStateHandoff
                : &m_postGbufferNormalizedStateHandoff
        );
        if(!shadowVisibilityCommandList->hasCommandBuffer())
            return;

        Optional<Core::GpuTimingMeasure> asyncShadowTiming;
        if(asyncShadowSchedule){
            asyncShadowTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncShadow,
                m_graphics.getDevice(),
                *shadowVisibilityCommandList
            );
        }

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

        if(asyncShadowSchedule){
            // The only exclusive cross-lane result: Compute releases after its final write, Graphics final acquires
            // before deferred lighting samples it, then returns it to Compute after composite.
            shadowVisibilityCommandList->releaseTextureOwnership(
                deferredTargets.shadowVisibility.get(),
                ECSRenderDetail::s_ShadowVisibilitySubresources,
                Core::RenderLane::Graphics
            );
        }

        if(asyncShadowTiming){
            asyncShadowTiming->finishTiming(*shadowVisibilityCommandList);
            asyncShadowTiming.reset();
        }

        shadowVisibilityCommandList->close(&m_shadowVisibilityStateHandoff);
        shadowVisibilityCommandListReady =
            m_shadowVisibilityStateHandoff.valid()
            && shadowVisibilityCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle causticsRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        causticsCommandList,
        &causticsCommandListReady,
        asyncSoftwareCausticsSchedule,
        causticsTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*causticsTimingTicketForPacket);
        causticsCommandList->open(
            asyncSoftwareCausticsSchedule
                ? &m_causticsComputeInputStateHandoff
                : &m_postGbufferNormalizedStateHandoff
        );
        if(!causticsCommandList->hasCommandBuffer())
            return;

        // Black is the additive identity for caustics. Keep that valid no-op input even when no refractive scene
        // work was prepared or the selected producer fails to record.
        m_raytracingSystem.clearCausticTargets(*causticsCommandList, deferredTargets);
        if(shadowVisibilityPrepared){
            if(hardwareShadowSupported){
                const bool causticsDispatched = m_raytracingSystem.renderHwCaustics(*causticsCommandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasHwCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic render pass failed"));
            }
            else{
                const bool causticsDispatched = m_raytracingSystem.renderGpuBvhCaustics(*causticsCommandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic render pass failed"));
            }
        }

        if(asyncSoftwareCausticsSchedule){
            // This is the sole software-caustic result that Graphics consumes. The temporal accumulator and resolve
            // scratch remain private to Compute; deferred lighting acquires only the resolved irradiance below.
            causticsCommandList->releaseTextureOwnership(
                deferredTargets.causticIrradiance.get(),
                ECSRenderDetail::s_FramebufferSubresources,
                Core::RenderLane::Graphics
            );
        }

        causticsCommandList->close(&m_causticsStateHandoff);
        causticsCommandListReady =
            m_causticsStateHandoff.valid()
            && causticsCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle surfelGiRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        surfelGiCommandList,
        &surfelGiCommandListReady,
        effectsTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*effectsTimingTicketForPacket);
        surfelGiCommandList->open(&m_postGbufferNormalizedStateHandoff);
        if(!surfelGiCommandList->hasCommandBuffer())
            return;

        // Spawn -> hash build -> trace -> resolve remains one ordered packet, so its persistent surfel buffers never
        // become visible to another worker halfway through their per-frame update.
        if(!m_raytracingSystem.renderSurfelGi(*surfelGiCommandList, deferredTargets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI render pass failed"));

        surfelGiCommandList->close(&m_surfelGiStateHandoff);
        surfelGiCommandListReady =
            m_surfelGiStateHandoff.valid()
            && surfelGiCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle avboitRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        csgFrameState,
        hasTransparentRenderers,
        avboitCommandList,
        &avboitCommandListReady,
        effectsTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*effectsTimingTicketForPacket);
        avboitCommandList->open(&m_postGbufferNormalizedStateHandoff);
        if(!avboitCommandList->hasCommandBuffer())
            return;

        // A no-transparency frame can still need one clear to retire the previous frame's accumulation. Record an
        // otherwise empty, valid packet as well so deferred lighting always imports the four-branch fan-in snapshot.
        if(hasTransparentRenderers || m_avboitState.m_targetsNeedClear){
            m_avboitSystem.clearAvboitTargets(*avboitCommandList, deferredTargets.avboit);
            m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
        }
        if(hasTransparentRenderers)
            m_avboitSystem.renderAvboitPasses(*avboitCommandList, deferredTargets, csgFrameState);

        // Transparent CSG interval construction uses the opaque G-buffer framebuffer, whose automatic attachment
        // tracking temporarily makes normal/world-position render targets and depth a read-only depth attachment.
        // Restore the normalized read inputs before the later deferred-lighting packet records from the merged state.
        // Albedo deliberately remains a render target: deferred lighting transitions it for sampling.
        avboitCommandList->setTextureState(
            deferredTargets.normal.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
        avboitCommandList->setTextureState(
            deferredTargets.worldPosition.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
        avboitCommandList->setTextureState(
            deferredTargets.depth.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );

        avboitCommandList->close(&m_avboitStateHandoff);
        avboitCommandListReady =
            m_avboitStateHandoff.valid()
            && avboitCommandList->hasCommandBuffer()
        ;
    });
    if(
        !shadowVisibilityRecordingJob.valid()
        || !causticsRecordingJob.valid()
        || !surfelGiRecordingJob.valid()
        || !avboitRecordingJob.valid()
    ){
        if(shadowVisibilityRecordingJob.valid())
            m_graphics.waitJob(shadowVisibilityRecordingJob);
        if(causticsRecordingJob.valid())
            m_graphics.waitJob(causticsRecordingJob);
        if(surfelGiRecordingJob.valid())
            m_graphics.waitJob(surfelGiRecordingJob);
        if(avboitRecordingJob.valid())
            m_graphics.waitJob(avboitRecordingJob);
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(shadowVisibilityRecordingJob);
    m_graphics.waitJob(causticsRecordingJob);
    m_graphics.waitJob(surfelGiRecordingJob);
    m_graphics.waitJob(avboitRecordingJob);
    if(!shadowVisibilityCommandListReady || !causticsCommandListReady || !surfelGiCommandListReady || !avboitCommandListReady){
        discardRenderPackets();
        return;
    }

    if(asyncShadowSchedule){
        bool asyncEffectsTimingEndCommandListReady = false;
        {
            Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*effectsTimingTicketForPacket);
            asyncEffectsTimingEndCommandList->open();
            if(asyncEffectsTimingEndCommandList->hasCommandBuffer()){
                if(asyncEffectsTiming){
                    asyncEffectsTiming->finishTiming(*asyncEffectsTimingEndCommandList);
                    asyncEffectsTiming.reset();
                }
                asyncEffectsTimingEndCommandList->close();
                asyncEffectsTimingEndCommandListReady = asyncEffectsTimingEndCommandList->hasCommandBuffer();
            }
        }
        if(!asyncEffectsTimingEndCommandListReady){
            discardRenderPackets();
            return;
        }
    }

    if(asyncShadowSchedule && !m_shadowVisibilityGraphicsStateHandoff.buildTextureSubset(
        m_shadowVisibilityStateHandoff,
        deferredTargets.shadowVisibility.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the async shadow visibility handoff"));
        discardRenderPackets();
        return;
    }
    if(asyncSoftwareCausticsSchedule && !m_causticIrradianceGraphicsStateHandoff.buildTextureSubset(
        m_causticsStateHandoff,
        deferredTargets.causticIrradiance.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the async software-caustic irradiance handoff"));
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* postGbufferBranchStates[] = {
        asyncShadowSchedule ? &m_shadowVisibilityGraphicsStateHandoff : &m_shadowVisibilityStateHandoff,
        asyncSoftwareCausticsSchedule ? &m_causticIrradianceGraphicsStateHandoff : &m_causticsStateHandoff,
        &m_surfelGiStateHandoff,
        &m_avboitStateHandoff,
    };
    if(!m_postGbufferFanInStateHandoff.buildFanIn(
        m_postGbufferNormalizedStateHandoff,
        postGbufferBranchStates,
        4u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: post-G-buffer packet state fan-in failed"));
        discardRenderPackets();
        return;
    }

    // Deferred lighting consumes the ray-effect outputs and follows AVBOIT because transparent CSG can touch shared
    // G-buffer attachments. Record it only after the four-way fan-in has reconciled every sibling packet's state.
    bool deferredLightingCommandListReady = false;
    const Core::Graphics::JobHandle deferredLightingRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        deferredLightingCommandList,
        &deferredLightingCommandListReady,
        &asyncFinalTiming,
        asyncShadowSchedule,
        finalTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*finalTimingTicketForPacket);
        deferredLightingCommandList->open(&m_postGbufferFanInStateHandoff);
        if(!deferredLightingCommandList->hasCommandBuffer())
            return;

        if(asyncShadowSchedule){
            asyncFinalTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncFinal,
                m_graphics.getDevice(),
                *deferredLightingCommandList
            );
            asyncFinalTiming->finishMarker();
        }

        const bool deferredLightingRecorded = m_deferredSystem.renderDeferredLighting(*deferredLightingCommandList, deferredTargets);
        deferredLightingCommandList->close(&m_deferredLightingStateHandoff);
        deferredLightingCommandListReady =
            deferredLightingRecorded
            && m_deferredLightingStateHandoff.valid()
            && deferredLightingCommandList->hasCommandBuffer()
        ;
    });
    if(!deferredLightingRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(deferredLightingRecordingJob);
    if(!deferredLightingCommandListReady){
        discardRenderPackets();
        return;
    }

    bool deferredCompositeCommandListReady = false;
    const Core::Graphics::JobHandle deferredCompositeRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        framebuffer,
        &deferredTargets,
        deferredCompositeCommandList,
        &frameTiming,
        &asyncFrameTiming,
        &asyncFinalTiming,
        &deferredCompositeCommandListReady,
        asyncShadowSchedule,
        asyncSoftwareCausticsSchedule,
        finalTimingTicketForPacket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*finalTimingTicketForPacket);
        deferredCompositeCommandList->open(&m_deferredLightingStateHandoff);
        if(!deferredCompositeCommandList->hasCommandBuffer())
            return;

        const bool deferredCompositeRecorded = m_deferredSystem.renderDeferredComposite(
            *deferredCompositeCommandList,
            deferredTargets,
            framebuffer
        );

        bool asyncFrameTimingEnded = true;
        // This endpoint completes the legacy one-submission frame scope and records the dedicated path's deferred
        // critical-path endpoint. The latter is not published until Graphics final is accepted below.
        if(frameTiming)
            frameTiming->finishTiming(*deferredCompositeCommandList);
        if(asyncShadowSchedule && deferredCompositeRecorded){
            deferredCompositeCommandList->releaseTextureOwnership(
                deferredTargets.shadowVisibility.get(),
                ECSRenderDetail::s_ShadowVisibilitySubresources,
                Core::RenderLane::AsyncCompute
            );
            if(asyncSoftwareCausticsSchedule){
                // Deferred lighting has completed the only Graphics read of the software caustic result. Return the
                // reusable irradiance target to Compute together with its final Graphics submission.
                deferredCompositeCommandList->releaseTextureOwnership(
                    deferredTargets.causticIrradiance.get(),
                    ECSRenderDetail::s_FramebufferSubresources,
                    Core::RenderLane::AsyncCompute
                );
            }
            if(asyncFinalTiming){
                asyncFinalTiming->finishTiming(*deferredCompositeCommandList);
                asyncFinalTiming.reset();
            }
            asyncFrameTimingEnded = asyncFrameTiming.recordEnd(*deferredCompositeCommandList);
        }
        deferredCompositeCommandList->close(&m_deferredCompositeStateHandoff);
        deferredCompositeCommandListReady =
            deferredCompositeRecorded
            && asyncFrameTimingEnded
            && m_deferredCompositeStateHandoff.valid()
            && deferredCompositeCommandList->hasCommandBuffer()
        ;
    });
    if(!deferredCompositeRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(deferredCompositeRecordingJob);
    if(!deferredCompositeCommandListReady){
        discardRenderPackets();
        return;
    }

    if(!asyncShadowSchedule){
        // The unsupported/disabled-lane fallback retains the established one ordered Graphics submission exactly.
        Core::CommandList* commandLists[] = {
            meshViewSetupCommandList,
            sceneShadingSetupCommandList,
            deferredClearCommandList,
            gbufferCommandList,
            postGbufferNormalizeCommandList,
            shadowVisibilityCommandList,
            causticsCommandList,
            surfelGiCommandList,
            avboitCommandList,
            deferredLightingCommandList,
            deferredCompositeCommandList,
        };
        const Core::QueueSubmissionToken fallbackSubmissionToken = renderTimingTicket.submit(
            device,
            commandLists,
            11u,
            Core::RenderLane::Graphics,
            Core::QueueSubmissionDesc{}
        );
        if(!fallbackSubmissionToken.valid()){
            discardRenderPackets();
            return;
        }

        frameTiming.reset();
        m_raytracingSystem.confirmShadowVisibilitySubmission(fallbackSubmissionToken);
        m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
        return;
    }

    const auto retireAsyncFrameTiming = [&](){
        if(!asyncFrameTiming.needsRetirement())
            return;

        asyncFrameTiming.prepareForRecovery();
        shadowOwnershipRecoveryCommandList->open();
        if(!shadowOwnershipRecoveryCommandList->hasCommandBuffer()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record async frame-timing recovery endpoint"));
            asyncFrameTiming.discard();
            return;
        }
        if(!asyncFrameTiming.recordEnd(*shadowOwnershipRecoveryCommandList)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to write async frame-timing recovery endpoint"));
            asyncFrameTiming.discard();
            return;
        }
        shadowOwnershipRecoveryCommandList->close();
        if(!shadowOwnershipRecoveryCommandList->hasCommandBuffer()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to close async frame-timing recovery endpoint"));
            asyncFrameTiming.discard();
            return;
        }

        Core::CommandList* recoveryCommandLists[] = { shadowOwnershipRecoveryCommandList };
        const Core::QueueSubmissionToken recoveryToken = device.executeCommandLists(
            recoveryCommandLists,
            1u,
            Core::RenderLane::Graphics,
            Core::QueueSubmissionDesc{}
        );
        if(!recoveryToken.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: async frame-timing recovery submission was rejected"));
            asyncFrameTiming.discard();
            return;
        }
        if(!asyncFrameTiming.confirmEndSubmission(false)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire async frame-timing recovery query"));
            asyncFrameTiming.discard();
            return;
        }
    };

    const auto recoverAsyncShadowOwnership = [&](
        const Core::QueueSubmissionToken shadowSubmissionToken,
        const bool recoverCausticIrradiance
    ) -> bool {
        if(
            !shadowSubmissionToken.valid()
            || !deferredTargets.shadowVisibility
            || (recoverCausticIrradiance && !deferredTargets.causticIrradiance)
        ){
            asyncFrameTiming.discard();
            return false;
        }

        m_shadowOwnershipRecoveryInputStateHandoff.reset();
        m_shadowOwnershipRecoveryStateHandoff.reset();
        bool recoveryInputReady = false;
        if(recoverCausticIrradiance){
            const Core::CommandListResourceStateHandoff* causticRecoveryBranch[] = {
                &m_causticIrradianceGraphicsStateHandoff,
            };
            recoveryInputReady = m_shadowOwnershipRecoveryInputStateHandoff.buildFanIn(
                m_shadowVisibilityGraphicsStateHandoff,
                causticRecoveryBranch,
                1u
            );
        }
        else{
            recoveryInputReady = m_shadowOwnershipRecoveryInputStateHandoff.buildTextureSubset(
                m_shadowVisibilityGraphicsStateHandoff,
                deferredTargets.shadowVisibility.get()
            );
        }
        if(!recoveryInputReady){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to select async shadow ownership-recovery input"));
            asyncFrameTiming.discard();
            return false;
        }

        shadowOwnershipRecoveryCommandList->open(&m_shadowOwnershipRecoveryInputStateHandoff);
        if(!shadowOwnershipRecoveryCommandList->hasCommandBuffer()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to record async shadow ownership recovery acquire"));
            asyncFrameTiming.discard();
            return false;
        }
        shadowOwnershipRecoveryCommandList->releaseTextureOwnership(
            deferredTargets.shadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::RenderLane::AsyncCompute
        );
        if(recoverCausticIrradiance){
            shadowOwnershipRecoveryCommandList->releaseTextureOwnership(
                deferredTargets.causticIrradiance.get(),
                ECSRenderDetail::s_FramebufferSubresources,
                Core::RenderLane::AsyncCompute
            );
        }
        asyncFrameTiming.prepareForRecovery();
        if(!asyncFrameTiming.recordEnd(*shadowOwnershipRecoveryCommandList)){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to write async shadow ownership-recovery timing endpoint"));
            asyncFrameTiming.discard();
            return false;
        }
        shadowOwnershipRecoveryCommandList->close(&m_shadowOwnershipRecoveryStateHandoff);
        if(
            !m_shadowOwnershipRecoveryStateHandoff.valid()
            || !shadowOwnershipRecoveryCommandList->hasCommandBuffer()
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to close async shadow ownership recovery"));
            asyncFrameTiming.discard();
            return false;
        }

        Core::CommandList* recoveryCommandLists[] = { shadowOwnershipRecoveryCommandList };
        Core::QueueSubmissionDesc recoverySubmitDesc;
        recoverySubmitDesc.setWaitTokens(&shadowSubmissionToken, 1u);
        const Core::QueueSubmissionToken recoveryToken = device.executeCommandLists(
            recoveryCommandLists,
            1u,
            Core::RenderLane::Graphics,
            recoverySubmitDesc
        );
        if(!recoveryToken.valid()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: async shadow ownership recovery submission was rejected"));
            asyncFrameTiming.discard();
            return false;
        }
        if(!asyncFrameTiming.confirmEndSubmission(false)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire async shadow ownership-recovery timing query"));
            asyncFrameTiming.discard();
        }
        if(!m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
            m_shadowOwnershipRecoveryStateHandoff,
            deferredTargets.shadowVisibility.get()
        )){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to retain async shadow ownership recovery state"));
            return false;
        }
        if(
            recoverCausticIrradiance
            && !m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                m_shadowOwnershipRecoveryStateHandoff,
                deferredTargets.causticIrradiance.get()
            )
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to retain async software-caustic ownership recovery state"));
            return false;
        }
        return true;
    };
    const auto failAsyncShadowOwnershipRecovery = [&](){
        m_asyncShadowOwnershipRecoveryFailed = true;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: cannot safely continue after an unresolved async shadow ownership release"));
    };

    // Distinct queues: submit the Graphics producer first, then run shadow plus (when supported) software caustics in
    // one accepted Compute packet. Graphics effects remain independent, and Graphics final waits for both packets.
    Core::CommandList* prefixCommandLists[] = {
        meshViewSetupCommandList,
        sceneShadingSetupCommandList,
        deferredClearCommandList,
        gbufferCommandList,
        postGbufferNormalizeCommandList,
    };
    const Core::QueueSubmissionToken prefixSubmissionToken = prefixTimingTicket.submit(
        device,
        prefixCommandLists,
        5u,
        Core::RenderLane::Graphics,
        Core::QueueSubmissionDesc{}
    );
    if(!prefixSubmissionToken.valid()){
        discardRenderPackets();
        return;
    }
    asyncFrameTiming.confirmBeginSubmission();

    Core::QueueSubmissionDesc shadowSubmitDesc;
    shadowSubmitDesc.setWaitTokens(&prefixSubmissionToken, 1u);
    Core::CommandList* shadowCommandLists[] = {
        shadowVisibilityCommandList,
        causticsCommandList,
    };
    const Core::QueueSubmissionToken shadowSubmissionToken = shadowTimingTicket.submit(
        device,
        shadowCommandLists,
        asyncSoftwareCausticsSchedule ? 2u : 1u,
        Core::RenderLane::AsyncCompute,
        shadowSubmitDesc
    );
    if(!shadowSubmissionToken.valid()){
        effectsTimingTicket.discard();
        finalTimingTicket.discard();
        restoreShadowCpuState();
        restoreEffectsCpuState();
        m_raytracingSystem.discardSoftShadowTemporalHistory();
        m_shadowComputeBaseStateHandoff.reset();
        m_shadowComputeInputStateHandoff.reset();
        m_shadowVisibilityStateHandoff.reset();
        m_shadowVisibilityGraphicsStateHandoff.reset();
        m_causticsComputeBaseStateHandoff.reset();
        m_causticsComputeInputStateHandoff.reset();
        m_causticsStateHandoff.reset();
        m_causticIrradianceGraphicsStateHandoff.reset();
        m_surfelGiStateHandoff.reset();
        m_postGbufferFanInStateHandoff.reset();
        m_deferredLightingStateHandoff.reset();
        m_deferredCompositeStateHandoff.reset();
        m_avboitStateHandoff.reset();
        retireAsyncFrameTiming();
        return;
    }

    // The Compute command buffer is now committed. Retain only its private scratch/history state for the next
    // Compute use: shared G-buffer and scene inputs must always come from this frame's Graphics prefix rather than
    // allowing a prior Compute handoff to overwrite their current state during the next fan-in.
    m_raytracingSystem.confirmShadowVisibilitySubmission(shadowSubmissionToken);
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
        m_shadowVisibilityStateHandoff,
        shadowComputeScratchTextures,
        sizeof(shadowComputeScratchTextures) / sizeof(shadowComputeScratchTextures[0]),
        shadowComputeScratchBuffers,
        sizeof(shadowComputeScratchBuffers) / sizeof(shadowComputeScratchBuffers[0])
    )){
        effectsTimingTicket.discard();
        finalTimingTicket.discard();
        restoreUnacceptedGraphicsEffectsCpuState();
        m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
        static_cast<void>(recoverAsyncShadowOwnership(shadowSubmissionToken, asyncSoftwareCausticsSchedule));
        // Without a retained Compute-side scratch snapshot the next packet cannot safely restore its layouts, even
        // when the visibility/caustic ownership recovery itself succeeds.
        failAsyncShadowOwnershipRecovery();
        return;
    }
    m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);

    if(asyncSoftwareCausticsSchedule){
        Core::Texture* const causticsComputeScratchTextures[] = {
            deferredTargets.causticAccumulator.get(),
            deferredTargets.causticHistory.get(),
            deferredTargets.causticResolveHalf.get(),
            deferredTargets.causticResolveGeometry.get(),
        };
        if(!m_causticsComputePersistentStateHandoff.buildResourceSubset(
            m_causticsStateHandoff,
            causticsComputeScratchTextures,
            sizeof(causticsComputeScratchTextures) / sizeof(causticsComputeScratchTextures[0]),
            nullptr,
            0u
        )){
            effectsTimingTicket.discard();
            finalTimingTicket.discard();
            restoreGraphicsEffectsCpuState();
            // Both Compute outputs are now accepted. Return each released result before suspending the renderer, so
            // validation and teardown never encounter a stranded queue-family owner.
            static_cast<void>(recoverAsyncShadowOwnership(shadowSubmissionToken, true));
            failAsyncShadowOwnershipRecovery();
            return;
        }
    }

    Core::CommandList* effectsCommandListsWithCaustics[] = {
        asyncEffectsTimingBeginCommandList,
        causticsCommandList,
        surfelGiCommandList,
        avboitCommandList,
        asyncEffectsTimingEndCommandList,
    };
    Core::CommandList* effectsCommandListsWithoutCaustics[] = {
        asyncEffectsTimingBeginCommandList,
        surfelGiCommandList,
        avboitCommandList,
        asyncEffectsTimingEndCommandList,
    };
    Core::CommandList* const* effectsCommandLists = asyncSoftwareCausticsSchedule
        ? effectsCommandListsWithoutCaustics
        : effectsCommandListsWithCaustics
    ;
    const Core::QueueSubmissionToken effectsSubmissionToken = effectsTimingTicket.submit(
        device,
        effectsCommandLists,
        asyncSoftwareCausticsSchedule ? 4u : 5u,
        Core::RenderLane::Graphics,
        Core::QueueSubmissionDesc{}
    );
    if(!effectsSubmissionToken.valid()){
        finalTimingTicket.discard();
        restoreUnacceptedGraphicsEffectsCpuState();
        if(!recoverAsyncShadowOwnership(shadowSubmissionToken, asyncSoftwareCausticsSchedule))
            failAsyncShadowOwnershipRecovery();
        return;
    }

    const Core::QueueSubmissionToken finalWaitTokens[] = {
        shadowSubmissionToken,
        effectsSubmissionToken,
    };
    Core::QueueSubmissionDesc finalSubmitDesc;
    finalSubmitDesc.setWaitTokens(finalWaitTokens, 2u);
    Core::CommandList* finalCommandLists[] = {
        deferredLightingCommandList,
        deferredCompositeCommandList,
    };
    const Core::QueueSubmissionToken finalSubmissionToken = finalTimingTicket.submit(
        device,
        finalCommandLists,
        2u,
        Core::RenderLane::Graphics,
        finalSubmitDesc
    );
    if(!finalSubmissionToken.valid()){
        if(!recoverAsyncShadowOwnership(shadowSubmissionToken, asyncSoftwareCausticsSchedule))
            failAsyncShadowOwnershipRecovery();
        return;
    }
    if(!asyncFrameTiming.confirmEndSubmission(true)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm async frame critical-path timing"));
        asyncFrameTiming.discard();
    }

    const bool shadowVisibilityReturnStateReady = m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
        m_deferredCompositeStateHandoff,
        deferredTargets.shadowVisibility.get()
    );
    const bool causticIrradianceReturnStateReady =
        !asyncSoftwareCausticsSchedule
        || m_causticIrradianceReturnStateHandoff.buildTextureSubset(
            m_deferredCompositeStateHandoff,
            deferredTargets.causticIrradiance.get()
        )
    ;
    if(!shadowVisibilityReturnStateReady || !causticIrradianceReturnStateReady){
        // The Graphics release has already been accepted, so do not guess a next-frame owner if retaining its state
        // fails unexpectedly. The current device must be rebuilt before recording another async packet.
        failAsyncShadowOwnershipRecovery();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

