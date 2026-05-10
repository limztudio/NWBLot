// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createDeferredFrameTargets(const u32 width, const u32 height){
    if(width == 0 || height == 0){
        resetDeferredFrameTargets();
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();
    const Core::Format::Enum albedoFormat = __hidden_ecs_render::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum normalFormat = __hidden_ecs_render::SelectGBufferVectorFormat(*device);
    const Core::Format::Enum worldPositionFormat = __hidden_ecs_render::SelectGBufferVectorFormat(*device);
    const Core::Format::Enum opaqueColorFormat = __hidden_ecs_render::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum depthFormat = __hidden_ecs_render::SelectGBufferDepthFormat(*device);
    const Core::Format::Enum avboitLowRasterFormat = SelectRendererAvboitLowRasterFormat(*device);
    const Core::Format::Enum avboitAccumColorFormat = SelectRendererAvboitAccumColorFormat(*device);
    const Core::Format::Enum avboitAccumExtinctionFormat = SelectRendererAvboitAccumExtinctionFormat(*device);
    const Core::Format::Enum avboitTransmittanceFormat = SelectRendererAvboitTransmittanceFormat(*device);
    if(
        albedoFormat == Core::Format::UNKNOWN
        || normalFormat == Core::Format::UNKNOWN
        || worldPositionFormat == Core::Format::UNKNOWN
        || opaqueColorFormat == Core::Format::UNKNOWN
        || depthFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported deferred framebuffer formats"));
        return false;
    }
    if(
        avboitLowRasterFormat == Core::Format::UNKNOWN
        || avboitAccumColorFormat == Core::Format::UNKNOWN
        || avboitAccumExtinctionFormat == Core::Format::UNKNOWN
        || avboitTransmittanceFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    if(!createDeferredLightingResources())
        return false;
    if(!createDeferredCompositeResources())
        return false;
    if(!createAvboitResources())
        return false;

    resetDeferredFrameTargets();
    m_materialPipelines.clear();
    m_deferredLightingPipeline.reset();
    m_deferredCompositePipeline.reset();

    DeferredFrameTargets createdTargets;
    createdTargets.width = width;
    createdTargets.height = height;
    createdTargets.albedoFormat = albedoFormat;
    createdTargets.normalFormat = normalFormat;
    createdTargets.worldPositionFormat = worldPositionFormat;
    createdTargets.opaqueColorFormat = opaqueColorFormat;
    createdTargets.depthFormat = depthFormat;

    Core::TextureDesc albedoDesc;
    albedoDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.albedoFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/gbuffer_albedo")
        .setClearValue(__hidden_ecs_render::s_ClearColor)
    ;
    createdTargets.albedo = m_graphics.createTexture(albedoDesc);
    if(!createdTargets.albedo){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred albedo target"));
        return false;
    }

    Core::TextureDesc normalDesc;
    normalDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.normalFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/gbuffer_normal")
        .setClearValue(Core::Color(0.5f, 0.5f, 1.f, 1.f))
    ;
    createdTargets.normal = m_graphics.createTexture(normalDesc);
    if(!createdTargets.normal){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred normal target"));
        return false;
    }

    Core::TextureDesc worldPositionDesc;
    worldPositionDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.worldPositionFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/gbuffer_world_position")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 1.f))
    ;
    createdTargets.worldPosition = m_graphics.createTexture(worldPositionDesc);
    if(!createdTargets.worldPosition){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred world-position target"));
        return false;
    }

    Core::TextureDesc opaqueColorDesc;
    opaqueColorDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.opaqueColorFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/opaque_color")
        .setClearValue(__hidden_ecs_render::s_ClearColor)
    ;
    createdTargets.opaqueColor = m_graphics.createTexture(opaqueColorDesc);
    if(!createdTargets.opaqueColor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred opaque color target"));
        return false;
    }

    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.depthFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/depth")
    ;
    createdTargets.depth = m_graphics.createTexture(depthDesc);
    if(!createdTargets.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred depth target"));
        return false;
    }

    Core::FramebufferDesc framebufferDesc;
    framebufferDesc
        .addColorAttachment(createdTargets.albedo.get(), __hidden_ecs_render::s_FramebufferSubresources)
        .addColorAttachment(createdTargets.normal.get(), __hidden_ecs_render::s_FramebufferSubresources)
        .addColorAttachment(createdTargets.worldPosition.get(), __hidden_ecs_render::s_FramebufferSubresources)
        .setDepthAttachment(createdTargets.depth.get(), __hidden_ecs_render::s_FramebufferSubresources)
    ;
    createdTargets.framebuffer = device->createFramebuffer(framebufferDesc);
    if(!createdTargets.framebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return false;
    }

    Core::FramebufferDesc opaqueLightingFramebufferDesc;
    opaqueLightingFramebufferDesc.addColorAttachment(createdTargets.opaqueColor.get(), __hidden_ecs_render::s_FramebufferSubresources);
    createdTargets.opaqueLightingFramebuffer = device->createFramebuffer(opaqueLightingFramebufferDesc);
    if(!createdTargets.opaqueLightingFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting framebuffer"));
        return false;
    }

    if(!createAvboitFrameTargets(
        createdTargets,
        avboitLowRasterFormat,
        avboitAccumColorFormat,
        avboitAccumExtinctionFormat,
        avboitTransmittanceFormat
    ))
        return false;

    Core::BindingSetDesc lightingBindingSetDesc;
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.albedo.get(),
        createdTargets.albedoFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        1,
        createdTargets.normal.get(),
        createdTargets.normalFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        2,
        createdTargets.worldPosition.get(),
        createdTargets.worldPositionFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        3,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Sampler(4, m_deferredSampler.get()));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(5, m_sceneShadingBuffer.get()));
    createdTargets.lightingBindingSet = device->createBindingSet(lightingBindingSetDesc, m_deferredLightingBindingLayout);
    if(!createdTargets.lightingBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding set"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.opaqueColor.get(),
        createdTargets.opaqueColorFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        1,
        createdTargets.avboit.accumColor.get(),
        createdTargets.avboit.accumColorFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        2,
        createdTargets.avboit.accumExtinction.get(),
        createdTargets.avboit.accumExtinctionFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(3, m_deferredSampler.get()));
    createdTargets.compositeBindingSet = device->createBindingSet(bindingSetDesc, m_deferredCompositeBindingLayout);
    if(!createdTargets.compositeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding set"));
        return false;
    }

    m_deferredTargets = Move(createdTargets);
    if(!createDeferredLightingPipeline(m_deferredTargets)){
        resetDeferredFrameTargets();
        return false;
    }

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, normal {}, world position {}, opaque color {}, depth {}, AVBOIT color {}, extinction {}, transmittance {})")
        , m_deferredTargets.width
        , m_deferredTargets.height
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.albedoFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.normalFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.worldPositionFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.opaqueColorFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.depthFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumColorFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumExtinctionFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.transmittanceFormat).name)
    );
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createDeferredCompositeResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_deferredCompositeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(3, 1));

        m_deferredCompositeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_deferredCompositeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding layout"));
            return false;
        }
    }

    if(!__hidden_ecs_render::CreatePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create deferred composite sampler")))
        return false;

    if(!loadShader(
        m_deferredCompositeVertexShader,
        __hidden_ecs_render::s_DeferredCompositeVertexShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Vertex,
        "ECSRender_DeferredCompositeVS"
    ))
        return false;

    if(!loadShader(
        m_deferredCompositePixelShader,
        __hidden_ecs_render::s_DeferredCompositePixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredCompositePS"
    ))
        return false;

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createDeferredCompositePipeline(Core::IFramebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;

    if(!createDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer->getFramebufferInfo();
    if(m_deferredCompositePipeline && m_deferredCompositePipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(m_deferredCompositeVertexShader)
        .setPixelShader(m_deferredCompositePixelShader)
        .setRenderState(__hidden_ecs_render::BuildCompositeRenderState())
        .addBindingLayout(m_deferredCompositeBindingLayout)
    ;

    Core::IDevice* device = m_graphics.getDevice();
    m_deferredCompositePipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredCompositePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite pipeline"));
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::clearDeferredTargets(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    if(targets.albedo){
        commandList.setTextureState(targets.albedo.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.normal){
        commandList.setTextureState(targets.normal.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.worldPosition){
        commandList.setTextureState(targets.worldPosition.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.opaqueColor){
        commandList.setTextureState(targets.opaqueColor.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.depth){
        commandList.setTextureState(targets.depth.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.commitBarriers();

    if(targets.albedo){
        commandList.clearTextureFloat(targets.albedo.get(), __hidden_ecs_render::s_FramebufferSubresources, __hidden_ecs_render::s_ClearColor);
    }

    if(targets.normal){
        commandList.clearTextureFloat(targets.normal.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::Color(0.5f, 0.5f, 1.f, 1.f));
    }

    if(targets.worldPosition){
        commandList.clearTextureFloat(targets.worldPosition.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 1.f));
    }

    if(targets.opaqueColor){
        commandList.clearTextureFloat(targets.opaqueColor.get(), __hidden_ecs_render::s_FramebufferSubresources, __hidden_ecs_render::s_ClearColor);
    }

    if(targets.depth){
        commandList.clearDepthStencilTexture(
            targets.depth.get(),
            __hidden_ecs_render::s_FramebufferSubresources,
            true,
            Core::s_DepthClearValue,
            false,
            0
        );
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::renderDeferredComposite(Core::ICommandList& commandList, DeferredFrameTargets& targets, Core::IFramebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;
    if(!targets.compositeBindingSet)
        return false;
    if(!m_deferredCompositePipeline && !createDeferredCompositePipeline(presentationFramebuffer))
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredCompositePipeline.get());
    graphicsState.setFramebuffer(presentationFramebuffer);
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(targets.compositeBindingSet.get());

    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
