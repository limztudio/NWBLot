// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/csg/format_private.h>
#include <impl/ecs_render/deferred/format_private.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredFrameTargets> RendererDeferredSystem::createDeferredFrameTargets(const u32 width, const u32 height){
    if(width == 0 || height == 0)
        return MakeUnexpected(Failure{});

    auto& device = m_graphics.getDevice();
    const Core::Format::Enum albedoFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(device);
    const Core::Format::Enum normalFormat = ECSRenderDetail::SelectGBufferVectorFormat(device);
    const Core::Format::Enum worldPositionFormat = ECSRenderDetail::SelectGBufferVectorFormat(device);
    const Core::Format::Enum opaqueColorFormat = ECSRenderDetail::SelectDeferredOpaqueColorFormat(device);
    const Core::Format::Enum depthFormat = ECSRenderDetail::SelectGBufferDepthFormat(device);
    const Core::Format::Enum csgCapNormalFormat = ECSRenderDetail::SelectCsgCapNormalFormat(device);
    const Core::Format::Enum csgIntervalDepthFormat = ECSRenderDetail::SelectCsgIntervalDepthFormat(device);
    const Core::Format::Enum csgIntervalIdFormat = ECSRenderDetail::SelectCsgIntervalIdFormat(device);
    const Core::Format::Enum csgReceiverEventDataFormat = ECSRenderDetail::SelectCsgReceiverEventDataFormat(device);
    const Core::Format::Enum csgReceiverEventCountFormat = ECSRenderDetail::SelectCsgReceiverEventCountFormat(device);
    const Core::Format::Enum csgReceiverSpanDataFormat = ECSRenderDetail::SelectCsgReceiverSpanDataFormat(device);
    const Core::Format::Enum csgReceiverSpanCountFormat = ECSRenderDetail::SelectCsgReceiverSpanCountFormat(device);
    const Core::Format::Enum csgRemovedIntervalDepthFormat = ECSRenderDetail::SelectCsgRemovedIntervalDepthFormat(device);
    const Core::Format::Enum csgRemovedIntervalCapNormalFormat = ECSRenderDetail::SelectCsgRemovedIntervalCapNormalFormat(device);
    const Core::Format::Enum csgRemovedIntervalDataFormat = ECSRenderDetail::SelectCsgRemovedIntervalDataFormat(device);
    const Core::Format::Enum csgRemovedIntervalCountFormat = ECSRenderDetail::SelectCsgRemovedIntervalCountFormat(device);
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
        return MakeUnexpected(Failure{});
    }
    if(!createDeferredLightingResources())
        return MakeUnexpected(Failure{});
    if(!createDeferredCompositeResources())
        return MakeUnexpected(Failure{});

    m_deferredState.m_lightingPipeline.reset();
    m_deferredState.m_compositeComputePipeline.reset();
    m_deferredState.m_presentPipeline.reset();

    DeferredFrameTargets createdTargets;
    createdTargets.width = width;
    createdTargets.height = height;
    createdTargets.albedoFormat = albedoFormat;
    createdTargets.normalFormat = normalFormat;
    createdTargets.worldPositionFormat = worldPositionFormat;
    createdTargets.specularRoughnessFormat = worldPositionFormat;
    createdTargets.opaqueColorFormat = opaqueColorFormat;
    createdTargets.compositeColorFormat = opaqueColorFormat;
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
        // AsyncCompute lighting samples every G-buffer attachment after Graphics AVBOIT; keep their sharing concurrent.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/deferred/gbuffer_albedo").setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    auto albedoResult = m_graphics.createTexture(albedoDesc);
    if(albedoResult)
        createdTargets.albedo = Move(*albedoResult);
    else
        createdTargets.albedo.reset();
    if(!albedoResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred albedo target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc normalDesc;
    normalDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.normalFormat)
        .setInRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_normal")
        .setClearValue(ECSRenderDetail::s_GBufferNormalClearColor)
    ;
    auto normalResult = m_graphics.createTexture(normalDesc);
    if(normalResult)
        createdTargets.normal = Move(*normalResult);
    else
        createdTargets.normal.reset();
    if(!normalResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred normal target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc worldPositionDesc;
    worldPositionDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.worldPositionFormat)
        .setInRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_world_position")
        .setClearValue(ECSRenderDetail::s_GBufferWorldPositionClearColor)
    ;
    auto worldPositionResult = m_graphics.createTexture(worldPositionDesc);
    if(worldPositionResult)
        createdTargets.worldPosition = Move(*worldPositionResult);
    else
        createdTargets.worldPosition.reset();
    if(!worldPositionResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred world-position target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc specularRoughnessDesc;
    specularRoughnessDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.specularRoughnessFormat)
        .setInRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_specular_roughness")
        .setClearValue(ECSRenderDetail::s_GBufferSpecularRoughnessClearColor)
    ;
    auto specularRoughnessResult = m_graphics.createTexture(specularRoughnessDesc);
    if(specularRoughnessResult)
        createdTargets.specularRoughness = Move(*specularRoughnessResult);
    else
        createdTargets.specularRoughness.reset();
    if(!specularRoughnessResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred specular/roughness target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc opaqueColorDesc;
    opaqueColorDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.opaqueColorFormat)
        .setInUAV(true)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/opaque_color")
        .setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    auto opaqueColorResult = m_graphics.createTexture(opaqueColorDesc);
    if(opaqueColorResult)
        createdTargets.opaqueColor = Move(*opaqueColorResult);
    else
        createdTargets.opaqueColor.reset();
    if(!opaqueColorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred opaque color target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc compositeColorDesc;
    compositeColorDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.compositeColorFormat)
        .setInUAV(true)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/composite_color")
        .setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    auto compositeColorResult = m_graphics.createTexture(compositeColorDesc);
    if(compositeColorResult)
        createdTargets.compositeColor = Move(*compositeColorResult);
    else
        createdTargets.compositeColor.reset();
    if(!compositeColorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite color target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc depthDesc;
    depthDesc.setWidth(createdTargets.width).setHeight(createdTargets.height).setFormat(createdTargets.depthFormat).setInRenderTarget(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/deferred/depth");
    auto depthResult = m_graphics.createTexture(depthDesc);
    if(depthResult)
        createdTargets.depth = Move(*depthResult);
    else
        createdTargets.depth.reset();
    if(!depthResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred depth target"));
        return MakeUnexpected(Failure{});
    }

    Core::FramebufferAttachment gbufferAttachments[NWB_MESH_GBUFFER_TARGET_COUNT] = {};
    gbufferAttachments[NWB_MESH_GBUFFER_BASE_COLOR_LOCATION].setTexture(createdTargets.albedo.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    gbufferAttachments[NWB_MESH_GBUFFER_NORMAL_LOCATION].setTexture(createdTargets.normal.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    gbufferAttachments[NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION].setTexture(createdTargets.worldPosition.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    gbufferAttachments[NWB_MESH_GBUFFER_SPECULAR_ROUGHNESS_LOCATION].setTexture(createdTargets.specularRoughness.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    Core::FramebufferDesc framebufferDesc;
    for(const Core::FramebufferAttachment& attachment : gbufferAttachments)
        framebufferDesc.addColorAttachment(attachment);
    framebufferDesc.setDepthAttachment(createdTargets.depth.get(), ECSRenderDetail::s_FramebufferSubresources);
    auto framebufferResult = device.createFramebuffer(framebufferDesc);
    if(framebufferResult)
        createdTargets.framebuffer = Move(*framebufferResult);
    else
        createdTargets.framebuffer.reset();
    if(!framebufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return MakeUnexpected(Failure{});
    }

    return createdTargets;
}

bool RendererDeferredSystem::createDeferredFrameTargetResources(DeferredFrameTargets& targets, Core::Sampler& avboitLinearSampler){
    if(!createDeferredBindlessFrameResources(targets, avboitLinearSampler))
        return false;
    if(createLaggedLightingHistoryResources(targets))
        return true;
    resetDeferredBindlessFrameResources(targets);
    return false;
}

void RendererDeferredSystem::resetDeferredFrameTargets(DeferredFrameTargets& targets){
    resetLaggedLightingHistoryResources(targets);
    resetDeferredBindlessFrameResources(targets);
    targets.framebuffer.reset();

    targets.albedo.reset();
    targets.normal.reset();
    targets.worldPosition.reset();
    targets.specularRoughness.reset();
    targets.csgCapBackNormal.reset();
    targets.csgIntervalDepth.reset();
    targets.csgIntervalId.reset();
    targets.csgReceiverEventData.reset();
    targets.csgReceiverEventCount.reset();
    targets.csgReceiverSpanData.reset();
    targets.csgReceiverSpanCount.reset();
    targets.csgRemovedIntervalDepth.reset();
    targets.csgRemovedIntervalCapNormal.reset();
    targets.csgRemovedIntervalData.reset();
    targets.csgRemovedIntervalCount.reset();
    targets.opaqueColor.reset();
    targets.compositeColor.reset();
    targets.depth.reset();
    targets.shadowVisibility.reset();

    targets = DeferredFrameTargets{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

