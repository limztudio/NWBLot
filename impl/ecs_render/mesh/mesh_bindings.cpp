// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"

#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>
#include <impl/ecs_render/mesh/compute_emulation_layout.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <core/common/log.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/runtime/runtime.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMeshSystem::prepareComputeEmulationResources(MeshResources& mesh){
    if(mesh.emulationVertexHeapHandle.valid())
        return true;
    if(!mesh.emulationVertexBuffer){
        const auto layout = ECSRenderDetail::ResolveComputeEmulationLayout(mesh.meshletLocalVertexRefBuffer->getDescription().byteSize, mesh.meshletPrimitiveIndexCount);
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: generated geometry layout exceeds the index byte-address range for mesh '{}'"), StringConvert(mesh.meshName.resolvedText()));
            return false;
        }
        const auto emulationVertexBufferName = DeriveName(mesh.meshName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for mesh '{}'"), StringConvert(mesh.meshName.resolvedText()));
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(layout->bufferByteSize)
            .setStructStride(ECSRenderDetail::s_EmulatedVertexStride)
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
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for mesh '{}'"), StringConvert(mesh.meshName.resolvedText()));
            return false;
        }
        mesh.emulationIndexByteOffset = layout->indexByteOffset;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation vertex buffer requires the initialized global descriptor heap"));
        return false;
    }
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    Core::GpuDescriptorRetirementGuard retireHandle(heap, handle);
    if(!handle.valid() || !heap.write(handle, Core::DescriptorWriteItem::StructuredBufferUav(0u, mesh.emulationVertexBuffer.get()))){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register compute-emulation vertex buffer in the descriptor heap for mesh '{}'"), StringConvert(mesh.meshName.resolvedText()));
        return false;
    }
    mesh.emulationVertexHeapHandle = handle;
    retireHandle.release();

    return true;
}

bool RendererMeshSystem::prepareMeshFrameBindings(const ECSRenderDetail::MaterialPassBufferSnapshot& materialBuffers){
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
    const Core::GpuDescriptorHandle instanceHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    Core::GpuDescriptorRetirementGuard retireInstance(heap, instanceHandle);
    const Core::GpuDescriptorHandle materialTypedHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    Core::GpuDescriptorRetirementGuard retireMaterialTyped(heap, materialTypedHandle);
    const Core::GpuDescriptorHandle viewHandle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    Core::GpuDescriptorRetirementGuard retireView(heap, viewHandle);
    const bool registered =
        instanceHandle.valid()
        && materialTypedHandle.valid()
        && viewHandle.valid()
        && heap.write(instanceHandle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, materialBuffers.instanceBuffer.get()))
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

bool RendererMeshSystem::meshGeometryHeapHandlesReady(const MeshResources& mesh)const noexcept{
    bool ready = true;
    ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool)noexcept{
        const Core::GpuDescriptorHandle handle = mesh.geometryHeapHandles[bindingSlot];
        ready = ready && handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
    }
    );
    return ready;
}

void RendererMeshSystem::populateMeshGeometryHeapSlots(InstanceGpuData& outInstance, const MeshResources& mesh)const noexcept{
    NWB_ASSERT(meshGeometryHeapHandlesReady(mesh));
    for(u32 slotIndex = 0u; slotIndex < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++slotIndex)
        outInstance.geometryHeapSlots[slotIndex] = 0u;
    ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool)noexcept{
        outInstance.geometryHeapSlots[bindingSlot] = mesh.geometryHeapHandles[bindingSlot].slot();
    }
    );
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

bool RendererMeshSystem::createMeshRenderBindings(MeshResources& mesh){
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
        const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        Core::GpuDescriptorRetirementGuard retireHandle(heap, handle);
        if(!handle.valid() || !heap.write(handle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.runtimeLocalBoundsBuffer.get()))){
            releaseMeshGeometryHeapHandles(mesh);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register runtime local bounds"));
            return false;
        }
        mesh.runtimeLocalBoundsHeapHandle = handle;
        retireHandle.release();
    }

    if(mesh.runtimeMesh && mesh.runtimeGeometryContentRevision != 0u && mesh.runtimeMeshletLocalBoundsBuffer && !mesh.runtimeMeshletLocalBoundsHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        const Core::BufferDesc& desc = mesh.runtimeMeshletLocalBoundsBuffer->getCreationDescription();
        const u64 requiredBytes = static_cast<u64>(mesh.meshletCount) * NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE;
        if(heap.isInitialized() && requiredBytes != 0u && desc.byteSize >= requiredBytes && desc.canHaveRawViews){
            const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
            Core::GpuDescriptorRetirementGuard retireHandle(heap, handle);
            if(handle.valid() && heap.write(handle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.runtimeMeshletLocalBoundsBuffer.get()))){
                mesh.runtimeMeshletLocalBoundsHeapHandle = handle;
                retireHandle.release();
            }
        }
        if(!mesh.runtimeMeshletLocalBoundsHeapHandle.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh '{}' could not register optional meshlet bounds; retaining whole-caster shadows"), StringConvert(mesh.meshName.resolvedText()));
        }
    }

    NWB_ASSERT(meshRenderBindingsReady(mesh));
    return true;
}

bool RendererMeshSystem::meshRenderBindingsReady(const MeshResources& mesh)const noexcept{
    if(!mesh.valid() || !meshGeometryHeapHandlesReady(mesh))
        return false;

    if(mesh.runtimeLocalBoundsBuffer && (!mesh.runtimeLocalBoundsHeapHandle.valid() || mesh.runtimeLocalBoundsHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer))
        return false;

    return true;
}

bool RendererMeshSystem::createMeshGeometryHeapHandles(MeshResources& mesh){
    if(meshGeometryHeapHandlesReady(mesh))
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap"), StringConvert(mesh.meshName.resolvedText()));
        return false;
    }

    // A non-empty partial set signals a broken lifetime transition, never a safe merge.
    for([[maybe_unused]] const Core::GpuDescriptorHandle handle : mesh.geometryHeapHandles)
        NWB_ASSERT(!handle.valid());

    Core::GpuDescriptorHandle acquired[NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT] = {};
    Core::GpuDescriptorRetirementGuard retireAcquired(heap, Span<const Core::GpuDescriptorHandle>(acquired, LengthOf(acquired)));
    bool registered = true;
    ForEachMeshSourceBuffer(mesh, [&](const u32 bindingSlot, const Core::BufferHandle& buffer, const bool){
        if(!registered)
            return;
        if(!buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no source buffer at binding {}"), StringConvert(mesh.meshName.resolvedText()), bindingSlot);
            registered = false;
            return;
        }

        const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        Core::GpuDescriptorRetirementGuard retireHandle(heap, handle);
        if(!handle.valid()){
            registered = false;
            return;
        }
        if(!heap.write(handle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, buffer.get()))){
            registered = false;
            return;
        }
        acquired[bindingSlot] = handle;
        retireHandle.release();
    }
    );

    if(!registered)
        return false;

    ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool)noexcept{
        mesh.geometryHeapHandles[bindingSlot] = acquired[bindingSlot];
    }
    );
    retireAcquired.release();
    NWB_ASSERT(meshGeometryHeapHandlesReady(mesh));
    return true;
}

bool RendererMeshSystem::ensureMeshSwBvhInputHeapHandles(MeshResources& mesh){
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
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no software-BVH position or triangle-index input"), StringConvert(mesh.meshName.resolvedText()));
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap for software-BVH inputs"), StringConvert(mesh.meshName.resolvedText()));
        return false;
    }

    const Core::GpuDescriptorHandle positionHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    Core::GpuDescriptorRetirementGuard retirePosition(heap, positionHandle);
    const Core::GpuDescriptorHandle triangleIndexHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    Core::GpuDescriptorRetirementGuard retireTriangleIndex(heap, triangleIndexHandle);
    const bool registered =
        positionHandle.valid()
        && triangleIndexHandle.valid()
        && heap.write(positionHandle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.positionBuffer.get()))
        && heap.write(triangleIndexHandle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.triangleIndexBuffer.get()))
    ;
    if(!registered){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-BVH inputs for mesh '{}'"), StringConvert(mesh.meshName.resolvedText()));
        return false;
    }

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

