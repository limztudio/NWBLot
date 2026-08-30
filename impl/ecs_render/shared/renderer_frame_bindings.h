// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct MaterialPassBufferSnapshot{
        Core::BufferHandle instanceBuffer;
        Core::BufferHandle materialTypedBuffer;
        usize instanceBufferCapacity = 0u;
        usize materialTypedBufferCapacity = 0u;

        [[nodiscard]] bool valid()const noexcept{
            return instanceBuffer && materialTypedBuffer;
        }
    };

    struct MeshViewBufferSnapshot{
        Core::BufferHandle buffer;
        Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::invalid();

        [[nodiscard]] bool valid()const noexcept{ return static_cast<bool>(buffer); }
        [[nodiscard]] bool bindingValid()const noexcept{
            return
                buffer
                && heapHandle.valid()
                && heapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
            ;
        }
    };

    // Mesh owns the descriptor-registration transaction for all three frame bindings. This retained snapshot keeps
    // each buffer paired with the exact heap handle published in that transaction while a graph packet is pending.
    struct MeshFrameBindingSnapshot{
        Core::BufferHandle instanceBuffer;
        Core::BufferHandle materialTypedBuffer;
        MeshViewBufferSnapshot meshView;
        Core::GpuDescriptorHandle instanceHeapHandle = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle materialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
        usize instanceBufferCapacity = 0u;
        usize materialTypedBufferCapacity = 0u;

        [[nodiscard]] bool bindingValid()const noexcept{
            return
                instanceBuffer
                && materialTypedBuffer
                && meshView.bindingValid()
                && instanceHeapHandle.valid()
                && instanceHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
                && materialTypedHeapHandle.valid()
                && materialTypedHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
            ;
        }
        [[nodiscard]] bool matches(
            const MaterialPassBufferSnapshot& materialBuffers,
            const Core::BufferHandle& meshViewBuffer
        )const noexcept{
            return
                bindingValid()
                && instanceBuffer == materialBuffers.instanceBuffer
                && materialTypedBuffer == materialBuffers.materialTypedBuffer
                && meshView.buffer == meshViewBuffer
                && instanceBufferCapacity == materialBuffers.instanceBufferCapacity
                && materialTypedBufferCapacity == materialBuffers.materialTypedBufferCapacity
            ;
        }
        [[nodiscard]] bool frameReady(const usize instanceCount, const usize materialTypedByteCount)const noexcept{
            if(
                materialTypedByteCount == 0u
                || (materialTypedByteCount & (sizeof(u32) - 1u)) != 0u
            )
                return false;

            return
                bindingValid()
                && instanceBufferCapacity >= instanceCount
                && materialTypedBufferCapacity >= Max<usize>(materialTypedByteCount, sizeof(u32))
            ;
        }
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

