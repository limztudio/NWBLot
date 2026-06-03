// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "renderer_capacity_private.h"
#include "timing_names.h"

#include <impl/assets/graphics/csg/binding_slots.h>
#include <impl/assets/graphics/csg/constants.h>
#include <impl/assets/graphics/mesh/binding_slots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_proxy_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateProxyPipeline(
    Core::Device& device,
    Core::Framebuffer* framebuffer,
    Core::ShaderHandle& meshShader,
    Core::ShaderHandle& pixelShader,
    Core::BindingLayoutHandle& capProxyBindingLayout,
    Core::BindingLayoutHandle& clipBindingLayout,
    Core::BindingLayoutHandle& capProxyOpeningMaskBindingLayout,
    Core::MeshletPipelineHandle& pipeline
){
    if(pipeline)
        return true;
    if(!framebuffer)
        return false;

    Core::MeshletPipelineDesc pipelineDesc;
    pipelineDesc
        .setMeshShader(meshShader)
        .setPixelShader(pixelShader)
        .setRenderState(ECSRenderDetail::BuildCsgCapProxyRenderState())
        .addBindingLayout(capProxyBindingLayout)
        .addBindingLayout(clipBindingLayout)
        .addBindingLayout(capProxyOpeningMaskBindingLayout)
    ;

    pipeline = device.createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(pipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy meshlet pipeline"));
    return false;
}

[[nodiscard]] static bool HasProxyShape(const u32 shapeMask, const u32 shapeBit){
    return (shapeMask & shapeBit) != 0u;
}

template<typename ShaderSystem>
[[nodiscard]] static bool LoadProxyMeshShader(
    ShaderSystem& shaderSystem,
    Core::ShaderHandle& meshShader,
    const Name& shaderName,
    const char* debugName,
    const bool needed
){
    if(!needed || meshShader)
        return true;
    return shaderSystem.loadShader(
        meshShader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Mesh,
        debugName
    );
}

[[nodiscard]] static bool CreateProxyPipelineIfNeeded(
    Core::Device& device,
    Core::Framebuffer* framebuffer,
    Core::ShaderHandle& meshShader,
    Core::ShaderHandle& pixelShader,
    Core::BindingLayoutHandle& capProxyBindingLayout,
    Core::BindingLayoutHandle& clipBindingLayout,
    Core::BindingLayoutHandle& capProxyOpeningMaskBindingLayout,
    Core::MeshletPipelineHandle& pipeline,
    const bool needed
){
    return !needed || CreateProxyPipeline(device, framebuffer, meshShader, pixelShader, capProxyBindingLayout, clipBindingLayout, capProxyOpeningMaskBindingLayout, pipeline);
}

static void RenderProxyShape(
    Core::Graphics& graphics,
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData,
    const CsgReceiverPass::Enum receiverPass,
    Core::BindingSet* capProxyBindingSet,
    Core::BindingSet* clipBindingSet,
    Core::BindingSet* capProxyOpeningMaskBindingSet,
    Core::MeshletPipeline* pipeline
){
    if(!pipeline)
        return;

    Core::MeshletState meshletState;
    meshletState.setPipeline(pipeline);
    meshletState.setFramebuffer(context.framebuffer);
    meshletState.setViewport(context.viewportState);
    meshletState.addBindingSet(capProxyBindingSet);
    meshletState.addBindingSet(clipBindingSet);
    meshletState.addBindingSet(capProxyOpeningMaskBindingSet);
    context.commandList.setMeshletState(meshletState);

    const ECSRenderDetail::ShaderDrivenPushConstants pushConstants = ECSRenderDetail::BuildShaderDrivenPushConstants(
        static_cast<u32>(csgFrameData.capProxyGpuItems.size()),
        static_cast<u32>(receiverPass),
        0u,
        context.viewportState,
        0u
    );
    context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    {
        Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, graphics.getDevice(), context.commandList);

        context.commandList.dispatchMesh(static_cast<u32>(csgFrameData.capProxyGpuItems.size()));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgCapProxyOpeningMaskResources(Core::Texture* openingMask){
    auto* device = graphics().getDevice();
    if(!csgState().m_capProxyOpeningMaskBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_BINDING_CAP_PROXY_OPENING_MASK, 1));

        csgState().m_capProxyOpeningMaskBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!csgState().m_capProxyOpeningMaskBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy opening mask binding layout"));
            return false;
        }
    }

    if(csgState().m_capProxyOpeningMaskBindingSet)
        return true;
    if(!openingMask){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG cap proxy opening mask resources require a valid opening mask target"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_BINDING_CAP_PROXY_OPENING_MASK,
        openingMask,
        openingMask->getDescription().format,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    csgState().m_capProxyOpeningMaskBindingSet = device->createBindingSet(bindingSetDesc, csgState().m_capProxyOpeningMaskBindingLayout);
    if(!csgState().m_capProxyOpeningMaskBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy opening mask binding set"));
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgCapProxyResources(Core::Framebuffer* framebuffer, const u32 shapeMask){
    if(!framebuffer)
        return false;
    if(shapeMask == 0u)
        return true;
    if(!createCsgClipResources())
        return false;
    if(!csgState().m_capProxyOpeningMaskBindingLayout)
        return false;

    auto* device = graphics().getDevice();
    if(!device->queryFeatureSupport(Core::Feature::Meshlets))
        return true;

    if(!csgState().m_capProxyPixelShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_capProxyPixelShader,
            ECSRenderDetail::s_CsgCapProxyPixelShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Pixel,
            "ECSRender_CsgCapProxyPS"
        ))
            return false;
    }

    if(!csgState().m_capProxyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Mesh);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CAP_PROXIES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        csgState().m_capProxyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!csgState().m_capProxyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy binding layout"));
            return false;
        }
    }

    auto& shaderSystem = m_renderer.shaderSystem();
    if(!__hidden_csg_cap_proxy_resources::LoadProxyMeshShader(
        shaderSystem,
        csgState().m_capProxyPlaneMeshShader,
        ECSRenderDetail::s_CsgCapProxyPlaneMeshShaderName,
        "ECSRender_CsgCapProxyPlaneMS",
        __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyPlaneShapeMask)
    ))
        return false;
    if(!__hidden_csg_cap_proxy_resources::LoadProxyMeshShader(
        shaderSystem,
        csgState().m_capProxyBoxMeshShader,
        ECSRenderDetail::s_CsgCapProxyBoxMeshShaderName,
        "ECSRender_CsgCapProxyBoxMS",
        __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyBoxShapeMask)
    ))
        return false;
    if(!__hidden_csg_cap_proxy_resources::LoadProxyMeshShader(
        shaderSystem,
        csgState().m_capProxySphereMeshShader,
        ECSRenderDetail::s_CsgCapProxySphereMeshShaderName,
        "ECSRender_CsgCapProxySphereMS",
        __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxySphereShapeMask)
    ))
        return false;
    if(!__hidden_csg_cap_proxy_resources::LoadProxyMeshShader(
        shaderSystem,
        csgState().m_capProxyCapsuleMeshShader,
        ECSRenderDetail::s_CsgCapProxyCapsuleMeshShaderName,
        "ECSRender_CsgCapProxyCapsuleMS",
        __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyCapsuleShapeMask)
    ))
        return false;

    Core::Device& pipelineDevice = *device;
    return
        __hidden_csg_cap_proxy_resources::CreateProxyPipelineIfNeeded(
            pipelineDevice,
            framebuffer,
            csgState().m_capProxyPlaneMeshShader,
            csgState().m_capProxyPixelShader,
            csgState().m_capProxyBindingLayout,
            csgState().m_clipBindingLayout,
            csgState().m_capProxyOpeningMaskBindingLayout,
            csgState().m_capProxyPlanePipeline,
            __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyPlaneShapeMask)
        )
        && __hidden_csg_cap_proxy_resources::CreateProxyPipelineIfNeeded(
            pipelineDevice,
            framebuffer,
            csgState().m_capProxyBoxMeshShader,
            csgState().m_capProxyPixelShader,
            csgState().m_capProxyBindingLayout,
            csgState().m_clipBindingLayout,
            csgState().m_capProxyOpeningMaskBindingLayout,
            csgState().m_capProxyBoxPipeline,
            __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyBoxShapeMask)
        )
        && __hidden_csg_cap_proxy_resources::CreateProxyPipelineIfNeeded(
            pipelineDevice,
            framebuffer,
            csgState().m_capProxySphereMeshShader,
            csgState().m_capProxyPixelShader,
            csgState().m_capProxyBindingLayout,
            csgState().m_clipBindingLayout,
            csgState().m_capProxyOpeningMaskBindingLayout,
            csgState().m_capProxySpherePipeline,
            __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxySphereShapeMask)
        )
        && __hidden_csg_cap_proxy_resources::CreateProxyPipelineIfNeeded(
            pipelineDevice,
            framebuffer,
            csgState().m_capProxyCapsuleMeshShader,
            csgState().m_capProxyPixelShader,
            csgState().m_capProxyBindingLayout,
            csgState().m_clipBindingLayout,
            csgState().m_capProxyOpeningMaskBindingLayout,
            csgState().m_capProxyCapsulePipeline,
            __hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyCapsuleShapeMask)
        )
    ;
}

bool RendererCsgSystem::reserveCsgCapProxyBufferCapacity(const usize proxyCount){
    if(proxyCount == 0u)
        return true;
    if(csgState().m_capProxyBuffer && csgState().m_capProxyBufferCapacity >= proxyCount)
        return true;
    if(proxyCount > Limit<usize>::s_Max / sizeof(CsgCapProxyGpuData))
        return false;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(csgState().m_capProxyBufferCapacity, proxyCount);
    if(capacity > Limit<usize>::s_Max / sizeof(CsgCapProxyGpuData))
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(CsgCapProxyGpuData)))
        .setStructStride(sizeof(CsgCapProxyGpuData))
        .setDebugName(ECSRenderDetail::s_CsgCapProxyBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = graphics().createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy buffer"));
        return false;
    }

    csgState().m_capProxyBuffer = Move(createdBuffer);
    csgState().m_capProxyBufferCapacity = capacity;
    csgState().m_capProxyBindingSet.reset();
    return true;
}

bool RendererCsgSystem::uploadCsgCapProxies(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasCapProxyWork())
        return true;
    if(csgFrameData.capProxyGpuItems.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(!reserveCsgCapProxyBufferCapacity(csgFrameData.capProxyGpuItems.size()))
        return false;
    if(!csgState().m_capProxyBindingLayout || !drawState().m_meshViewBuffer)
        return false;
    if(!csgState().m_capProxyBindingSet){
        Core::BindingSetDesc bindingSetDesc(arena());
        bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CAP_PROXIES, csgState().m_capProxyBuffer.get()));
        bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, drawState().m_meshViewBuffer.get()));
        csgState().m_capProxyBindingSet = graphics().getDevice()->createBindingSet(bindingSetDesc, csgState().m_capProxyBindingLayout);
        if(!csgState().m_capProxyBindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy binding set"));
            return false;
        }
    }

    commandList.setBufferState(csgState().m_capProxyBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        csgState().m_capProxyBuffer.get(),
        csgFrameData.capProxyGpuItems.data(),
        csgFrameData.capProxyGpuItems.size() * sizeof(CsgCapProxyGpuData)
    );
    commandList.setBufferState(csgState().m_capProxyBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

void RendererCsgSystem::renderCsgOpaqueCapProxies(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData,
    Core::Texture* openingMaskTarget
){
    if(!csgFrameData.hasCapProxyWork())
        return;
    if(!csgState().m_capProxyBuffer)
        return;
    if(!openingMaskTarget)
        return;
    const u32 shapeMask = csgFrameData.capProxyShapeMask;
    const bool proxyResourcesReady =
        csgState().m_capProxyBindingLayout
        && csgState().m_clipBindingLayout
        && csgState().m_capProxyOpeningMaskBindingLayout
        && csgState().m_capProxyPixelShader
        && (!__hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyPlaneShapeMask) || csgState().m_capProxyPlanePipeline)
        && (!__hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyBoxShapeMask) || csgState().m_capProxyBoxPipeline)
        && (!__hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxySphereShapeMask) || csgState().m_capProxySpherePipeline)
        && (!__hidden_csg_cap_proxy_resources::HasProxyShape(shapeMask, s_CsgCapProxyCapsuleShapeMask) || csgState().m_capProxyCapsulePipeline)
    ;
    if(!proxyResourcesReady)
        return;
    if(!csgState().m_clipBindingSet)
        return;
    if(!csgState().m_capProxyOpeningMaskBindingSet)
        return;
    if(!csgState().m_capProxyBindingSet)
        return;

    context.commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    context.commandList.setBufferState(csgState().m_capProxyBuffer.get(), Core::ResourceStates::ShaderResource);
    context.commandList.setTextureState(openingMaskTarget, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    setCsgClipBufferStates(context.commandList);
    context.commandList.setResourceStatesForBindingSet(csgState().m_capProxyBindingSet.get());
    context.commandList.setResourceStatesForBindingSet(csgState().m_clipBindingSet.get());
    context.commandList.setResourceStatesForBindingSet(csgState().m_capProxyOpeningMaskBindingSet.get());
    context.commandList.commitBarriers();

    auto renderShape = [&](const u32 shapeMask, Core::MeshletPipeline* pipeline){
        if(!__hidden_csg_cap_proxy_resources::HasProxyShape(csgFrameData.capProxyShapeMask, shapeMask))
            return;

        __hidden_csg_cap_proxy_resources::RenderProxyShape(
            graphics(),
            context,
            csgFrameData,
            CsgReceiverPass::Opaque,
            csgState().m_capProxyBindingSet.get(),
            csgState().m_clipBindingSet.get(),
            csgState().m_capProxyOpeningMaskBindingSet.get(),
            pipeline
        );
    };

    renderShape(s_CsgCapProxyPlaneShapeMask, csgState().m_capProxyPlanePipeline.get());
    renderShape(s_CsgCapProxyBoxShapeMask, csgState().m_capProxyBoxPipeline.get());
    renderShape(s_CsgCapProxySphereShapeMask, csgState().m_capProxySpherePipeline.get());
    renderShape(s_CsgCapProxyCapsuleShapeMask, csgState().m_capProxyCapsulePipeline.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
