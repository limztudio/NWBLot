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
    , m_csgShapeRegistry(arena)
    , m_meshState(arena)
    , m_materialState(arena)
    , m_csgState(arena)
    , m_shaderSystem(*this)
    , m_meshSystem(*this)
    , m_materialSystem(*this)
    , m_csgSystem(*this)
    , m_deferredSystem(*this)
    , m_avboitSystem(*this)
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


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
    m_materialSystem.pruneMaterialInstanceMutableCache();
}

bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;

    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;
    if(deferredTargets.valid() && deferredTargets.width == width && deferredTargets.height == height)
        return true;

    return m_deferredSystem.createDeferredFrameTargets(width, height);
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

    m_meshSystem.pruneRuntimeMeshResources();

    if(!m_deferredState.m_targets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    Core::Alloc::ScratchArena scratchArena;
    const CsgFrameState csgFrameState = HasCsgFrameCandidates(m_world)
        ? m_csgSystem.buildFrameState(scratchArena)
        : CsgFrameState{}
    ;
    const bool hasCsgFrameWork = !csgFrameState.empty();
    if(hasCsgFrameWork && !deferredTargets.csgOpeningMask)
        return;

    auto* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create render command list"));
        return;
    }
    commandList->open();

    m_deferredSystem.clearDeferredTargets(*commandList, deferredTargets, hasCsgFrameWork);

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
    const bool meshViewReady = m_materialSystem.updateMeshViewBuffer(*commandList, meshViewAspectRatio);
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
            materialTypedBytes
        );
    }

    const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
    const bool deferredUploadReady =
        hasDeferredDrawItems
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
        const bool csgUploadReady = opaqueDrawItems.csg.empty() || m_csgSystem.uploadCsgFrameBuffers(*commandList, csgFrameData);
        const MaterialPassDrawContext opaqueDrawContext{
            *commandList,
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            nullptr,
            nullptr,
            deferredViewportState
        };
        m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.regular);
        if(csgUploadReady){
            m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
        }
    }
    commandList->endRenderPass();

    if(!m_deferredSystem.renderDeferredLighting(*commandList, deferredTargets)){
        commandList->close();
        return;
    }

    const bool hasTransparentRenderers = m_materialSystem.hasTransparentRenderers();
    if(hasTransparentRenderers || m_avboitState.m_targetsNeedClear){
        m_avboitSystem.clearAvboitTargets(*commandList, deferredTargets.avboit);
        m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
    }
    if(hasTransparentRenderers)
        m_avboitSystem.renderAvboitPasses(*commandList, deferredTargets, csgFrameState);

    commandList->setResourceStatesForBindingSet(deferredTargets.compositeBindingSet.get());
    commandList->commitBarriers();
    if(!m_deferredSystem.renderDeferredComposite(*commandList, deferredTargets, framebuffer)){
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

