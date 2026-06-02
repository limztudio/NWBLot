// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"
#include "csg_cap_source.h"

#include <impl/ecs_mesh_runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh{


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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh){
    outMesh = nullptr;

    const Name meshPath = meshAsset.name();
    if(!meshPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer mesh is empty"));
        return false;
    }

    const auto foundMesh = m_meshState.m_meshes.find(meshPath);
    if(foundMesh != m_meshState.m_meshes.end()){
        outMesh = &foundMesh.value();
        NWB_ASSERT(outMesh->valid());
        return true;
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

    MeshResources createdMesh(m_arena);
    createdMesh.meshName = meshPath;
    createdMesh.meshletCount = static_cast<u32>(mesh.meshlets().size());
    createdMesh.meshletPrimitiveIndexCount = static_cast<u32>(mesh.meshletPrimitiveIndices().size());
    if(!ECSRenderCsgCapSource::BuildPlaneCapTriangles(meshPath, mesh, createdMesh.csgPlaneCapTriangles))
        return false;

    bool uploaded = true;
    uploaded = __hidden_mesh::AssignMeshBuffer<Float3U>(
        m_graphics,
        meshPath,
        createdMesh.positionBuffer,
        AStringView(":positions"),
        mesh.positionStream(),
        NWB_TEXT("position")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.normalBuffer,
        AStringView(":normals"),
        mesh.normalStream(),
        NWB_TEXT("normal")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.tangentBuffer,
        AStringView(":tangents"),
        mesh.tangentStream(),
        NWB_TEXT("tangent")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Float2U>(
        m_graphics,
        meshPath,
        createdMesh.uv0Buffer,
        AStringView(":uv0"),
        mesh.uv0Stream(),
        NWB_TEXT("uv0")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.colorBuffer,
        AStringView(":colors"),
        mesh.colorStream(),
        NWB_TEXT("color")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletDesc>(
        m_graphics,
        meshPath,
        createdMesh.meshletDescBuffer,
        AStringView(":meshlets"),
        mesh.meshlets(),
        NWB_TEXT("meshlet descriptor")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletBounds>(
        m_graphics,
        meshPath,
        createdMesh.meshletBoundsBuffer,
        AStringView(":meshlet_bounds"),
        mesh.meshletBounds(),
        NWB_TEXT("meshlet bounds"),
        true
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<u8>(
        m_graphics,
        meshPath,
        createdMesh.meshletPositionRefDeltaBuffer,
        AStringView(":meshlet_position_ref_deltas"),
        mesh.meshletPositionRefDeltas(),
        NWB_TEXT("meshlet position ref delta"),
        true
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<u8>(
        m_graphics,
        meshPath,
        createdMesh.meshletAttributeRefDeltaBuffer,
        AStringView(":meshlet_attribute_ref_deltas"),
        mesh.meshletAttributeRefDeltas(),
        NWB_TEXT("meshlet attribute ref delta"),
        true
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletLocalVertexRef>(
        m_graphics,
        meshPath,
        createdMesh.meshletLocalVertexRefBuffer,
        AStringView(":meshlet_local_vertex_refs"),
        mesh.meshletLocalVertexRefs(),
        NWB_TEXT("meshlet local vertex ref")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<u8>(
        m_graphics,
        meshPath,
        createdMesh.meshletPrimitiveIndexBuffer,
        AStringView(":meshlet_primitive_indices"),
        mesh.meshletPrimitiveIndices(),
        NWB_TEXT("meshlet primitive index"),
        true
    ) && uploaded;
    if(!uploaded)
        return false;
    NWB_ASSERT(createdMesh.valid());

    auto result = m_meshState.m_meshes.try_emplace(meshPath, Move(createdMesh));
    auto it = result.first;

    outMesh = &it.value();
    NWB_ASSERT(outMesh->valid());
    return true;
}

bool RendererSystem::createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh){
    outMesh = nullptr;

    NWB_ASSERT(desc.valid());

    const auto foundMesh = m_meshState.m_meshes.find(desc.meshKey);
    if(foundMesh != m_meshState.m_meshes.end()){
        if(!foundMesh.value().runtimeMesh){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime mesh '{}' collides with a static mesh resource")
                , StringConvert(desc.meshKey.c_str())
            );
            return false;
        }
        if(foundMesh.value().runtimeMeshVersion != desc.version){
            m_meshState.m_meshes.erase(foundMesh);
        }
        else{
            outMesh = &foundMesh.value();
            NWB_ASSERT(outMesh->valid());
            return true;
        }
    }

    MeshResources createdMesh(m_arena);
    createdMesh.meshName = desc.meshKey;
    createdMesh.positionBuffer = desc.positionBuffer;
    createdMesh.normalBuffer = desc.normalBuffer;
    createdMesh.tangentBuffer = desc.tangentBuffer;
    createdMesh.uv0Buffer = desc.uv0Buffer;
    createdMesh.colorBuffer = desc.colorBuffer;
    createdMesh.meshletDescBuffer = desc.meshletDescBuffer;
    createdMesh.meshletBoundsBuffer = desc.meshletBoundsBuffer;
    createdMesh.meshletPositionRefDeltaBuffer = desc.meshletPositionRefDeltaBuffer;
    createdMesh.meshletAttributeRefDeltaBuffer = desc.meshletAttributeRefDeltaBuffer;
    createdMesh.meshletLocalVertexRefBuffer = desc.meshletLocalVertexRefBuffer;
    createdMesh.meshletPrimitiveIndexBuffer = desc.meshletPrimitiveIndexBuffer;
    createdMesh.meshletCount = desc.meshletCount;
    createdMesh.runtimeMesh = true;
    createdMesh.dynamicMeshletBoundsFresh = desc.dynamicMeshletBoundsFresh;
    createdMesh.dynamicMeshletConesFresh = desc.dynamicMeshletConesFresh;
    createdMesh.runtimeMeshVersion = desc.version;
    if(!__hidden_mesh::ResolveBufferElementCount(
        createdMesh.meshletPrimitiveIndexBuffer,
        createdMesh.meshletPrimitiveIndexCount,
        createdMesh.meshName,
        NWB_TEXT("meshlet primitive index")
    ))
        return false;
    NWB_ASSERT(createdMesh.valid());

    auto result = m_meshState.m_meshes.try_emplace(desc.meshKey, Move(createdMesh));
    auto it = result.first;

    outMesh = &it.value();
    NWB_ASSERT(outMesh->valid());
    return true;
}

void RendererSystem::pruneRuntimeMeshResources(){
    if(m_meshState.m_meshes.empty())
        return;

    const auto* meshSystem = m_world.getSystem<NWB::Impl::MeshSystem>();
    for(auto it = m_meshState.m_meshes.begin(); it != m_meshState.m_meshes.end();){
        const MeshResources& mesh = it.value();
        if(!mesh.runtimeMesh){
            ++it;
            continue;
        }

        if(meshSystem && meshSystem->containsRuntimeMesh(mesh.meshName, mesh.runtimeMeshVersion)){
            ++it;
            continue;
        }

        it = m_meshState.m_meshes.erase(it);
    }
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

