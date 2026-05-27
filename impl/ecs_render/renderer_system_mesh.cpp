// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"

#include <impl/ecs_mesh/runtime_mesh_buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_system_mesh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static Core::BufferHandle SetupMeshBuffer(
    Core::Graphics& graphics,
    const Name& meshName,
    const AStringView suffix,
    const PayloadVector& payload,
    const tchar* label,
    const bool canHaveRawViews = false
){
    const Name bufferName = DeriveName(meshName, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive {} buffer name for mesh '{}'")
            , label
            , StringConvert(meshName.c_str())
        );
        return {};
    }

    Core::BufferHandle buffer;
    const RuntimeMeshBufferUpload::BufferSetupFailure::Enum failure = RuntimeMeshBufferUpload::SetupRequiredBuffer<PayloadT>(
        graphics,
        bufferName,
        payload,
        { false, canHaveRawViews },
        buffer
    );
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return buffer;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has empty {} payload")
            , StringConvert(meshName.c_str())
            , label
        );
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} payload byte size overflows")
            , StringConvert(meshName.c_str())
            , label
        );
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create {} buffer for mesh '{}'")
        , label
        , StringConvert(meshName.c_str())
    );
    return {};
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static bool AssignMeshBuffer(
    Core::Graphics& graphics,
    const Name& meshName,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const tchar* label,
    const bool canHaveRawViews = false
){
    outBuffer = SetupMeshBuffer<PayloadT>(
        graphics,
        meshName,
        suffix,
        payload,
        label,
        canHaveRawViews
    );
    return outBuffer != nullptr;
}

[[nodiscard]] static bool ResolveBufferElementCount(
    const Core::BufferHandle& buffer,
    u32& outCount,
    const Name& meshName,
    const tchar* label
){
    outCount = 0u;
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no {} buffer")
            , StringConvert(meshName.c_str())
            , label
        );
        return false;
    }

    const Core::BufferDesc& desc = buffer->getDescription();
    if(desc.structStride == 0u || desc.byteSize == 0u || (desc.byteSize % desc.structStride) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} buffer has invalid structured layout")
            , StringConvert(meshName.c_str())
            , label
        );
        return false;
    }

    const u64 count = desc.byteSize / desc.structStride;
    if(count > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} buffer element count exceeds u32 limits")
            , StringConvert(meshName.c_str())
            , label
        );
        return false;
    }

    outCount = static_cast<u32>(count);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::destroyMeshBindingSets(){
    m_emulationViewBindingSet = nullptr;
    for(auto it = m_meshMeshes.begin(); it != m_meshMeshes.end(); ++it){
        MeshResources& mesh = it.value();
        mesh.meshBindingSet = nullptr;
        mesh.computeBindingSet = nullptr;
    }
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

bool RendererSystem::createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh){
    outMesh = nullptr;

    const Name meshPath = meshAsset.name();
    if(!meshPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer mesh is empty"));
        return false;
    }

    const auto foundMesh = m_meshMeshes.find(meshPath);
    if(foundMesh != m_meshMeshes.end()){
        outMesh = &foundMesh.value();
        return outMesh->valid();
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Mesh::AssetTypeName(), meshPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load mesh '{}'"), StringConvert(meshPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Mesh::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not mesh"), StringConvert(meshPath.c_str()));
        return false;
    }

    const Mesh& mesh = static_cast<const Mesh&>(*loadedAsset);
    if(!mesh.validatePayload())
        return false;

    if(
        mesh.meshlets().size() > static_cast<usize>(Limit<u32>::s_Max)
        || mesh.meshletPrimitiveIndices().size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' meshlet payload exceeds u32 limits")
            , StringConvert(meshPath.c_str())
        );
        return false;
    }

    MeshResources createdMesh;
    createdMesh.meshName = meshPath;
    createdMesh.meshletCount = static_cast<u32>(mesh.meshlets().size());
    createdMesh.meshletPrimitiveIndexCount = static_cast<u32>(mesh.meshletPrimitiveIndices().size());

    bool uploaded = true;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<Float3U>(
        m_graphics,
        meshPath,
        createdMesh.positionBuffer,
        AStringView(":positions"),
        mesh.positionStream(),
        NWB_TEXT("position")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.normalBuffer,
        AStringView(":normals"),
        mesh.normalStream(),
        NWB_TEXT("normal")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.tangentBuffer,
        AStringView(":tangents"),
        mesh.tangentStream(),
        NWB_TEXT("tangent")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<Float2U>(
        m_graphics,
        meshPath,
        createdMesh.uv0Buffer,
        AStringView(":uv0"),
        mesh.uv0Stream(),
        NWB_TEXT("uv0")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.colorBuffer,
        AStringView(":colors"),
        mesh.colorStream(),
        NWB_TEXT("color")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<MeshletDesc>(
        m_graphics,
        meshPath,
        createdMesh.meshletDescBuffer,
        AStringView(":meshlets"),
        mesh.meshlets(),
        NWB_TEXT("meshlet descriptor")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<MeshletBounds>(
        m_graphics,
        meshPath,
        createdMesh.meshletBoundsBuffer,
        AStringView(":meshlet_bounds"),
        mesh.meshletBounds(),
        NWB_TEXT("meshlet bounds"),
        true
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<MeshletDeformedPositionRef>(
        m_graphics,
        meshPath,
        createdMesh.meshletPositionRefBuffer,
        AStringView(":meshlet_position_refs"),
        mesh.meshletPositionRefs(),
        NWB_TEXT("meshlet position ref")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<MeshletShadingAttributeRef>(
        m_graphics,
        meshPath,
        createdMesh.meshletAttributeRefBuffer,
        AStringView(":meshlet_attribute_refs"),
        mesh.meshletAttributeRefs(),
        NWB_TEXT("meshlet attribute ref")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<MeshletLocalVertexRef>(
        m_graphics,
        meshPath,
        createdMesh.meshletLocalVertexRefBuffer,
        AStringView(":meshlet_local_vertex_refs"),
        mesh.meshletLocalVertexRefs(),
        NWB_TEXT("meshlet local vertex ref")
    ) && uploaded;
    uploaded = __hidden_renderer_system_mesh::AssignMeshBuffer<u8>(
        m_graphics,
        meshPath,
        createdMesh.meshletPrimitiveIndexBuffer,
        AStringView(":meshlet_primitive_indices"),
        mesh.meshletPrimitiveIndices(),
        NWB_TEXT("meshlet primitive index"),
        true
    ) && uploaded;
    if(!uploaded || !createdMesh.valid())
        return false;

    auto result = m_meshMeshes.try_emplace(meshPath, Move(createdMesh));
    auto it = result.first;

    outMesh = &it.value();
    return outMesh->valid();
}

bool RendererSystem::createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh){
    outMesh = nullptr;

    if(!desc.valid())
        return false;

    const auto foundMesh = m_meshMeshes.find(desc.meshKey);
    if(foundMesh != m_meshMeshes.end()){
        if(!foundMesh.value().runtimeMesh){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime mesh '{}' collides with a static mesh resource")
                , StringConvert(desc.meshKey.c_str())
            );
            return false;
        }
        if(foundMesh.value().runtimeMeshVersion != desc.version){
            m_meshMeshes.erase(foundMesh);
        }
        else{
            outMesh = &foundMesh.value();
            return outMesh->valid();
        }
    }

    MeshResources createdMesh;
    createdMesh.meshName = desc.meshKey;
    createdMesh.positionBuffer = desc.positionBuffer;
    createdMesh.normalBuffer = desc.normalBuffer;
    createdMesh.tangentBuffer = desc.tangentBuffer;
    createdMesh.uv0Buffer = desc.uv0Buffer;
    createdMesh.colorBuffer = desc.colorBuffer;
    createdMesh.meshletDescBuffer = desc.meshletDescBuffer;
    createdMesh.meshletBoundsBuffer = desc.meshletBoundsBuffer;
    createdMesh.meshletPositionRefBuffer = desc.meshletPositionRefBuffer;
    createdMesh.meshletAttributeRefBuffer = desc.meshletAttributeRefBuffer;
    createdMesh.meshletLocalVertexRefBuffer = desc.meshletLocalVertexRefBuffer;
    createdMesh.meshletPrimitiveIndexBuffer = desc.meshletPrimitiveIndexBuffer;
    createdMesh.meshletCount = desc.meshletCount;
    createdMesh.runtimeMesh = true;
    createdMesh.runtimeMeshVersion = desc.version;
    if(!__hidden_renderer_system_mesh::ResolveBufferElementCount(
        createdMesh.meshletPrimitiveIndexBuffer,
        createdMesh.meshletPrimitiveIndexCount,
        createdMesh.meshName,
        NWB_TEXT("meshlet primitive index")
    ))
        return false;
    if(!createdMesh.valid())
        return false;

    auto result = m_meshMeshes.try_emplace(desc.meshKey, Move(createdMesh));
    auto it = result.first;

    outMesh = &it.value();
    return outMesh->valid();
}

void RendererSystem::pruneRuntimeMeshResources(){
    if(m_meshMeshes.empty())
        return;

    const auto* meshSystem = m_world.getSystem<NWB::Impl::MeshSystem>();
    for(auto it = m_meshMeshes.begin(); it != m_meshMeshes.end();){
        const MeshResources& mesh = it.value();
        if(!mesh.runtimeMesh){
            ++it;
            continue;
        }

        if(meshSystem && meshSystem->containsRuntimeMesh(mesh.meshName, mesh.runtimeMeshVersion)){
            ++it;
            continue;
        }

        it = m_meshMeshes.erase(it);
    }
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
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(s_MeshInstanceBindingSlot, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(s_MeshViewBindingSlot, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(s_MeshMaterialTypedBindingSlot, m_materialTypedBuffer.get()));
}

void RendererSystem::addMeshDrawBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const{
    addMeshSourceBindingItems(bindingSetDesc, mesh);
    addMeshFrameBindingItems(bindingSetDesc);
}

bool RendererSystem::meshFrameBindingResourcesReady(const tchar* context)const{
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires an instance buffer"), context);
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a mesh view buffer"), context);
        return false;
    }
    if(!m_materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a material typed buffer"), context);
        return false;
    }

    return true;
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

    Core::IDevice* device = m_graphics.getDevice();
    mesh.meshBindingSet = device->createBindingSet(bindingSetDesc, m_meshBindingLayout);
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

    Core::IDevice* device = m_graphics.getDevice();
    mesh.computeBindingSet = device->createBindingSet(bindingSetDesc, m_computeBindingLayout);
    if(!mesh.computeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding set for mesh '{}'")
            , StringConvert(mesh.meshName.c_str())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

