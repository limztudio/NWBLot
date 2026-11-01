// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/assets/graphics/refraction/push_constants.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitState::invalidateResources()noexcept{
    m_linearSampler.reset();
    m_depthWarpComputeShader.reset();
    m_integrateComputeShader.reset();
    m_depthWarpPipeline.reset();
    m_integratePipeline.reset();
    m_targetsNeedClear = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitFrameTargets::valid()const noexcept{
#if defined(NWB_DEBUG)
    return
        fullWidth > 0
        && fullHeight > 0
        && lowWidth > 0
        && lowHeight > 0
        && virtualSliceCount > 0
        && physicalSliceCount > 0
        && lowRasterFormat != Core::Format::UNKNOWN
        && accumColorFormat != Core::Format::UNKNOWN
        && accumExtinctionFormat != Core::Format::UNKNOWN
        && transmittanceFormat != Core::Format::UNKNOWN
        && lowRasterTarget != nullptr
        && accumColor != nullptr
        && accumExtinction != nullptr
        && transmittanceTexture != nullptr
        && lowFramebuffer != nullptr
        && accumulationFramebuffer != nullptr
        && coverageBuffer != nullptr
        && depthWarpBuffer != nullptr
        && controlBuffer != nullptr
        && extinctionBuffer != nullptr
        && extinctionOverflowBuffer != nullptr
        && refractionTileList != nullptr
        && refractionTileArguments != nullptr
        && coverageBufferDescriptor.valid()
        && depthWarpBufferDescriptor.valid()
        && controlBufferDescriptor.valid()
        && extinctionBufferDescriptor.valid()
        && extinctionOverflowBufferDescriptor.valid()
        && transmittanceTextureStorageDescriptor.valid()
        && refractionTileListDescriptor.valid()
        && refractionTileArgumentsDescriptor.valid()
        && deferredSlotsBufferDescriptor.valid()
    ;
#else
    return accumulationFramebuffer != nullptr;
#endif
}


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
    const b8 shareWithAsyncCompute = false
){
    Core::TextureDesc desc;
    desc.setWidth(width).setHeight(height).setFormat(format).setIsRenderTarget(true).setName(Name(debugName)).setClearValue(clearValue).setInitialState(Core::ResourceStates::Common).setKeepInitialState(true);
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
        .setIsUAV(true)
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

static Expected<Core::GpuDescriptorHandle> RegisterWorkBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(!heap.write(handle, Core::DescriptorWriteItem::StructuredBufferUav(0u, &buffer))){
        return MakeUnexpected(Failure{});
    }

    retireUnpublished.release();
    return handle;
}

static Expected<Core::GpuDescriptorHandle> RegisterTransmittanceStorageTexture(Core::GpuDescriptorHeap& heap, Core::Texture& texture, const Core::Format::Enum format){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(!heap.write(handle, Core::DescriptorWriteItem::TextureUav(0u, &texture, format, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture3D))){
        return MakeUnexpected(Failure{});
    }

    retireUnpublished.release();
    return handle;
}

static void RetireTargetDescriptors(Core::GpuDescriptorHeap& heap, AvboitFrameTargets& targets){
    heap.free(targets.coverageBufferDescriptor);
    heap.free(targets.depthWarpBufferDescriptor);
    heap.free(targets.controlBufferDescriptor);
    heap.free(targets.extinctionBufferDescriptor);
    heap.free(targets.extinctionOverflowBufferDescriptor);
    heap.free(targets.transmittanceTextureStorageDescriptor);
    heap.free(targets.refractionTileListDescriptor);
    heap.free(targets.refractionTileArgumentsDescriptor);
    targets.coverageBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.depthWarpBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.controlBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.extinctionBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.extinctionOverflowBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.transmittanceTextureStorageDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.refractionTileListDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.refractionTileArgumentsDescriptor = Core::GpuDescriptorHandle::Invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitSystem::resetAvboitFrameTargets(AvboitFrameTargets& targets){
    // Release owned descriptors only; the slot payload is borrowed.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized())
        __hidden_avboit_targets::RetireTargetDescriptors(heap, targets);

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

b8 RendererAvboitSystem::createAvboitFrameTargets(DeferredFrameTargets& createdTargets){
    auto& device = m_graphics.getDevice();
    const Core::Format::Enum lowRasterFormat = SelectRendererAvboitLowRasterFormat(device);
    const Core::Format::Enum accumColorFormat = SelectRendererAvboitAccumColorFormat(device);
    const Core::Format::Enum accumExtinctionFormat = SelectRendererAvboitAccumExtinctionFormat(device);
    const Core::Format::Enum transmittanceFormat = SelectRendererAvboitTransmittanceFormat(device);
    if(lowRasterFormat == Core::Format::UNKNOWN || accumColorFormat == Core::Format::UNKNOWN || accumExtinctionFormat == Core::Format::UNKNOWN || transmittanceFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    AvboitTargetLayout layout;
    if(!TryResolveAvboitTargetLayout(createdTargets.width, createdTargets.height, NWB_REFRACTION_GROUP_SIZE, layout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: invalid AVBOIT target layout dimensions"));
        return false;
    }

    AvboitFrameTargets avboitTargets;
    avboitTargets.fullWidth = createdTargets.width;
    avboitTargets.fullHeight = createdTargets.height;
    avboitTargets.lowWidth = layout.lowWidth;
    avboitTargets.lowHeight = layout.lowHeight;
    avboitTargets.virtualSliceCount = layout.virtualSliceCount;
    avboitTargets.physicalSliceCount = layout.physicalSliceCount;
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
        .setFormat(Core::Format::RGBA16_FLOAT).setIsUAV(true)
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

    Array<Core::FramebufferAttachment, NWB_AVBOIT_ACCUM_TARGET_COUNT> accumulationAttachments = {};
    accumulationAttachments[NWB_AVBOIT_ACCUM_COLOR_LOCATION].setTexture(avboitTargets.accumColor.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    accumulationAttachments[NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION].setTexture(avboitTargets.accumExtinction.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    accumulationAttachments[NWB_AVBOIT_ACCUM_FOREGROUND_COLOR_LOCATION].setTexture(avboitTargets.foregroundAccumColor.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    accumulationAttachments[NWB_AVBOIT_ACCUM_FOREGROUND_EXTINCTION_LOCATION].setTexture(avboitTargets.foregroundAccumExtinction.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);

    Core::FramebufferDesc accumulationFramebufferDesc;
    for(const Core::FramebufferAttachment& attachment : accumulationAttachments)
        accumulationFramebufferDesc.addColorAttachment(attachment);
    accumulationFramebufferDesc.setDepthAttachment(Core::FramebufferAttachment().setTexture(createdTargets.depth.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources).setIsReadOnly(true));
    auto accumulationFramebufferResult = device.createFramebuffer(accumulationFramebufferDesc);
    if(accumulationFramebufferResult)
        avboitTargets.accumulationFramebuffer = Move(*accumulationFramebufferResult);
    else
        avboitTargets.accumulationFramebuffer.reset();
    if(!accumulationFramebufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation framebuffer"));
        return false;
    }

    const u64 coverageBytes = layout.coverageWordCount * sizeof(u32);
    const u64 depthWarpBytes = layout.depthWarpWordCount * sizeof(u32);
    const u64 extinctionBytes = layout.extinctionWordCount * sizeof(u32);
    const u64 extinctionOverflowBytes = layout.pixelCount * sizeof(u32);

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

    auto controlBufferResult = __hidden_avboit_targets::CreateU32Buffer(m_graphics, static_cast<u64>(s_AvboitControlWordCount) * sizeof(u32), "engine/avboit/control");
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

    const u64 tileCount = layout.refractionTileCount;
    const auto createTileBuffer = [&](const u64 byteSize, const Name name, const b8 indirect){
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

b8 RendererAvboitSystem::registerAvboitFrameTargetDescriptors(DeferredFrameTargets& createdTargets, AvboitFrameTargets& avboitTargets){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT target bindings require the descriptor-buffer global heap"));
        return false;
    }
    NWB_ASSERT(createdTargets.bindless.slotsBufferDescriptor.valid());
    NWB_ASSERT(createdTargets.bindless.slotsBufferDescriptor.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer);
    NWB_ASSERT(!createdTargets.bindless.slotsUploaded);
    NWB_ASSERT(avboitTargets.coverageBuffer);
    NWB_ASSERT(avboitTargets.depthWarpBuffer);
    NWB_ASSERT(avboitTargets.controlBuffer);
    NWB_ASSERT(avboitTargets.extinctionBuffer);
    NWB_ASSERT(avboitTargets.extinctionOverflowBuffer);
    NWB_ASSERT(avboitTargets.transmittanceTexture);
    NWB_ASSERT(avboitTargets.refractionTileList);
    NWB_ASSERT(avboitTargets.refractionTileArguments);
    NWB_ASSERT(!avboitTargets.coverageBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.depthWarpBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.controlBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.extinctionBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.extinctionOverflowBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.transmittanceTextureStorageDescriptor.valid());
    NWB_ASSERT(!avboitTargets.refractionTileListDescriptor.valid());
    NWB_ASSERT(!avboitTargets.refractionTileArgumentsDescriptor.valid());

    const auto targetResourcesRegistered = [&]() -> Expected<void>{
        if(const auto descriptor = __hidden_avboit_targets::RegisterWorkBuffer(heap, *avboitTargets.coverageBuffer); descriptor)
            avboitTargets.coverageBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_targets::RegisterWorkBuffer(heap, *avboitTargets.depthWarpBuffer); descriptor)
            avboitTargets.depthWarpBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_targets::RegisterWorkBuffer(heap, *avboitTargets.controlBuffer); descriptor)
            avboitTargets.controlBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_targets::RegisterWorkBuffer(heap, *avboitTargets.extinctionBuffer); descriptor)
            avboitTargets.extinctionBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_targets::RegisterWorkBuffer(heap, *avboitTargets.extinctionOverflowBuffer); descriptor)
            avboitTargets.extinctionOverflowBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_targets::RegisterTransmittanceStorageTexture(heap, *avboitTargets.transmittanceTexture, avboitTargets.transmittanceFormat); descriptor)
            avboitTargets.transmittanceTextureStorageDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_targets::RegisterWorkBuffer(heap, *avboitTargets.refractionTileList); descriptor)
            avboitTargets.refractionTileListDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_targets::RegisterWorkBuffer(heap, *avboitTargets.refractionTileArguments); descriptor)
            avboitTargets.refractionTileArgumentsDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        return {};
    };
    if(!targetResourcesRegistered()){
        __hidden_avboit_targets::RetireTargetDescriptors(heap, avboitTargets);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register AVBOIT work resources in the descriptor heap"));
        return false;
    }

    // Borrow the shared slot payload; populate it before deferred uploads it.
    avboitTargets.deferredSlotsBufferDescriptor = createdTargets.bindless.slotsBufferDescriptor;
    createdTargets.bindless.slots.avboitCoverage = avboitTargets.coverageBufferDescriptor.slot();
    createdTargets.bindless.slots.avboitDepthWarp = avboitTargets.depthWarpBufferDescriptor.slot();
    createdTargets.bindless.slots.avboitControl = avboitTargets.controlBufferDescriptor.slot();
    createdTargets.bindless.slots.avboitExtinction = avboitTargets.extinctionBufferDescriptor.slot();
    createdTargets.bindless.slots.avboitExtinctionOverflow = avboitTargets.extinctionOverflowBufferDescriptor.slot();
    createdTargets.bindless.slots.avboitTransmittanceStorage = avboitTargets.transmittanceTextureStorageDescriptor.slot();

    return true;
}

Core::Sampler& RendererAvboitSystem::linearSampler()const noexcept{
    NWB_ASSERT(m_avboitState.m_linearSampler);
    return *m_avboitState.m_linearSampler;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

