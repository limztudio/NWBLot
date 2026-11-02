// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassBufferSnapshot{
    Core::BufferHandle instanceBuffer;
    Core::BufferHandle materialTypedBuffer;
    usize instanceBufferCapacity = 0u;
    usize materialTypedBufferCapacity = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept{ return instanceBuffer && materialTypedBuffer; }
};

struct MeshViewBufferSnapshot{
    Core::BufferHandle buffer;
    Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::Invalid();


public:
    [[nodiscard]] b8 valid()const noexcept{ return static_cast<b8>(buffer); }
    [[nodiscard]] b8 bindingValid()const noexcept{ return buffer && heapHandle.valid() && heapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer; }
};

// Mesh owns the registration transaction; snapshot pairs buffers with published handles.
struct MeshFrameBindingSnapshot{
    Core::BufferHandle instanceBuffer;
    Core::BufferHandle materialTypedBuffer;
    MeshViewBufferSnapshot meshView;
    Core::GpuDescriptorHandle instanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle materialTypedHeapHandle = Core::GpuDescriptorHandle::Invalid();
    usize instanceBufferCapacity = 0u;
    usize materialTypedBufferCapacity = 0u;


public:
    [[nodiscard]] b8 bindingValid()const noexcept;
    [[nodiscard]] b8 matches(const MaterialPassBufferSnapshot& materialBuffers, const Core::BufferHandle& meshViewBuffer)const noexcept;
    [[nodiscard]] b8 frameReady(usize instanceCount, usize materialTypedByteCount)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

