// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_targets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::TextureHandle CreateRenderTarget(
    Core::Graphics& graphics,
    const u32 width,
    const u32 height,
    const Core::Format::Enum format,
    const char* debugName,
    const Core::Color& clearValue,
    const bool shareWithAsyncCompute = false
){
    Core::TextureDesc desc;
    desc
        .setWidth(width)
        .setHeight(height)
        .setFormat(format)
        .setInRenderTarget(true)
        .setName(debugName)
        .setClearValue(clearValue)
    ;
    if(shareWithAsyncCompute)
        desc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    Core::TextureHandle texture = graphics.createTexture(desc);
    if(texture)
        return texture;

    return {};
}

static Core::TextureHandle CreateTransmittanceVolume(
    Core::Graphics& graphics,
    const u32 width,
    const u32 height,
    const u32 depth,
    const Core::Format::Enum format
){
    Core::TextureDesc desc;
    desc
        .setWidth(width)
        .setHeight(height)
        .setDepth(depth)
        .setFormat(format)
        .setDimension(Core::TextureDimension::Texture3D)
        .setInUAV(true)
        // Integration writes this volume on AsyncCompute and accumulation samples it on Graphics. The ordered
        // cross-lane submissions provide execution and memory dependencies without a queue-family ownership ping-pong.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/avboit/transmittance_volume")
        .setClearValue(Core::Color(1.f, 1.f, 1.f, 1.f))
    ;
    Core::TextureHandle texture = graphics.createTexture(desc);
    if(texture)
        return texture;

    return {};
}

static Core::BufferHandle CreateU32Buffer(
    Core::Graphics& graphics,
    const u64 byteSize,
    const char* debugName
){
    Core::BufferDesc desc;
    desc
        .setByteSize(byteSize)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        // Occupancy/extinction raster passes and the interleaved compute kernels exchange these work buffers.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(debugName)
    ;
    Core::BufferHandle buffer = graphics.createBuffer(desc);
    if(buffer)
        return buffer;

    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitSystem::resetAvboitFrameTargets(AvboitFrameTargets& targets){
    // AVBOIT owns its five transient work-buffer registrations plus the writable transmittance StorageImage. The
    // shared deferred slot-payload descriptor is borrowed, so release only owned descriptors before their targets.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(targets.coverageBufferDescriptor);
        heap.free(targets.depthWarpBufferDescriptor);
        heap.free(targets.controlBufferDescriptor);
        heap.free(targets.extinctionBufferDescriptor);
        heap.free(targets.extinctionOverflowBufferDescriptor);
        heap.free(targets.transmittanceTextureStorageDescriptor);
    }

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

bool RendererAvboitSystem::createAvboitFrameTargets(DeferredFrameTargets& createdTargets){
    auto& device = m_graphics.getDevice();
    const Core::Format::Enum lowRasterFormat = SelectRendererAvboitLowRasterFormat(device);
    const Core::Format::Enum accumColorFormat = SelectRendererAvboitAccumColorFormat(device);
    const Core::Format::Enum accumExtinctionFormat = SelectRendererAvboitAccumExtinctionFormat(device);
    const Core::Format::Enum transmittanceFormat = SelectRendererAvboitTransmittanceFormat(device);
    if(
        lowRasterFormat == Core::Format::UNKNOWN
        || accumColorFormat == Core::Format::UNKNOWN
        || accumExtinctionFormat == Core::Format::UNKNOWN
        || transmittanceFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    AvboitFrameTargets avboitTargets;
    avboitTargets.fullWidth = createdTargets.width;
    avboitTargets.fullHeight = createdTargets.height;
    const u64 lowWidth = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.width), static_cast<u64>(ECSRenderAvboitDetail::s_AvboitDownsample))
    );
    const u64 lowHeight = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.height), static_cast<u64>(ECSRenderAvboitDetail::s_AvboitDownsample))
    );
    if(lowWidth > Limit<u32>::s_Max || lowHeight > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution dimensions exceed u32 limits"));
        return false;
    }
    avboitTargets.lowWidth = static_cast<u32>(lowWidth);
    avboitTargets.lowHeight = static_cast<u32>(lowHeight);
    avboitTargets.virtualSliceCount = ECSRenderAvboitDetail::s_AvboitVirtualSlices;
    avboitTargets.physicalSliceCount = ECSRenderAvboitDetail::s_AvboitPhysicalSlices;
    avboitTargets.lowRasterFormat = lowRasterFormat;
    avboitTargets.accumColorFormat = accumColorFormat;
    avboitTargets.accumExtinctionFormat = accumExtinctionFormat;
    avboitTargets.transmittanceFormat = transmittanceFormat;

    const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
    avboitTargets.lowRasterTarget = __hidden_avboit_targets::CreateRenderTarget(
        m_graphics,
        avboitTargets.lowWidth,
        avboitTargets.lowHeight,
        avboitTargets.lowRasterFormat,
        "engine/avboit/low_raster",
        transparentBlack
    );
    if(!avboitTargets.lowRasterTarget){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution raster target"));
        return false;
    }

    avboitTargets.accumColor = __hidden_avboit_targets::CreateRenderTarget(
        m_graphics,
        avboitTargets.fullWidth,
        avboitTargets.fullHeight,
        avboitTargets.accumColorFormat,
        "engine/avboit/accum_color",
        transparentBlack,
        true
    );
    if(!avboitTargets.accumColor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated color target"));
        return false;
    }

    avboitTargets.accumExtinction = __hidden_avboit_targets::CreateRenderTarget(
        m_graphics,
        avboitTargets.fullWidth,
        avboitTargets.fullHeight,
        avboitTargets.accumExtinctionFormat,
        "engine/avboit/accum_extinction",
        transparentBlack,
        true
    );
    if(!avboitTargets.accumExtinction){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated extinction target"));
        return false;
    }

    Core::FramebufferDesc lowFramebufferDesc;
    lowFramebufferDesc.addColorAttachment(avboitTargets.lowRasterTarget.get(), ECSRenderDetail::s_FramebufferSubresources);
    avboitTargets.lowFramebuffer = device.createFramebuffer(lowFramebufferDesc);
    if(!avboitTargets.lowFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution framebuffer"));
        return false;
    }

    Core::FramebufferAttachment accumulationAttachments[NWB_AVBOIT_ACCUM_TARGET_COUNT] = {};
    accumulationAttachments[NWB_AVBOIT_ACCUM_COLOR_LOCATION]
        .setTexture(avboitTargets.accumColor.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    accumulationAttachments[NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION]
        .setTexture(avboitTargets.accumExtinction.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;

    Core::FramebufferDesc accumulationFramebufferDesc;
    for(const Core::FramebufferAttachment& attachment : accumulationAttachments)
        accumulationFramebufferDesc.addColorAttachment(attachment);
    accumulationFramebufferDesc.setDepthAttachment(
        Core::FramebufferAttachment()
            .setTexture(createdTargets.depth.get())
            .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
            .setReadOnly(true)
    );
    avboitTargets.accumulationFramebuffer = device.createFramebuffer(accumulationFramebufferDesc);
    if(!avboitTargets.accumulationFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation framebuffer"));
        return false;
    }

    const u32 coverageWordCount = DivideUp(avboitTargets.virtualSliceCount, NWB_AVBOIT_COVERAGE_SLICES_PER_WORD);
    const u64 coverageBytes = static_cast<u64>(coverageWordCount) * sizeof(u32);
    const u64 depthWarpBytes = static_cast<u64>(avboitTargets.virtualSliceCount) * sizeof(u32);
    const u64 lowPixelCount = static_cast<u64>(avboitTargets.lowWidth) * avboitTargets.lowHeight;
    if(lowPixelCount > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution pixel count exceeds u32 limits"));
        return false;
    }
    const u32 physicalExtinctionWordCount = DivideUp(
        avboitTargets.physicalSliceCount,
        ECSRenderAvboitDetail::s_AvboitExtinctionSlicesPerWord
    );
    if(physicalExtinctionWordCount == 0 || lowPixelCount > static_cast<u64>(Limit<u32>::s_Max) / physicalExtinctionWordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT packed extinction word count exceeds u32 limits"));
        return false;
    }
    const u64 extinctionWordCount = lowPixelCount * physicalExtinctionWordCount;
    const u64 extinctionBytes = extinctionWordCount * sizeof(u32);
    const u64 extinctionOverflowBytes = lowPixelCount * sizeof(u32);

    avboitTargets.coverageBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        coverageBytes,
        "engine/avboit/depth_coverage"
    );
    if(!avboitTargets.coverageBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT coverage buffer"));
        return false;
    }

    avboitTargets.depthWarpBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        depthWarpBytes,
        "engine/avboit/depth_warp_lut"
    );
    if(!avboitTargets.depthWarpBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth warp buffer"));
        return false;
    }

    avboitTargets.controlBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        static_cast<u64>(ECSRenderAvboitDetail::s_AvboitControlWordCount) * sizeof(u32),
        "engine/avboit/control"
    );
    if(!avboitTargets.controlBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT control buffer"));
        return false;
    }

    avboitTargets.extinctionBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        extinctionBytes,
        "engine/avboit/packed_extinction_volume"
    );
    if(!avboitTargets.extinctionBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction volume"));
        return false;
    }

    avboitTargets.extinctionOverflowBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        extinctionOverflowBytes,
        "engine/avboit/extinction_overflow_depth"
    );
    if(!avboitTargets.extinctionOverflowBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction overflow buffer"));
        return false;
    }

    avboitTargets.transmittanceTexture = __hidden_avboit_targets::CreateTransmittanceVolume(
        m_graphics,
        avboitTargets.lowWidth,
        avboitTargets.lowHeight,
        avboitTargets.physicalSliceCount,
        avboitTargets.transmittanceFormat
    );
    if(!avboitTargets.transmittanceTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT transmittance volume"));
        return false;
    }

    // AVBOIT material passes share DeferredBindlessFrameResources::slotsBuffer. That buffer is created after all
    // frame targets are registered in the global heap, so pass setup waits for the deferred target builder.
    createdTargets.avboit = Move(avboitTargets);
    m_avboitState.m_targetsNeedClear = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

