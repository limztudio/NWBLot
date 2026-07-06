// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgPeelTargets(DeferredFrameTargets& targets){
    if(targets.csgIntervalTargetsValid())
        return true;

    destroyCsgIntervalPeelBindingSet();
    csgState().m_receiverSurfaceBindingSet.reset();
    csgState().m_intervalSampleBindingSet.reset();

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

    NWB_ASSERT(targets.width > 0u && targets.height > 0u);
    NWB_ASSERT(targets.csgCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalIdFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgPeelLayerCount == ECSRenderDetail::s_CsgPeelLayerCount);
    NWB_ASSERT(targets.csgReceiverEventLayerCount == ECSRenderDetail::s_CsgReceiverEventLayerCount);
    NWB_ASSERT(targets.csgReceiverSpanLayerCount == ECSRenderDetail::s_CsgReceiverSpanLayerCount);
    NWB_ASSERT(targets.csgRemovedIntervalLayerCount == ECSRenderDetail::s_CsgRemovedIntervalLayerCount);

    auto createCsgTexture = [&](const Core::Format::Enum format, const Name& name, const u32 layerCount, const bool renderTarget){
        Core::TextureDesc desc;
        desc
            .setWidth(targets.width)
            .setHeight(targets.height)
            .setArraySize(layerCount)
            .setFormat(format)
            .setDimension(Core::TextureDimension::Texture2DArray)
        ;
        if(renderTarget)
            desc.setInRenderTarget(true);
        desc.setInUAV(true).setName(name);
        return graphics().createTexture(desc);
    };
    auto createPeelTexture = [&](const Core::Format::Enum format, const Name& name){
        return createCsgTexture(format, name, targets.csgPeelLayerCount, true);
    };
    auto createReceiverEventTexture = [&](const Core::Format::Enum format, const Name& name){
        return createCsgTexture(format, name, targets.csgReceiverEventLayerCount, false);
    };
    auto createReceiverEventCounterTexture = [&](const Core::Format::Enum format, const Name& name){
        return createCsgTexture(format, name, 1u, false);
    };
    auto createReceiverSpanTexture = [&](const Core::Format::Enum format, const Name& name){
        return createCsgTexture(format, name, targets.csgReceiverSpanLayerCount, false);
    };
    auto createReceiverSpanCounterTexture = [&](const Core::Format::Enum format, const Name& name){
        return createCsgTexture(format, name, 1u, false);
    };
    auto createRemovedIntervalTexture = [&](const Core::Format::Enum format, const Name& name){
        return createCsgTexture(format, name, targets.csgRemovedIntervalLayerCount, false);
    };
    auto createRemovedIntervalCounterTexture = [&](const Core::Format::Enum format, const Name& name){
        return createCsgTexture(format, name, 1u, false);
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

    targets.csgIntervalId = createPeelTexture(targets.csgIntervalIdFormat, Name("engine/deferred/csg_interval_id"));
    if(!targets.csgIntervalId){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval id peel target"));
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

    targets.csgReceiverEventCount = createReceiverEventCounterTexture(
        targets.csgReceiverEventCountFormat,
        Name("engine/deferred/csg_receiver_event_count")
    );
    if(!targets.csgReceiverEventCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event count target"));
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

    targets.csgReceiverSpanCount = createReceiverSpanCounterTexture(
        targets.csgReceiverSpanCountFormat,
        Name("engine/deferred/csg_receiver_span_count")
    );
    if(!targets.csgReceiverSpanCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span count target"));
        return false;
    }

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

    targets.csgRemovedIntervalCount = createRemovedIntervalCounterTexture(
        targets.csgRemovedIntervalCountFormat,
        Name("engine/deferred/csg_removed_interval_count")
    );
    if(!targets.csgRemovedIntervalCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval count target"));
        return false;
    }


    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

