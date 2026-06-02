// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::destroyMeshBindingSets(){
    m_drawState.m_emulationViewBindingSet = nullptr;
    for(auto it = m_meshState.m_meshes.begin(); it != m_meshState.m_meshes.end(); ++it){
        MeshResources& mesh = it.value();
        mesh.meshBindingSet = nullptr;
        mesh.computeBindingSet = nullptr;
    }
}

bool RendererSystem::createMeshBindingSet(MeshResources& mesh){
    if(mesh.meshBindingSet)
        return true;
    if(!createMeshShaderResources())
        return false;
    if(!meshFrameBindingResourcesReady(NWB_TEXT("mesh binding set")))
        return false;

    Core::BindingSetDesc bindingSetDesc(m_arena);
    addMeshDrawBindingItems(bindingSetDesc, mesh);

    auto* device = m_graphics.getDevice();
    mesh.meshBindingSet = device->createBindingSet(bindingSetDesc, m_drawState.m_meshBindingLayout);
    if(!mesh.meshBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding set for mesh '{}'"), StringConvert(mesh.meshName.c_str()));
        return false;
    }

    return true;
}

bool RendererSystem::createComputeBindingSet(MeshResources& mesh){
    if(mesh.computeBindingSet)
        return true;
    if(!createComputeEmulationResources())
        return false;
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
        mesh.emulationVertexBuffer = m_graphics.createBuffer(emulationVertexBufferDesc);
        if(!mesh.emulationVertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for mesh '{}'")
                , StringConvert(mesh.meshName.c_str())
            );
            return false;
        }
    }

    Core::BindingSetDesc bindingSetDesc(m_arena);
    addMeshDrawBindingItems(bindingSetDesc, mesh);
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(s_MeshGeneratedVertexBindingSlot, mesh.emulationVertexBuffer.get()));

    auto* device = m_graphics.getDevice();
    mesh.computeBindingSet = device->createBindingSet(bindingSetDesc, m_drawState.m_computeBindingLayout);
    if(!mesh.computeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding set for mesh '{}'")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    return true;
}

bool RendererSystem::meshFrameBindingResourcesReady(const tchar* context)const{
    if(!m_drawState.m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires an instance buffer"), context);
        return false;
    }
    if(!m_drawState.m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a mesh view buffer"), context);
        return false;
    }
    if(!m_drawState.m_materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a material typed buffer"), context);
        return false;
    }

    return true;
}

void RendererSystem::addMeshSourceBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const{
    forEachMeshSourceBuffer(mesh, [&](const u32 bindingSlot, const Core::BufferHandle& buffer, const bool rawView){
        if(rawView)
            bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(bindingSlot, buffer.get()));
        else
            bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(bindingSlot, buffer.get()));
    });
}

void RendererSystem::addMeshFrameBindingItems(Core::BindingSetDesc& bindingSetDesc)const{
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(s_MeshInstanceBindingSlot, m_drawState.m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(s_MeshViewBindingSlot, m_drawState.m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(s_MeshMaterialTypedBindingSlot, m_drawState.m_materialTypedBuffer.get()));
}

void RendererSystem::addMeshDrawBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const{
    addMeshSourceBindingItems(bindingSetDesc, mesh);
    addMeshFrameBindingItems(bindingSetDesc);
}

void RendererSystem::addMeshSourceBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc){
    forEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool rawView){
        if(rawView)
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(bindingSlot, 1));
        else
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(bindingSlot, 1));
    });
}

void RendererSystem::addMeshFrameBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc){
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(s_MeshInstanceBindingSlot, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(s_MeshViewBindingSlot, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(s_MeshMaterialTypedBindingSlot, 1));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

