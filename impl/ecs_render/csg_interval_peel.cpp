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


enum class CsgTextureAccess{
    SRV,
    UAV
};

static void AddCsgTextureBindingLayoutItem(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    u32 slot,
    CsgTextureAccess access
){
    switch(access){
    case CsgTextureAccess::SRV:
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(slot, 1));
        break;
    case CsgTextureAccess::UAV:
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(slot, 1));
        break;
    }
}

static void AddCsgTextureBindingSetItem(
    Core::BindingSetDesc& bindingSetDesc,
    u32 slot,
    Core::Texture* texture,
    Core::Format::Enum format,
    const Core::TextureSubresourceSet& subresources,
    CsgTextureAccess access
){
    switch(access){
    case CsgTextureAccess::SRV:
        bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
            slot,
            texture,
            format,
            subresources,
            Core::TextureDimension::Texture2DArray
        ));
        break;
    case CsgTextureAccess::UAV:
        bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
            slot,
            texture,
            format,
            subresources,
            Core::TextureDimension::Texture2DArray
        ));
        break;
    }
}

static void AddCsgIntervalTargetLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_CAP_BACK_NORMAL, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_DEPTH, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_ID, access);
}

static void AddCsgReceiverMaskLayoutItems(
    Core::BindingLayoutDesc& bindingLayoutDesc,
    CsgTextureAccess access
){
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_SURFACE_MASK, access);
    AddCsgTextureBindingLayoutItem(bindingLayoutDesc, NWB_CSG_INTERVAL_BINDING_RECEIVER_BACK_SURFACE_MASK, access);
}

static void AddCsgIntervalTargetBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& subresources,
    CsgTextureAccess access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_CAP_BACK_NORMAL,
        targets.csgCapBackNormal.get(),
        targets.csgCapNormalFormat,
        subresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_DEPTH,
        targets.csgIntervalDepth.get(),
        targets.csgIntervalDepthFormat,
        subresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_ID,
        targets.csgIntervalId.get(),
        targets.csgIntervalIdFormat,
        subresources,
        access
    );
}

static void AddCsgReceiverMaskBindingSetItems(
    Core::BindingSetDesc& bindingSetDesc,
    const DeferredFrameTargets& targets,
    const Core::TextureSubresourceSet& subresources,
    CsgTextureAccess access
){
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_SURFACE_MASK,
        targets.csgReceiverSurfaceMask.get(),
        targets.csgReceiverSurfaceMaskFormat,
        subresources,
        access
    );
    AddCsgTextureBindingSetItem(
        bindingSetDesc,
        NWB_CSG_INTERVAL_BINDING_RECEIVER_BACK_SURFACE_MASK,
        targets.csgReceiverBackSurfaceMask.get(),
        targets.csgReceiverBackSurfaceMaskFormat,
        subresources,
        access
    );
}

[[nodiscard]] static bool CreateCsgIntervalBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    Core::ShaderType::Mask visibility,
    CsgTextureAccess intervalAccess,
    bool includeReceiverMasks,
    CsgTextureAccess receiverMaskAccess
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(visibility);
    AddCsgIntervalTargetLayoutItems(bindingLayoutDesc, intervalAccess);
    if(includeReceiverMasks)
        AddCsgReceiverMaskLayoutItems(bindingLayoutDesc, receiverMaskAccess);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}

[[nodiscard]] static bool CreateCsgIntervalBindingSet(
    Core::GraphicsArena& arena,
    Core::Device& device,
    const DeferredFrameTargets& targets,
    Core::Buffer* meshViewBuffer,
    Core::BindingLayout* layout,
    Core::BindingSetHandle& bindingSet,
    CsgTextureAccess intervalAccess,
    bool includeReceiverMasks,
    CsgTextureAccess receiverMaskAccess
){
    if(bindingSet)
        return true;
    if(!targets.csgCapBackNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !meshViewBuffer || !layout)
        return false;
    if(includeReceiverMasks && (!targets.csgReceiverSurfaceMask || !targets.csgReceiverBackSurfaceMask))
        return false;

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
    const Core::TextureSubresourceSet csgReceiverSurfaceSubresources(0, 1, 0, ECSRenderDetail::s_CsgReceiverSurfaceLayerCount);
    Core::BindingSetDesc bindingSetDesc(arena);
    AddCsgIntervalTargetBindingSetItems(bindingSetDesc, targets, csgPeelSubresources, intervalAccess);
    if(includeReceiverMasks)
        AddCsgReceiverMaskBindingSetItems(bindingSetDesc, targets, csgReceiverSurfaceSubresources, receiverMaskAccess);
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, meshViewBuffer));

    bindingSet = device.createBindingSet(bindingSetDesc, layout);
    return bindingSet != nullptr;
}

[[nodiscard]] static bool CreateIntervalSampleBindingLayouts(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& intervalSampleBindingLayout,
    Core::BindingLayoutHandle& receiverSurfaceBindingLayout
){
    if(!CreateCsgIntervalBindingLayout(
        arena,
        device,
        intervalSampleBindingLayout,
        Core::ShaderType::Pixel,
        CsgTextureAccess::SRV,
        true,
        CsgTextureAccess::SRV
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample binding layout"));
        return false;
    }

    if(!CreateCsgIntervalBindingLayout(
        arena,
        device,
        receiverSurfaceBindingLayout,
        Core::ShaderType::Pixel,
        CsgTextureAccess::SRV,
        true,
        CsgTextureAccess::UAV
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver surface binding layout"));
        return false;
    }

    return true;
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
    const Core::BindingLayoutHandle& clipBindingLayout,
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
        .addBindingLayout(clipBindingLayout)
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
    if(!targets.csgCapBackNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !targets.csgReceiverSurfaceMask || !targets.csgReceiverBackSurfaceMask || targets.csgPeelLayerCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires valid peel targets"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingLayout(
        arena(),
        *device,
        csgState().m_intervalPeelBindingLayout,
        Core::ShaderType::Compute,
        __hidden_csg_interval_peel::CsgTextureAccess::UAV,
        false,
        __hidden_csg_interval_peel::CsgTextureAccess::SRV
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding layout"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalSampleBindingLayouts(
        arena(),
        *device,
        csgState().m_intervalSampleBindingLayout,
        csgState().m_receiverSurfaceBindingLayout
    ))
        return false;

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
        csgState().m_clipBindingLayout,
        targets.framebuffer->getFramebufferInfo()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval cap fill pipeline"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingSet(
        arena(),
        *device,
        targets,
        drawState().m_meshViewBuffer.get(),
        csgState().m_intervalPeelBindingLayout.get(),
        csgState().m_intervalPeelBindingSet,
        __hidden_csg_interval_peel::CsgTextureAccess::UAV,
        false,
        __hidden_csg_interval_peel::CsgTextureAccess::SRV
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
    if(!targets.csgCapBackNormal || !targets.csgIntervalDepth || !targets.csgIntervalId || !targets.csgReceiverSurfaceMask || !targets.csgReceiverBackSurfaceMask || targets.csgPeelLayerCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires valid peel targets"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateIntervalSampleBindingLayouts(
        arena(),
        *device,
        csgState().m_intervalSampleBindingLayout,
        csgState().m_receiverSurfaceBindingLayout
    ))
        return false;

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingSet(
        arena(),
        *device,
        targets,
        drawState().m_meshViewBuffer.get(),
        csgState().m_receiverSurfaceBindingLayout.get(),
        csgState().m_receiverSurfaceBindingSet,
        __hidden_csg_interval_peel::CsgTextureAccess::SRV,
        true,
        __hidden_csg_interval_peel::CsgTextureAccess::UAV
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver surface binding set"));
        return false;
    }

    if(!__hidden_csg_interval_peel::CreateCsgIntervalBindingSet(
        arena(),
        *device,
        targets,
        drawState().m_meshViewBuffer.get(),
        csgState().m_intervalSampleBindingLayout.get(),
        csgState().m_intervalSampleBindingSet,
        __hidden_csg_interval_peel::CsgTextureAccess::SRV,
        true,
        __hidden_csg_interval_peel::CsgTextureAccess::SRV
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
    NWB_ASSERT(csgState().m_clipBindingSet);
    NWB_ASSERT(targets.framebuffer);

    commandList.setResourceStatesForBindingSet(csgState().m_intervalSampleBindingSet.get());
    commandList.setResourceStatesForBindingSet(csgState().m_clipBindingSet.get());
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(targets.framebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(csgState().m_intervalCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.framebuffer.get());
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(csgState().m_intervalSampleBindingSet.get());
    graphicsState.addBindingSet(csgState().m_clipBindingSet.get());
    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
