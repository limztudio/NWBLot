// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/deferred_descriptor_register.h>

#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeferredDescriptorRegisterDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RegisterSampledTexture(
    Core::GpuDescriptorHeap& heap,
    Core::GpuDescriptorHandle& handle,
    Core::GpuDescriptorClass::Enum descriptorClass,
    Core::Texture* texture,
    Core::Format::Enum format,
    const Core::TextureSubresourceSet& subresources,
    Core::TextureDimension::Enum dimension
){
    handle = heap.allocate(descriptorClass);
    if(!handle.valid())
        return false;
    if(heap.write(handle, Core::DescriptorWriteItem::Texture_SRV(0u, texture, format, subresources, dimension)))
        return true;
    heap.free(handle);
    handle = Core::GpuDescriptorHandle::invalid();
    return false;
}

[[nodiscard]] bool RegisterStorageTexture(
    Core::GpuDescriptorHeap& heap,
    Core::GpuDescriptorHandle& handle,
    Core::Texture* texture,
    Core::Format::Enum format,
    Core::TextureDimension::Enum dimension
){
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
}

[[nodiscard]] bool RegisterSampler(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle, Core::Sampler* sampler){
    handle = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!handle.valid())
        return false;
    if(heap.write(handle, Core::DescriptorWriteItem::Sampler(0u, sampler)))
        return true;
    heap.free(handle);
    handle = Core::GpuDescriptorHandle::invalid();
    return false;
}

[[nodiscard]] bool RegisterStructuredBuffer(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle, Core::Buffer* buffer){
    handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle.valid())
        return false;
    if(heap.write(handle, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer)))
        return true;
    heap.free(handle);
    handle = Core::GpuDescriptorHandle::invalid();
    return false;
}

[[nodiscard]] bool RegisterConstantBuffer(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle, Core::Buffer* buffer){
    handle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!handle.valid())
        return false;
    if(heap.write(handle, Core::DescriptorWriteItem::ConstantBuffer(0u, buffer)))
        return true;
    heap.free(handle);
    handle = Core::GpuDescriptorHandle::invalid();
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

