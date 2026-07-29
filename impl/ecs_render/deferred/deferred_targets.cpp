// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_targets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgIntervalSubresources{
    Core::TextureSubresourceSet peel;
    Core::TextureSubresourceSet receiverEvent;
    Core::TextureSubresourceSet receiverEventCounter;
    Core::TextureSubresourceSet receiverSpan;
    Core::TextureSubresourceSet receiverSpanCounter;
    Core::TextureSubresourceSet removedInterval;
    Core::TextureSubresourceSet removedIntervalCounter;
};

[[nodiscard]] static CsgIntervalSubresources MakeCsgIntervalSubresources(const DeferredFrameTargets& targets){
    return {
        Core::TextureSubresourceSet(0, 1, 0, targets.csgPeelLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, targets.csgReceiverEventLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, 1),
        Core::TextureSubresourceSet(0, 1, 0, targets.csgReceiverSpanLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, 1),
        Core::TextureSubresourceSet(0, 1, 0, targets.csgRemovedIntervalLayerCount),
        Core::TextureSubresourceSet(0, 1, 0, 1)
    };
}

static void AssertCsgIntervalTargetsAvailable([[maybe_unused]] const DeferredFrameTargets& targets){
    NWB_ASSERT(targets.csgCapBackNormal);
    NWB_ASSERT(targets.csgIntervalDepth);
    NWB_ASSERT(targets.csgIntervalId);
    NWB_ASSERT(targets.csgReceiverEventData);
    NWB_ASSERT(targets.csgReceiverEventCount);
    NWB_ASSERT(targets.csgReceiverSpanData);
    NWB_ASSERT(targets.csgReceiverSpanCount);
    NWB_ASSERT(targets.csgRemovedIntervalDepth);
    NWB_ASSERT(targets.csgRemovedIntervalCapNormal);
    NWB_ASSERT(targets.csgRemovedIntervalData);
    NWB_ASSERT(targets.csgRemovedIntervalCount);
    NWB_ASSERT(targets.csgPeelLayerCount > 0u);
    NWB_ASSERT(targets.csgReceiverEventLayerCount > 0u);
    NWB_ASSERT(targets.csgReceiverSpanLayerCount > 0u);
    NWB_ASSERT(targets.csgRemovedIntervalLayerCount > 0u);
}

static void SetCsgIntervalTargetCopyDestStates(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgIntervalSubresources& subresources
){
    commandList.setTextureState(targets.csgCapBackNormal.get(), subresources.peel, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgIntervalDepth.get(), subresources.peel, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgIntervalId.get(), subresources.peel, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventData.get(), subresources.receiverEvent, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverEventCount.get(), subresources.receiverEventCounter, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanData.get(), subresources.receiverSpan, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgReceiverSpanCount.get(), subresources.receiverSpanCounter, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), subresources.removedInterval, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), subresources.removedInterval, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalData.get(), subresources.removedInterval, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.csgRemovedIntervalCount.get(), subresources.removedIntervalCounter, Core::ResourceStates::CopyDest);
}

static void ClearCsgIntervalTargets(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgIntervalSubresources& subresources,
    const Core::Rect& csgClearRect
){
    // The CSG interval targets are per-pixel append buffers. Every consumer bounds its reads by a
    // per-pixel counter (receiver-event / span / removed-interval counts) or by the cutter-interval
    // id, and the span/removed-interval counts and flags are rewritten for every work-region pixel by
    // their producing compute pass. Only state that accumulates across a frame needs resetting:
    //  - receiver event count is atomically incremented from zero by the surface pass (its overflow is
    //    derived later from count > layer budget, so no separate event-flags target is needed),
    //  - interval id is written sparsely by the peel pass (unwritten layers must read back as empty).
    // The bulk depth/normal/data layers and the span/removed counters are written before they are
    // read, so clearing them is wasted bandwidth (this clear dominated the CSG frame cost).
    commandList.clearTextureRectUInt(targets.csgIntervalId.get(), subresources.peel, csgClearRect, 0u);
    commandList.clearTextureRectUInt(targets.csgReceiverEventCount.get(), subresources.receiverEventCounter, csgClearRect, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDeferredSystem::resetAvboitFrameTargets(AvboitFrameTargets& targets){
    // AVBOIT owns its five transient work-buffer registrations plus the writable transmittance StorageImage. The
    // shared deferred slot-payload descriptor is borrowed, so release only owned descriptors before their targets.
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
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

bool RendererDeferredSystem::createDeferredBindlessFrameResources(DeferredFrameTargets& targets){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred lighting/compositor requires the global descriptor heap"));
        return false;
    }
    if(!deferredState().m_sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred bindless resources require the deferred sampler"));
        return false;
    }
    if(!avboitState().m_linearSampler){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred bindless resources require the AVBOIT linear sampler"));
        return false;
    }
    if(!deferredState().m_sceneShadingBuffer || !deferredState().m_lightBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred bindless resources require the scene shading + light buffers"));
        return false;
    }
    if(!targets.csgIntervalTargetsValid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred bindless resources require the CSG interval targets"));
        return false;
    }

    auto registerTexture = [&heap](
        Core::GpuDescriptorHandle& handle,
        const Core::GpuDescriptorClass::Enum descriptorClass,
        Core::Texture* texture,
        const Core::Format::Enum format,
        const Core::TextureSubresourceSet& subresources,
        const Core::TextureDimension::Enum dimension
    ) -> bool{
        handle = heap.allocate(descriptorClass);
        if(!handle.valid())
            return false;
        if(heap.write(handle, Core::DescriptorWriteItem::Texture_SRV(0u, texture, format, subresources, dimension)))
            return true;
        heap.free(handle);
        handle = Core::GpuDescriptorHandle::invalid();
        return false;
    };

    // One target may need both sampled and storage views.  Keep a persistent StorageImage descriptor for every
    // writable target; shader aliases choose the appropriate 2D/2DArray/typed view while explicit command-list
    // transitions select SHADER_READ_ONLY vs GENERAL for each use.
    auto registerStorageTexture = [&heap](
        Core::GpuDescriptorHandle& handle,
        Core::Texture* texture,
        const Core::Format::Enum format,
        const Core::TextureDimension::Enum dimension
    ) -> bool{
        handle = heap.allocate(Core::GpuDescriptorClass::StorageImage);
        if(!handle.valid())
            return false;
        if(heap.write(handle, Core::DescriptorWriteItem::Texture_UAV(
            0u,
            texture,
            format,
            Core::s_AllSubresources,
            dimension
        )))
            return true;
        heap.free(handle);
        handle = Core::GpuDescriptorHandle::invalid();
        return false;
    };

    auto registerSampler = [&heap](Core::GpuDescriptorHandle& handle, Core::Sampler* sampler) -> bool{
        handle = heap.allocate(Core::GpuDescriptorClass::Sampler);
        if(!handle.valid())
            return false;
        if(heap.write(handle, Core::DescriptorWriteItem::Sampler(0u, sampler)))
            return true;
        heap.free(handle);
        handle = Core::GpuDescriptorHandle::invalid();
        return false;
    };

    // Shared scene buffers: the light list rides the StorageBuffer table (structured SRV), the scene-shading cbuffer
    // the UniformBuffer table. Both are read-only per-frame singletons the lighting shader selects by slot.
    auto registerStructuredBuffer = [&heap](Core::GpuDescriptorHandle& handle, Core::Buffer* buffer) -> bool{
        handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!handle.valid())
            return false;
        if(heap.write(handle, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer)))
            return true;
        heap.free(handle);
        handle = Core::GpuDescriptorHandle::invalid();
        return false;
    };

    auto registerConstantBuffer = [&heap](Core::GpuDescriptorHandle& handle, Core::Buffer* buffer) -> bool{
        handle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
        if(!handle.valid())
            return false;
        if(heap.write(handle, Core::DescriptorWriteItem::ConstantBuffer(0u, buffer)))
            return true;
        heap.free(handle);
        handle = Core::GpuDescriptorHandle::invalid();
        return false;
    };

    DeferredBindlessFrameResources& bindless = targets.bindless;
    const bool registered =
        registerTexture(bindless.gbufferBaseColor, Core::GpuDescriptorClass::SampledImage, targets.albedo.get(), targets.albedoFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.gbufferNormal, Core::GpuDescriptorClass::SampledImage, targets.normal.get(), targets.normalFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.gbufferWorldPosition, Core::GpuDescriptorClass::SampledImage, targets.worldPosition.get(), targets.worldPositionFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.gbufferDepth, Core::GpuDescriptorClass::SampledImage, targets.depth.get(), targets.depthFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.shadowVisibility, Core::GpuDescriptorClass::SampledImage2DArray, targets.shadowVisibility.get(), targets.shadowVisibilityFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.shadowVisibilityStorage, targets.shadowVisibility.get(), targets.shadowVisibilityFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.causticIrradiance, Core::GpuDescriptorClass::SampledImage, targets.causticIrradiance.get(), targets.causticIrradianceFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.causticIrradianceStorage, targets.causticIrradiance.get(), targets.causticIrradianceFormat, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.surfelIrradiance, Core::GpuDescriptorClass::SampledImage, targets.surfelIrradiance.get(), targets.surfelIrradianceFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.surfelIrradianceStorage, targets.surfelIrradiance.get(), targets.surfelIrradianceFormat, Core::TextureDimension::Texture2D)
        // The surfel GI resolve/upsample pair selects every persistent buffer and frame image through heap slots in
        // push constants; both writable irradiance views therefore have StorageImage registrations.
        && registerTexture(bindless.surfelIrradianceHalf, Core::GpuDescriptorClass::SampledImage, targets.surfelIrradianceHalf.get(), targets.surfelIrradianceFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.surfelIrradianceHalfStorage, targets.surfelIrradianceHalf.get(), targets.surfelIrradianceFormat, Core::TextureDimension::Texture2D)
        && registerSampler(bindless.sampler, deferredState().m_sampler.get())
        && registerTexture(bindless.opaqueColor, Core::GpuDescriptorClass::SampledImage, targets.opaqueColor.get(), targets.opaqueColorFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.avboitAccumColor, Core::GpuDescriptorClass::SampledImage, targets.avboit.accumColor.get(), targets.avboit.accumColorFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.avboitAccumExtinction, Core::GpuDescriptorClass::SampledImage, targets.avboit.accumExtinction.get(), targets.avboit.accumExtinctionFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.avboitTransmittance, Core::GpuDescriptorClass::SampledImage3D, targets.avboit.transmittanceTexture.get(), targets.avboit.transmittanceFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture3D)
        && registerSampler(bindless.avboitLinearSampler, avboitState().m_linearSampler.get())
        // Scene-shading cbuffer (uniform-buffer table) + light-list storage buffer (structured-buffer table): the two
        // shared singletons the deferred lighting pass now reads from the heap via its two spare avboit slot lanes.
        && registerConstantBuffer(bindless.sceneShading, deferredState().m_sceneShadingBuffer.get())
        && registerStructuredBuffer(bindless.lightList, deferredState().m_lightBuffer.get())
        // CSG interval/peel resources use one persistent StorageImage descriptor each. Their target-generation slots
        // are consumed by the CSG compute, material surface, and cap-fill shaders through the shared slot cbuffer.
        && registerStorageTexture(bindless.csgCapBackNormal, targets.csgCapBackNormal.get(), targets.csgCapNormalFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgIntervalDepth, targets.csgIntervalDepth.get(), targets.csgIntervalDepthFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgIntervalId, targets.csgIntervalId.get(), targets.csgIntervalIdFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgReceiverEventData, targets.csgReceiverEventData.get(), targets.csgReceiverEventDataFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgReceiverEventCount, targets.csgReceiverEventCount.get(), targets.csgReceiverEventCountFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgReceiverSpanData, targets.csgReceiverSpanData.get(), targets.csgReceiverSpanDataFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgReceiverSpanCount, targets.csgReceiverSpanCount.get(), targets.csgReceiverSpanCountFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgRemovedIntervalDepth, targets.csgRemovedIntervalDepth.get(), targets.csgRemovedIntervalDepthFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgRemovedIntervalCapNormal, targets.csgRemovedIntervalCapNormal.get(), targets.csgRemovedIntervalCapNormalFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgRemovedIntervalData, targets.csgRemovedIntervalData.get(), targets.csgRemovedIntervalDataFormat, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.csgRemovedIntervalCount, targets.csgRemovedIntervalCount.get(), targets.csgRemovedIntervalCountFormat, Core::TextureDimension::Texture2DArray)
        // The caustic resolve carries all sampled inputs through target-generation slots. The R32_UINT accumulator uses
        // the heap's dedicated typed uint Texture2DArray table; the remaining resources are floating-point Texture2Ds.
        && registerTexture(bindless.causticAccumulator, Core::GpuDescriptorClass::SampledImage2DArrayUint, targets.causticAccumulator.get(), targets.causticAccumulatorFormat, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.causticAccumulatorStorage, targets.causticAccumulator.get(), targets.causticAccumulatorFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.causticHistory, Core::GpuDescriptorClass::SampledImage, targets.causticHistory.get(), targets.causticHistoryFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.causticHistoryStorage, targets.causticHistory.get(), targets.causticHistoryFormat, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.causticResolveHalf, Core::GpuDescriptorClass::SampledImage, targets.causticResolveHalf.get(), targets.causticHistoryFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.causticResolveHalfStorage, targets.causticResolveHalf.get(), targets.causticHistoryFormat, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.causticResolveGeometry, Core::GpuDescriptorClass::SampledImage, targets.causticResolveGeometry.get(), targets.causticHistoryFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.causticResolveGeometryStorage, targets.causticResolveGeometry.get(), targets.causticHistoryFormat, Core::TextureDimension::Texture2D)
        // Every soft-shadow producer/resolve work image has both its sampled read view and, where a compute pass
        // writes it, a StorageImage view. The heap owns each target generation until recorded dispatches retire.
        && registerStorageTexture(bindless.shadowCoarseTransmittanceStorage, targets.shadowCoarseTransmittance.get(), targets.shadowCoarseTransmittanceFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.shadowSoftGeometry, Core::GpuDescriptorClass::SampledImage, targets.shadowSoftGeometry.get(), targets.shadowSoftGeometryFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.shadowSoftGeometryStorage, targets.shadowSoftGeometry.get(), targets.shadowSoftGeometryFormat, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.shadowSoftGeometryPrev, Core::GpuDescriptorClass::SampledImage, targets.shadowSoftGeometryPrev.get(), targets.shadowSoftGeometryFormat, ECSRenderDetail::s_FramebufferSubresources, Core::TextureDimension::Texture2D)
        && registerStorageTexture(bindless.shadowSoftGeometryPrevStorage, targets.shadowSoftGeometryPrev.get(), targets.shadowSoftGeometryFormat, Core::TextureDimension::Texture2D)
        && registerTexture(bindless.shadowSoftHalfA, Core::GpuDescriptorClass::SampledImage2DArray, targets.shadowSoftHalfA.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.shadowSoftHalfAStorage, targets.shadowSoftHalfA.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.shadowSoftHalfB, Core::GpuDescriptorClass::SampledImage2DArray, targets.shadowSoftHalfB.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.shadowSoftHalfBStorage, targets.shadowSoftHalfB.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.shadowHistA, Core::GpuDescriptorClass::SampledImage2DArray, targets.shadowHistA.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.shadowHistAStorage, targets.shadowHistA.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.shadowHistB, Core::GpuDescriptorClass::SampledImage2DArray, targets.shadowHistB.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.shadowHistBStorage, targets.shadowHistB.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.shadowMomentsA, Core::GpuDescriptorClass::SampledImage2DArray, targets.shadowMomentsA.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.shadowMomentsAStorage, targets.shadowMomentsA.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.shadowMomentsB, Core::GpuDescriptorClass::SampledImage2DArray, targets.shadowMomentsB.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.shadowMomentsBStorage, targets.shadowMomentsB.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.transparentSoftHalf, Core::GpuDescriptorClass::SampledImage2DArray, targets.transparentSoftHalf.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.transparentSoftHalfStorage, targets.transparentSoftHalf.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.transparentHistA, Core::GpuDescriptorClass::SampledImage2DArray, targets.transparentHistA.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.transparentHistAStorage, targets.transparentHistA.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.transparentHistB, Core::GpuDescriptorClass::SampledImage2DArray, targets.transparentHistB.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.transparentHistBStorage, targets.transparentHistB.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.transparentMomentsA, Core::GpuDescriptorClass::SampledImage2DArray, targets.transparentMomentsA.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.transparentMomentsAStorage, targets.transparentMomentsA.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
        && registerTexture(bindless.transparentMomentsB, Core::GpuDescriptorClass::SampledImage2DArray, targets.transparentMomentsB.get(), targets.shadowSoftFormat, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::TextureDimension::Texture2DArray)
        && registerStorageTexture(bindless.transparentMomentsBStorage, targets.transparentMomentsB.get(), targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray)
    ;
    if(!registered){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register deferred frame resources in the descriptor heap"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    bindless.slots.gbufferBaseColor = bindless.gbufferBaseColor.slot();
    bindless.slots.gbufferNormal = bindless.gbufferNormal.slot();
    bindless.slots.gbufferWorldPosition = bindless.gbufferWorldPosition.slot();
    bindless.slots.gbufferDepth = bindless.gbufferDepth.slot();
    bindless.slots.shadowVisibility = bindless.shadowVisibility.slot();
    bindless.slots.causticIrradiance = bindless.causticIrradiance.slot();
    bindless.slots.surfelIrradiance = bindless.surfelIrradiance.slot();
    bindless.slots.sampler = bindless.sampler.slot();
    bindless.slots.opaqueColor = bindless.opaqueColor.slot();
    bindless.slots.avboitAccumColor = bindless.avboitAccumColor.slot();
    bindless.slots.avboitAccumExtinction = bindless.avboitAccumExtinction.slot();
    bindless.slots.avboitTransmittance = bindless.avboitTransmittance.slot();
    bindless.slots.avboitLinearSampler = bindless.avboitLinearSampler.slot();
    bindless.slots.sceneShading = bindless.sceneShading.slot();
    bindless.slots.lightList = bindless.lightList.slot();
    bindless.slots.csgCapBackNormal = bindless.csgCapBackNormal.slot();
    bindless.slots.csgIntervalDepth = bindless.csgIntervalDepth.slot();
    bindless.slots.csgIntervalId = bindless.csgIntervalId.slot();
    bindless.slots.csgReceiverEventData = bindless.csgReceiverEventData.slot();
    bindless.slots.csgReceiverEventCount = bindless.csgReceiverEventCount.slot();
    bindless.slots.csgReceiverSpanData = bindless.csgReceiverSpanData.slot();
    bindless.slots.csgReceiverSpanCount = bindless.csgReceiverSpanCount.slot();
    bindless.slots.csgRemovedIntervalDepth = bindless.csgRemovedIntervalDepth.slot();
    bindless.slots.csgRemovedIntervalCapNormal = bindless.csgRemovedIntervalCapNormal.slot();
    bindless.slots.csgRemovedIntervalData = bindless.csgRemovedIntervalData.slot();
    bindless.slots.csgRemovedIntervalCount = bindless.csgRemovedIntervalCount.slot();

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(DeferredBindlessResourceSlots))
        .setIsConstantBuffer(true)
        .setDebugName("ECSRender_DeferredBindlessResourceSlots")
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    bindless.slotsBuffer = graphics().createBuffer(slotsBufferDesc);
    if(!bindless.slotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred bindless slot buffer"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    // The indirection payload is itself heap-addressable.  Every consumer receives this one UniformBuffer slot in
    // push constants instead of binding a local selector CBV, keeping the descriptor heap as the only resource
    // binding surface for ordinary renderer passes.
    bindless.slotsBufferDescriptor = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(
        !bindless.slotsBufferDescriptor.valid()
        || !heap.write(
            bindless.slotsBufferDescriptor,
            Core::DescriptorWriteItem::ConstantBuffer(0u, bindless.slotsBuffer.get())
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register deferred bindless slot buffer in the descriptor heap"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    return true;
}

void RendererDeferredSystem::resetDeferredBindlessFrameResources(DeferredFrameTargets& targets){
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(targets.bindless.slotsBufferDescriptor);
        heap.free(targets.bindless.gbufferBaseColor);
        heap.free(targets.bindless.gbufferNormal);
        heap.free(targets.bindless.gbufferWorldPosition);
        heap.free(targets.bindless.gbufferDepth);
        heap.free(targets.bindless.shadowVisibility);
        heap.free(targets.bindless.shadowVisibilityStorage);
        heap.free(targets.bindless.causticIrradiance);
        heap.free(targets.bindless.causticIrradianceStorage);
        heap.free(targets.bindless.surfelIrradiance);
        heap.free(targets.bindless.surfelIrradianceStorage);
        heap.free(targets.bindless.surfelIrradianceHalf);
        heap.free(targets.bindless.surfelIrradianceHalfStorage);
        heap.free(targets.bindless.sampler);
        heap.free(targets.bindless.opaqueColor);
        heap.free(targets.bindless.avboitAccumColor);
        heap.free(targets.bindless.avboitAccumExtinction);
        heap.free(targets.bindless.avboitTransmittance);
        heap.free(targets.bindless.avboitLinearSampler);
        heap.free(targets.bindless.sceneShading);
        heap.free(targets.bindless.lightList);
        heap.free(targets.bindless.causticAccumulator);
        heap.free(targets.bindless.causticAccumulatorStorage);
        heap.free(targets.bindless.causticHistory);
        heap.free(targets.bindless.causticHistoryStorage);
        heap.free(targets.bindless.causticResolveHalf);
        heap.free(targets.bindless.causticResolveHalfStorage);
        heap.free(targets.bindless.causticResolveGeometry);
        heap.free(targets.bindless.causticResolveGeometryStorage);
        heap.free(targets.bindless.shadowCoarseTransmittanceStorage);
        heap.free(targets.bindless.shadowSoftGeometry);
        heap.free(targets.bindless.shadowSoftGeometryStorage);
        heap.free(targets.bindless.shadowSoftGeometryPrev);
        heap.free(targets.bindless.shadowSoftGeometryPrevStorage);
        heap.free(targets.bindless.shadowSoftHalfA);
        heap.free(targets.bindless.shadowSoftHalfAStorage);
        heap.free(targets.bindless.shadowSoftHalfB);
        heap.free(targets.bindless.shadowSoftHalfBStorage);
        heap.free(targets.bindless.shadowHistA);
        heap.free(targets.bindless.shadowHistAStorage);
        heap.free(targets.bindless.shadowHistB);
        heap.free(targets.bindless.shadowHistBStorage);
        heap.free(targets.bindless.shadowMomentsA);
        heap.free(targets.bindless.shadowMomentsAStorage);
        heap.free(targets.bindless.shadowMomentsB);
        heap.free(targets.bindless.shadowMomentsBStorage);
        heap.free(targets.bindless.transparentSoftHalf);
        heap.free(targets.bindless.transparentSoftHalfStorage);
        heap.free(targets.bindless.transparentHistA);
        heap.free(targets.bindless.transparentHistAStorage);
        heap.free(targets.bindless.transparentHistB);
        heap.free(targets.bindless.transparentHistBStorage);
        heap.free(targets.bindless.transparentMomentsA);
        heap.free(targets.bindless.transparentMomentsAStorage);
        heap.free(targets.bindless.transparentMomentsB);
        heap.free(targets.bindless.transparentMomentsBStorage);
        heap.free(targets.bindless.csgCapBackNormal);
        heap.free(targets.bindless.csgIntervalDepth);
        heap.free(targets.bindless.csgIntervalId);
        heap.free(targets.bindless.csgReceiverEventData);
        heap.free(targets.bindless.csgReceiverEventCount);
        heap.free(targets.bindless.csgReceiverSpanData);
        heap.free(targets.bindless.csgReceiverSpanCount);
        heap.free(targets.bindless.csgRemovedIntervalDepth);
        heap.free(targets.bindless.csgRemovedIntervalCapNormal);
        heap.free(targets.bindless.csgRemovedIntervalData);
        heap.free(targets.bindless.csgRemovedIntervalCount);
    }
    targets.bindless = DeferredBindlessFrameResources{};
}

bool RendererDeferredSystem::uploadDeferredBindlessFrameResources(Core::CommandList& commandList, DeferredFrameTargets& targets){
    DeferredBindlessFrameResources& bindless = targets.bindless;
    if(bindless.slotsUploaded)
        return true;
    if(!bindless.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot upload incomplete deferred bindless resources"));
        return false;
    }

    commandList.setBufferState(bindless.slotsBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(bindless.slotsBuffer.get(), &bindless.slots, sizeof(bindless.slots));
    commandList.setBufferState(bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    bindless.slotsUploaded = true;
    return true;
}

void RendererDeferredSystem::resetDeferredFrameTargets(){
    resetDeferredBindlessFrameResources(deferredState().m_targets);
    resetAvboitFrameTargets(deferredState().m_targets.avboit);
    deferredState().m_targets.framebuffer.reset();
    deferredState().m_targets.opaqueLightingFramebuffer.reset();

    deferredState().m_targets.albedo.reset();
    deferredState().m_targets.normal.reset();
    deferredState().m_targets.worldPosition.reset();
    deferredState().m_targets.csgCapBackNormal.reset();
    deferredState().m_targets.csgIntervalDepth.reset();
    deferredState().m_targets.csgIntervalId.reset();
    deferredState().m_targets.csgReceiverEventData.reset();
    deferredState().m_targets.csgReceiverEventCount.reset();
    deferredState().m_targets.csgReceiverSpanData.reset();
    deferredState().m_targets.csgReceiverSpanCount.reset();
    deferredState().m_targets.csgRemovedIntervalDepth.reset();
    deferredState().m_targets.csgRemovedIntervalCapNormal.reset();
    deferredState().m_targets.csgRemovedIntervalData.reset();
    deferredState().m_targets.csgRemovedIntervalCount.reset();
    deferredState().m_targets.opaqueColor.reset();
    deferredState().m_targets.depth.reset();
    deferredState().m_targets.shadowVisibility.reset();

    deferredState().m_targets = DeferredFrameTargets{};
}

bool RendererDeferredSystem::createDeferredFrameTargets(const u32 width, const u32 height){
    if(width == 0 || height == 0){
        resetDeferredFrameTargets();
        return false;
    }

    auto& device = graphics().getDevice();
    const Core::Format::Enum albedoFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(device);
    const Core::Format::Enum normalFormat = ECSRenderDetail::SelectGBufferVectorFormat(device);
    const Core::Format::Enum worldPositionFormat = ECSRenderDetail::SelectGBufferVectorFormat(device);
    const Core::Format::Enum opaqueColorFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(device);
    const Core::Format::Enum depthFormat = ECSRenderDetail::SelectGBufferDepthFormat(device);
    const Core::Format::Enum csgCapNormalFormat = ECSRenderDetail::SelectCsgCapNormalFormat(device);
    const Core::Format::Enum csgIntervalDepthFormat = ECSRenderDetail::SelectCsgIntervalDepthFormat(device);
    const Core::Format::Enum csgIntervalIdFormat = ECSRenderDetail::SelectCsgIntervalIdFormat(device);
    const Core::Format::Enum csgReceiverEventDataFormat = ECSRenderDetail::SelectCsgReceiverEventDataFormat(device);
    const Core::Format::Enum csgReceiverEventCountFormat = ECSRenderDetail::SelectCsgReceiverEventCountFormat(device);
    const Core::Format::Enum csgReceiverSpanDataFormat = ECSRenderDetail::SelectCsgReceiverSpanDataFormat(device);
    const Core::Format::Enum csgReceiverSpanCountFormat = ECSRenderDetail::SelectCsgReceiverSpanCountFormat(device);
    const Core::Format::Enum csgRemovedIntervalDepthFormat = ECSRenderDetail::SelectCsgRemovedIntervalDepthFormat(device);
    const Core::Format::Enum csgRemovedIntervalCapNormalFormat = ECSRenderDetail::SelectCsgRemovedIntervalCapNormalFormat(device);
    const Core::Format::Enum csgRemovedIntervalDataFormat = ECSRenderDetail::SelectCsgRemovedIntervalDataFormat(device);
    const Core::Format::Enum csgRemovedIntervalCountFormat = ECSRenderDetail::SelectCsgRemovedIntervalCountFormat(device);
    const Core::Format::Enum avboitLowRasterFormat = SelectRendererAvboitLowRasterFormat(device);
    const Core::Format::Enum avboitAccumColorFormat = SelectRendererAvboitAccumColorFormat(device);
    const Core::Format::Enum avboitAccumExtinctionFormat = SelectRendererAvboitAccumExtinctionFormat(device);
    const Core::Format::Enum avboitTransmittanceFormat = SelectRendererAvboitTransmittanceFormat(device);
    if(
        albedoFormat == Core::Format::UNKNOWN
        || normalFormat == Core::Format::UNKNOWN
        || worldPositionFormat == Core::Format::UNKNOWN
        || opaqueColorFormat == Core::Format::UNKNOWN
        || depthFormat == Core::Format::UNKNOWN
        || csgCapNormalFormat == Core::Format::UNKNOWN
        || csgIntervalDepthFormat == Core::Format::UNKNOWN
        || csgIntervalIdFormat == Core::Format::UNKNOWN
        || csgReceiverEventDataFormat == Core::Format::UNKNOWN
        || csgReceiverEventCountFormat == Core::Format::UNKNOWN
        || csgReceiverSpanDataFormat == Core::Format::UNKNOWN
        || csgReceiverSpanCountFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDepthFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCapNormalFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDataFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCountFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported deferred framebuffer formats"));
        return false;
    }
    if(
        avboitLowRasterFormat == Core::Format::UNKNOWN
        || avboitAccumColorFormat == Core::Format::UNKNOWN
        || avboitAccumExtinctionFormat == Core::Format::UNKNOWN
        || avboitTransmittanceFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    if(!createDeferredLightingResources())
        return false;
    if(!createDeferredCompositeResources())
        return false;
    if(!m_renderer.avboitSystem().createAvboitResources())
        return false;

    resetDeferredFrameTargets();
    materialState().m_pipelines.clear();
    deferredState().m_lightingPipeline.reset();
    deferredState().m_compositePipeline.reset();

    DeferredFrameTargets createdTargets;
    createdTargets.width = width;
    createdTargets.height = height;
    createdTargets.albedoFormat = albedoFormat;
    createdTargets.normalFormat = normalFormat;
    createdTargets.worldPositionFormat = worldPositionFormat;
    createdTargets.opaqueColorFormat = opaqueColorFormat;
    createdTargets.depthFormat = depthFormat;
    createdTargets.csgCapNormalFormat = csgCapNormalFormat;
    createdTargets.csgIntervalDepthFormat = csgIntervalDepthFormat;
    createdTargets.csgIntervalIdFormat = csgIntervalIdFormat;
    createdTargets.csgReceiverEventDataFormat = csgReceiverEventDataFormat;
    createdTargets.csgReceiverEventCountFormat = csgReceiverEventCountFormat;
    createdTargets.csgReceiverSpanDataFormat = csgReceiverSpanDataFormat;
    createdTargets.csgReceiverSpanCountFormat = csgReceiverSpanCountFormat;
    createdTargets.csgRemovedIntervalDepthFormat = csgRemovedIntervalDepthFormat;
    createdTargets.csgRemovedIntervalCapNormalFormat = csgRemovedIntervalCapNormalFormat;
    createdTargets.csgRemovedIntervalDataFormat = csgRemovedIntervalDataFormat;
    createdTargets.csgRemovedIntervalCountFormat = csgRemovedIntervalCountFormat;
    createdTargets.csgPeelLayerCount = ECSRenderDetail::s_CsgPeelLayerCount;
    createdTargets.csgReceiverEventLayerCount = ECSRenderDetail::s_CsgReceiverEventLayerCount;
    createdTargets.csgReceiverSpanLayerCount = ECSRenderDetail::s_CsgReceiverSpanLayerCount;
    createdTargets.csgRemovedIntervalLayerCount = ECSRenderDetail::s_CsgRemovedIntervalLayerCount;

    Core::TextureDesc albedoDesc;
    albedoDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.albedoFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/gbuffer_albedo")
        .setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    createdTargets.albedo = graphics().createTexture(albedoDesc);
    if(!createdTargets.albedo){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred albedo target"));
        return false;
    }

    Core::TextureDesc normalDesc;
    normalDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.normalFormat)
        .setInRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_normal")
        .setClearValue(Core::Color(0.5f, 0.5f, 1.f, 1.f))
    ;
    createdTargets.normal = graphics().createTexture(normalDesc);
    if(!createdTargets.normal){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred normal target"));
        return false;
    }

    Core::TextureDesc worldPositionDesc;
    worldPositionDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.worldPositionFormat)
        .setInRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_world_position")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 1.f))
    ;
    createdTargets.worldPosition = graphics().createTexture(worldPositionDesc);
    if(!createdTargets.worldPosition){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred world-position target"));
        return false;
    }

    Core::TextureDesc opaqueColorDesc;
    opaqueColorDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.opaqueColorFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/opaque_color")
        .setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    createdTargets.opaqueColor = graphics().createTexture(opaqueColorDesc);
    if(!createdTargets.opaqueColor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred opaque color target"));
        return false;
    }

    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.depthFormat)
        .setInRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/depth")
    ;
    createdTargets.depth = graphics().createTexture(depthDesc);
    if(!createdTargets.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred depth target"));
        return false;
    }

    Core::FramebufferAttachment gbufferAttachments[NWB_MESH_GBUFFER_TARGET_COUNT] = {};
    gbufferAttachments[NWB_MESH_GBUFFER_BASE_COLOR_LOCATION]
        .setTexture(createdTargets.albedo.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    gbufferAttachments[NWB_MESH_GBUFFER_NORMAL_LOCATION]
        .setTexture(createdTargets.normal.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    gbufferAttachments[NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION]
        .setTexture(createdTargets.worldPosition.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    Core::FramebufferDesc framebufferDesc;
    for(const Core::FramebufferAttachment& attachment : gbufferAttachments)
        framebufferDesc.addColorAttachment(attachment);
    framebufferDesc.setDepthAttachment(createdTargets.depth.get(), ECSRenderDetail::s_FramebufferSubresources);
    createdTargets.framebuffer = device.createFramebuffer(framebufferDesc);
    if(!createdTargets.framebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return false;
    }

    Core::FramebufferDesc opaqueLightingFramebufferDesc;
    opaqueLightingFramebufferDesc.addColorAttachment(createdTargets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources);
    createdTargets.opaqueLightingFramebuffer = device.createFramebuffer(opaqueLightingFramebufferDesc);
    if(!createdTargets.opaqueLightingFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting framebuffer"));
        return false;
    }

    if(!m_renderer.avboitSystem().createAvboitFrameTargets(
        createdTargets,
        avboitLowRasterFormat,
        avboitAccumColorFormat,
        avboitAccumExtinctionFormat,
        avboitTransmittanceFormat
    ))
        return false;

    if(!m_renderer.csgSystem().createCsgPeelTargets(createdTargets))
        return false;

    if(!m_renderer.raytracingSystem().createShadowVisibilityTarget(createdTargets))
        return false;

    if(!m_renderer.raytracingSystem().createCausticTargets(createdTargets))
        return false;

    if(!createDeferredBindlessFrameResources(createdTargets))
        return false;

    deferredState().m_targets = Move(createdTargets);
    if(!createDeferredLightingPipeline(deferredState().m_targets)){
        resetDeferredFrameTargets();
        return false;
    }

    // AVBOIT registers its target-generation work resources after deferred lighting has created the shared slot
    // payload. The material and compute pipeline layouts remain push-only gaps; no local descriptor object is allocated.
    if(!m_renderer.avboitSystem().registerAvboitFrameTargetDescriptors(
            deferredState().m_targets,
            deferredState().m_targets.avboit
        )
    ){
        resetDeferredFrameTargets();
        return false;
    }

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, normal {}, world position {}, opaque color {}, depth {}, shadow visibility {}, CSG peel {} layers: cap back normal {}, interval depth {}, interval id {}, receiver events {} layers: event data {}, event count {}, receiver spans {} layers: span data {}, span count {}, removed intervals {} layers: interval depth {}, cap normal {}, interval data {}, interval count {}, AVBOIT color {}, extinction {}, transmittance {})")
        , deferredState().m_targets.width
        , deferredState().m_targets.height
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.albedoFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.normalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.worldPositionFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.opaqueColorFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.depthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.shadowVisibilityFormat).name)
        , deferredState().m_targets.csgPeelLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgCapNormalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgIntervalIdFormat).name)
        , deferredState().m_targets.csgReceiverEventLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverEventCountFormat).name)
        , deferredState().m_targets.csgReceiverSpanLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgReceiverSpanCountFormat).name)
        , deferredState().m_targets.csgRemovedIntervalLayerCount
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalDepthFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalCapNormalFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalDataFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.csgRemovedIntervalCountFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.accumColorFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.accumExtinctionFormat).name)
        , StringConvert(Core::GetFormatInfo(deferredState().m_targets.avboit.transmittanceFormat).name)
    );
    return true;
}

void RendererDeferredSystem::clearDeferredTargets(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool clearCsgTargets,
    const Core::Rect& csgClearRect,
    const bool clearSurfelIrradiance
){
    NWB_ASSERT(targets.albedo);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.opaqueColor);
    NWB_ASSERT(targets.depth);
    if(clearCsgTargets)
        __hidden_deferred_targets::AssertCsgIntervalTargetsAvailable(targets);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_DeferredClear, graphics().getDevice(), commandList);

    const __hidden_deferred_targets::CsgIntervalSubresources csgSubresources =
        __hidden_deferred_targets::MakeCsgIntervalSubresources(targets)
    ;

    commandList.setTextureState(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    if(clearCsgTargets)
        __hidden_deferred_targets::SetCsgIntervalTargetCopyDestStates(commandList, targets, csgSubresources);

    commandList.setTextureState(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    // On the Graphics fallback this is reset here. A dedicated AsyncCompute surfel packet owns the same clear so its
    // exclusive result never has to make a Graphics -> Compute ownership trip before the producer can write it.
    if(clearSurfelIrradiance && targets.surfelIrradiance)
        commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    commandList.clearTextureFloat(targets.albedo.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);
    commandList.clearTextureFloat(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.5f, 0.5f, 1.f, 1.f));
    commandList.clearTextureFloat(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 1.f));
    if(clearSurfelIrradiance && targets.surfelIrradiance)
        commandList.clearTextureFloat(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));

    if(clearCsgTargets){
        Core::GpuTimingMeasure csgClearTiming(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalClear, graphics().getDevice(), commandList);

        __hidden_deferred_targets::ClearCsgIntervalTargets(commandList, targets, csgSubresources, csgClearRect);
    }

    commandList.clearTextureFloat(targets.opaqueColor.get(), ECSRenderDetail::s_FramebufferSubresources, ECSRenderDetail::s_ClearColor);

    commandList.clearDepthStencilTexture(
        targets.depth.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        true,
        Core::s_DepthClearValue,
        false,
        0
    );
}

void RendererDeferredSystem::clearCsgIntervalTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::Rect& csgClearRect){
    __hidden_deferred_targets::AssertCsgIntervalTargetsAvailable(targets);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgIntervalClear, graphics().getDevice(), commandList);

    const __hidden_deferred_targets::CsgIntervalSubresources csgSubresources =
        __hidden_deferred_targets::MakeCsgIntervalSubresources(targets)
    ;

    __hidden_deferred_targets::SetCsgIntervalTargetCopyDestStates(commandList, targets, csgSubresources);

    commandList.commitBarriers();

    __hidden_deferred_targets::ClearCsgIntervalTargets(commandList, targets, csgSubresources, csgClearRect);
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

