// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::resetAvboitFrameTargets(AvboitFrameTargets& targets){
    targets.occupancyBindingSet.reset();
    targets.depthWarpBindingSet.reset();
    targets.extinctionBindingSet.reset();
    targets.integrateBindingSet.reset();
    targets.accumulateBindingSet.reset();

    targets.lowFramebuffer.reset();
    targets.accumulationFramebuffer.reset();

    targets.lowRasterTarget.reset();
    targets.accumColor.reset();
    targets.accumExtinction.reset();
    targets.transmittanceTexture.reset();

    targets.coverageBuffer.reset();
    targets.depthWarpBuffer.reset();
    targets.controlBuffer.reset();
    targets.extinctionBuffer.reset();
    targets.extinctionOverflowBuffer.reset();

    targets = AvboitFrameTargets{};
}

void RendererSystem::resetDeferredFrameTargets(){
    m_deferredTargets.lightingBindingSet.reset();
    m_deferredTargets.compositeBindingSet.reset();
    resetAvboitFrameTargets(m_deferredTargets.avboit);

    m_deferredTargets.framebuffer.reset();
    m_deferredTargets.opaqueLightingFramebuffer.reset();

    m_deferredTargets.albedo.reset();
    m_deferredTargets.normal.reset();
    m_deferredTargets.worldPosition.reset();
    m_deferredTargets.opaqueColor.reset();
    m_deferredTargets.depth.reset();

    m_deferredTargets = DeferredFrameTargets{};
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createDeferredFrameTargets(const u32 width, const u32 height){
    if(width == 0 || height == 0){
        resetDeferredFrameTargets();
        return false;
    }

    auto* device = m_graphics.getDevice();
    const Core::Format::Enum albedoFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum normalFormat = ECSRenderDetail::SelectGBufferVectorFormat(*device);
    const Core::Format::Enum worldPositionFormat = ECSRenderDetail::SelectGBufferVectorFormat(*device);
    const Core::Format::Enum opaqueColorFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum depthFormat = ECSRenderDetail::SelectGBufferDepthFormat(*device);
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
        .setClearValue(ECSRenderDetail::s_ClearColor)
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
        .setClearValue(ECSRenderDetail::s_ClearColor)
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
        .addColorAttachment(createdTargets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources)
        .addColorAttachment(createdTargets.normal.get(), ECSRenderDetail::s_FramebufferSubresources)
        .addColorAttachment(createdTargets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources)
        .setDepthAttachment(createdTargets.depth.get(), ECSRenderDetail::s_FramebufferSubresources)
    ;
    createdTargets.framebuffer = device->createFramebuffer(framebufferDesc);
    if(!createdTargets.framebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return false;
    }

    Core::FramebufferDesc opaqueLightingFramebufferDesc;
    opaqueLightingFramebufferDesc.addColorAttachment(createdTargets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources);
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

    Core::BindingSetDesc lightingBindingSetDesc(m_arena);
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_BASE_COLOR,
        createdTargets.albedo.get(),
        createdTargets.albedoFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_NORMAL,
        createdTargets.normal.get(),
        createdTargets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_WORLD_POSITION,
        createdTargets.worldPosition.get(),
        createdTargets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_DEPTH,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_DEFERRED_LIGHTING_BINDING_SAMPLER, m_deferredSampler.get()));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SCENE_SHADING_DEFERRED_LIGHTING_BINDING, m_sceneShadingBuffer.get()));
    createdTargets.lightingBindingSet = device->createBindingSet(lightingBindingSetDesc, m_deferredLightingBindingLayout);
    if(!createdTargets.lightingBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding set"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc(m_arena);
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_COMPOSITE_BINDING_OPAQUE_COLOR,
        createdTargets.opaqueColor.get(),
        createdTargets.opaqueColorFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_COLOR,
        createdTargets.avboit.accumColor.get(),
        createdTargets.avboit.accumColorFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_EXTINCTION,
        createdTargets.avboit.accumExtinction.get(),
        createdTargets.avboit.accumExtinctionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_DEFERRED_COMPOSITE_BINDING_SAMPLER, m_deferredSampler.get()));
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

bool RendererSystem::createDeferredCompositeResources(){
    auto* device = m_graphics.getDevice();

    if(!m_deferredCompositeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_COMPOSITE_BINDING_OPAQUE_COLOR, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_COLOR, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_COMPOSITE_BINDING_AVBOIT_ACCUM_EXTINCTION, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_DEFERRED_COMPOSITE_BINDING_SAMPLER, 1));

        m_deferredCompositeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_deferredCompositeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreatePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create deferred composite sampler")))
        return false;

    if(!loadDeferredCompositeVertexShader())
        return false;

    if(!loadShader(
        m_deferredCompositePixelShader,
        ECSRenderDetail::s_DeferredCompositePixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredCompositePS"
    ))
        return false;

    return true;
}

bool RendererSystem::createDeferredCompositePipeline(Core::Framebuffer* presentationFramebuffer){
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
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(m_deferredCompositeBindingLayout)
    ;

    auto* device = m_graphics.getDevice();
    m_deferredCompositePipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredCompositePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite pipeline"));
        return false;
    }

    return true;
}

void RendererSystem::clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(targets.albedo)
        commandList.setTextureState(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(targets.normal)
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(targets.worldPosition)
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(targets.opaqueColor)
        commandList.setTextureState(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(targets.depth)
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    if(targets.albedo)
        commandList.clearTextureFloat(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);

    if(targets.normal)
        commandList.clearTextureFloat(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.5f, 0.5f, 1.f, 1.f));

    if(targets.worldPosition)
        commandList.clearTextureFloat(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 1.f));

    if(targets.opaqueColor)
        commandList.clearTextureFloat(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);

    if(targets.depth){
        commandList.clearDepthStencilTexture(
            targets.depth.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            true,
            Core::s_DepthClearValue,
            false,
            0
        );
    }
}

bool RendererSystem::renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer){
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

