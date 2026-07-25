// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMeshSystem::destroyMeshBindingSets(){
    drawState().m_emulationViewBindingSet = nullptr;
    drawState().m_bindlessEmulationViewBindingSet = nullptr;
    for(auto it = meshState().m_meshes.begin(); it != meshState().m_meshes.end(); ++it){
        MeshResources& mesh = it.value();
        mesh.meshBindingSet = nullptr;
        mesh.bindlessMeshBindingSet = nullptr;
        mesh.computeBindingSet = nullptr;
    }
}

bool RendererMeshSystem::createMeshBindingSet(MeshResources& mesh){
    if(mesh.meshBindingSet)
        return true;
    if(!drawState().m_meshBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh shader binding layout was not validated before mesh binding-set creation"));
        return false;
    }
    if(!meshFrameBindingResourcesReady(NWB_TEXT("mesh binding set")))
        return false;

    Core::BindingSetDesc bindingSetDesc(arena());
    addMeshDrawBindingItems(bindingSetDesc, mesh);

    auto* device = graphics().getDevice();
    mesh.meshBindingSet = device->createBindingSet(bindingSetDesc, drawState().m_meshBindingLayout);
    if(!mesh.meshBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding set for mesh '{}'"), StringConvert(mesh.meshName.c_str()));
        return false;
    }

    return true;
}

bool RendererMeshSystem::createBindlessMeshBindingSet(MeshResources& mesh){
    if(mesh.bindlessMeshBindingSet)
        return true;
    if(!drawState().m_bindlessMeshBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: bindless mesh shader binding layout was not validated before mesh binding-set creation"));
        return false;
    }
    if(!meshFrameBindingResourcesReady(NWB_TEXT("bindless mesh binding set")))
        return false;

    Core::BindingSetDesc bindingSetDesc(arena());
    addMeshDrawBindingItems(bindingSetDesc, mesh);

    auto* device = graphics().getDevice();
    mesh.bindlessMeshBindingSet = device->createBindingSet(bindingSetDesc, drawState().m_bindlessMeshBindingLayout);
    if(!mesh.bindlessMeshBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create bindless mesh shader binding set for mesh '{}'"), StringConvert(mesh.meshName.c_str()));
        return false;
    }

    return true;
}

bool RendererMeshSystem::createComputeBindingSet(MeshResources& mesh){
    if(mesh.computeBindingSet)
        return true;
    if(!drawState().m_computeBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation binding layout was not validated before mesh binding-set creation"));
        return false;
    }
    if(!meshFrameBindingResourcesReady(NWB_TEXT("compute binding set")))
        return false;

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
    addMeshDrawBindingItems(bindingSetDesc, mesh);
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

bool RendererMeshSystem::meshFrameBindingResourcesReady(const tchar* context)const{
    if(!drawState().m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires an instance buffer"), context);
        return false;
    }
    if(!drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a mesh view buffer"), context);
        return false;
    }
    if(!drawState().m_materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a material typed buffer"), context);
        return false;
    }

    return true;
}

void RendererMeshSystem::addMeshFrameBindingItems(Core::BindingSetDesc& bindingSetDesc)const{
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(s_MeshInstanceBindingSlot, drawState().m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(s_MeshViewBindingSlot, drawState().m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(s_MeshMaterialTypedBindingSlot, drawState().m_materialTypedBuffer.get()));
}

void RendererMeshSystem::addMeshDrawBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const{
    static_cast<void>(mesh);
    addMeshFrameBindingItems(bindingSetDesc);
}

void RendererMeshSystem::addMeshFrameBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc){
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(s_MeshInstanceBindingSlot, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(s_MeshViewBindingSlot, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(s_MeshMaterialTypedBindingSlot, 1));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

