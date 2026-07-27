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
    m_gbufferCommandList.reset();
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
    NWB_ASSERT(m_postGbufferCommandList);
    NWB_ASSERT(m_shadowPrepareCommandList);

    auto& device = *m_graphics.getDevice();

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
    if(!traceMaterialContextUploaded || !m_shadowPrepareStateHandoff.valid())
        return false;

    Core::CommandList* shadowPrepareCommandLists[] = { m_shadowPrepareCommandList.get() };
    if(device.executeCommandLists(shadowPrepareCommandLists, 1) == 0u){
        m_shadowPrepareStateHandoff.reset();
        return false;
    }

    return true;
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

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto* device = m_graphics.getDevice();
    Core::CommandList* gbufferCommandList = m_gbufferCommandList.get();
    Core::CommandList* postGbufferCommandList = m_postGbufferCommandList.get();
    Core::CommandList* commandList = gbufferCommandList;
    NWB_ASSERT(commandList);
    NWB_ASSERT(postGbufferCommandList);
    commandList->open(&m_shadowPrepareStateHandoff);

    // Reset every GPU-timing query pool on the device timeline now, while the command buffer has no render pass
    // open yet (vkCmdResetQueryPool is illegal inside a dynamic render pass). This makes every pool defined before
    // the timestamp writes below, so the validation layer never reports a first-use "query not reset" for the
    // per-pass timers. collect() already read back and cleared last frame's results before this frame's render.
    m_graphics.gpuTiming().recordFrameReset(*commandList);

    bool commandListReady = true;
    {
        Core::GpuTimingMeasure frameTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_Frame, device, *commandList);

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

        // The opaque/G-buffer producer and every post-G-buffer consumer remain ordered on Graphics, but use
        // distinct primary command buffers. Capture the actual final tracked state only after close has appended
        // keepInitialState restores; the next list imports that snapshot before recording its first barrier.
        // This is intentionally sequential for now: it establishes the cross-buffer state contract that worker
        // recording will use before we move any pass to another CPU thread.
        frameTiming.finishMarker();
        commandList->close(&m_gbufferStateHandoff);
        if(!m_gbufferStateHandoff.valid()){
            frameTiming.discardTiming();
            return;
        }

        commandList = postGbufferCommandList;
        commandList->open(&m_gbufferStateHandoff);

        const bool hardwareShadowSupported =
            m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
            && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
        ;

        bool shadowVisibilityWritten = false;
        if(m_preparedShadowVisibilityReady && hardwareShadowSupported){
            // renderShadowVisibility casts the opaque shadow through the half-res soft denoise chain and, when
            // softTransparentShadowReady is true, folds colored transparent shadow into that same chain. Transparent shadow
            // stays on the software Moeller-Trumbore path: HW RayQuery and SW traversal can disagree by +/-1 crossing at
            // grazing silhouettes, which corrupts the colored chord even though the binary opaque test is unaffected.
            shadowVisibilityWritten = m_raytracingSystem.renderShadowVisibility(*commandList, deferredTargets);
            if(!shadowVisibilityWritten)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
            // Hybrid multiply fallback: if the soft transparent fold did not run but the scene still has a transparent
            // occluder with a ready SW BVH, run the transparent software trace as a second pass and multiply it onto the
            // opaque mask. Skip it when renderShadowVisibility already folded the colored shadow.
            else if(!m_raytracingSystem.softTransparentShadowReady() && m_raytracingSystem.hybridTransparentShadowReady()){
                if(!m_raytracingSystem.renderGpuBvhShadowVisibility(*commandList, deferredTargets, true))
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow pass failed"));
            }
        }
        else if(m_preparedShadowVisibilityReady){
            // No hardware ray tracing: trace the same per-light occlusion against the software scene/instance
            // BVH prepared earlier this frame.
            shadowVisibilityWritten = m_raytracingSystem.renderGpuBvhShadowVisibility(*commandList, deferredTargets);
        }
        // The deferred lighting pass always samples the visibility image; clear it to "all lit" on any frame neither shadow backend wrote it so lighting never reads stale or undefined occlusion.
        if(!shadowVisibilityWritten)
            m_raytracingSystem.clearShadowVisibility(*commandList, deferredTargets);

        // The deferred lighting pass always samples the caustic irradiance image (additive). Clear the caustic
        // targets to BLACK unconditionally so the additive term is a no-op and the buffers are always a valid black
        // input -- the inverse of the shadow buffer's all-lit white default. The producer below overwrites them only
        // when there is caustic work.
        m_raytracingSystem.clearCausticTargets(*commandList, deferredTargets);

        // Caustic producer -- EXACTLY ONE backend runs per frame, mirroring the shadow backend split above: the
        // hardware ray-traced producer on the HW path, the software-BVH producer otherwise. Both emit
        // photons into the just-cleared R32_UINT accumulators, then resolve them into the RGBA16F irradiance buffer
        // the lighting pass adds pre-tonemap. Each runs only when there is >=1 caustic light AND >=1 refractive
        // instance (has*CausticWork, checked inside); else the black-cleared buffer is the additive no-op. Runs
        // BEFORE renderDeferredLighting so the lighting read sees the resolve.
        if(m_preparedShadowVisibilityReady){
            // A false result with no caustic work is the common no-op. If work was present, preserve the black-cleared
            // fallback while surfacing that the producer or resolve pass could not be recorded.
            if(hardwareShadowSupported){
                const bool causticsDispatched = m_raytracingSystem.renderHwCaustics(*commandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasHwCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic render pass failed"));
            }
            else{
                const bool causticsDispatched = m_raytracingSystem.renderGpuBvhCaustics(*commandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic render pass failed"));
            }
        }

        // Surfel GI render hook: the spawn -> hash-build -> trace passes run between the caustic producer and the
        // deferred lighting, so the lighting gather sees this frame's integrated surfel irradiance. Inert (returns
        // true without dispatching) until m_surfelEnabled is set once the SW scene BVH is resident.
        if(!m_raytracingSystem.renderSurfelGi(*commandList, deferredTargets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI render pass failed"));

        commandListReady = m_deferredSystem.renderDeferredLighting(*commandList, deferredTargets);
        if(commandListReady){
            const bool hasTransparentRenderers = m_preparedHasTransparentRenderers;
            if(hasTransparentRenderers || m_avboitState.m_targetsNeedClear){
                m_avboitSystem.clearAvboitTargets(*commandList, deferredTargets.avboit);
                m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
            }
            if(hasTransparentRenderers)
                m_avboitSystem.renderAvboitPasses(*commandList, deferredTargets, csgFrameState);

            commandListReady = m_deferredSystem.renderDeferredComposite(*commandList, deferredTargets, framebuffer);
        }

        // Vulkan timestamps may span command buffers as long as submission order is fixed. The debug marker closed
        // above remains local to the producer command buffer; this ending timestamp preserves the full frame metric.
        frameTiming.finishTiming(*commandList);
    }

    commandList->close();
    if(!commandListReady)
        return;

    Core::CommandList* commandLists[] = { gbufferCommandList, postGbufferCommandList };
    device->executeCommandLists(commandLists, 2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

