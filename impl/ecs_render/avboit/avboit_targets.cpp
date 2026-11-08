// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/avboit/avboit_raster_configuration.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/avboit/avboit_target_factory.h>
#include <impl/ecs_render/refraction/refraction_target_builder.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
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


static void RetireTargetDescriptors(Core::GpuDescriptorHeap& heap, AvboitFrameTargets& targets){
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.coverageBufferDescriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.depthWarpBufferDescriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.controlBufferDescriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.extinctionBufferDescriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.extinctionOverflowBufferDescriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.transmittanceTextureStorageDescriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.refractionTileListDescriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, targets.refractionTileArgumentsDescriptor);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitSystem::resetAvboitFrameTargets(AvboitFrameTargets& targets){
    // Release owned descriptors only; the slot payload is borrowed.
    __hidden_avboit_targets::RetireTargetDescriptors(m_graphics.getDevice().getDescriptorHeap(), targets);

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
    const Core::Format::Enum lowRasterFormat = AvboitRasterConfiguration::SelectRendererAvboitLowRasterFormat(device);
    const Core::Format::Enum accumColorFormat = AvboitRasterConfiguration::SelectRendererAvboitAccumColorFormat(device);
    const Core::Format::Enum accumExtinctionFormat = AvboitRasterConfiguration::SelectRendererAvboitAccumExtinctionFormat(device);
    const Core::Format::Enum transmittanceFormat = AvboitRasterConfiguration::SelectRendererAvboitTransmittanceFormat(device);
    if(lowRasterFormat == Core::Format::UNKNOWN || accumColorFormat == Core::Format::UNKNOWN || accumExtinctionFormat == Core::Format::UNKNOWN || transmittanceFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    const Expected<AvboitTargetLayout> layoutResult = TryResolveAvboitTargetLayout(createdTargets.width, createdTargets.height, NWB_REFRACTION_GROUP_SIZE);
    if(!layoutResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: invalid AVBOIT target layout dimensions"));
        return false;
    }
    const AvboitTargetLayout& layout = *layoutResult;

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
    auto lowRasterTargetResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, avboitTargets.lowWidth, avboitTargets.lowHeight, avboitTargets.lowRasterFormat, "engine/avboit/low_raster", transparentBlack);
    if(!lowRasterTargetResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution raster target"));
        return false;
    }
    avboitTargets.lowRasterTarget = Move(*lowRasterTargetResult);

    auto accumColorResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, avboitTargets.fullWidth, avboitTargets.fullHeight, avboitTargets.accumColorFormat, "engine/avboit/accum_color", transparentBlack, true);
    if(!accumColorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated color target"));
        return false;
    }
    avboitTargets.accumColor = Move(*accumColorResult);

    auto accumExtinctionResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, avboitTargets.fullWidth, avboitTargets.fullHeight, avboitTargets.accumExtinctionFormat, "engine/avboit/accum_extinction", transparentBlack, true);
    if(!accumExtinctionResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated extinction target"));
        return false;
    }
    avboitTargets.accumExtinction = Move(*accumExtinctionResult);

    RefractionTargetBuilder refractionTargets(m_graphics);
    if(!refractionTargets.createImages(createdTargets, avboitTargets, accumColorFormat, accumExtinctionFormat, transparentBlack))
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

    auto coverageBufferResult = AvboitTargetFactory::CreateU32Buffer(m_graphics, coverageBytes, "engine/avboit/depth_coverage");
    if(!coverageBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT coverage buffer"));
        return false;
    }
    avboitTargets.coverageBuffer = Move(*coverageBufferResult);

    auto depthWarpBufferResult = AvboitTargetFactory::CreateU32Buffer(m_graphics, depthWarpBytes, "engine/avboit/depth_warp_lut");
    if(!depthWarpBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth warp buffer"));
        return false;
    }
    avboitTargets.depthWarpBuffer = Move(*depthWarpBufferResult);

    auto controlBufferResult = AvboitTargetFactory::CreateU32Buffer(m_graphics, static_cast<u64>(s_AvboitControlWordCount) * sizeof(u32), "engine/avboit/control");
    if(!controlBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT control buffer"));
        return false;
    }
    avboitTargets.controlBuffer = Move(*controlBufferResult);

    auto extinctionBufferResult = AvboitTargetFactory::CreateU32Buffer(m_graphics, extinctionBytes, "engine/avboit/packed_extinction_volume");
    if(!extinctionBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction volume"));
        return false;
    }
    avboitTargets.extinctionBuffer = Move(*extinctionBufferResult);

    auto extinctionOverflowBufferResult = AvboitTargetFactory::CreateU32Buffer(m_graphics, extinctionOverflowBytes, "engine/avboit/extinction_overflow_depth");
    if(!extinctionOverflowBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction overflow buffer"));
        return false;
    }
    avboitTargets.extinctionOverflowBuffer = Move(*extinctionOverflowBufferResult);

    auto transmittanceTextureResult = AvboitTargetFactory::CreateTransmittanceVolume(m_graphics, avboitTargets.lowWidth, avboitTargets.lowHeight, avboitTargets.physicalSliceCount, avboitTargets.transmittanceFormat);
    if(!transmittanceTextureResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT transmittance volume"));
        return false;
    }
    avboitTargets.transmittanceTexture = Move(*transmittanceTextureResult);

    if(!refractionTargets.createTileBuffers(layout.refractionTileCount, avboitTargets))
        return false;

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

    const b8 targetResourcesRegistered =
        RendererDescriptorRegisterDetail::AssignStructuredBufferUav(heap, *avboitTargets.coverageBuffer, avboitTargets.coverageBufferDescriptor)
        && RendererDescriptorRegisterDetail::AssignStructuredBufferUav(heap, *avboitTargets.depthWarpBuffer, avboitTargets.depthWarpBufferDescriptor)
        && RendererDescriptorRegisterDetail::AssignStructuredBufferUav(heap, *avboitTargets.controlBuffer, avboitTargets.controlBufferDescriptor)
        && RendererDescriptorRegisterDetail::AssignStructuredBufferUav(heap, *avboitTargets.extinctionBuffer, avboitTargets.extinctionBufferDescriptor)
        && RendererDescriptorRegisterDetail::AssignStructuredBufferUav(heap, *avboitTargets.extinctionOverflowBuffer, avboitTargets.extinctionOverflowBufferDescriptor)
        && RendererDescriptorRegisterDetail::AssignDescriptor(
            heap,
            Core::GpuDescriptorClass::StorageImage,
            Core::DescriptorWriteItem::TextureUav(0u, avboitTargets.transmittanceTexture.get(), avboitTargets.transmittanceFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture3D),
            avboitTargets.transmittanceTextureStorageDescriptor
        )
        && RendererDescriptorRegisterDetail::AssignStructuredBufferUav(heap, *avboitTargets.refractionTileList, avboitTargets.refractionTileListDescriptor)
        && RendererDescriptorRegisterDetail::AssignStructuredBufferUav(heap, *avboitTargets.refractionTileArguments, avboitTargets.refractionTileArgumentsDescriptor)
    ;
    if(!targetResourcesRegistered){
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

