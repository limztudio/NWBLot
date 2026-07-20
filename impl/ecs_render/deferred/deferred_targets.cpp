// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_targets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgIntervalSubresources{
    Core::TextureSubresourceSet peel;
    Core::TextureSubresourceSet receiverEvent;
    Core::TextureSubresourceSet receiverEventCounter;
    Core::TextureSubresourceSet receiverSpan;
    Core::TextureSubresourceSet receiverSpanCounter;
    Core::TextureSubresourceSet removedInterval;
    Core::TextureSubresourceSet removedIntervalCounter;
};

[[nodiscard]] static CsgIntervalSubresources MakeCsgIntervalSubresources(const DeferredFrameTargets& targets){
    return {
        Core::TextureSubresourceSet(0, 1, 0, targets.csgPeelLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, targets.csgReceiverEventLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, 1),
        Core::TextureSubresourceSet(0, 1, 0, targets.csgReceiverSpanLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, 1),
        Core::TextureSubresourceSet(0, 1, 0, targets.csgRemovedIntervalLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, 1)
    };
}

static void AssertCsgIntervalTargetsAvailable([[maybe_unused]] const DeferredFrameTargets& targets){
    NWB_ASSERT(targets.csgCapBackNormal);
    NWB_ASSERT(targets.csgIntervalDepth);
    NWB_ASSERT(targets.csgIntervalId);
    NWB_ASSERT(targets.csgReceiverEventData);
    NWB_ASSERT(targets.csgReceiverEventCount);
    NWB_ASSERT(targets.csgReceiverSpanData);
    NWB_ASSERT(targets.csgReceiverSpanCount);
    NWB_ASSERT(targets.csgRemovedIntervalDepth);
    NWB_ASSERT(targets.csgRemovedIntervalCapNormal);
    NWB_ASSERT(targets.csgRemovedIntervalData);
    NWB_ASSERT(targets.csgRemovedIntervalCount);
    NWB_ASSERT(targets.csgPeelLayerCount > 0u);
    NWB_ASSERT(targets.csgReceiverEventLayerCount > 0u);
    NWB_ASSERT(targets.csgReceiverSpanLayerCount > 0u);
    NWB_ASSERT(targets.csgRemovedIntervalLayerCount > 0u);
}

static void SetCsgIntervalTargetCopyDestStates(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgIntervalSubresources& subresources
){
    commandList.setTextureState(targets.csgCapBackNormal.get(), subresources.peel, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgIntervalDepth.get(), subresources.peel, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgIntervalId.get(), subresources.peel, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventData.get(), subresources.receiverEvent, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventCount.get(), subresources.receiverEventCounter, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanData.get(), subresources.receiverSpan, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanCount.get(), subresources.receiverSpanCounter, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), subresources.removedInterval, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), subresources.removedInterval, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalData.get(), subresources.removedInterval, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalCount.get(), subresources.removedIntervalCounter, Core::ResourceStates::CopyDest);
}

static void ClearCsgIntervalTargets(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgIntervalSubresources& subresources,
    const Core::Rect& csgClearRect
){
    // The CSG interval targets are per-pixel append buffers. Every consumer bounds its reads by a
    // per-pixel counter (receiver-event / span / removed-interval counts) or by the cutter-interval
    // id, and the span/removed-interval counts and flags are rewritten for every work-region pixel by
    // their producing compute pass. Only state that accumulates across a frame needs resetting:
    //  - receiver event count is atomically incremented from zero by the surface pass (its overflow is
    //    derived later from count > layer budget, so no separate event-flags target is needed),
    //  - interval id is written sparsely by the peel pass (unwritten layers must read back as empty).
    // The bulk depth/normal/data layers and the span/removed counters are written before they are
    // read, so clearing them is wasted bandwidth (this clear dominated the CSG frame cost).
    commandList.clearTextureRectUInt(targets.csgIntervalId.get(), subresources.peel, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgReceiverEventCount.get(), subresources.receiverEventCounter, csgClearRect, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


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
    rayTracingState().m_shadowBindingSet.reset();
    rayTracingState().m_shadowBindingSetTlas = nullptr;

    deferredState().m_targets.framebuffer.reset();
    deferredState().m_targets.opaqueLightingFramebuffer.reset();

    deferredState().m_targets.albedo.reset();
    deferredState().m_targets.normal.reset();
    deferredState().m_targets.worldPosition.reset();
    deferredState().m_targets.csgCapBackNormal.reset();
    deferredState().m_targets.csgIntervalDepth.reset();
    deferredState().m_targets.csgIntervalId.reset();
    deferredState().m_targets.csgReceiverEventData.reset();
    deferredState().m_targets.csgReceiverEventCount.reset();
    deferredState().m_targets.csgReceiverSpanData.reset();
    deferredState().m_targets.csgReceiverSpanCount.reset();
    deferredState().m_targets.csgRemovedIntervalDepth.reset();
    deferredState().m_targets.csgRemovedIntervalCapNormal.reset();
    deferredState().m_targets.csgRemovedIntervalData.reset();
    deferredState().m_targets.csgRemovedIntervalCount.reset();
    deferredState().m_targets.opaqueColor.reset();
    deferredState().m_targets.depth.reset();
    deferredState().m_targets.shadowVisibility.reset();

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
    const Core::Format::Enum csgReceiverEventDataFormat = ECSRenderDetail::SelectCsgReceiverEventDataFormat(*device);
    const Core::Format::Enum csgReceiverEventCountFormat = ECSRenderDetail::SelectCsgReceiverEventCountFormat(*device);
    const Core::Format::Enum csgReceiverSpanDataFormat = ECSRenderDetail::SelectCsgReceiverSpanDataFormat(*device);
    const Core::Format::Enum csgReceiverSpanCountFormat = ECSRenderDetail::SelectCsgReceiverSpanCountFormat(*device);
    const Core::Format::Enum csgRemovedIntervalDepthFormat = ECSRenderDetail::SelectCsgRemovedIntervalDepthFormat(*device);
    const Core::Format::Enum csgRemovedIntervalCapNormalFormat = ECSRenderDetail::SelectCsgRemovedIntervalCapNormalFormat(*device);
    const Core::Format::Enum csgRemovedIntervalDataFormat = ECSRenderDetail::SelectCsgRemovedIntervalDataFormat(*device);
    const Core::Format::Enum csgRemovedIntervalCountFormat = ECSRenderDetail::SelectCsgRemovedIntervalCountFormat(*device);
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
        || csgReceiverEventDataFormat == Core::Format::UNKNOWN
        || csgReceiverEventCountFormat == Core::Format::UNKNOWN
        || csgReceiverSpanDataFormat == Core::Format::UNKNOWN
        || csgReceiverSpanCountFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDepthFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCapNormalFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDataFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCountFormat == Core::Format::UNKNOWN
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
    createdTargets.csgReceiverEventDataFormat = csgReceiverEventDataFormat;
    createdTargets.csgReceiverEventCountFormat = csgReceiverEventCountFormat;
    createdTargets.csgReceiverSpanDataFormat = csgReceiverSpanDataFormat;
    createdTargets.csgReceiverSpanCountFormat = csgReceiverSpanCountFormat;
    createdTargets.csgRemovedIntervalDepthFormat = csgRemovedIntervalDepthFormat;
    createdTargets.csgRemovedIntervalCapNormalFormat = csgRemovedIntervalCapNormalFormat;
    createdTargets.csgRemovedIntervalDataFormat = csgRemovedIntervalDataFormat;
    createdTargets.csgRemovedIntervalCountFormat = csgRemovedIntervalCountFormat;
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

    if(!m_renderer.csgSystem().createCsgPeelTargets(createdTargets))
        return false;

    if(!m_renderer.raytracingSystem().createShadowVisibilityTarget(createdTargets))
        return false;

    if(!m_renderer.raytracingSystem().createCausticTargets(createdTargets))
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
    lightingBindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_DEFERRED_LIGHTING_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_DEFERRED_LIGHTING_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_LIGHTING_BINDING_SHADOW_VISIBILITY,
        createdTargets.shadowVisibility.get(),
        createdTargets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_LIGHTING_BINDING_CAUSTIC_IRRADIANCE,
        createdTargets.causticIrradiance.get(),
        createdTargets.causticIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    // Surfel GI: the RESOLVED screen-space irradiance texture (surfel_resolve_cs writes it; the lighting samples it).
    // Created WITH these targets (same lifetime), so it is bound here at creation -- no lazy rebuild. Reading the
    // resolved texture (not the RW surfel pool) is what keeps the pool off the pixel shader = no frames-in-flight race.
    lightingBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_DEFERRED_LIGHTING_BINDING_GI_SURFEL_IRRADIANCE,
        createdTargets.surfelIrradiance.get(),
        createdTargets.surfelIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
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

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, normal {}, world position {}, opaque color {}, depth {}, shadow visibility {}, CSG peel {} layers: cap back normal {}, interval depth {}, interval id {}, receiver events {} layers: event data {}, event count {}, receiver spans {} layers: span data {}, span count {}, removed intervals {} layers: interval depth {}, cap normal {}, interval data {}, interval count {}, AVBOIT color {}, extinction {}, transmittance {})")
        , deferredState().m_targets.width
        , deferredState().m_targets.height
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.albedoFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.normalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.worldPositionFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.opaqueColorFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.depthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.shadowVisibilityFormat).name)
        , deferredState().m_targets.csgPeelLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgCapNormalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalIdFormat).name)
        , deferredState().m_targets.csgReceiverEventLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventCountFormat).name)
        , deferredState().m_targets.csgReceiverSpanLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanCountFormat).name)
        , deferredState().m_targets.csgRemovedIntervalLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalCapNormalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalCountFormat).name)
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
    if(clearCsgTargets)
        __hidden_deferred_targets::AssertCsgIntervalTargetsAvailable(targets);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_DeferredClear, graphics().getDevice(), commandList);

    const __hidden_deferred_targets::CsgIntervalSubresources csgSubresources =
        __hidden_deferred_targets::MakeCsgIntervalSubresources(targets)
    ;

    commandList.setTextureState(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(clearCsgTargets)
        __hidden_deferred_targets::SetCsgIntervalTargetCopyDestStates(commandList, targets, csgSubresources);

    commandList.setTextureState(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    // Surfel-GI resolved irradiance: cleared to 0 (alpha 0 = "no surfel covered this pixel") each frame. When the surfel
    // resolve pass runs (SW GI enabled) it overwrites every pixel; when it does NOT (GI off / HW path), the cleared 0
    // makes the lighting fall back to hemiAmbient -- a correct no-op. Same per-frame reset as the caustic irradiance.
    if(targets.surfelIrradiance)
        commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    commandList.clearTextureFloat(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);
    commandList.clearTextureFloat(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.5f, 0.5f, 1.f, 1.f));
    commandList.clearTextureFloat(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 1.f));
    if(targets.surfelIrradiance)
        commandList.clearTextureFloat(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));

    if(clearCsgTargets){
        Core::GpuTimingMeasure csgClearTiming(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalClear, graphics().getDevice(), commandList);

        __hidden_deferred_targets::ClearCsgIntervalTargets(commandList, targets, csgSubresources, csgClearRect);
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
    __hidden_deferred_targets::AssertCsgIntervalTargetsAvailable(targets);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalClear, graphics().getDevice(), commandList);

    const __hidden_deferred_targets::CsgIntervalSubresources csgSubresources =
        __hidden_deferred_targets::MakeCsgIntervalSubresources(targets)
    ;

    __hidden_deferred_targets::SetCsgIntervalTargetCopyDestStates(commandList, targets, csgSubresources);

    commandList.commitBarriers();

    __hidden_deferred_targets::ClearCsgIntervalTargets(commandList, targets, csgSubresources, csgClearRect);
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

