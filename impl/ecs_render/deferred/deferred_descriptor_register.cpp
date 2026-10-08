// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/deferred_descriptor_register.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/scope_exit.h>


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
    const Core::GpuDescriptorHandle handle = heap.allocate(descriptorClass);
    if(!handle.valid())
        return MakeUnexpected(Failure{});
    ScopeExit retireUnpublished([&]()noexcept{ heap.free(handle); });

    if(heap.write(handle, Core::DescriptorWriteItem::TextureSrv(0u, texture, format, subresources, dimension))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterStorageTexture(
    Core::GpuDescriptorHeap& heap,
    Core::Texture* texture,
    Core::Format::Enum format,
    Core::TextureDimension::Enum dimension
){
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(!handle.valid())
        return MakeUnexpected(Failure{});
    ScopeExit retireUnpublished([&]()noexcept{ heap.free(handle); });

    if(heap.write(handle, Core::DescriptorWriteItem::TextureUav(
        0u,
        texture,
        format,
        Core::s_AllSubresources,
        dimension
    ))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterSampler(Core::GpuDescriptorHeap& heap, Core::Sampler* sampler){
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!handle.valid())
        return MakeUnexpected(Failure{});
    ScopeExit retireUnpublished([&]()noexcept{ heap.free(handle); });

    if(heap.write(handle, Core::DescriptorWriteItem::Sampler(0u, sampler))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterStructuredBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer* buffer){
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle.valid())
        return MakeUnexpected(Failure{});
    ScopeExit retireUnpublished([&]()noexcept{ heap.free(handle); });

    if(heap.write(handle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, buffer))){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterConstantBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer* buffer){
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!handle.valid())
        return MakeUnexpected(Failure{});
    ScopeExit retireUnpublished([&]()noexcept{ heap.free(handle); });

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

