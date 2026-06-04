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
    NWB_ASSERT(targets.width > 0u && targets.height > 0u);
    NWB_ASSERT(targets.csgCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalIdFormat != Core::Format::UNKNOWN);
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

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
