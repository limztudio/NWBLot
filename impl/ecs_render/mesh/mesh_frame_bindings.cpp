// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_frame_bindings.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshFrameBindings::prepare(Core::GraphicsRuntime& graphics, const ECSRenderDetail::MaterialPassBufferSnapshot& materialBuffers, const Core::BufferHandle& meshViewBuffer){
    if(m_snapshot.matches(materialBuffers, meshViewBuffer))
        return true;

    if(!materialBuffers.valid() || !meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires instance, typed-material, and view buffers"));
        return false;
    }

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires the initialized global descriptor heap"));
        return false;
    }
    const Expected<Core::GpuDescriptorHandle> instanceHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!instanceHandleResult)
        return false;
    const Core::GpuDescriptorHandle instanceHandle = *instanceHandleResult;
    Core::GpuDescriptorRetirementGuard retireInstance(heap, instanceHandle);
    const Expected<Core::GpuDescriptorHandle> materialTypedHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!materialTypedHandleResult)
        return false;
    const Core::GpuDescriptorHandle materialTypedHandle = *materialTypedHandleResult;
    Core::GpuDescriptorRetirementGuard retireMaterialTyped(heap, materialTypedHandle);
    const Expected<Core::GpuDescriptorHandle> viewHandleResult = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!viewHandleResult)
        return false;
    const Core::GpuDescriptorHandle viewHandle = *viewHandleResult;
    Core::GpuDescriptorRetirementGuard retireView(heap, viewHandle);
    const b8 registered =
        heap.write(instanceHandle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, materialBuffers.instanceBuffer.get()))
        && heap.write(materialTypedHandle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, materialBuffers.materialTypedBuffer.get()))
        && heap.write(viewHandle, Core::DescriptorWriteItem::ConstantBuffer(0u, meshViewBuffer.get()))
    ;
    if(!registered)
        return false;

    ECSRenderDetail::MeshFrameBindingSnapshot replacement = {
        .instanceBuffer = materialBuffers.instanceBuffer,
        .materialTypedBuffer = materialBuffers.materialTypedBuffer,
        .meshView = { .buffer = meshViewBuffer, .heapHandle = viewHandle, },
        .instanceHeapHandle = instanceHandle,
        .materialTypedHeapHandle = materialTypedHandle,
        .instanceBufferCapacity = materialBuffers.instanceBufferCapacity,
        .materialTypedBufferCapacity = materialBuffers.materialTypedBufferCapacity,
    };
    ECSRenderDetail::MeshFrameBindingSnapshot retired = Move(m_snapshot);
    m_snapshot = Move(replacement);
    retireInstance.release();
    retireMaterialTyped.release();
    retireView.release();

    // Keep retired buffers alive until descriptors enter deferred-reuse quarantine.
    if(retired.instanceHeapHandle.valid())
        heap.free(retired.instanceHeapHandle);
    if(retired.materialTypedHeapHandle.valid())
        heap.free(retired.materialTypedHeapHandle);
    if(retired.meshView.heapHandle.valid())
        heap.free(retired.meshView.heapHandle);

    NWB_ASSERT(m_snapshot.matches(materialBuffers, meshViewBuffer));
    return true;
}

ECSRenderDetail::MeshFrameBindingSnapshot MeshFrameBindings::snapshot(const Core::BufferHandle& meshViewBuffer)const noexcept{
    const ECSRenderDetail::MeshFrameBindingSnapshot& bindings = m_snapshot;
    return {
        .instanceBuffer = bindings.instanceBuffer,
        .materialTypedBuffer = bindings.materialTypedBuffer,
        .meshView = viewSnapshot(meshViewBuffer),
        .instanceHeapHandle = bindings.instanceHeapHandle,
        .materialTypedHeapHandle = bindings.materialTypedHeapHandle,
        .instanceBufferCapacity = bindings.instanceBufferCapacity,
        .materialTypedBufferCapacity = bindings.materialTypedBufferCapacity,
    };
}

ECSRenderDetail::MeshViewBufferSnapshot MeshFrameBindings::viewSnapshot(const Core::BufferHandle& meshViewBuffer)const noexcept{
    ECSRenderDetail::MeshViewBufferSnapshot snapshot;
    snapshot.buffer = meshViewBuffer;
    if(m_snapshot.meshView.buffer == meshViewBuffer)
        snapshot.heapHandle = m_snapshot.meshView.heapHandle;
    return snapshot;
}

void MeshFrameBindings::release(Core::GraphicsRuntime& graphics){
    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        if(m_snapshot.instanceHeapHandle.valid())
            heap.free(m_snapshot.instanceHeapHandle);
        if(m_snapshot.materialTypedHeapHandle.valid())
            heap.free(m_snapshot.materialTypedHeapHandle);
        if(m_snapshot.meshView.heapHandle.valid())
            heap.free(m_snapshot.meshView.heapHandle);
    }
    m_snapshot = {};
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

