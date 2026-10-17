// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgPeelTargets(DeferredFrameTargets& targets){
    if(targets.csgIntervalTargetsValid())
        return true;


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
        desc.setWidth(targets.width).setHeight(targets.height).setArraySize(layerCount).setFormat(format).setDimension(Core::TextureDimension::Texture2DArray).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        if(renderTarget)
            desc.setInRenderTarget(true);
        desc.setInUAV(true).setName(name);
        return m_graphics.createTexture(desc);
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

    auto csgCapBackNormalResult = createPeelTexture(targets.csgCapNormalFormat, Name("engine/deferred/csg_cap_back_normal"));
    if(csgCapBackNormalResult)
        targets.csgCapBackNormal = Move(*csgCapBackNormalResult);
    else
        targets.csgCapBackNormal.reset();
    if(!csgCapBackNormalResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG cap back-normal peel target"));
        return false;
    }

    auto csgIntervalDepthResult = createPeelTexture(targets.csgIntervalDepthFormat, Name("engine/deferred/csg_interval_depth"));
    if(csgIntervalDepthResult)
        targets.csgIntervalDepth = Move(*csgIntervalDepthResult);
    else
        targets.csgIntervalDepth.reset();
    if(!csgIntervalDepthResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval depth peel target"));
        return false;
    }

    auto csgIntervalIdResult = createPeelTexture(targets.csgIntervalIdFormat, Name("engine/deferred/csg_interval_id"));
    if(csgIntervalIdResult)
        targets.csgIntervalId = Move(*csgIntervalIdResult);
    else
        targets.csgIntervalId.reset();
    if(!csgIntervalIdResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG interval id peel target"));
        return false;
    }

    auto csgReceiverEventDataResult = createReceiverEventTexture(targets.csgReceiverEventDataFormat, Name("engine/deferred/csg_receiver_event_data"));
    if(csgReceiverEventDataResult)
        targets.csgReceiverEventData = Move(*csgReceiverEventDataResult);
    else
        targets.csgReceiverEventData.reset();
    if(!csgReceiverEventDataResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event data target"));
        return false;
    }

    auto csgReceiverEventCountResult = createReceiverEventCounterTexture(targets.csgReceiverEventCountFormat, Name("engine/deferred/csg_receiver_event_count"));
    if(csgReceiverEventCountResult)
        targets.csgReceiverEventCount = Move(*csgReceiverEventCountResult);
    else
        targets.csgReceiverEventCount.reset();
    if(!csgReceiverEventCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event count target"));
        return false;
    }

    auto csgReceiverSpanDataResult = createReceiverSpanTexture(targets.csgReceiverSpanDataFormat, Name("engine/deferred/csg_receiver_span_data"));
    if(csgReceiverSpanDataResult)
        targets.csgReceiverSpanData = Move(*csgReceiverSpanDataResult);
    else
        targets.csgReceiverSpanData.reset();
    if(!csgReceiverSpanDataResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span data target"));
        return false;
    }

    auto csgReceiverSpanCountResult = createReceiverSpanCounterTexture(targets.csgReceiverSpanCountFormat, Name("engine/deferred/csg_receiver_span_count"));
    if(csgReceiverSpanCountResult)
        targets.csgReceiverSpanCount = Move(*csgReceiverSpanCountResult);
    else
        targets.csgReceiverSpanCount.reset();
    if(!csgReceiverSpanCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span count target"));
        return false;
    }

    auto csgRemovedIntervalDepthResult = createRemovedIntervalTexture(targets.csgRemovedIntervalDepthFormat, Name("engine/deferred/csg_removed_interval_depth"));
    if(csgRemovedIntervalDepthResult)
        targets.csgRemovedIntervalDepth = Move(*csgRemovedIntervalDepthResult);
    else
        targets.csgRemovedIntervalDepth.reset();
    if(!csgRemovedIntervalDepthResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval depth target"));
        return false;
    }

    auto csgRemovedIntervalCapNormalResult = createRemovedIntervalTexture(targets.csgRemovedIntervalCapNormalFormat, Name("engine/deferred/csg_removed_interval_cap_normal"));
    if(csgRemovedIntervalCapNormalResult)
        targets.csgRemovedIntervalCapNormal = Move(*csgRemovedIntervalCapNormalResult);
    else
        targets.csgRemovedIntervalCapNormal.reset();
    if(!csgRemovedIntervalCapNormalResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval cap-normal target"));
        return false;
    }

    auto csgRemovedIntervalDataResult = createRemovedIntervalTexture(targets.csgRemovedIntervalDataFormat, Name("engine/deferred/csg_removed_interval_data"));
    if(csgRemovedIntervalDataResult)
        targets.csgRemovedIntervalData = Move(*csgRemovedIntervalDataResult);
    else
        targets.csgRemovedIntervalData.reset();
    if(!csgRemovedIntervalDataResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval data target"));
        return false;
    }

    auto csgRemovedIntervalCountResult = createRemovedIntervalCounterTexture(targets.csgRemovedIntervalCountFormat, Name("engine/deferred/csg_removed_interval_count"));
    if(csgRemovedIntervalCountResult)
        targets.csgRemovedIntervalCount = Move(*csgRemovedIntervalCountResult);
    else
        targets.csgRemovedIntervalCount.reset();
    if(!csgRemovedIntervalCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval count target"));
        return false;
    }


    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

