// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMeshSystem::createComputeEmulationHeapHandle(MeshResources& mesh){
    if(mesh.emulationVertexHeapHandle.valid())
        return true;
    if(!mesh.emulationVertexBuffer){
        const Name emulationVertexBufferName = DeriveName(mesh.meshName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for mesh '{}'")
                , StringConvert(mesh.meshName.c_str())
            );
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(static_cast<u64>(mesh.meshletPrimitiveIndexCount) * ECSRenderDetail::s_EmulatedVertexStride)
            .setStructStride(ECSRenderDetail::s_EmulatedVertexStride)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setDebugName(emulationVertexBufferName)
        ;
        mesh.emulationVertexBuffer = graphics().createBuffer(emulationVertexBufferDesc);
        if(!mesh.emulationVertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for mesh '{}'")
                , StringConvert(mesh.meshName.c_str())
            );
            return false;
        }
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation vertex buffer requires the initialized global descriptor heap"));
        return false;
    }
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(
        !handle.valid()
        || !heap.write(handle, Core::DescriptorWriteItem::StructuredBuffer_UAV(0u, mesh.emulationVertexBuffer.get()))
    ){
        if(handle.valid())
            heap.free(handle);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register compute-emulation vertex buffer in the descriptor heap for mesh '{}'")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }
    mesh.emulationVertexHeapHandle = handle;

    return true;
}

bool RendererMeshSystem::createMeshFrameHeapHandles(){
    if(meshFrameHeapHandlesReady())
        return true;

    NWB_ASSERT(!drawState().m_instanceBufferHeapHandle.valid());
    NWB_ASSERT(!drawState().m_materialTypedBufferHeapHandle.valid());
    NWB_ASSERT(!drawState().m_meshViewBufferHeapHandle.valid());
    if(!drawState().m_instanceBuffer || !drawState().m_materialTypedBuffer || !drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires instance, typed-material, and view buffers"));
        return false;
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires the initialized global descriptor heap"));
        return false;
    }
    const Core::GpuDescriptorHandle instanceHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    const Core::GpuDescriptorHandle materialTypedHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    const Core::GpuDescriptorHandle viewHandle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    const bool registered =
        instanceHandle.valid()
        && materialTypedHandle.valid()
        && viewHandle.valid()
        && heap.write(instanceHandle, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, drawState().m_instanceBuffer.get()))
        && heap.write(materialTypedHandle, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, drawState().m_materialTypedBuffer.get()))
        && heap.write(viewHandle, Core::DescriptorWriteItem::ConstantBuffer(0u, drawState().m_meshViewBuffer.get()))
    ;
    if(!registered){
        if(instanceHandle.valid())
            heap.free(instanceHandle);
        if(materialTypedHandle.valid())
            heap.free(materialTypedHandle);
        if(viewHandle.valid())
            heap.free(viewHandle);
        return false;
    }

    drawState().m_instanceBufferHeapHandle = instanceHandle;
    drawState().m_materialTypedBufferHeapHandle = materialTypedHandle;
    drawState().m_meshViewBufferHeapHandle = viewHandle;
    NWB_ASSERT(meshFrameHeapHandlesReady());
    return true;
}

bool RendererMeshSystem::meshFrameHeapHandlesReady()const{
    return
        drawState().m_instanceBuffer
        && drawState().m_materialTypedBuffer
        && drawState().m_meshViewBuffer
        && drawState().m_instanceBufferHeapHandle.valid()
        && drawState().m_instanceBufferHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && drawState().m_materialTypedBufferHeapHandle.valid()
        && drawState().m_materialTypedBufferHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && drawState().m_meshViewBufferHeapHandle.valid()
        && drawState().m_meshViewBufferHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

void RendererMeshSystem::populateMeshFrameHeapSlots(ECSRenderDetail::MeshFrameHeapSlots& outSlots)const{
    NWB_ASSERT(meshFrameHeapHandlesReady());
    outSlots.instance = drawState().m_instanceBufferHeapHandle.slot();
    outSlots.materialTyped = drawState().m_materialTypedBufferHeapHandle.slot();
    outSlots.view = drawState().m_meshViewBufferHeapHandle.slot();
    outSlots.generatedVertex = 0u;
}

void RendererMeshSystem::releaseMeshFrameHeapHandles(){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        if(drawState().m_instanceBufferHeapHandle.valid())
            heap.free(drawState().m_instanceBufferHeapHandle);
        if(drawState().m_materialTypedBufferHeapHandle.valid())
            heap.free(drawState().m_materialTypedBufferHeapHandle);
        if(drawState().m_meshViewBufferHeapHandle.valid())
            heap.free(drawState().m_meshViewBufferHeapHandle);
    }
    drawState().m_instanceBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    drawState().m_materialTypedBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    drawState().m_meshViewBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererMeshSystem::createMeshGeometryHeapHandles(MeshResources& mesh){
    if(meshGeometryHeapHandlesReady(mesh))
        return true;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    // A failed earlier attempt leaves no live handles behind (the failure path below resets every acquired slot),
    // so a non-empty partial set signals a broken lifetime transition rather than something we can safely merge.
    for([[maybe_unused]] const Core::GpuDescriptorHandle handle : mesh.geometryHeapHandles)
        NWB_ASSERT(!handle.valid());

    Core::GpuDescriptorHandle acquired[NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT] = {};
    bool registered = true;
    forEachMeshSourceBuffer(mesh, [&](const u32 bindingSlot, const Core::BufferHandle& buffer, const bool){
        if(!registered)
            return;
        if(!buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no source buffer at binding {}")
                , StringConvert(mesh.meshName.c_str())
                , bindingSlot
            );
            registered = false;
            return;
        }

        const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!handle.valid()){
            registered = false;
            return;
        }
        if(!heap.write(handle, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer.get()))){
            heap.free(handle);
            registered = false;
            return;
        }
        acquired[bindingSlot] = handle;
    });

    if(!registered){
        for(const Core::GpuDescriptorHandle handle : acquired){
            if(handle.valid())
                heap.free(handle);
        }
        return false;
    }

    forEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool){
        mesh.geometryHeapHandles[bindingSlot] = acquired[bindingSlot];
    });
    NWB_ASSERT(meshGeometryHeapHandlesReady(mesh));
    return true;
}

bool RendererMeshSystem::meshGeometryHeapHandlesReady(const MeshResources& mesh)const{
    bool ready = true;
    forEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool){
        const Core::GpuDescriptorHandle handle = mesh.geometryHeapHandles[bindingSlot];
        ready = ready
            && handle.valid()
            && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        ;
    });
    return ready;
}

void RendererMeshSystem::populateMeshGeometryHeapSlots(InstanceGpuData& outInstance, const MeshResources& mesh)const{
    NWB_ASSERT(meshGeometryHeapHandlesReady(mesh));
    for(u32 slotIndex = 0u; slotIndex < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++slotIndex)
        outInstance.geometryHeapSlots[slotIndex] = 0u;
    forEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool){
        outInstance.geometryHeapSlots[bindingSlot] = mesh.geometryHeapHandles[bindingSlot].slot();
    });
}

bool RendererMeshSystem::ensureMeshSwBvhInputHeapHandles(MeshResources& mesh){
    const auto inputsReady = [&mesh](){
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
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no software-BVH position or triangle-index input")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap for software-BVH inputs")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    const Core::GpuDescriptorHandle positionHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    const Core::GpuDescriptorHandle triangleIndexHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    const bool registered =
        positionHandle.valid()
        && triangleIndexHandle.valid()
        && heap.write(positionHandle, Core::DescriptorWriteItem::RawBuffer_SRV(0u, mesh.positionBuffer.get()))
        && heap.write(triangleIndexHandle, Core::DescriptorWriteItem::RawBuffer_SRV(0u, mesh.triangleIndexBuffer.get()))
    ;
    if(!registered){
        if(positionHandle.valid())
            heap.free(positionHandle);
        if(triangleIndexHandle.valid())
            heap.free(triangleIndexHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-BVH inputs for mesh '{}'")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    mesh.swBvhPositionHeapHandle = positionHandle;
    mesh.swBvhTriangleIndexHeapHandle = triangleIndexHandle;
    NWB_ASSERT(inputsReady());
    return true;
}

void RendererMeshSystem::releaseMeshGeometryHeapHandles(MeshResources& mesh){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles){
            if(handle.valid())
                heap.free(handle);
            handle = Core::GpuDescriptorHandle::invalid();
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
        mesh.swBvhPositionHeapHandle = Core::GpuDescriptorHandle::invalid();
        mesh.swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::invalid();
        mesh.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
        mesh.swBvhParentHeapHandle = Core::GpuDescriptorHandle::invalid();
        mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::invalid();
        return;
    }

    for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles)
        handle = Core::GpuDescriptorHandle::invalid();
    mesh.swBvhPositionHeapHandle = Core::GpuDescriptorHandle::invalid();
    mesh.swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::invalid();
    mesh.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    mesh.swBvhParentHeapHandle = Core::GpuDescriptorHandle::invalid();
    mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::invalid();
}

void RendererMeshSystem::releaseAllMeshGeometryHeapHandles(){
    for(auto it = meshState().m_meshes.begin(); it != meshState().m_meshes.end(); ++it)
        releaseMeshGeometryHeapHandles(it.value());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

