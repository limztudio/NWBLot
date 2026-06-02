// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "renderer_private.h"


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
    , m_meshState(arena)
    , m_materialState(arena)
{
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


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
    pruneMaterialInstanceMutableCache();
}

bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;

    if(m_deferredState.m_targets.valid() && m_deferredState.m_targets.width == width && m_deferredState.m_targets.height == height)
        return true;

    return createDeferredFrameTargets(width, height);
}

void RendererSystem::invalidateResources(){
    m_meshState.invalidateResources();
    m_materialState.invalidateResources();
    m_drawState.invalidateResources();
    m_csgState.invalidateResources();
    m_deferredState.invalidateResources();
    m_avboitState.invalidateResources();
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return;

    pruneRuntimeMeshResources();

    if(!m_deferredState.m_targets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    auto* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create render command list"));
        return;
    }
    commandList->open();

    clearDeferredTargets(*commandList, deferredTargets);

    Core::Alloc::ScratchArena scratchArena;
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
    const bool meshViewReady = updateMeshViewBuffer(*commandList, meshViewAspectRatio);
    const bool sceneShadingReady = updateSceneShadingBuffer(*commandList, meshViewAspectRatio);
    if(meshViewReady && sceneShadingReady){
        gatherMaterialPassDrawItems(
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            false,
            opaqueDrawItems,
            instanceData,
            csgFrameData,
#if defined(NWB_DEBUG)
            materialTypedRanges,
#endif
            materialTypedBytes
        );
    }

    const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
    const bool deferredUploadReady =
        hasDeferredDrawItems
        && uploadMaterialPassDrawBuffers(
            *commandList,
            instanceData,
#if defined(NWB_DEBUG)
            materialTypedRanges,
#endif
            materialTypedBytes
        )
    ;
    if(deferredUploadReady){
        const bool csgUploadReady = opaqueDrawItems.csg.empty() || uploadCsgFrameBuffers(*commandList, csgFrameData);
        const MaterialPassDrawContext opaqueDrawContext{
            *commandList,
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            nullptr,
            nullptr,
            deferredViewportState
        };
        renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.regular);
        if(csgUploadReady)
            renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
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

