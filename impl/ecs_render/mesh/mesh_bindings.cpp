// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"

#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/mesh/instance_gpu_data.adhd.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <core/common/log.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/runtime/runtime.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererMeshSystem::prepareComputeEmulationResources(MeshResources& mesh){
    if(mesh.emulationVertexHeapHandle.valid())
        return true;
    if(!mesh.emulationVertexBuffer){
        const Expected<ComputeEmulationLayout> layoutResult = TryResolveComputeEmulationLayout(mesh.meshletLocalVertexRefBuffer->getDescription().byteSize, mesh.meshletPrimitiveIndexCount);
        if(!layoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: generated geometry layout exceeds the index byte-address range for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        const ComputeEmulationLayout& layout = *layoutResult;
        const auto emulationVertexBufferName = DeriveName(mesh.meshName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(layout.bufferByteSize)
            .setStructStride(s_EmulatedVertexStride)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(*emulationVertexBufferName)
        ;
        auto emulationVertexBufferResult = m_graphics.createBuffer(emulationVertexBufferDesc);
        if(emulationVertexBufferResult)
            mesh.emulationVertexBuffer = Move(*emulationVertexBufferResult);
        else
            mesh.emulationVertexBuffer.reset();
        if(!emulationVertexBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        mesh.emulationIndexByteOffset = layout.indexByteOffset;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation vertex buffer requires the initialized global descriptor heap"));
        return false;
    }
    if(RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferUav(0u, mesh.emulationVertexBuffer.get()), mesh.emulationVertexHeapHandle))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register compute-emulation vertex buffer in the descriptor heap for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
    return false;
}

b8 RendererMeshSystem::prepareMeshFrameBindings(const ECSRenderDetail::MaterialPassBufferSnapshot& materialBuffers){
    if(m_meshState.m_frameBindings.matches(materialBuffers, m_meshState.m_meshViewBuffer))
        return true;

    if(!materialBuffers.valid() || !m_meshState.m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires instance, typed-material, and view buffers"));
        return false;
    }

    auto& device = m_graphics.getDevice();
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
        && heap.write(viewHandle, Core::DescriptorWriteItem::ConstantBuffer(0u, m_meshState.m_meshViewBuffer.get()))
    ;
    if(!registered)
        return false;

    ECSRenderDetail::MeshFrameBindingSnapshot replacement = {
        .instanceBuffer = materialBuffers.instanceBuffer,
        .materialTypedBuffer = materialBuffers.materialTypedBuffer,
        .meshView = { .buffer = m_meshState.m_meshViewBuffer, .heapHandle = viewHandle, },
        .instanceHeapHandle = instanceHandle,
        .materialTypedHeapHandle = materialTypedHandle,
        .instanceBufferCapacity = materialBuffers.instanceBufferCapacity,
        .materialTypedBufferCapacity = materialBuffers.materialTypedBufferCapacity,
    };
    ECSRenderDetail::MeshFrameBindingSnapshot retired = Move(m_meshState.m_frameBindings);
    m_meshState.m_frameBindings = Move(replacement);
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

    NWB_ASSERT(m_meshState.m_frameBindings.matches(materialBuffers, m_meshState.m_meshViewBuffer));
    return true;
}

ECSRenderDetail::MeshFrameBindingSnapshot RendererMeshSystem::meshFrameBindingSnapshot()const noexcept{
    const ECSRenderDetail::MeshFrameBindingSnapshot& bindings = m_meshState.m_frameBindings;
    return {
        .instanceBuffer = bindings.instanceBuffer,
        .materialTypedBuffer = bindings.materialTypedBuffer,
        .meshView = meshViewBufferSnapshot(),
        .instanceHeapHandle = bindings.instanceHeapHandle,
        .materialTypedHeapHandle = bindings.materialTypedHeapHandle,
        .instanceBufferCapacity = bindings.instanceBufferCapacity,
        .materialTypedBufferCapacity = bindings.materialTypedBufferCapacity,
    };
}

void RendererMeshSystem::populateMeshGeometryHeapSlots(const MeshResources& mesh, InstanceGpuData& outInstance)const noexcept{
    NWB_ASSERT(GeometryHeapHandlesReady(mesh.geometryHeapHandles));
    for(u32 slotIndex = 0u; slotIndex < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++slotIndex)
        outInstance.geometryHeapSlots[slotIndex] = 0u;
    ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const b8)noexcept{
        outInstance.geometryHeapSlots[bindingSlot] = mesh.geometryHeapHandles[bindingSlot].slot();
    });
}

void RendererMeshSystem::releaseMeshGeometryHeapHandles(MeshResources& mesh){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles){
            if(handle.valid())
                heap.free(handle);
            handle = Core::GpuDescriptorHandle::Invalid();
        }
        if(mesh.swBvhPositionHeapHandle.valid())
            heap.free(mesh.swBvhPositionHeapHandle);
        if(mesh.swBvhTriangleIndexHeapHandle.valid())
            heap.free(mesh.swBvhTriangleIndexHeapHandle);
        if(mesh.swBvhNodeHeapHandle.valid())
            heap.free(mesh.swBvhNodeHeapHandle);
        if(mesh.swBvhParentHeapHandle.valid())
            heap.free(mesh.swBvhParentHeapHandle);
        if(mesh.emulationVertexHeapHandle.valid())
            heap.free(mesh.emulationVertexHeapHandle);
        if(mesh.runtimeLocalBoundsHeapHandle.valid())
            heap.free(mesh.runtimeLocalBoundsHeapHandle);
        if(mesh.runtimeMeshletLocalBoundsHeapHandle.valid())
            heap.free(mesh.runtimeMeshletLocalBoundsHeapHandle);
        if(mesh.objectGeometryCache.heapHandle.valid())
            heap.free(mesh.objectGeometryCache.heapHandle);
        mesh.swBvhPositionHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.swBvhParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.runtimeLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.runtimeMeshletLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.objectGeometryCache.heapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.objectGeometryCache.acceptedContent = false;
        return;
    }

    for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles)
        handle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhPositionHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.runtimeLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.runtimeMeshletLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.objectGeometryCache.heapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.objectGeometryCache.acceptedContent = false;
}

void RendererMeshSystem::releaseMeshFrameHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        if(m_meshState.m_frameBindings.instanceHeapHandle.valid())
            heap.free(m_meshState.m_frameBindings.instanceHeapHandle);
        if(m_meshState.m_frameBindings.materialTypedHeapHandle.valid())
            heap.free(m_meshState.m_frameBindings.materialTypedHeapHandle);
        if(m_meshState.m_frameBindings.meshView.heapHandle.valid())
            heap.free(m_meshState.m_frameBindings.meshView.heapHandle);
    }
    m_meshState.m_frameBindings = {};
}

b8 RendererMeshSystem::createMeshRenderBindings(MeshResources& mesh){
    if(!createMeshGeometryHeapHandles(mesh))
        return false;

    if(mesh.runtimeLocalBoundsBuffer && !mesh.runtimeLocalBoundsHeapHandle.valid()){
        auto& device = m_graphics.getDevice();
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        const Core::BufferDesc& desc = mesh.runtimeLocalBoundsBuffer->getCreationDescription();
        if(!heap.isInitialized() || desc.byteSize < NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE || !desc.canHaveRawViews){
            releaseMeshGeometryHeapHandles(mesh);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime local bounds require a complete raw GPU buffer"));
            return false;
        }
        if(!RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.runtimeLocalBoundsBuffer.get()), mesh.runtimeLocalBoundsHeapHandle)){
            releaseMeshGeometryHeapHandles(mesh);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register runtime local bounds"));
            return false;
        }
    }

    if(mesh.runtimeMesh && mesh.runtimeGeometryContentRevision != 0u && mesh.runtimeMeshletLocalBoundsBuffer && !mesh.runtimeMeshletLocalBoundsHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        const Core::BufferDesc& desc = mesh.runtimeMeshletLocalBoundsBuffer->getCreationDescription();
        const u64 requiredBytes = static_cast<u64>(mesh.meshletCount) * NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE;
        if(!(heap.isInitialized() && requiredBytes != 0u && desc.byteSize >= requiredBytes && desc.canHaveRawViews
            && RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.runtimeMeshletLocalBoundsBuffer.get()), mesh.runtimeMeshletLocalBoundsHeapHandle)))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh '{}' could not register optional meshlet bounds; retaining whole-caster shadows"), ConvertString(mesh.meshName.resolvedText()));
    }

    NWB_ASSERT(meshRenderBindingsReady(mesh));
    return true;
}

b8 RendererMeshSystem::meshRenderBindingsReady(const MeshResources& mesh)const noexcept{
    if(!mesh.valid() || !GeometryHeapHandlesReady(mesh.geometryHeapHandles))
        return false;

    if(mesh.runtimeLocalBoundsBuffer && (!mesh.runtimeLocalBoundsHeapHandle.valid() || mesh.runtimeLocalBoundsHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer))
        return false;

    return true;
}

b8 RendererMeshSystem::createMeshGeometryHeapHandles(MeshResources& mesh){
    if(GeometryHeapHandlesReady(mesh.geometryHeapHandles))
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    }

    // A non-empty partial set signals a broken lifetime transition, never a safe merge.
#if defined(NWB_DEBUG)
    for(const Core::GpuDescriptorHandle handle : mesh.geometryHeapHandles)
        NWB_ASSERT(!handle.valid());
#endif

    Array<Core::GpuDescriptorHandle, NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT> acquired{};
    Core::GpuDescriptorRetirementGuard retireAcquired(heap, acquired);
    b8 registered = true;
    ForEachMeshSourceBuffer(mesh, [&](const u32 bindingSlot, const Core::BufferHandle& buffer, const b8){
        if(!registered)
            return;
        if(!buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no source buffer at binding {}"), ConvertString(mesh.meshName.resolvedText()), bindingSlot);
            registered = false;
            return;
        }

        if(!RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferSrv(0u, buffer.get()), acquired[bindingSlot]))
            registered = false;
    });

    if(!registered)
        return false;

    ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const b8)noexcept{
        mesh.geometryHeapHandles[bindingSlot] = acquired[bindingSlot];
    });
    retireAcquired.release();
    NWB_ASSERT(GeometryHeapHandlesReady(mesh.geometryHeapHandles));
    return true;
}

b8 RendererMeshSystem::ensureMeshSwBvhInputHeapHandles(MeshResources& mesh){
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

    auto& device = m_graphics.getDevice();
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

void RendererMeshSystem::releaseAllMeshGeometryHeapHandles(){
    for(auto it = m_meshState.m_meshes.begin(); it != m_meshState.m_meshes.end(); ++it)
        releaseMeshGeometryHeapHandles(it.value());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

