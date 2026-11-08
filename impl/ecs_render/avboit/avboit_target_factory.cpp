// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_target_factory.h"

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::TextureHandle> AvboitTargetFactory::CreateRenderTarget(
    Core::GraphicsRuntime& graphics,
    const u32 width,
    const u32 height,
    const Core::Format::Enum format,
    const NameText<char> debugName,
    const Core::Color& clearValue,
    const b8 shareWithAsyncCompute
){
    Core::TextureDesc desc;
    desc.setWidth(width).setHeight(height).setFormat(format).setIsRenderTarget(true).setName(Name(debugName)).setClearValue(clearValue).setInitialState(Core::ResourceStates::Common).setKeepInitialState(true);
    if(shareWithAsyncCompute)
        desc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    return graphics.createTexture(desc);
}

Expected<Core::TextureHandle> AvboitTargetFactory::CreateTransmittanceVolume(Core::GraphicsRuntime& graphics, const u32 width, const u32 height, const u32 depth, const Core::Format::Enum format){
    Core::TextureDesc desc;
    desc
        .setWidth(width)
        .setHeight(height)
        .setDepth(depth)
        .setFormat(format)
        .setDimension(Core::TextureDimension::Texture3D)
        .setIsUAV(true)
        // Cross-lane submissions own the execution/memory dependencies.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/avboit/transmittance_volume").setClearValue(Core::Color(1.f, 1.f, 1.f, 1.f)).setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
    ;
    return graphics.createTexture(desc);
}

Expected<Core::BufferHandle> AvboitTargetFactory::CreateU32Buffer(Core::GraphicsRuntime& graphics, const u64 byteSize, const NameText<char> debugName){
    Core::BufferDesc desc;
    desc
        .setByteSize(byteSize)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        // Shared by raster passes and interleaved compute kernels.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(Name(debugName)).enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    return graphics.createBuffer(desc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

