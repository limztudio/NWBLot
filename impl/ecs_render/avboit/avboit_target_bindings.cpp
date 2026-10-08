// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_private.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_target_bindings{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<Core::GpuDescriptorHandle> RegisterWorkBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::Buffer* buffer
){
    if(!buffer)
        return MakeUnexpected(Failure{});

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle.valid())
        return MakeUnexpected(Failure{});
    ScopeExit retireUnpublished([&]()noexcept{ heap.free(handle); });

    if(!heap.write(handle, Core::DescriptorWriteItem::StructuredBufferUav(0u, buffer))){
        return MakeUnexpected(Failure{});
    }

    retireUnpublished.release();
    return handle;
}

static Expected<Core::GpuDescriptorHandle> RegisterTransmittanceStorageTexture(
    Core::GpuDescriptorHeap& heap,
    Core::Texture* texture,
    const Core::Format::Enum format
){
    if(!texture)
        return MakeUnexpected(Failure{});

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(!handle.valid())
        return MakeUnexpected(Failure{});
    ScopeExit retireUnpublished([&]()noexcept{ heap.free(handle); });

    if(!heap.write(handle, Core::DescriptorWriteItem::TextureUav(
        0u,
        texture,
        format,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ))){
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
    targets.coverageBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.depthWarpBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.controlBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.extinctionBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.extinctionOverflowBufferDescriptor = Core::GpuDescriptorHandle::Invalid();
    targets.transmittanceTextureStorageDescriptor = Core::GpuDescriptorHandle::Invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::registerAvboitFrameTargetDescriptors(
    DeferredFrameTargets& createdTargets,
    AvboitFrameTargets& avboitTargets
){
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
    NWB_ASSERT(!avboitTargets.coverageBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.depthWarpBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.controlBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.extinctionBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.extinctionOverflowBufferDescriptor.valid());
    NWB_ASSERT(!avboitTargets.transmittanceTextureStorageDescriptor.valid());

    const auto targetResourcesRegistered = [&]() -> Expected<void>{
        if(const auto descriptor = __hidden_avboit_target_bindings::RegisterWorkBuffer(
            heap,
            avboitTargets.coverageBuffer.get()
        ); descriptor)
            avboitTargets.coverageBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_target_bindings::RegisterWorkBuffer(
            heap,
            avboitTargets.depthWarpBuffer.get()
        ); descriptor)
            avboitTargets.depthWarpBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_target_bindings::RegisterWorkBuffer(
            heap,
            avboitTargets.controlBuffer.get()
        ); descriptor)
            avboitTargets.controlBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_target_bindings::RegisterWorkBuffer(
            heap,
            avboitTargets.extinctionBuffer.get()
        ); descriptor)
            avboitTargets.extinctionBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_target_bindings::RegisterWorkBuffer(
            heap,
            avboitTargets.extinctionOverflowBuffer.get()
        ); descriptor)
            avboitTargets.extinctionOverflowBufferDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = __hidden_avboit_target_bindings::RegisterTransmittanceStorageTexture(
            heap,
            avboitTargets.transmittanceTexture.get(),
            avboitTargets.transmittanceFormat
        ); descriptor)
            avboitTargets.transmittanceTextureStorageDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        return {};
    };
    if(!targetResourcesRegistered()){
        __hidden_avboit_target_bindings::RetireTargetDescriptors(heap, avboitTargets);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

