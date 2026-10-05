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
    outHandle = Core::GpuDescriptorHandle::Invalid();
    if(!buffer)
        return false;

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::StructuredBufferUav(0u, buffer))){
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
    outHandle = Core::GpuDescriptorHandle::Invalid();
    if(!texture)
        return false;

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::TextureUav(
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
        NWB_LOGGER_ERROR(GLB_TEXT("RendererSystem: AVBOIT target bindings require the descriptor-buffer global heap"));
        return false;
    }
    GLB_ASSERT(createdTargets.bindless.slotsBufferDescriptor.valid());
    GLB_ASSERT(createdTargets.bindless.slotsBufferDescriptor.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer);
    GLB_ASSERT(!createdTargets.bindless.slotsUploaded);
    GLB_ASSERT(avboitTargets.coverageBuffer);
    GLB_ASSERT(avboitTargets.depthWarpBuffer);
    GLB_ASSERT(avboitTargets.controlBuffer);
    GLB_ASSERT(avboitTargets.extinctionBuffer);
    GLB_ASSERT(avboitTargets.extinctionOverflowBuffer);
    GLB_ASSERT(avboitTargets.transmittanceTexture);
    GLB_ASSERT(!avboitTargets.coverageBufferDescriptor.valid());
    GLB_ASSERT(!avboitTargets.depthWarpBufferDescriptor.valid());
    GLB_ASSERT(!avboitTargets.controlBufferDescriptor.valid());
    GLB_ASSERT(!avboitTargets.extinctionBufferDescriptor.valid());
    GLB_ASSERT(!avboitTargets.extinctionOverflowBufferDescriptor.valid());
    GLB_ASSERT(!avboitTargets.transmittanceTextureStorageDescriptor.valid());

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
        NWB_LOGGER_ERROR(GLB_TEXT("RendererSystem: failed to register AVBOIT work resources in the descriptor heap"));
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

