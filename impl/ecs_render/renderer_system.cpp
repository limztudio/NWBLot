// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize RendererSystem::MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    Core::CoreDetail::HashCombine(seed, static_cast<u32>(key.pass));
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.depthFormat);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleCount);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleQuality);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        Core::CoreDetail::HashCombine(seed, format);

    return seed;
}

bool RendererSystem::MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return lhs.material == rhs.material && lhs.pass == rhs.pass && lhs.framebufferInfo == rhs.framebufferInfo;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(
    Core::Alloc::CustomArena& arena,
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
    , m_geometryMeshes(0, Hasher<Name>(), EqualTo<Name>(), GeometryResourcesMapAllocator(arena))
    , m_materialSurfaceInfos(0, Hasher<Name>(), EqualTo<Name>(), MaterialSurfaceInfoMapAllocator(arena))
    , m_materialPipelines(0, MaterialPipelineKeyHasher(), MaterialPipelineKeyEqualTo(), MaterialPipelineMapAllocator(arena))
    , m_loggedMaterialPaths(0, Hasher<Name>(), EqualTo<Name>(), LoggedMaterialPathMapAllocator(arena))
{
    readAccess<NWB::Core::Scene::SceneComponent>();
    readAccess<NWB::Core::Scene::TransformComponent>();
    readAccess<NWB::Core::Scene::CameraComponent>();
    readAccess<RendererComponent>();
}
RendererSystem::~RendererSystem(){}


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
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
    m_geometryMeshes.clear();
    m_materialPipelines.clear();
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
    m_materialParameterBuffer.reset();
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
    m_materialParameterBufferCapacity = 0u;
}

void RendererSystem::render(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return;

    pruneRuntimeGeometryResources();

    if(!m_deferredTargets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredTargets;

    Core::IDevice* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create render command list"));
        return;
    }
    commandList->open();

    clearDeferredTargets(*commandList, deferredTargets);

    Core::Alloc::ScratchArena<> scratchArena;
    MaterialPassDrawItemVector opaqueMeshDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector opaqueComputeDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    InstanceGpuDataVector instanceData{Core::Alloc::ScratchAllocator<InstanceGpuData>(scratchArena)};
    MaterialParameterGpuDataVector materialParameters{Core::Alloc::ScratchAllocator<MaterialParameterGpuData>(scratchArena)};

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

    const Core::FramebufferInfoEx& meshViewFramebufferInfo = deferredTargets.framebuffer->getFramebufferInfo();
    f32 meshViewAspectRatio = 1.0f;
    if(meshViewFramebufferInfo.width != 0 && meshViewFramebufferInfo.height != 0)
        meshViewAspectRatio = static_cast<f32>(meshViewFramebufferInfo.width) / static_cast<f32>(meshViewFramebufferInfo.height);
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
            materialParameters
        );
    }

    const bool hasDeferredDrawItems =
        !opaqueMeshDrawItems.empty()
        || !opaqueComputeDrawItems.empty()
    ;
    const bool deferredUploadReady =
        hasDeferredDrawItems
        && uploadInstanceBuffer(*commandList, instanceData)
        && uploadMaterialParameterBuffer(*commandList, materialParameters)
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
    Core::ICommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

