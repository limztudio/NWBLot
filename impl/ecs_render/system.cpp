// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize RendererSystem::MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    Core::CoreDetail::HashCombine(seed, static_cast<u32>(key.pass));
    Core::CoreDetail::HashCombine(seed, key.twoSided ? 1u : 0u);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.depthFormat);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleCount);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleQuality);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        Core::CoreDetail::HashCombine(seed, format);

    return seed;
}

bool RendererSystem::MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return lhs.material == rhs.material && lhs.pass == rhs.pass && lhs.twoSided == rhs.twoSided && lhs.framebufferInfo == rhs.framebufferInfo;
}


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
    , m_meshMeshes(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_materialSurfaceInfos(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_materialPipelines(0, MaterialPipelineKeyHasher(), MaterialPipelineKeyEqualTo(), arena)
    , m_materialInstanceMutableCache(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_loggedMaterialPaths(0, Hasher<Name>(), EqualTo<Name>(), arena)
{
    readAccess<NWB::Impl::Scene::ActiveCameraComponent>();
    readAccess<NWB::Impl::Scene::TransformComponent>();
    readAccess<NWB::Impl::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    readAccess<MaterialInstanceComponent>();
}
RendererSystem::~RendererSystem(){}


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
    pruneMaterialInstanceMutableCache();
}

bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;

    if(m_deferredTargets.valid() && m_deferredTargets.width == width && m_deferredTargets.height == height)
        return true;

    return createDeferredFrameTargets(width, height);
}

void RendererSystem::invalidateResources(){
    m_meshMeshes.clear();
    m_materialPipelines.clear();
    m_materialInstanceMutableCache.clear();
    m_loggedMaterialPaths.clear();

    m_meshBindingLayout.reset();
    m_computeBindingLayout.reset();
    m_emulationViewBindingLayout.reset();
    m_deferredLightingBindingLayout.reset();
    m_deferredCompositeBindingLayout.reset();
    m_avboitEmptyBindingLayout.reset();
    m_avboitOccupancyBindingLayout.reset();
    m_avboitDepthWarpBindingLayout.reset();
    m_avboitExtinctionBindingLayout.reset();
    m_avboitIntegrateBindingLayout.reset();
    m_avboitAccumulateBindingLayout.reset();
    m_deferredSampler.reset();
    m_avboitLinearSampler.reset();
    m_instanceBuffer.reset();
    m_materialTypedBuffer.reset();
    m_meshViewBuffer.reset();
    m_sceneShadingBuffer.reset();
    m_emulationViewBindingSet.reset();
    m_emulationVertexShader.reset();
    m_deferredCompositeVertexShader.reset();
    m_deferredLightingPixelShader.reset();
    m_deferredCompositePixelShader.reset();
    m_avboitOccupancyPixelShader.reset();
    m_avboitDepthWarpComputeShader.reset();
    m_avboitExtinctionPixelShader.reset();
    m_avboitIntegrateComputeShader.reset();
    m_avboitAccumulatePixelShader.reset();
    m_emulationInputLayout.reset();
    m_deferredLightingPipeline.reset();
    m_deferredCompositePipeline.reset();
    m_avboitDepthWarpPipeline.reset();
    m_avboitIntegratePipeline.reset();
    resetDeferredFrameTargets();

    m_instanceBufferCapacity = 0u;
    m_materialTypedBufferCapacity = 0u;
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return;

    pruneRuntimeMeshResources();

    if(!m_deferredTargets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredTargets;

    auto* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create render command list"));
        return;
    }
    commandList->open();

    clearDeferredTargets(*commandList, deferredTargets);

    Core::Alloc::ScratchArena scratchArena;
    MaterialPassDrawItemVector opaqueMeshDrawItems{scratchArena};
    MaterialPassDrawItemVector opaqueComputeDrawItems{scratchArena};
    InstanceGpuDataVector instanceData{scratchArena};
    ECSRenderDetail::MaterialTypedInstanceRangeCollector materialTypedRanges{scratchArena};
    MaterialTypedByteDataVector materialTypedBytes{scratchArena};

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(deferredTargets.framebuffer->getFramebufferInfo());
    const bool meshViewReady = updateMeshViewBuffer(*commandList, meshViewAspectRatio);
    const bool sceneShadingReady = updateSceneShadingBuffer(*commandList, meshViewAspectRatio);
    if(meshViewReady && sceneShadingReady){
        gatherMaterialPassDrawItems(
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            false,
            opaqueMeshDrawItems,
            opaqueComputeDrawItems,
            instanceData,
            materialTypedRanges,
            materialTypedBytes
        );
    }

    const bool hasDeferredDrawItems = !opaqueMeshDrawItems.empty() || !opaqueComputeDrawItems.empty();
    const bool deferredUploadReady =
        hasDeferredDrawItems
        && uploadMaterialPassDrawBuffers(
            *commandList,
            instanceData,
            materialTypedRanges,
            materialTypedBytes
        )
    ;
    if(deferredUploadReady){
        const MaterialPassDrawContext opaqueDrawContext{
            *commandList,
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            nullptr,
            nullptr,
            deferredViewportState
        };
        renderMeshMaterialPassDrawItems(opaqueDrawContext, opaqueMeshDrawItems);
        renderComputeMaterialPassDrawItems(opaqueDrawContext, opaqueComputeDrawItems);
    }
    commandList->endRenderPass();

    if(!renderDeferredLighting(*commandList, deferredTargets)){
        commandList->close();
        return;
    }

    clearAvboitTargets(*commandList, deferredTargets.avboit);
    if(hasTransparentRenderers())
        renderAvboitPasses(*commandList, deferredTargets);

    commandList->setResourceStatesForBindingSet(deferredTargets.compositeBindingSet.get());
    commandList->commitBarriers();
    if(!renderDeferredComposite(*commandList, deferredTargets, framebuffer)){
        commandList->close();
        return;
    }

    commandList->close();
    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

