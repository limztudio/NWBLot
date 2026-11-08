// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_mesh_resource_cache.h"

#include "mesh_descriptor_transaction.h"
#include "mesh_system.h"

#include <impl/ecs_render/mesh/mesh_acceleration_state_policy.adhd.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>
#include <impl/ecs_mesh/runtime/types.adhd.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>


NWB_IMPL_BEGIN


namespace __hidden_runtime_mesh_resource_cache{


[[nodiscard]] static b8 RuntimeMeshSourceMatches(const MeshResources& mesh, const RuntimeMeshDesc& desc)noexcept{
    return
        mesh.runtimeMeshVersion == desc.version
        && mesh.sameBuffers(desc)
        && mesh.triangleIndexBuffer == desc.triangleIndexBuffer
        && mesh.attributeBuffer == desc.attributeBuffer
        && mesh.runtimeLocalBoundsBuffer == desc.localBoundsBuffer
        && mesh.runtimeMeshletLocalBoundsBuffer == desc.meshletLocalBoundsBuffer
        && mesh.meshletCount == desc.meshletCount
        && mesh.meshletPrimitiveIndexCount == desc.meshletPrimitiveIndexCount
    ;
}

static void RefreshRuntimeMeshContent(MeshResources& mesh, const RuntimeMeshDesc& desc)noexcept{
    mesh.runtimeGeometryContentRevision = desc.geometryContentRevision;
    mesh.dynamicMeshletBoundsFresh = desc.dynamicMeshletBoundsFresh;
    mesh.dynamicMeshletConesFresh = desc.dynamicMeshletConesFresh;
    RefreshRuntimeMeshCsgBounds(LoadFloatInt(desc.localBounds.minBounds), LoadFloatInt(desc.localBounds.maxBounds), desc.localBounds.finite(), mesh.csgLocalBounds);
}

[[nodiscard]] static b8 ValidateRawBufferLogicalByteCount(const Core::BufferHandle& buffer, const u32 logicalByteCount, const Name& meshName, const AStringView label){
    const Core::BufferDesc emptyDesc{};
    const Core::BufferDesc& desc = buffer ? buffer->getCreationDescription() : emptyDesc;
    return ValidateMeshRawBufferLogicalByteCount(buffer != nullptr, desc.structStride, desc.byteSize, logicalByteCount, meshName, label);
}


};


Expected<NotNull<MeshResources*>> RuntimeMeshResourceCache::create(const RuntimeMeshDesc& desc){
    NWB_ASSERT(desc.valid());

    const auto foundMesh = m_system.m_meshState.m_meshes.find(desc.meshKey);
    if(foundMesh != m_system.m_meshState.m_meshes.end()){
        if(!foundMesh.value().runtimeMesh){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime mesh '{}' collides with a static mesh resource"), ConvertString(desc.meshKey.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        if(!__hidden_runtime_mesh_resource_cache::RuntimeMeshSourceMatches(foundMesh.value(), desc)){
            m_system.releaseMeshGeometryHeapHandles(foundMesh.value());
            m_system.m_meshState.m_meshes.erase(foundMesh);
        }
        else{
            NWB_ASSERT(m_system.meshRenderBindingsReady(foundMesh.value()));
            MeshResources& mesh = foundMesh.value();
            __hidden_runtime_mesh_resource_cache::RefreshRuntimeMeshContent(mesh, desc);
            NWB_ASSERT(mesh.valid());
            return MakeNotNull(&mesh);
        }
    }

    MeshResources createdMesh;
    createdMesh.meshName = desc.meshKey;
    createdMesh.solidTriangleWords = Core::MakeGlobalUnique<u32[]>(m_system.m_arena, desc.solidTriangleWords.size());
    if(!createdMesh.solidTriangleWords)
        return MakeUnexpected(Failure{});
    const usize solidByteCount = desc.solidTriangleWords.size_bytes();
    NWB_MEMCPY(createdMesh.solidTriangleWords.get(), solidByteCount, desc.solidTriangleWords.data(), solidByteCount);
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
    createdMesh.triangleIndexBuffer = desc.triangleIndexBuffer;
    createdMesh.attributeBuffer = desc.attributeBuffer;
    createdMesh.runtimeLocalBoundsBuffer = desc.localBoundsBuffer;
    createdMesh.runtimeMeshletLocalBoundsBuffer = desc.meshletLocalBoundsBuffer;
    createdMesh.blasBuildPending = (desc.triangleIndexBuffer != nullptr);
    createdMesh.meshletCount = desc.meshletCount;
    createdMesh.meshletPrimitiveIndexCount = desc.meshletPrimitiveIndexCount;
    createdMesh.runtimeMesh = true;
    createdMesh.runtimeMeshVersion = desc.version;
    NWB_ASSERT(desc.localBounds.valid());
    __hidden_runtime_mesh_resource_cache::RefreshRuntimeMeshContent(createdMesh, desc);
    if(!ValidateRuntimeMeshPrimitiveIndexCount(createdMesh.meshName, createdMesh.meshletPrimitiveIndexCount, s_MeshletTriangleIndexCount))
        return MakeUnexpected(Failure{});
    if(!__hidden_runtime_mesh_resource_cache::ValidateRawBufferLogicalByteCount(createdMesh.meshletPrimitiveIndexBuffer, createdMesh.meshletPrimitiveIndexCount, createdMesh.meshName, s_MeshletPrimitiveIndexBufferLabel))
        return MakeUnexpected(Failure{});
    NWB_ASSERT(createdMesh.valid());
    MeshDescriptorTransaction retireUnpublished(m_system, createdMesh);
    if(!m_system.createMeshRenderBindings(createdMesh))
        return MakeUnexpected(Failure{});

    auto result = m_system.m_meshState.m_meshes.try_emplace(desc.meshKey, Move(createdMesh));
    if(result.second)
        retireUnpublished.release();
    auto it = result.first;

    NWB_ASSERT(it.value().valid());
    return MakeNotNull(&it.value());
}

Expected<NotNull<MeshResources*>> RuntimeMeshResourceCache::find(const RuntimeMeshDesc& desc)noexcept{
    NWB_ASSERT(desc.valid());

    const auto foundMesh = m_system.m_meshState.m_meshes.find(desc.meshKey);
    if(foundMesh == m_system.m_meshState.m_meshes.end())
        return MakeUnexpected(Failure{});

    MeshResources& mesh = foundMesh.value();
    if(!mesh.runtimeMesh || !__hidden_runtime_mesh_resource_cache::RuntimeMeshSourceMatches(mesh, desc))
        return MakeUnexpected(Failure{});

    NWB_ASSERT(m_system.meshRenderBindingsReady(mesh));
    __hidden_runtime_mesh_resource_cache::RefreshRuntimeMeshContent(mesh, desc);
    NWB_ASSERT(mesh.valid());
    return MakeNotNull(&mesh);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
