// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"
#include "mesh_swbvh_bindings.h"
#include "mesh_emulation_resources.h"

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
    return MeshEmulationResources{}.prepare(m_graphics, mesh);
}

b8 RendererMeshSystem::prepareMeshFrameBindings(const ECSRenderDetail::MaterialPassBufferSnapshot& materialBuffers){
    return m_meshState.m_frameBindings.prepare(m_graphics, materialBuffers, m_meshState.m_meshViewBuffer);
}

ECSRenderDetail::MeshFrameBindingSnapshot RendererMeshSystem::meshFrameBindingSnapshot()const noexcept{
    return m_meshState.m_frameBindings.snapshot(m_meshState.m_meshViewBuffer);
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
    m_meshState.m_frameBindings.release(m_graphics);
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
    return MeshSoftwareBvhBindings{}.ensure(m_graphics, mesh);
}

void RendererMeshSystem::releaseAllMeshGeometryHeapHandles(){
    for(auto it = m_meshState.m_meshes.begin(); it != m_meshState.m_meshes.end(); ++it)
        releaseMeshGeometryHeapHandles(it.value());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

