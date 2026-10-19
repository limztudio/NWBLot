// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_descriptor_register.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeferredDescriptorRegisterDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterSampledTexture(
    Core::GpuDescriptorHeap& heap,
    Core::GpuDescriptorClass::Enum descriptorClass,
    Core::Texture* texture,
    Core::Format::Enum format,
    const Core::TextureSubresourceSet& subresources,
    Core::TextureDimension::Enum dimension
){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(descriptorClass);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(heap.write(handle, Core::DescriptorWriteItem::TextureSrv(0u, texture, format, subresources, dimension))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterStorageTexture(Core::GpuDescriptorHeap& heap, Core::Texture* texture, Core::Format::Enum format, Core::TextureDimension::Enum dimension){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(heap.write(handle, Core::DescriptorWriteItem::TextureUav(0u, texture, format, Core::s_AllSubresources, dimension))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterSampler(Core::GpuDescriptorHeap& heap, Core::Sampler* sampler){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(heap.write(handle, Core::DescriptorWriteItem::Sampler(0u, sampler))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterStructuredBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer* buffer){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(heap.write(handle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, buffer))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterConstantBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer* buffer){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(heap.write(handle, Core::DescriptorWriteItem::ConstantBuffer(0u, buffer))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

