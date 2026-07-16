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

    if(!m_materialSystem.createMeshShaderResources())
        return false;

    if(!m_graphics.queryFeatureSupport(Core::Feature::Meshlets)){
        if(!m_materialSystem.createComputeEmulationResources())
            return false;

        if(!m_materialSystem.createEmulationViewBindingLayout())
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
    m_renderCommandList.reset();
    m_shadowPrepareCommandList.reset();
    m_meshState.invalidateResources();
    m_materialState.invalidateResources();
    m_drawState.invalidateResources();
    m_csgState.invalidateResources();
    m_deferredState.invalidateResources();
    m_avboitState.invalidateResources();
    m_rayTracingState.invalidateResources();
}

void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
    m_materialSystem.pruneMaterialInstanceMutableCache();
}

bool RendererSystem::ensureFrameCommandLists(){
    auto& device = *m_graphics.getDevice();

    if(!m_renderCommandList){
        m_renderCommandList = device.createCommandList();
        if(!m_renderCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create render command list"));
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
        const Name* scopeName;
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
        if(!m_graphics.gpuTiming().prepareScopeQueries(*reservation.scopeName, &device, reservation.queryCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare GPU timing scope '{}'"), reservation.scopeName->c_str());
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

    if(!m_renderCommandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: render command list was not validated"));
        return false;
    }

    if(!m_shadowPrepareCommandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow preparation command list was not validated"));
        return false;
    }

    auto& device = *m_graphics.getDevice();

    m_shadowPrepareCommandList->open();
    // Surfel GI resources are prepared inside prepareShadowVisibilityResources, after the ray-tracing scene
    // structures are resident, so the producer can run on the same frame without startup latency.
    const bool shadowResourcesPrepared = m_raytracingSystem.prepareShadowVisibilityResources(
        *m_shadowPrepareCommandList,
        deferredTargets,
        scratchArena,
        m_preparedShadowVisibilityReady
    );
    m_shadowPrepareCommandList->close();
    if(!shadowResourcesPrepared)
        return false;

    Core::CommandList* shadowPrepareCommandLists[] = { m_shadowPrepareCommandList.get() };
    device.executeCommandLists(shadowPrepareCommandLists, 1);

    return true;
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return;

    if(!m_deferredState.m_targets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    if(!m_preparedCsgFrameStateValid)
        return;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasCsgFrameWork = !csgFrameState.empty();
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    if(hasCsgFrameWork && !deferredTargets.csgIntervalTargetsValid())
        return;
    auto* device = m_graphics.getDevice();
    Core::CommandList* commandList = m_renderCommandList.get();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: render command list was not prepared"));
        return;
    }
    commandList->open();

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
        // hardware ray-traced producer (P4) on the HW path, the software-BVH producer (P3) otherwise. Both emit
        // photons into the just-cleared R32_UINT accumulators, then resolve them into the RGBA16F irradiance buffer
        // the lighting pass adds pre-tonemap. Each runs only when there is >=1 caustic light AND >=1 refractive
        // instance (has*CausticWork, checked inside); else the black-cleared buffer is the additive no-op. Runs
        // BEFORE renderDeferredLighting so the lighting read sees the resolve.
        if(m_preparedShadowVisibilityReady){
            // false = no caustic work this frame (the common case: no caustic light or no refractive instance) or a
            // pipeline-build failure already logged inside; either way the black-cleared buffer is the additive no-op.
            if(hardwareShadowSupported){
                [[maybe_unused]] const bool causticsDispatched = m_raytracingSystem.renderHwCaustics(*commandList, deferredTargets);
            }
            else{
                [[maybe_unused]] const bool causticsDispatched = m_raytracingSystem.renderGpuBvhCaustics(*commandList, deferredTargets);
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

            commandList->setResourceStatesForBindingSet(deferredTargets.compositeBindingSet.get());
            commandList->commitBarriers();
            commandListReady = m_deferredSystem.renderDeferredComposite(*commandList, deferredTargets, framebuffer);
        }
    }

    commandList->close();
    if(!commandListReady)
        return;

    Core::CommandList* commandLists[] = { commandList };
    device->executeCommandLists(commandLists, 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

