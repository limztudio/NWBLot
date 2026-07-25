// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMeshSystem::createComputeBindingSet(MeshResources& mesh){
    if(mesh.computeBindingSet)
        return true;
    if(!drawState().m_computeBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation binding layout was not validated before mesh binding-set creation"));
        return false;
    }
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

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(s_MeshGeneratedVertexBindingSlot, mesh.emulationVertexBuffer.get()));

    auto* device = graphics().getDevice();
    mesh.computeBindingSet = device->createBindingSet(bindingSetDesc, drawState().m_computeBindingLayout);
    if(!mesh.computeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding set for mesh '{}'")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    return true;
}

bool RendererMeshSystem::createMeshFrameHeapHandles(){
    if(meshFrameHeapHandlesReady())
        return true;

    if(
        drawState().m_instanceBufferHeapHandle.valid()
        || drawState().m_materialTypedBufferHeapHandle.valid()
        || drawState().m_meshViewBufferHeapHandle.valid()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration is partially initialized"));
        return false;
    }
    if(!drawState().m_instanceBuffer || !drawState().m_materialTypedBuffer || !drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires instance, typed-material, and view buffers"));
        return false;
    }

    auto* device = graphics().getDevice();
    if(!device || !device->getDescriptorHeap().isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires the initialized global descriptor heap"));
        return false;
    }
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    const Core::GpuDescriptorHandle instanceHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    const Core::GpuDescriptorHandle materialTypedHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    const Core::GpuDescriptorHandle viewHandle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    const bool registered =
        instanceHandle.valid()
        && materialTypedHandle.valid()
        && viewHandle.valid()
        && heap.write(instanceHandle, Core::BindingSetItem::StructuredBuffer_SRV(0u, drawState().m_instanceBuffer.get()))
        && heap.write(materialTypedHandle, Core::BindingSetItem::StructuredBuffer_SRV(0u, drawState().m_materialTypedBuffer.get()))
        && heap.write(viewHandle, Core::BindingSetItem::ConstantBuffer(0u, drawState().m_meshViewBuffer.get()))
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
    outSlots.reserved = 0u;
}

void RendererMeshSystem::releaseMeshFrameHeapHandles(){
    auto* device = graphics().getDevice();
    if(device && device->getDescriptorHeap().isInitialized()){
        Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
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

    auto* device = graphics().getDevice();
    if(!device){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' cannot register geometry without a graphics device")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    // A failed earlier attempt leaves no live handles behind (the failure path below resets every acquired slot),
    // so a non-empty partial set signals a broken lifetime transition rather than something we can safely merge.
    for(const Core::GpuDescriptorHandle handle : mesh.geometryHeapHandles){
        if(handle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has a partial geometry heap registration")
                , StringConvert(mesh.meshName.c_str())
            );
            return false;
        }
    }

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
        if(!heap.write(handle, Core::BindingSetItem::StructuredBuffer_SRV(0u, buffer.get()))){
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

void RendererMeshSystem::releaseMeshGeometryHeapHandles(MeshResources& mesh){
    auto* device = graphics().getDevice();
    if(device && device->getDescriptorHeap().isInitialized()){
        Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
        for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles){
            if(handle.valid())
                heap.free(handle);
            handle = Core::GpuDescriptorHandle::invalid();
        }
        return;
    }

    for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles)
        handle = Core::GpuDescriptorHandle::invalid();
}

void RendererMeshSystem::releaseAllMeshGeometryHeapHandles(){
    for(auto it = meshState().m_meshes.begin(); it != meshState().m_meshes.end(); ++it)
        releaseMeshGeometryHeapHandles(it.value());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

