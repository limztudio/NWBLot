// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgPeelTargets(DeferredFrameTargets& targets){
    NWB_ASSERT(!targets.csgCapBackNormal);
    NWB_ASSERT(!targets.csgIntervalDepth);
    NWB_ASSERT(!targets.csgIntervalLinearDepth);
    NWB_ASSERT(!targets.csgIntervalId);
    NWB_ASSERT(!targets.csgReceiverEventDepth);
    NWB_ASSERT(!targets.csgReceiverEventData);
    NWB_ASSERT(!targets.csgReceiverEventCount);
    NWB_ASSERT(!targets.csgReceiverEventFlags);
    NWB_ASSERT(!targets.csgReceiverSpanDepth);
    NWB_ASSERT(!targets.csgReceiverSpanData);
    NWB_ASSERT(!targets.csgReceiverSpanCount);
    NWB_ASSERT(!targets.csgReceiverSpanFlags);
    NWB_ASSERT(!targets.csgRemovedIntervalDepth);
    NWB_ASSERT(!targets.csgRemovedIntervalCapNormal);
    NWB_ASSERT(!targets.csgRemovedIntervalData);
    NWB_ASSERT(!targets.csgRemovedIntervalCount);
    NWB_ASSERT(!targets.csgRemovedIntervalFlags);
    NWB_ASSERT(targets.width > 0u && targets.height > 0u);
    NWB_ASSERT(targets.csgCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalLinearDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalIdFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventFlagsFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanFlagsFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalFlagsFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgPeelLayerCount == ECSRenderDetail::s_CsgPeelLayerCount);
    NWB_ASSERT(targets.csgReceiverEventLayerCount == ECSRenderDetail::s_CsgReceiverEventLayerCount);
    NWB_ASSERT(targets.csgReceiverSpanLayerCount == ECSRenderDetail::s_CsgReceiverSpanLayerCount);
    NWB_ASSERT(targets.csgRemovedIntervalLayerCount == ECSRenderDetail::s_CsgRemovedIntervalLayerCount);

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

    targets.csgCapBackNormal = createPeelTexture(targets.csgCapNormalFormat, Name("engine/deferred/csg_cap_back_normal"));
    if(!targets.csgCapBackNormal){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG cap back-normal peel target"));
        return false;
    }

    targets.csgIntervalDepth = createPeelTexture(targets.csgIntervalDepthFormat, Name("engine/deferred/csg_interval_depth"));
    if(!targets.csgIntervalDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval depth peel target"));
        return false;
    }

    auto createPeelComputeTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(targets.csgPeelLayerCount)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgIntervalLinearDepth = createPeelComputeTexture(
        targets.csgIntervalLinearDepthFormat,
        Name("engine/deferred/csg_interval_linear_depth")
    );
    if(!targets.csgIntervalLinearDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval linear-depth target"));
        return false;
    }

    targets.csgIntervalId = createPeelTexture(targets.csgIntervalIdFormat, Name("engine/deferred/csg_interval_id"));
    if(!targets.csgIntervalId){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval id peel target"));
        return false;
    }

    auto createReceiverEventTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(targets.csgReceiverEventLayerCount)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgReceiverEventDepth = createReceiverEventTexture(
        targets.csgReceiverEventDepthFormat,
        Name("engine/deferred/csg_receiver_event_depth")
    );
    if(!targets.csgReceiverEventDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event depth target"));
        return false;
    }

    targets.csgReceiverEventData = createReceiverEventTexture(
        targets.csgReceiverEventDataFormat,
        Name("engine/deferred/csg_receiver_event_data")
    );
    if(!targets.csgReceiverEventData){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event data target"));
        return false;
    }

    auto createReceiverEventCounterTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(1u)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgReceiverEventCount = createReceiverEventCounterTexture(
        targets.csgReceiverEventCountFormat,
        Name("engine/deferred/csg_receiver_event_count")
    );
    if(!targets.csgReceiverEventCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event count target"));
        return false;
    }

    targets.csgReceiverEventFlags = createReceiverEventCounterTexture(
        targets.csgReceiverEventFlagsFormat,
        Name("engine/deferred/csg_receiver_event_flags")
    );
    if(!targets.csgReceiverEventFlags){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event flags target"));
        return false;
    }

    auto createReceiverSpanTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(targets.csgReceiverSpanLayerCount)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgReceiverSpanDepth = createReceiverSpanTexture(
        targets.csgReceiverSpanDepthFormat,
        Name("engine/deferred/csg_receiver_span_depth")
    );
    if(!targets.csgReceiverSpanDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span depth target"));
        return false;
    }

    targets.csgReceiverSpanData = createReceiverSpanTexture(
        targets.csgReceiverSpanDataFormat,
        Name("engine/deferred/csg_receiver_span_data")
    );
    if(!targets.csgReceiverSpanData){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span data target"));
        return false;
    }

    auto createReceiverSpanCounterTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(1u)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgReceiverSpanCount = createReceiverSpanCounterTexture(
        targets.csgReceiverSpanCountFormat,
        Name("engine/deferred/csg_receiver_span_count")
    );
    if(!targets.csgReceiverSpanCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span count target"));
        return false;
    }

    targets.csgReceiverSpanFlags = createReceiverSpanCounterTexture(
        targets.csgReceiverSpanFlagsFormat,
        Name("engine/deferred/csg_receiver_span_flags")
    );
    if(!targets.csgReceiverSpanFlags){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span flags target"));
        return false;
    }

    auto createRemovedIntervalTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(targets.csgRemovedIntervalLayerCount)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgRemovedIntervalDepth = createRemovedIntervalTexture(
        targets.csgRemovedIntervalDepthFormat,
        Name("engine/deferred/csg_removed_interval_depth")
    );
    if(!targets.csgRemovedIntervalDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval depth target"));
        return false;
    }

    targets.csgRemovedIntervalCapNormal = createRemovedIntervalTexture(
        targets.csgRemovedIntervalCapNormalFormat,
        Name("engine/deferred/csg_removed_interval_cap_normal")
    );
    if(!targets.csgRemovedIntervalCapNormal){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval cap-normal target"));
        return false;
    }

    targets.csgRemovedIntervalData = createRemovedIntervalTexture(
        targets.csgRemovedIntervalDataFormat,
        Name("engine/deferred/csg_removed_interval_data")
    );
    if(!targets.csgRemovedIntervalData){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval data target"));
        return false;
    }

    auto createRemovedIntervalCounterTexture = [&](const Core::Format::Enum format, const Name& name){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(1u)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
            .setInUAV(true)
            .setName(name)
        ;
        return graphics().createTexture(desc);
    };

    targets.csgRemovedIntervalCount = createRemovedIntervalCounterTexture(
        targets.csgRemovedIntervalCountFormat,
        Name("engine/deferred/csg_removed_interval_count")
    );
    if(!targets.csgRemovedIntervalCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval count target"));
        return false;
    }

    targets.csgRemovedIntervalFlags = createRemovedIntervalCounterTexture(
        targets.csgRemovedIntervalFlagsFormat,
        Name("engine/deferred/csg_removed_interval_flags")
    );
    if(!targets.csgRemovedIntervalFlags){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval flags target"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

