// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include <impl/assets/graphics/csg/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgIntervalPeelPushConstants{
    u32 frameWidth = 0u;
    u32 frameHeight = 0u;
    u32 receiverCount = 0u;
    u32 layerCount = 0u;
};

static_assert(sizeof(CsgIntervalPeelPushConstants) == NWB_CSG_INTERVAL_PEEL_PUSH_CONSTANT_BYTE_SIZE, "CSG interval peel push constants must match shader layout");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateIntervalPeelBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CSG_INTERVAL_BINDING_CAP_NORMAL, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CSG_INTERVAL_BINDING_DEPTH, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CSG_INTERVAL_BINDING_ID, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}

[[nodiscard]] static bool CreateIntervalSampleBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_CAP_NORMAL, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_DEPTH, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_ID, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_RECEIVER_FRONT_SURFACE_MASK, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_RECEIVER_SURFACE_MASK, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_RECEIVER_BACK_SURFACE_MASK, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}

[[nodiscard]] static bool CreateReceiverSurfaceBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_CAP_NORMAL, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_DEPTH, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_INTERVAL_BINDING_ID, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CSG_INTERVAL_BINDING_RECEIVER_FRONT_SURFACE_MASK, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CSG_INTERVAL_BINDING_RECEIVER_SURFACE_MASK, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CSG_INTERVAL_BINDING_RECEIVER_BACK_SURFACE_MASK, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}

[[nodiscard]] static bool CreateIntervalPeelBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::Buffer* meshViewBuffer,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;
    if(!targets.csgCapNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !meshViewBuffer || !layout)
        return false;

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
    Core::BindingSetDesc bindingSetDesc(arena);
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CSG_INTERVAL_BINDING_CAP_NORMAL,
        targets.csgCapNormal.get(),
        targets.csgCapNormalFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CSG_INTERVAL_BINDING_DEPTH,
        targets.csgIntervalDepth.get(),
        targets.csgIntervalDepthFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CSG_INTERVAL_BINDING_ID,
        targets.csgIntervalId.get(),
        targets.csgIntervalIdFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, meshViewBuffer));

    bindingSet = device.createBindingSet(bindingSetDesc, layout);
    return bindingSet != nullptr;
}

[[nodiscard]] static bool CreateIntervalSampleBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::Buffer* meshViewBuffer,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;
    if(!targets.csgCapNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !targets.csgReceiverFrontSurfaceMask || !targets.csgReceiverSurfaceMask || !targets.csgReceiverBackSurfaceMask || !meshViewBuffer || !layout)
        return false;

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
    Core::BindingSetDesc bindingSetDesc(arena);
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_CAP_NORMAL,
        targets.csgCapNormal.get(),
        targets.csgCapNormalFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_DEPTH,
        targets.csgIntervalDepth.get(),
        targets.csgIntervalDepthFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_ID,
        targets.csgIntervalId.get(),
        targets.csgIntervalIdFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_RECEIVER_FRONT_SURFACE_MASK,
        targets.csgReceiverFrontSurfaceMask.get(),
        targets.csgReceiverFrontSurfaceMaskFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_RECEIVER_SURFACE_MASK,
        targets.csgReceiverSurfaceMask.get(),
        targets.csgReceiverSurfaceMaskFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_RECEIVER_BACK_SURFACE_MASK,
        targets.csgReceiverBackSurfaceMask.get(),
        targets.csgReceiverBackSurfaceMaskFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, meshViewBuffer));

    bindingSet = device.createBindingSet(bindingSetDesc, layout);
    return bindingSet != nullptr;
}

[[nodiscard]] static bool CreateReceiverSurfaceBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::Buffer* meshViewBuffer,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;
    if(!targets.csgCapNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !targets.csgReceiverFrontSurfaceMask || !targets.csgReceiverSurfaceMask || !targets.csgReceiverBackSurfaceMask || !meshViewBuffer || !layout)
        return false;

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
    Core::BindingSetDesc bindingSetDesc(arena);
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_CAP_NORMAL,
        targets.csgCapNormal.get(),
        targets.csgCapNormalFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_DEPTH,
        targets.csgIntervalDepth.get(),
        targets.csgIntervalDepthFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_INTERVAL_BINDING_ID,
        targets.csgIntervalId.get(),
        targets.csgIntervalIdFormat,
        csgPeelSubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CSG_INTERVAL_BINDING_RECEIVER_FRONT_SURFACE_MASK,
        targets.csgReceiverFrontSurfaceMask.get(),
        targets.csgReceiverFrontSurfaceMaskFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CSG_INTERVAL_BINDING_RECEIVER_SURFACE_MASK,
        targets.csgReceiverSurfaceMask.get(),
        targets.csgReceiverSurfaceMaskFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CSG_INTERVAL_BINDING_RECEIVER_BACK_SURFACE_MASK,
        targets.csgReceiverBackSurfaceMask.get(),
        targets.csgReceiverBackSurfaceMaskFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, meshViewBuffer));

    bindingSet = device.createBindingSet(bindingSetDesc, layout);
    return bindingSet != nullptr;
}

[[nodiscard]] static bool CreateIntervalPeelPipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& intervalBindingLayout,
    const Core::BindingLayoutHandle& clipBindingLayout
){
    if(pipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(intervalBindingLayout)
        .addBindingLayout(clipBindingLayout)
    ;

    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}

[[nodiscard]] static bool CreateIntervalCapFillPipeline(
    Core::Device& device,
    Core::GraphicsPipelineHandle& pipeline,
    const Core::ShaderHandle& vertexShader,
    const Core::ShaderHandle& pixelShader,
    const Core::BindingLayoutHandle& intervalSampleBindingLayout,
    const Core::FramebufferInfo& framebufferInfo
){
    if(pipeline && pipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(pixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(intervalSampleBindingLayout)
    ;

    pipeline = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    return pipeline != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgIntervalPeelResources(DeferredFrameTargets& targets){
    auto* device = graphics().getDevice();
    if(!createCsgClipResources())
        return false;
    if(!csgState().m_clipBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a CSG clip binding layout"));
        return false;
    }
    if(!drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a mesh view buffer"));
        return false;
    }
    if(!targets.csgCapNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !targets.csgReceiverFrontSurfaceMask || !targets.csgReceiverSurfaceMask || !targets.csgReceiverBackSurfaceMask || targets.csgPeelLayerCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires valid peel targets"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalPeelBindingLayout(
        arena(),
        *device,
        csgState().m_intervalPeelBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalSampleBindingLayout(
        arena(),
        *device,
        csgState().m_intervalSampleBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateReceiverSurfaceBindingLayout(
        arena(),
        *device,
        csgState().m_receiverSurfaceBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver surface binding layout"));
        return false;
    }

    if(!csgState().m_intervalPeelComputeShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_intervalPeelComputeShader,
            AssetsGraphicsCsg::s_IntervalPeelComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_CsgIntervalPeelCS"
        ))
            return false;
    }

    if(!m_renderer.shaderSystem().loadDeferredCompositeVertexShader())
        return false;

    if(!csgState().m_intervalCapFillPixelShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_intervalCapFillPixelShader,
            AssetsGraphicsCsg::s_IntervalCapFillPixelShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Pixel,
            "ECSRender_CsgIntervalCapFillPS"
        ))
            return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalPeelPipeline(
        *device,
        csgState().m_intervalPeelPipeline,
        csgState().m_intervalPeelComputeShader,
        csgState().m_intervalPeelBindingLayout,
        csgState().m_clipBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel pipeline"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalCapFillPipeline(
        *device,
        csgState().m_intervalCapFillPipeline,
        deferredState().m_compositeVertexShader,
        csgState().m_intervalCapFillPixelShader,
        csgState().m_intervalSampleBindingLayout,
        targets.framebuffer->getFramebufferInfo()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval cap fill pipeline"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalPeelBindingSet(
        arena(),
        *device,
        targets,
        drawState().m_meshViewBuffer.get(),
        csgState().m_intervalPeelBindingLayout.get(),
        csgState().m_intervalPeelBindingSet
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding set"));
        return false;
    }

    if(!createCsgIntervalSampleResources(targets))
        return false;

    return true;
}

bool RendererCsgSystem::createCsgIntervalSampleResources(DeferredFrameTargets& targets){
    auto* device = graphics().getDevice();
    if(!drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires a mesh view buffer"));
        return false;
    }
    if(!targets.csgCapNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !targets.csgReceiverFrontSurfaceMask || !targets.csgReceiverSurfaceMask || !targets.csgReceiverBackSurfaceMask || targets.csgPeelLayerCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires valid peel targets"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalSampleBindingLayout(
        arena(),
        *device,
        csgState().m_intervalSampleBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateReceiverSurfaceBindingLayout(
        arena(),
        *device,
        csgState().m_receiverSurfaceBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver surface binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateReceiverSurfaceBindingSet(
        arena(),
        *device,
        targets,
        drawState().m_meshViewBuffer.get(),
        csgState().m_receiverSurfaceBindingLayout.get(),
        csgState().m_receiverSurfaceBindingSet
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver surface binding set"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalSampleBindingSet(
        arena(),
        *device,
        targets,
        drawState().m_meshViewBuffer.get(),
        csgState().m_intervalSampleBindingLayout.get(),
        csgState().m_intervalSampleBindingSet
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample binding set"));
        return false;
    }

    return true;
}

void RendererCsgSystem::destroyCsgIntervalPeelBindingSet(){
    csgState().m_intervalPeelBindingSet.reset();
}

void RendererCsgSystem::dispatchCsgIntervalPeels(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(csgState().m_intervalPeelPipeline);
    NWB_ASSERT(csgState().m_intervalPeelBindingSet);
    NWB_ASSERT(csgState().m_clipBindingSet);

    commandList.setResourceStatesForBindingSet(csgState().m_intervalPeelBindingSet.get());
    commandList.setResourceStatesForBindingSet(csgState().m_clipBindingSet.get());
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(csgState().m_intervalPeelPipeline.get());
    computeState.addBindingSet(csgState().m_intervalPeelBindingSet.get());
    computeState.addBindingSet(csgState().m_clipBindingSet.get());
    commandList.setComputeState(computeState);

    __hidden_csg_interval_peel::CsgIntervalPeelPushConstants pushConstants;
    pushConstants.frameWidth = targets.width;
    pushConstants.frameHeight = targets.height;
    pushConstants.receiverCount = static_cast<u32>(csgFrameData.receiverRanges.size());
    pushConstants.layerCount = Min(targets.csgPeelLayerCount, static_cast<u32>(NWB_CSG_PEEL_LAYER_COUNT));
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X)),
        DivideUp(targets.height, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y)),
        1u
    );
}

void RendererCsgSystem::renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets){
    NWB_ASSERT(csgState().m_intervalCapFillPipeline);
    NWB_ASSERT(csgState().m_intervalSampleBindingSet);
    NWB_ASSERT(targets.framebuffer);

    commandList.setResourceStatesForBindingSet(csgState().m_intervalSampleBindingSet.get());
    commandList.commitBarriers();

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(targets.framebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(csgState().m_intervalCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.framebuffer.get());
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(csgState().m_intervalSampleBindingSet.get());
    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
