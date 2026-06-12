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
    csgState().m_receiverSpanBuildBindingSet.reset();
    csgState().m_intervalCombineBindingSet.reset();
    csgState().m_receiverSurfaceBindingSet.reset();
    csgState().m_intervalSampleBindingSet.reset();

    deferredState().m_targets.framebuffer.reset();
    deferredState().m_targets.opaqueLightingFramebuffer.reset();

    deferredState().m_targets.albedo.reset();
    deferredState().m_targets.normal.reset();
    deferredState().m_targets.worldPosition.reset();
    deferredState().m_targets.csgCapBackNormal.reset();
    deferredState().m_targets.csgIntervalDepth.reset();
    deferredState().m_targets.csgIntervalLinearDepth.reset();
    deferredState().m_targets.csgIntervalId.reset();
    deferredState().m_targets.csgReceiverEventDepth.reset();
    deferredState().m_targets.csgReceiverEventData.reset();
    deferredState().m_targets.csgReceiverEventCount.reset();
    deferredState().m_targets.csgReceiverEventFlags.reset();
    deferredState().m_targets.csgReceiverSpanDepth.reset();
    deferredState().m_targets.csgReceiverSpanData.reset();
    deferredState().m_targets.csgReceiverSpanCount.reset();
    deferredState().m_targets.csgReceiverSpanFlags.reset();
    deferredState().m_targets.csgRemovedIntervalDepth.reset();
    deferredState().m_targets.csgRemovedIntervalCapNormal.reset();
    deferredState().m_targets.csgRemovedIntervalData.reset();
    deferredState().m_targets.csgRemovedIntervalCount.reset();
    deferredState().m_targets.csgRemovedIntervalFlags.reset();
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
    const Core::Format::Enum csgIntervalLinearDepthFormat = ECSRenderDetail::SelectCsgIntervalLinearDepthFormat(*device);
    const Core::Format::Enum csgIntervalIdFormat = ECSRenderDetail::SelectCsgIntervalIdFormat(*device);
    const Core::Format::Enum csgReceiverEventDepthFormat = ECSRenderDetail::SelectCsgReceiverEventDepthFormat(*device);
    const Core::Format::Enum csgReceiverEventDataFormat = ECSRenderDetail::SelectCsgReceiverEventDataFormat(*device);
    const Core::Format::Enum csgReceiverEventCountFormat = ECSRenderDetail::SelectCsgReceiverEventCountFormat(*device);
    const Core::Format::Enum csgReceiverEventFlagsFormat = ECSRenderDetail::SelectCsgReceiverEventFlagsFormat(*device);
    const Core::Format::Enum csgReceiverSpanDepthFormat = ECSRenderDetail::SelectCsgReceiverSpanDepthFormat(*device);
    const Core::Format::Enum csgReceiverSpanDataFormat = ECSRenderDetail::SelectCsgReceiverSpanDataFormat(*device);
    const Core::Format::Enum csgReceiverSpanCountFormat = ECSRenderDetail::SelectCsgReceiverSpanCountFormat(*device);
    const Core::Format::Enum csgReceiverSpanFlagsFormat = ECSRenderDetail::SelectCsgReceiverSpanFlagsFormat(*device);
    const Core::Format::Enum csgRemovedIntervalDepthFormat = ECSRenderDetail::SelectCsgRemovedIntervalDepthFormat(*device);
    const Core::Format::Enum csgRemovedIntervalCapNormalFormat = ECSRenderDetail::SelectCsgRemovedIntervalCapNormalFormat(*device);
    const Core::Format::Enum csgRemovedIntervalDataFormat = ECSRenderDetail::SelectCsgRemovedIntervalDataFormat(*device);
    const Core::Format::Enum csgRemovedIntervalCountFormat = ECSRenderDetail::SelectCsgRemovedIntervalCountFormat(*device);
    const Core::Format::Enum csgRemovedIntervalFlagsFormat = ECSRenderDetail::SelectCsgRemovedIntervalFlagsFormat(*device);
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
        || csgIntervalLinearDepthFormat == Core::Format::UNKNOWN
        || csgIntervalIdFormat == Core::Format::UNKNOWN
        || csgReceiverEventDepthFormat == Core::Format::UNKNOWN
        || csgReceiverEventDataFormat == Core::Format::UNKNOWN
        || csgReceiverEventCountFormat == Core::Format::UNKNOWN
        || csgReceiverEventFlagsFormat == Core::Format::UNKNOWN
        || csgReceiverSpanDepthFormat == Core::Format::UNKNOWN
        || csgReceiverSpanDataFormat == Core::Format::UNKNOWN
        || csgReceiverSpanCountFormat == Core::Format::UNKNOWN
        || csgReceiverSpanFlagsFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDepthFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCapNormalFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDataFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCountFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalFlagsFormat == Core::Format::UNKNOWN
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
    createdTargets.csgIntervalLinearDepthFormat = csgIntervalLinearDepthFormat;
    createdTargets.csgIntervalIdFormat = csgIntervalIdFormat;
    createdTargets.csgReceiverEventDepthFormat = csgReceiverEventDepthFormat;
    createdTargets.csgReceiverEventDataFormat = csgReceiverEventDataFormat;
    createdTargets.csgReceiverEventCountFormat = csgReceiverEventCountFormat;
    createdTargets.csgReceiverEventFlagsFormat = csgReceiverEventFlagsFormat;
    createdTargets.csgReceiverSpanDepthFormat = csgReceiverSpanDepthFormat;
    createdTargets.csgReceiverSpanDataFormat = csgReceiverSpanDataFormat;
    createdTargets.csgReceiverSpanCountFormat = csgReceiverSpanCountFormat;
    createdTargets.csgReceiverSpanFlagsFormat = csgReceiverSpanFlagsFormat;
    createdTargets.csgRemovedIntervalDepthFormat = csgRemovedIntervalDepthFormat;
    createdTargets.csgRemovedIntervalCapNormalFormat = csgRemovedIntervalCapNormalFormat;
    createdTargets.csgRemovedIntervalDataFormat = csgRemovedIntervalDataFormat;
    createdTargets.csgRemovedIntervalCountFormat = csgRemovedIntervalCountFormat;
    createdTargets.csgRemovedIntervalFlagsFormat = csgRemovedIntervalFlagsFormat;
    createdTargets.csgPeelLayerCount = ECSRenderDetail::s_CsgPeelLayerCount;
    createdTargets.csgReceiverEventLayerCount = ECSRenderDetail::s_CsgReceiverEventLayerCount;
    createdTargets.csgReceiverSpanLayerCount = ECSRenderDetail::s_CsgReceiverSpanLayerCount;
    createdTargets.csgRemovedIntervalLayerCount = ECSRenderDetail::s_CsgRemovedIntervalLayerCount;

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

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, normal {}, world position {}, opaque color {}, depth {}, lazy CSG peel {} layers: cap back normal {}, interval depth {}, interval linear depth {}, interval id {}, lazy receiver events {} layers: event depth {}, event data {}, event count {}, event flags {}, lazy receiver spans {} layers: span depth {}, span data {}, span count {}, span flags {}, lazy removed intervals {} layers: interval depth {}, cap normal {}, interval data {}, interval count {}, interval flags {}, AVBOIT color {}, extinction {}, transmittance {})")
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
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalLinearDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalIdFormat).name)
        , deferredState().m_targets.csgReceiverEventLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventCountFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventFlagsFormat).name)
        , deferredState().m_targets.csgReceiverSpanLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanCountFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanFlagsFormat).name)
        , deferredState().m_targets.csgRemovedIntervalLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalCapNormalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalCountFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalFlagsFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.accumColorFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.accumExtinctionFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.transmittanceFormat).name)
    );
    return true;
}

void RendererDeferredSystem::clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const bool clearCsgTargets, const Core::Rect& csgClearRect){
    NWB_ASSERT(targets.albedo);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.opaqueColor);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(!clearCsgTargets || targets.csgCapBackNormal);
    NWB_ASSERT(!clearCsgTargets || targets.csgIntervalDepth);
    NWB_ASSERT(!clearCsgTargets || targets.csgIntervalLinearDepth);
    NWB_ASSERT(!clearCsgTargets || targets.csgIntervalId);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverEventDepth);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverEventData);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverEventCount);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverEventFlags);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverSpanDepth);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverSpanData);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverSpanCount);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverSpanFlags);
    NWB_ASSERT(!clearCsgTargets || targets.csgRemovedIntervalDepth);
    NWB_ASSERT(!clearCsgTargets || targets.csgRemovedIntervalCapNormal);
    NWB_ASSERT(!clearCsgTargets || targets.csgRemovedIntervalData);
    NWB_ASSERT(!clearCsgTargets || targets.csgRemovedIntervalCount);
    NWB_ASSERT(!clearCsgTargets || targets.csgRemovedIntervalFlags);
    NWB_ASSERT(!clearCsgTargets || targets.csgPeelLayerCount > 0u);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverEventLayerCount > 0u);
    NWB_ASSERT(!clearCsgTargets || targets.csgReceiverSpanLayerCount > 0u);
    NWB_ASSERT(!clearCsgTargets || targets.csgRemovedIntervalLayerCount > 0u);

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventSubresources(0, 1, 0, targets.csgReceiverEventLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventCounterSubresources(0, 1, 0, 1);
    const Core::TextureSubresourceSet csgReceiverSpanSubresources(0, 1, 0, targets.csgReceiverSpanLayerCount);
    const Core::TextureSubresourceSet csgReceiverSpanCounterSubresources(0, 1, 0, 1);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(0, 1, 0, targets.csgRemovedIntervalLayerCount);
    const Core::TextureSubresourceSet csgRemovedIntervalCounterSubresources(0, 1, 0, 1);

    commandList.setTextureState(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(clearCsgTargets){
        commandList.setTextureState(targets.csgCapBackNormal.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgIntervalDepth.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgIntervalLinearDepth.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgIntervalId.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverEventDepth.get(), csgReceiverEventSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverEventData.get(), csgReceiverEventSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverEventCount.get(), csgReceiverEventCounterSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverEventFlags.get(), csgReceiverEventCounterSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverSpanDepth.get(), csgReceiverSpanSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverSpanData.get(), csgReceiverSpanSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverSpanCount.get(), csgReceiverSpanCounterSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgReceiverSpanFlags.get(), csgReceiverSpanCounterSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), csgRemovedIntervalSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), csgRemovedIntervalSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgRemovedIntervalData.get(), csgRemovedIntervalSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgRemovedIntervalCount.get(), csgRemovedIntervalCounterSubresources, Core::ResourceStates::CopyDest);
        commandList.setTextureState(targets.csgRemovedIntervalFlags.get(), csgRemovedIntervalCounterSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.setTextureState(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    commandList.clearTextureFloat(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);
    commandList.clearTextureFloat(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.5f, 0.5f, 1.f, 1.f));
    commandList.clearTextureFloat(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 1.f));

    if(clearCsgTargets){
        commandList.clearTextureRectFloat(targets.csgCapBackNormal.get(), csgPeelSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureRectFloat(targets.csgIntervalDepth.get(), csgPeelSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureRectFloat(targets.csgIntervalLinearDepth.get(), csgPeelSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureRectUInt(targets.csgIntervalId.get(), csgPeelSubresources, csgClearRect, 0u);
        commandList.clearTextureRectFloat(targets.csgReceiverEventDepth.get(), csgReceiverEventSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureRectUInt(targets.csgReceiverEventData.get(), csgReceiverEventSubresources, csgClearRect, 0u);
        commandList.clearTextureRectUInt(targets.csgReceiverEventCount.get(), csgReceiverEventCounterSubresources, csgClearRect, 0u);
        commandList.clearTextureRectUInt(targets.csgReceiverEventFlags.get(), csgReceiverEventCounterSubresources, csgClearRect, 0u);
        commandList.clearTextureRectFloat(targets.csgReceiverSpanDepth.get(), csgReceiverSpanSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureRectUInt(targets.csgReceiverSpanData.get(), csgReceiverSpanSubresources, csgClearRect, 0u);
        commandList.clearTextureRectUInt(targets.csgReceiverSpanCount.get(), csgReceiverSpanCounterSubresources, csgClearRect, 0u);
        commandList.clearTextureRectUInt(targets.csgReceiverSpanFlags.get(), csgReceiverSpanCounterSubresources, csgClearRect, 0u);
        commandList.clearTextureRectFloat(targets.csgRemovedIntervalDepth.get(), csgRemovedIntervalSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureRectFloat(targets.csgRemovedIntervalCapNormal.get(), csgRemovedIntervalSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
        commandList.clearTextureRectUInt(targets.csgRemovedIntervalData.get(), csgRemovedIntervalSubresources, csgClearRect, 0u);
        commandList.clearTextureRectUInt(targets.csgRemovedIntervalCount.get(), csgRemovedIntervalCounterSubresources, csgClearRect, 0u);
        commandList.clearTextureRectUInt(targets.csgRemovedIntervalFlags.get(), csgRemovedIntervalCounterSubresources, csgClearRect, 0u);
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

void RendererDeferredSystem::clearCsgIntervalTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::Rect& csgClearRect){
    NWB_ASSERT(targets.csgCapBackNormal);
    NWB_ASSERT(targets.csgIntervalDepth);
    NWB_ASSERT(targets.csgIntervalLinearDepth);
    NWB_ASSERT(targets.csgIntervalId);
    NWB_ASSERT(targets.csgReceiverEventDepth);
    NWB_ASSERT(targets.csgReceiverEventData);
    NWB_ASSERT(targets.csgReceiverEventCount);
    NWB_ASSERT(targets.csgReceiverEventFlags);
    NWB_ASSERT(targets.csgReceiverSpanDepth);
    NWB_ASSERT(targets.csgReceiverSpanData);
    NWB_ASSERT(targets.csgReceiverSpanCount);
    NWB_ASSERT(targets.csgReceiverSpanFlags);
    NWB_ASSERT(targets.csgRemovedIntervalDepth);
    NWB_ASSERT(targets.csgRemovedIntervalCapNormal);
    NWB_ASSERT(targets.csgRemovedIntervalData);
    NWB_ASSERT(targets.csgRemovedIntervalCount);
    NWB_ASSERT(targets.csgRemovedIntervalFlags);
    NWB_ASSERT(targets.csgPeelLayerCount > 0u);
    NWB_ASSERT(targets.csgReceiverEventLayerCount > 0u);
    NWB_ASSERT(targets.csgReceiverSpanLayerCount > 0u);
    NWB_ASSERT(targets.csgRemovedIntervalLayerCount > 0u);

    const Core::TextureSubresourceSet csgPeelSubresources(0, 1, 0, targets.csgPeelLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventSubresources(0, 1, 0, targets.csgReceiverEventLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventCounterSubresources(0, 1, 0, 1);
    const Core::TextureSubresourceSet csgReceiverSpanSubresources(0, 1, 0, targets.csgReceiverSpanLayerCount);
    const Core::TextureSubresourceSet csgReceiverSpanCounterSubresources(0, 1, 0, 1);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(0, 1, 0, targets.csgRemovedIntervalLayerCount);
    const Core::TextureSubresourceSet csgRemovedIntervalCounterSubresources(0, 1, 0, 1);

    commandList.setTextureState(targets.csgCapBackNormal.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgIntervalDepth.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgIntervalLinearDepth.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgIntervalId.get(), csgPeelSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventDepth.get(), csgReceiverEventSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventData.get(), csgReceiverEventSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventCount.get(), csgReceiverEventCounterSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventFlags.get(), csgReceiverEventCounterSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanDepth.get(), csgReceiverSpanSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanData.get(), csgReceiverSpanSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanCount.get(), csgReceiverSpanCounterSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanFlags.get(), csgReceiverSpanCounterSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), csgRemovedIntervalSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), csgRemovedIntervalSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalData.get(), csgRemovedIntervalSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalCount.get(), csgRemovedIntervalCounterSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalFlags.get(), csgRemovedIntervalCounterSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    commandList.clearTextureRectFloat(targets.csgCapBackNormal.get(), csgPeelSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureRectFloat(targets.csgIntervalDepth.get(), csgPeelSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureRectFloat(targets.csgIntervalLinearDepth.get(), csgPeelSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureRectUInt(targets.csgIntervalId.get(), csgPeelSubresources, csgClearRect, 0u);
    commandList.clearTextureRectFloat(targets.csgReceiverEventDepth.get(), csgReceiverEventSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureRectUInt(targets.csgReceiverEventData.get(), csgReceiverEventSubresources, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgReceiverEventCount.get(), csgReceiverEventCounterSubresources, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgReceiverEventFlags.get(), csgReceiverEventCounterSubresources, csgClearRect, 0u);
    commandList.clearTextureRectFloat(targets.csgReceiverSpanDepth.get(), csgReceiverSpanSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureRectUInt(targets.csgReceiverSpanData.get(), csgReceiverSpanSubresources, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgReceiverSpanCount.get(), csgReceiverSpanCounterSubresources, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgReceiverSpanFlags.get(), csgReceiverSpanCounterSubresources, csgClearRect, 0u);
    commandList.clearTextureRectFloat(targets.csgRemovedIntervalDepth.get(), csgRemovedIntervalSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureRectFloat(targets.csgRemovedIntervalCapNormal.get(), csgRemovedIntervalSubresources, csgClearRect, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureRectUInt(targets.csgRemovedIntervalData.get(), csgRemovedIntervalSubresources, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgRemovedIntervalCount.get(), csgRemovedIntervalCounterSubresources, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgRemovedIntervalFlags.get(), csgRemovedIntervalCounterSubresources, csgClearRect, 0u);
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

