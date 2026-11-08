// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_swbvh_bindings.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSoftwareBvhBindings::ensure(Core::GraphicsRuntime& graphics, MeshResources& mesh)const{
    const auto inputsReady = [&mesh]()noexcept{
        return
            mesh.swBvhPositionHeapHandle.valid()
            && mesh.swBvhPositionHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
            && mesh.swBvhTriangleIndexHeapHandle.valid()
            && mesh.swBvhTriangleIndexHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        ;
    };
    if(inputsReady())
        return true;

    NWB_ASSERT(!mesh.swBvhPositionHeapHandle.valid());
    NWB_ASSERT(!mesh.swBvhTriangleIndexHeapHandle.valid());
    if(!mesh.positionBuffer || !mesh.triangleIndexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no software-BVH position or triangle-index input"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    }

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap for software-BVH inputs"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    }

    const auto registrationFailed = [&mesh](){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-BVH inputs for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    };
    const Expected<Core::GpuDescriptorHandle> positionHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!positionHandleResult)
        return registrationFailed();
    const Core::GpuDescriptorHandle positionHandle = *positionHandleResult;
    Core::GpuDescriptorRetirementGuard retirePosition(heap, positionHandle);
    const Expected<Core::GpuDescriptorHandle> triangleIndexHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!triangleIndexHandleResult)
        return registrationFailed();
    const Core::GpuDescriptorHandle triangleIndexHandle = *triangleIndexHandleResult;
    Core::GpuDescriptorRetirementGuard retireTriangleIndex(heap, triangleIndexHandle);
    if(!heap.write(positionHandle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.positionBuffer.get())) || !heap.write(triangleIndexHandle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.triangleIndexBuffer.get())))
        return registrationFailed();

    mesh.swBvhPositionHeapHandle = positionHandle;
    mesh.swBvhTriangleIndexHeapHandle = triangleIndexHandle;
    retirePosition.release();
    retireTriangleIndex.release();
    NWB_ASSERT(inputsReady());
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

