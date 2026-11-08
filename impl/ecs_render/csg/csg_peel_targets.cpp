// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"
#include "format_private.h"

#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/csg/graph_types.adhd.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum SelectCsgFormat(const Core::Format::Enum candidate, const Core::FormatSupport::Mask requiredSupport, Core::Device& device)noexcept{
    const Array<Core::Format::Enum, 1> candidates = { candidate };
    return SelectSupportedFormat(device, candidates, requiredSupport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererCsgSystem::createCsgPeelTargets(DeferredFrameTargets& targets){
    if(targets.csgIntervalTargetsValid())
        return true;


    ResetCsgIntervalTargets(targets);

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
    NWB_ASSERT(targets.csgPeelLayerCount == s_CsgPeelLayerCount);
    NWB_ASSERT(targets.csgReceiverEventLayerCount == s_CsgReceiverEventLayerCount);
    NWB_ASSERT(targets.csgReceiverSpanLayerCount == s_CsgReceiverSpanLayerCount);
    NWB_ASSERT(targets.csgRemovedIntervalLayerCount == s_CsgRemovedIntervalLayerCount);

    const auto createCsgTexture = [&](
        const Core::Format::Enum format,
        const Name& name,
        const u32 layerCount,
        const b8 renderTarget,
        const tchar* const errorMessage,
        Core::TextureHandle& outTexture
    ){
        Core::TextureDesc desc;
        desc.setWidth(targets.width).setHeight(targets.height).setArraySize(layerCount).setFormat(format).setDimension(Core::TextureDimension::Texture2DArray).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        if(renderTarget)
            desc.setIsRenderTarget(true);
        desc.setIsUAV(true).setName(name);
        auto result = m_graphics.createTexture(desc);
        if(!result){
            outTexture.reset();
            NWB_LOGGER_ERROR(errorMessage);
            return false;
        }
        outTexture = Move(*result);
        return true;
    };

    return
        createCsgTexture(targets.csgCapNormalFormat, Name(s_RenderNameEngineDeferredCsgCapBackNormal), targets.csgPeelLayerCount, true, NWB_TEXT("RendererSystem: failed to create deferred CSG cap back-normal peel target"), targets.csgCapBackNormal)
        && createCsgTexture(targets.csgIntervalDepthFormat, Name(s_RenderNameEngineDeferredCsgIntervalDepth), targets.csgPeelLayerCount, true, NWB_TEXT("RendererSystem: failed to create deferred CSG interval depth peel target"), targets.csgIntervalDepth)
        && createCsgTexture(targets.csgIntervalIdFormat, Name(s_RenderNameEngineDeferredCsgIntervalId), targets.csgPeelLayerCount, true, NWB_TEXT("RendererSystem: failed to create deferred CSG interval id peel target"), targets.csgIntervalId)
        && createCsgTexture(
            targets.csgReceiverEventDataFormat, Name(s_RenderNameEngineDeferredCsgReceiverEventData), targets.csgReceiverEventLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event data target"), targets.csgReceiverEventData
        )
        && createCsgTexture(targets.csgReceiverEventCountFormat, Name(s_RenderNameEngineDeferredCsgReceiverEventCount), 1u, false, NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event count target"), targets.csgReceiverEventCount)
        && createCsgTexture(
            targets.csgReceiverSpanDataFormat, Name(s_RenderNameEngineDeferredCsgReceiverSpanData), targets.csgReceiverSpanLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span data target"), targets.csgReceiverSpanData
        )
        && createCsgTexture(targets.csgReceiverSpanCountFormat, Name(s_RenderNameEngineDeferredCsgReceiverSpanCount), 1u, false, NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span count target"), targets.csgReceiverSpanCount)
        && createCsgTexture(
            targets.csgRemovedIntervalDepthFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalDepth), targets.csgRemovedIntervalLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval depth target"), targets.csgRemovedIntervalDepth
        )
        && createCsgTexture(
            targets.csgRemovedIntervalCapNormalFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalCapNormal), targets.csgRemovedIntervalLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval cap-normal target"), targets.csgRemovedIntervalCapNormal
        )
        && createCsgTexture(
            targets.csgRemovedIntervalDataFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalData), targets.csgRemovedIntervalLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval data target"), targets.csgRemovedIntervalData
        )
        && createCsgTexture(targets.csgRemovedIntervalCountFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalCount), 1u, false, NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval count target"), targets.csgRemovedIntervalCount)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

