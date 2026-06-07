// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgPeelTargets(DeferredFrameTargets& targets){
    NWB_ASSERT(!targets.csgCapNormal);
    NWB_ASSERT(!targets.csgIntervalDepth);
    NWB_ASSERT(!targets.csgIntervalId);
    NWB_ASSERT(!targets.csgReceiverFrontSurfaceMask);
    NWB_ASSERT(!targets.csgReceiverSurfaceMask);
    NWB_ASSERT(!targets.csgReceiverBackSurfaceMask);
    NWB_ASSERT(targets.width > 0u && targets.height > 0u);
    NWB_ASSERT(targets.csgCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalIdFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverFrontSurfaceMaskFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSurfaceMaskFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverBackSurfaceMaskFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgPeelLayerCount == ECSRenderDetail::s_CsgPeelLayerCount);

    auto createPeelTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(targets.csgPeelLayerCount)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInRenderTarget(true)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgCapNormal = createPeelTexture(targets.csgCapNormalFormat, Name("engine/deferred/csg_cap_normal"));
    if(!targets.csgCapNormal){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG cap normal peel target"));
        return false;
    }

    targets.csgIntervalDepth = createPeelTexture(targets.csgIntervalDepthFormat, Name("engine/deferred/csg_interval_depth"));
    if(!targets.csgIntervalDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval depth peel target"));
        return false;
    }

    targets.csgIntervalId = createPeelTexture(targets.csgIntervalIdFormat, Name("engine/deferred/csg_interval_id"));
    if(!targets.csgIntervalId){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval id peel target"));
        return false;
    }

    Core::TextureDesc receiverFrontSurfaceMaskDesc;
    receiverFrontSurfaceMaskDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.csgReceiverFrontSurfaceMaskFormat)
        .setDimension(Core::TextureDimension::Texture2D)
        .setInRenderTarget(true)
        .setInUAV(true)
        .setName("engine/deferred/csg_receiver_front_surface_mask")
    ;
    targets.csgReceiverFrontSurfaceMask = graphics().createTexture(receiverFrontSurfaceMaskDesc);
    if(!targets.csgReceiverFrontSurfaceMask){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver front surface mask target"));
        return false;
    }

    Core::TextureDesc receiverSurfaceMaskDesc;
    receiverSurfaceMaskDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.csgReceiverSurfaceMaskFormat)
        .setDimension(Core::TextureDimension::Texture2D)
        .setInRenderTarget(true)
        .setInUAV(true)
        .setName("engine/deferred/csg_receiver_surface_mask")
    ;
    targets.csgReceiverSurfaceMask = graphics().createTexture(receiverSurfaceMaskDesc);
    if(!targets.csgReceiverSurfaceMask){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver surface mask target"));
        return false;
    }

    Core::TextureDesc receiverBackSurfaceMaskDesc;
    receiverBackSurfaceMaskDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.csgReceiverBackSurfaceMaskFormat)
        .setDimension(Core::TextureDimension::Texture2D)
        .setInRenderTarget(true)
        .setInUAV(true)
        .setName("engine/deferred/csg_receiver_back_surface_mask")
    ;
    targets.csgReceiverBackSurfaceMask = graphics().createTexture(receiverBackSurfaceMaskDesc);
    if(!targets.csgReceiverBackSurfaceMask){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver back surface mask target"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
