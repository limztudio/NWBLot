// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_private.h>
#include <impl/assets/graphics/refraction/push_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_targets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<Core::TextureHandle> CreateRenderTarget(
    Core::GraphicsRuntime& graphics,
    const u32 width,
    const u32 height,
    const Core::Format::Enum format,
    const NameText<char> debugName,
    const Core::Color& clearValue,
    const bool shareWithAsyncCompute = false
){
    Core::TextureDesc desc;
    desc.setWidth(width).setHeight(height).setFormat(format).setInRenderTarget(true).setName(Name(debugName)).setClearValue(clearValue).setInitialState(Core::ResourceStates::Common).setKeepInitialState(true);
    if(shareWithAsyncCompute)
        desc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    return graphics.createTexture(desc);
}

static Expected<Core::TextureHandle> CreateTransmittanceVolume(Core::GraphicsRuntime& graphics, const u32 width, const u32 height, const u32 depth, const Core::Format::Enum format){
    Core::TextureDesc desc;
    desc
        .setWidth(width)
        .setHeight(height)
        .setDepth(depth)
        .setFormat(format)
        .setDimension(Core::TextureDimension::Texture3D)
        .setInUAV(true)
        // Cross-lane submissions own the execution/memory dependencies.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/avboit/transmittance_volume").setClearValue(Core::Color(1.f, 1.f, 1.f, 1.f)).setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
    ;
    return graphics.createTexture(desc);
}

static Expected<Core::BufferHandle> CreateU32Buffer(Core::GraphicsRuntime& graphics, const u64 byteSize, const NameText<char> debugName){
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitSystem::resetAvboitFrameTargets(AvboitFrameTargets& targets){
    // Release owned descriptors only; the slot payload is borrowed.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(targets.coverageBufferDescriptor);
        heap.free(targets.depthWarpBufferDescriptor);
        heap.free(targets.controlBufferDescriptor);
        heap.free(targets.extinctionBufferDescriptor);
        heap.free(targets.extinctionOverflowBufferDescriptor);
        heap.free(targets.transmittanceTextureStorageDescriptor);
        heap.free(targets.refractionTileListDescriptor);
        heap.free(targets.refractionTileArgumentsDescriptor);
    }

    targets.lowFramebuffer.reset();
    targets.accumulationFramebuffer.reset();
    targets.refractionFramebuffer.reset();

    targets.lowRasterTarget.reset();
    targets.accumColor.reset();
    targets.accumExtinction.reset();
    targets.transmittanceTexture.reset();

    targets.coverageBuffer.reset();
    targets.depthWarpBuffer.reset();
    targets.controlBuffer.reset();
    targets.extinctionBuffer.reset();
    targets.extinctionOverflowBuffer.reset();
    targets.refractionTileList.reset();
    targets.refractionTileArguments.reset();

    targets = AvboitFrameTargets{};
}

bool RendererAvboitSystem::createAvboitFrameTargets(DeferredFrameTargets& createdTargets){
    auto& device = m_graphics.getDevice();
    const Core::Format::Enum lowRasterFormat = SelectRendererAvboitLowRasterFormat(device);
    const Core::Format::Enum accumColorFormat = SelectRendererAvboitAccumColorFormat(device);
    const Core::Format::Enum accumExtinctionFormat = SelectRendererAvboitAccumExtinctionFormat(device);
    const Core::Format::Enum transmittanceFormat = SelectRendererAvboitTransmittanceFormat(device);
    if(lowRasterFormat == Core::Format::UNKNOWN || accumColorFormat == Core::Format::UNKNOWN || accumExtinctionFormat == Core::Format::UNKNOWN || transmittanceFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    AvboitFrameTargets avboitTargets;
    avboitTargets.fullWidth = createdTargets.width;
    avboitTargets.fullHeight = createdTargets.height;
    const u64 lowWidth = Max<u64>(1u, DivideUp(static_cast<u64>(createdTargets.width), static_cast<u64>(ECSRenderAvboitDetail::s_AvboitDownsample)));
    const u64 lowHeight = Max<u64>(1u, DivideUp(static_cast<u64>(createdTargets.height), static_cast<u64>(ECSRenderAvboitDetail::s_AvboitDownsample)));
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
    auto lowRasterTargetResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, avboitTargets.lowWidth, avboitTargets.lowHeight, avboitTargets.lowRasterFormat, "engine/avboit/low_raster", transparentBlack);
    if(!lowRasterTargetResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution raster target"));
        return false;
    }
    avboitTargets.lowRasterTarget = Move(*lowRasterTargetResult);

    auto accumColorResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, avboitTargets.fullWidth, avboitTargets.fullHeight, avboitTargets.accumColorFormat, "engine/avboit/accum_color", transparentBlack, true);
    if(!accumColorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated color target"));
        return false;
    }
    avboitTargets.accumColor = Move(*accumColorResult);

    auto accumExtinctionResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, avboitTargets.fullWidth, avboitTargets.fullHeight, avboitTargets.accumExtinctionFormat, "engine/avboit/accum_extinction", transparentBlack, true);
    if(!accumExtinctionResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated extinction target"));
        return false;
    }
    avboitTargets.accumExtinction = Move(*accumExtinctionResult);

    // Capture the primary interface before blending loses it.
    auto foregroundAccumColorResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, accumColorFormat, "engine/avboit/foreground_color", transparentBlack, true);
    auto foregroundAccumExtinctionResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, accumExtinctionFormat, "engine/avboit/foreground_extinction", transparentBlack, true);
    auto refractionNormalIorResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RGBA16_FLOAT, "engine/avboit/refraction_normal_ior", transparentBlack, true);
    auto refractionTintCoverageResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RGBA16_FLOAT, "engine/avboit/refraction_tint_coverage", transparentBlack, true);
    auto refractionInstanceResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RG32_FLOAT, "engine/avboit/refraction_instance", transparentBlack, true);
    auto refractionSpecularRoughnessResult = __hidden_avboit_targets::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RGBA16_FLOAT, "engine/avboit/refraction_specular_roughness", Core::Color(0.f, 0.f, 0.f, 1.f), true);
    Core::TextureDesc refractionDepthDesc = createdTargets.depth->getCreationDescription();
    refractionDepthDesc.setName("engine/avboit/refraction_depth").setInitialState(Core::ResourceStates::Common).setKeepInitialState(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    auto refractionDepthResult = m_graphics.createTexture(refractionDepthDesc);
    if(refractionDepthResult)
        avboitTargets.refractionDepth = Move(*refractionDepthResult);
    else
        avboitTargets.refractionDepth.reset();
    Core::TextureDesc refractionResolveDesc;
    refractionResolveDesc.setWidth(createdTargets.width).setHeight(createdTargets.height)
        .setFormat(Core::Format::RGBA16_FLOAT).setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/avboit/refraction_resolve").setClearValue(transparentBlack)
        .setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
    ;
    auto refractionResolveResult = m_graphics.createTexture(refractionResolveDesc);
    if(refractionResolveResult)
        avboitTargets.refractionResolve = Move(*refractionResolveResult);
    else
        avboitTargets.refractionResolve.reset();
    if(!foregroundAccumColorResult || !foregroundAccumExtinctionResult || !refractionNormalIorResult || !refractionTintCoverageResult || !refractionInstanceResult || !refractionSpecularRoughnessResult || !refractionDepthResult || !refractionResolveResult)
        return false;
    avboitTargets.foregroundAccumColor = Move(*foregroundAccumColorResult);
    avboitTargets.foregroundAccumExtinction = Move(*foregroundAccumExtinctionResult);
    avboitTargets.refractionNormalIor = Move(*refractionNormalIorResult);
    avboitTargets.refractionTintCoverage = Move(*refractionTintCoverageResult);
    avboitTargets.refractionInstance = Move(*refractionInstanceResult);
    avboitTargets.refractionSpecularRoughness = Move(*refractionSpecularRoughnessResult);
    Core::FramebufferDesc refractionFramebufferDesc;
    refractionFramebufferDesc
        .addColorAttachment(avboitTargets.refractionNormalIor.get(), ECSRenderDetail::s_FramebufferSubresources)
        .addColorAttachment(avboitTargets.refractionTintCoverage.get(), ECSRenderDetail::s_FramebufferSubresources)
        .addColorAttachment(avboitTargets.refractionInstance.get(), ECSRenderDetail::s_FramebufferSubresources)
        // Fourth output carries reflection inputs; keep it off foreground extinction.
        .addColorAttachment(avboitTargets.refractionSpecularRoughness.get(), ECSRenderDetail::s_FramebufferSubresources).setDepthAttachment(avboitTargets.refractionDepth.get(), ECSRenderDetail::s_FramebufferSubresources)
    ;
    auto refractionFramebufferResult = device.createFramebuffer(refractionFramebufferDesc);
    if(refractionFramebufferResult)
        avboitTargets.refractionFramebuffer = Move(*refractionFramebufferResult);
    else
        avboitTargets.refractionFramebuffer.reset();
    if(!refractionFramebufferResult)
        return false;

    Core::FramebufferDesc lowFramebufferDesc;
    lowFramebufferDesc.addColorAttachment(avboitTargets.lowRasterTarget.get(), ECSRenderDetail::s_FramebufferSubresources);
    auto lowFramebufferResult = device.createFramebuffer(lowFramebufferDesc);
    if(lowFramebufferResult)
        avboitTargets.lowFramebuffer = Move(*lowFramebufferResult);
    else
        avboitTargets.lowFramebuffer.reset();
    if(!lowFramebufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution framebuffer"));
        return false;
    }

    Core::FramebufferAttachment accumulationAttachments[NWB_AVBOIT_ACCUM_TARGET_COUNT] = {};
    accumulationAttachments[NWB_AVBOIT_ACCUM_COLOR_LOCATION].setTexture(avboitTargets.accumColor.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    accumulationAttachments[NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION].setTexture(avboitTargets.accumExtinction.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    accumulationAttachments[NWB_AVBOIT_ACCUM_FOREGROUND_COLOR_LOCATION].setTexture(avboitTargets.foregroundAccumColor.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    accumulationAttachments[NWB_AVBOIT_ACCUM_FOREGROUND_EXTINCTION_LOCATION].setTexture(avboitTargets.foregroundAccumExtinction.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);

    Core::FramebufferDesc accumulationFramebufferDesc;
    for(const Core::FramebufferAttachment& attachment : accumulationAttachments)
        accumulationFramebufferDesc.addColorAttachment(attachment);
    accumulationFramebufferDesc.setDepthAttachment(Core::FramebufferAttachment().setTexture(createdTargets.depth.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources).setReadOnly(true));
    auto accumulationFramebufferResult = device.createFramebuffer(accumulationFramebufferDesc);
    if(accumulationFramebufferResult)
        avboitTargets.accumulationFramebuffer = Move(*accumulationFramebufferResult);
    else
        avboitTargets.accumulationFramebuffer.reset();
    if(!accumulationFramebufferResult){
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
    const u32 physicalExtinctionWordCount = DivideUp(avboitTargets.physicalSliceCount, ECSRenderAvboitDetail::s_AvboitExtinctionSlicesPerWord);
    if(physicalExtinctionWordCount == 0 || lowPixelCount > static_cast<u64>(Limit<u32>::s_Max) / physicalExtinctionWordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT packed extinction word count exceeds u32 limits"));
        return false;
    }
    const u64 extinctionWordCount = lowPixelCount * physicalExtinctionWordCount;
    const u64 extinctionBytes = extinctionWordCount * sizeof(u32);
    const u64 extinctionOverflowBytes = lowPixelCount * sizeof(u32);

    auto coverageBufferResult = __hidden_avboit_targets::CreateU32Buffer(m_graphics, coverageBytes, "engine/avboit/depth_coverage");
    if(!coverageBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT coverage buffer"));
        return false;
    }
    avboitTargets.coverageBuffer = Move(*coverageBufferResult);

    auto depthWarpBufferResult = __hidden_avboit_targets::CreateU32Buffer(m_graphics, depthWarpBytes, "engine/avboit/depth_warp_lut");
    if(!depthWarpBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth warp buffer"));
        return false;
    }
    avboitTargets.depthWarpBuffer = Move(*depthWarpBufferResult);

    auto controlBufferResult = __hidden_avboit_targets::CreateU32Buffer(m_graphics, static_cast<u64>(ECSRenderAvboitDetail::s_AvboitControlWordCount) * sizeof(u32), "engine/avboit/control");
    if(!controlBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT control buffer"));
        return false;
    }
    avboitTargets.controlBuffer = Move(*controlBufferResult);

    auto extinctionBufferResult = __hidden_avboit_targets::CreateU32Buffer(m_graphics, extinctionBytes, "engine/avboit/packed_extinction_volume");
    if(!extinctionBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction volume"));
        return false;
    }
    avboitTargets.extinctionBuffer = Move(*extinctionBufferResult);

    auto extinctionOverflowBufferResult = __hidden_avboit_targets::CreateU32Buffer(m_graphics, extinctionOverflowBytes, "engine/avboit/extinction_overflow_depth");
    if(!extinctionOverflowBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction overflow buffer"));
        return false;
    }
    avboitTargets.extinctionOverflowBuffer = Move(*extinctionOverflowBufferResult);

    auto transmittanceTextureResult = __hidden_avboit_targets::CreateTransmittanceVolume(m_graphics, avboitTargets.lowWidth, avboitTargets.lowHeight, avboitTargets.physicalSliceCount, avboitTargets.transmittanceFormat);
    if(!transmittanceTextureResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT transmittance volume"));
        return false;
    }
    avboitTargets.transmittanceTexture = Move(*transmittanceTextureResult);

    const u64 tileCount = DivideUp(static_cast<u64>(createdTargets.width), static_cast<u64>(NWB_REFRACTION_GROUP_SIZE)) * DivideUp(static_cast<u64>(createdTargets.height), static_cast<u64>(NWB_REFRACTION_GROUP_SIZE));
    if(tileCount == 0u || tileCount > static_cast<u64>(Limit<u32>::s_Max) / sizeof(u32))
        return false;
    const auto createTileBuffer = [&](const u64 byteSize, const Name name, const bool indirect){
        Core::BufferDesc desc;
        desc.setByteSize(byteSize).setStructStride(sizeof(u32)).setCanHaveUAVs(true).setCanHaveRawViews(true).setIsDrawIndirectArgs(indirect)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setDebugName(name).enableAutomaticStateTracking(Core::ResourceStates::Common);
        return m_graphics.createBuffer(desc);
    };
    auto tileList = createTileBuffer(tileCount * sizeof(u32), Name("engine/refraction/tile_list"), false);
    auto tileArguments = createTileBuffer(NWB_REFRACTION_TILE_ARGUMENT_BYTES, Name("engine/refraction/tile_arguments"), true);
    if(!tileList || !tileArguments){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create refraction tile scheduling buffers"));
        return false;
    }
    avboitTargets.refractionTileList = Move(*tileList);
    avboitTargets.refractionTileArguments = Move(*tileArguments);

    // Pass setup waits for the deferred target builder.
    createdTargets.avboit = Move(avboitTargets);
    m_avboitState.m_targetsNeedClear = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

