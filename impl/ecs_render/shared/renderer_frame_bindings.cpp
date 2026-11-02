// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_frame_bindings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshFrameBindingSnapshot::bindingValid()const noexcept{
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

b8 MeshFrameBindingSnapshot::matches(const MaterialPassBufferSnapshot& materialBuffers, const Core::BufferHandle& meshViewBuffer)const noexcept{
    return
        bindingValid()
        && instanceBuffer == materialBuffers.instanceBuffer
        && materialTypedBuffer == materialBuffers.materialTypedBuffer
        && meshView.buffer == meshViewBuffer
        && instanceBufferCapacity == materialBuffers.instanceBufferCapacity
        && materialTypedBufferCapacity == materialBuffers.materialTypedBufferCapacity
    ;
}

b8 MeshFrameBindingSnapshot::frameReady(const usize instanceCount, const usize materialTypedByteCount)const noexcept{
    if(materialTypedByteCount == 0u || (materialTypedByteCount & (sizeof(u32) - 1u)) != 0u)
        return false;
    return bindingValid() && instanceBufferCapacity >= instanceCount && materialTypedBufferCapacity >= materialTypedByteCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

