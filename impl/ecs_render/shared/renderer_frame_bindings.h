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


    public:
        [[nodiscard]] b8 valid()const noexcept{
            return instanceBuffer && materialTypedBuffer;
        }
    };

    struct MeshViewBufferSnapshot{
        Core::BufferHandle buffer;
        Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::Invalid();


    public:
        [[nodiscard]] b8 valid()const noexcept{ return static_cast<b8>(buffer); }

        [[nodiscard]] b8 bindingValid()const noexcept{
            return buffer && heapHandle.valid() && heapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer;
        }
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
        [[nodiscard]] b8 bindingValid()const noexcept{
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

        [[nodiscard]] b8 matches(const MaterialPassBufferSnapshot& materialBuffers, const Core::BufferHandle& meshViewBuffer)const noexcept{
            return
                bindingValid()
                && instanceBuffer == materialBuffers.instanceBuffer
                && materialTypedBuffer == materialBuffers.materialTypedBuffer
                && meshView.buffer == meshViewBuffer
                && instanceBufferCapacity == materialBuffers.instanceBufferCapacity
                && materialTypedBufferCapacity == materialBuffers.materialTypedBufferCapacity
            ;
        }

        [[nodiscard]] b8 frameReady(const usize instanceCount, const usize materialTypedByteCount)const noexcept{
            if(materialTypedByteCount == 0u || (materialTypedByteCount & (sizeof(u32) - 1u)) != 0u)
                return false;

            return bindingValid() && instanceBufferCapacity >= instanceCount && materialTypedBufferCapacity >= materialTypedByteCount;
        }
    };

    // Shared opaque/gbuffer record prologue: frame setup is ready when both setup flags are set.
    [[nodiscard]] inline b8 FrameSetupReady(const b8& meshViewSetupReady, const b8& sceneShadingSetupReady)noexcept{
        return meshViewSetupReady && sceneShadingSetupReady;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

