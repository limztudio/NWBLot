// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDeferredSystem::resetAvboitFrameTargets(AvboitFrameTargets& targets){
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

void RendererDeferredSystem::resetDeferredFrameTargets(){
    deferredState().m_targets.lightingBindingSet.reset();
    deferredState().m_targets.compositeBindingSet.reset();
    resetAvboitFrameTargets(deferredState().m_targets.avboit);
    csgState().m_intervalPeelBindingSet.reset();
    csgState().m_receiverSurfaceBindingSet.reset();
    csgState().m_intervalSampleBindingSet.reset();

    deferredState().m_targets.framebuffer.reset();
    deferredState().m_targets.opaqueLightingFramebuffer.reset();

    deferredState().m_targets.albedo.reset();
    deferredState().m_targets.normal.reset();
    deferredState().m_targets.worldPosition.reset();
    deferredState().m_targets.csgCapNormal.reset();
    deferredState().m_targets.csgIntervalDepth.reset();
    deferredState().m_targets.csgIntervalId.reset();
    deferredState().m_targets.csgReceiverFrontSurfaceMask.reset();
    deferredState().m_targets.csgReceiverSurfaceMask.reset();
    deferredState().m_targets.csgReceiverBackSurfaceMask.reset();
    deferredState().m_targets.opaqueColor.reset();
    deferredState().m_targets.depth.reset();

    deferredState().m_targets = DeferredFrameTargets{};
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredFrameTargets(const u32 width, const u32 height){
    if(width == 0 || height == 0){
        resetDeferredFrameTargets();
        return false;
    }

    auto* device = graphics().getDevice();
    const Core::Format::Enum albedoFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum normalFormat = ECSRenderDetail::SelectGBufferVectorFormat(*device);
    const Core::Format::Enum worldPositionFormat = ECSRenderDetail::SelectGBufferVectorFormat(*device);
    const Core::Format::Enum opaqueColorFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum depthFormat = ECSRenderDetail::SelectGBufferDepthFormat(*device);
    const Core::Format::Enum csgCapNormalFormat = ECSRenderDetail::SelectCsgCapNormalFormat(*device);
    const Core::Format::Enum csgIntervalDepthFormat = ECSRenderDetail::SelectCsgIntervalDepthFormat(*device);
    const Core::Format::Enum csgIntervalIdFormat = ECSRenderDetail::SelectCsgIntervalIdFormat(*device);
    const Core::Format::Enum csgReceiverFrontSurfaceMaskFormat = ECSRenderDetail::SelectCsgReceiverFrontSurfaceMaskFormat(*device);
    const Core::Format::Enum csgReceiverSurfaceMaskFormat = ECSRenderDetail::SelectCsgReceiverSurfaceMaskFormat(*device);
    const Core::Format::Enum csgReceiverBackSurfaceMaskFormat = ECSRenderDetail::SelectCsgReceiverBackSurfaceMaskFormat(*device);
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
        || csgCapNormalFormat == Core::Format::UNKNOWN
        || csgIntervalDepthFormat == Core::Format::UNKNOWN
        || csgIntervalIdFormat == Core::Format::UNKNOWN
        || csgReceiverFrontSurfaceMaskFormat == Core::Format::UNKNOWN
        || csgReceiverSurfaceMaskFormat == Core::Format::UNKNOWN
        || csgReceiverBackSurfaceMaskFormat == Core::Format::UNKNOWN
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
    if(!m_renderer.avboitSystem().createAvboitResources())
        return false;

    resetDeferredFrameTargets();
    materialState().m_pipelines.clear();
    deferredState().m_lightingPipeline.reset();
    deferredState().m_compositePipeline.reset();

    DeferredFrameTargets createdTargets;
    createdTargets.width = width;
    createdTargets.height = height;
    createdTargets.albedoFormat = albedoFormat;
    createdTargets.normalFormat = normalFormat;
    createdTargets.worldPositionFormat = worldPositionFormat;
    createdTargets.opaqueColorFormat = opaqueColorFormat;
    createdTargets.depthFormat = depthFormat;
    createdTargets.csgCapNormalFormat = csgCapNormalFormat;
    createdTargets.csgIntervalDepthFormat = csgIntervalDepthFormat;
    createdTargets.csgIntervalIdFormat = csgIntervalIdFormat;
    createdTargets.csgReceiverFrontSurfaceMaskFormat = csgReceiverFrontSurfaceMaskFormat;
    createdTargets.csgReceiverSurfaceMaskFormat = csgReceiverSurfaceMaskFormat;
    createdTargets.csgReceiverBackSurfaceMaskFormat = csgReceiverBackSurfaceMaskFormat;
    createdTargets.csgPeelLayerCount = ECSRenderDetail::s_CsgPeelLayerCount;

    Core::TextureDesc albedoDesc;
    albedoDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.albedoFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/gbuffer_albedo")
        .setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    createdTargets.albedo = graphics().createTexture(albedoDesc);
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
    createdTargets.normal = graphics().createTexture(normalDesc);
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
    createdTargets.worldPosition = graphics().createTexture(worldPositionDesc);
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
    createdTargets.opaqueColor = graphics().createTexture(opaqueColorDesc);
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
    createdTargets.depth = graphics().createTexture(depthDesc);
    if(!createdTargets.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred depth target"));
        return false;
    }
    if(!m_renderer.csgSystem().createCsgPeelTargets(createdTargets))
        return false;

    Core::FramebufferAttachment gbufferAttachments[NWB_MESH_GBUFFER_TARGET_COUNT] = {};
    gbufferAttachments[NWB_MESH_GBUFFER_BASE_COLOR_LOCATION]
        .setTexture(createdTargets.albedo.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    gbufferAttachments[NWB_MESH_GBUFFER_NORMAL_LOCATION]
        .setTexture(createdTargets.normal.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    gbufferAttachments[NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION]
        .setTexture(createdTargets.worldPosition.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    Core::FramebufferDesc framebufferDesc;
    for(const Core::FramebufferAttachment& attachment : gbufferAttachments)
        framebufferDesc.addColorAttachment(attachment);
    framebufferDesc.setDepthAttachment(createdTargets.depth.get(), ECSRenderDetail::s_FramebufferSubresources);
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

    if(!m_renderer.avboitSystem().createAvboitFrameTargets(
        createdTargets,
        avboitLowRasterFormat,
        avboitAccumColorFormat,
        avboitAccumExtinctionFormat,
        avboitTransmittanceFormat
    ))
        return false;

    Core::BindingSetDesc lightingBindingSetDesc(arena());
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
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_DEFERRED_LIGHTING_BINDING_SAMPLER, deferredState().m_sampler.get()));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SCENE_SHADING_DEFERRED_LIGHTING_BINDING, deferredState().m_sceneShadingBuffer.get()));
    createdTargets.lightingBindingSet = device->createBindingSet(lightingBindingSetDesc, deferredState().m_lightingBindingLayout);
    if(!createdTargets.lightingBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding set"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc(arena());
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
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_DEFERRED_COMPOSITE_BINDING_SAMPLER, deferredState().m_sampler.get()));
    createdTargets.compositeBindingSet = device->createBindingSet(bindingSetDesc, deferredState().m_compositeBindingLayout);
    if(!createdTargets.compositeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding set"));
        return false;
    }

    deferredState().m_targets = Move(createdTargets);
    if(!createDeferredLightingPipeline(deferredState().m_targets)){
        resetDeferredFrameTargets();
        return false;
    }

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, normal {}, world position {}, opaque color {}, depth {}, CSG peel {} layers: cap normal {}, interval depth {}, interval id {}, receiver front surface mask {}, receiver surface mask {}, receiver back surface mask {}, AVBOIT color {}, extinction {}, transmittance {})")
        , deferredState().m_targets.width
        , deferredState().m_targets.height
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.albedoFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.normalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.worldPositionFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.opaqueColorFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.depthFormat).name)
        , deferredState().m_targets.csgPeelLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgCapNormalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalIdFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverFrontSurfaceMaskFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSurfaceMaskFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverBackSurfaceMaskFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.accumColorFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.accumExtinctionFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.transmittanceFormat).name)
    );
    return true;
}

void RendererDeferredSystem::clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const bool clearCsgTargets){
    NWB_ASSERT(targets.albedo);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.opaqueColor);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(!clearCsgTargets || targets.csgCapNormal);
    NWB_ASSERT(!clearCsgTargets || targets.csgIntervalDepth);
    NWB_ASSERT(!clearCsgTargets || targets.csgIntervalId);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverFrontSurfaceMask);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverSurfaceMask);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverBackSurfaceMask);
    NWB_ASSERT(!clearCsgTargets || targets.csgPeelLayerCount > 0u);

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);

    commandList.setTextureState(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(clearCsgTargets){
        commandList.setTextureState(targets.csgCapNormal.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgIntervalDepth.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgIntervalId.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverFrontSurfaceMask.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverSurfaceMask.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverBackSurfaceMask.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.setTextureState(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    commandList.clearTextureFloat(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);
    commandList.clearTextureFloat(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.5f, 0.5f, 1.f, 1.f));
    commandList.clearTextureFloat(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 1.f));

    if(clearCsgTargets){
        commandList.clearTextureFloat(targets.csgCapNormal.get(), csgPeelSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureFloat(targets.csgIntervalDepth.get(), csgPeelSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureUInt(targets.csgIntervalId.get(), csgPeelSubresources, 0u);
        commandList.clearTextureUInt(targets.csgReceiverFrontSurfaceMask.get(), ECSRenderDetail::s_FramebufferSubresources, NWB_CSG_RECEIVER_FRONT_SURFACE_MASK_INVALID);
        commandList.clearTextureUInt(targets.csgReceiverSurfaceMask.get(), ECSRenderDetail::s_FramebufferSubresources, NWB_CSG_RECEIVER_SURFACE_MASK_INVALID);
        commandList.clearTextureUInt(targets.csgReceiverBackSurfaceMask.get(), ECSRenderDetail::s_FramebufferSubresources, NWB_CSG_RECEIVER_BACK_SURFACE_MASK_INVALID);
    }

    commandList.clearTextureFloat(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);

    commandList.clearDepthStencilTexture(
        targets.depth.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        true,
        Core::s_DepthClearValue,
        false,
        0
    );
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

