// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_target_bindings{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool RegisterWorkBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::GpuDescriptorHandle& outHandle,
    Core::Buffer* buffer
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    if(!buffer)
        return false;

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer))){
        heap.free(handle);
        return false;
    }

    outHandle = handle;
    return true;
}

static bool RegisterTransmittanceStorageTexture(
    Core::GpuDescriptorHeap& heap,
    Core::GpuDescriptorHandle& outHandle,
    Core::Texture* texture,
    const Core::Format::Enum format
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    if(!texture)
        return false;

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::Texture_UAV(
        0u,
        texture,
        format,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ))){
        heap.free(handle);
        return false;
    }

    outHandle = handle;
    return true;
}

static void RetireTargetDescriptors(Core::GpuDescriptorHeap& heap, AvboitFrameTargets& targets){
    heap.free(targets.coverageBufferDescriptor);
    heap.free(targets.depthWarpBufferDescriptor);
    heap.free(targets.controlBufferDescriptor);
    heap.free(targets.extinctionBufferDescriptor);
    heap.free(targets.extinctionOverflowBufferDescriptor);
    heap.free(targets.transmittanceTextureStorageDescriptor);
    targets.coverageBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    targets.depthWarpBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    targets.controlBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    targets.extinctionBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    targets.extinctionOverflowBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    targets.transmittanceTextureStorageDescriptor = Core::GpuDescriptorHandle::invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::registerAvboitFrameTargetDescriptors(
    DeferredFrameTargets& createdTargets,
    AvboitFrameTargets& avboitTargets
){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT target bindings require the descriptor-buffer global heap"));
        return false;
    }
    if(
        !createdTargets.bindless.slotsBufferDescriptor.valid()
        || createdTargets.bindless.slotsBufferDescriptor.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || createdTargets.bindless.slotsUploaded
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT target bindings require an unuploaded shared deferred slot descriptor"));
        return false;
    }
    if(
        !avboitTargets.coverageBuffer
        || !avboitTargets.depthWarpBuffer
        || !avboitTargets.controlBuffer
        || !avboitTargets.extinctionBuffer
        || !avboitTargets.extinctionOverflowBuffer
        || !avboitTargets.transmittanceTexture
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT target bindings require every work resource"));
        return false;
    }
    if(
        avboitTargets.coverageBufferDescriptor.valid()
        || avboitTargets.depthWarpBufferDescriptor.valid()
        || avboitTargets.controlBufferDescriptor.valid()
        || avboitTargets.extinctionBufferDescriptor.valid()
        || avboitTargets.extinctionOverflowBufferDescriptor.valid()
        || avboitTargets.transmittanceTextureStorageDescriptor.valid()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT target heap registrations already exist"));
        return false;
    }

    const bool targetResourcesRegistered =
        __hidden_avboit_target_bindings::RegisterWorkBuffer(heap, avboitTargets.coverageBufferDescriptor, avboitTargets.coverageBuffer.get())
        && __hidden_avboit_target_bindings::RegisterWorkBuffer(heap, avboitTargets.depthWarpBufferDescriptor, avboitTargets.depthWarpBuffer.get())
        && __hidden_avboit_target_bindings::RegisterWorkBuffer(heap, avboitTargets.controlBufferDescriptor, avboitTargets.controlBuffer.get())
        && __hidden_avboit_target_bindings::RegisterWorkBuffer(heap, avboitTargets.extinctionBufferDescriptor, avboitTargets.extinctionBuffer.get())
        && __hidden_avboit_target_bindings::RegisterWorkBuffer(heap, avboitTargets.extinctionOverflowBufferDescriptor, avboitTargets.extinctionOverflowBuffer.get())
        && __hidden_avboit_target_bindings::RegisterTransmittanceStorageTexture(
            heap,
            avboitTargets.transmittanceTextureStorageDescriptor,
            avboitTargets.transmittanceTexture.get(),
            avboitTargets.transmittanceFormat
        )
    ;
    if(!targetResourcesRegistered){
        __hidden_avboit_target_bindings::RetireTargetDescriptors(heap, avboitTargets);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register AVBOIT work resources in the descriptor heap"));
        return false;
    }

    // AVBOIT borrows this target generation's shared slot-payload descriptor. The five owned StorageBuffer handles
    // plus the writable transmittance StorageImage handle populate that payload before deferred rendering uploads it.
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

