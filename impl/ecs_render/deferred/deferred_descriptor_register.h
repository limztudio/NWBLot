// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/api.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeferredDescriptorRegisterDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterDescriptor(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorClass::Enum descriptorClass, const Core::DescriptorWriteItem& item);

[[nodiscard]] inline Expected<Core::GpuDescriptorHandle> RegisterSampledTexture(
    Core::GpuDescriptorHeap& heap,
    const Core::GpuDescriptorClass::Enum descriptorClass,
    const Core::TextureHandle& texture,
    const Core::Format::Enum format,
    const Core::TextureSubresourceSet& subresources,
    const Core::TextureDimension::Enum dimension
){
    return RegisterDescriptor(heap, descriptorClass, Core::DescriptorWriteItem::TextureSrv(0u, texture.get(), format, subresources, dimension));
}

[[nodiscard]] inline Expected<Core::GpuDescriptorHandle> RegisterStorageTexture(Core::GpuDescriptorHeap& heap, const Core::TextureHandle& texture, const Core::Format::Enum format, const Core::TextureDimension::Enum dimension){
    return RegisterDescriptor(heap, Core::GpuDescriptorClass::StorageImage, Core::DescriptorWriteItem::TextureUav(0u, texture.get(), format, Core::s_AllSubresources, dimension));
}

[[nodiscard]] inline Expected<Core::GpuDescriptorHandle> RegisterSampler(Core::GpuDescriptorHeap& heap, Core::Sampler& sampler){
    return RegisterDescriptor(heap, Core::GpuDescriptorClass::Sampler, Core::DescriptorWriteItem::Sampler(0u, &sampler));
}

[[nodiscard]] inline Expected<Core::GpuDescriptorHandle> RegisterStructuredBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer){
    return RegisterDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferSrv(0u, &buffer));
}

[[nodiscard]] inline Expected<Core::GpuDescriptorHandle> RegisterConstantBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer){
    return RegisterDescriptor(heap, Core::GpuDescriptorClass::UniformBuffer, Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer));
}

[[nodiscard]] inline Expected<Core::GpuDescriptorHandle> RegisterConstantBuffer(Core::GpuDescriptorHeap& heap, const Core::BufferHandle& buffer){
    return RegisterDescriptor(heap, Core::GpuDescriptorClass::UniformBuffer, Core::DescriptorWriteItem::ConstantBuffer(0u, buffer.get()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

