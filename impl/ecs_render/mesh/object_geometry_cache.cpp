// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "object_geometry_cache.h"

#include "mesh_system.h"

#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/assets_mesh/payload_types.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ObjectGeometryCacheSnapshot::valid()const noexcept{
    return buffer && decoderPipeline && heapHandle.valid() && heapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
}

bool ResolveObjectGeometryCacheByteSize(const u64 localVertexRefByteSize, u64& outByteSize)noexcept{
    outByteSize = 0u;
    if(localVertexRefByteSize == 0u || localVertexRefByteSize % sizeof(MeshletLocalVertexRef) != 0u)
        return false;
    const u64 vertexCount = localVertexRefByteSize / sizeof(MeshletLocalVertexRef) + NWB_MESH_OBJECT_FIRST_VERTEX_INDEX;
    constexpr u64 s_AddressableBytes = static_cast<u64>(Limit<u32>::s_Max) + 1u;
    if(vertexCount > s_AddressableBytes / NWB_MESH_OBJECT_VERTEX_BYTE_SIZE)
        return false;
    outByteSize = vertexCount * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE;
    return true;
}

bool AcceptObjectGeometryCacheWrite(
    MeshResources& mesh,
    const RuntimeMeshBuffers& sourceBuffers,
    const ObjectGeometryCacheSnapshot& expected,
    const bool runtimeMesh)noexcept{
    ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    if(!expected.valid() || cache.buffer != expected.buffer)
        return false;
    // Accepted writes establish the retained buffer state even if current source content already moved on.
    cache.initialized = true;
    cache.acceptedContent = false;
    if(
        cache.decoderPipeline != expected.decoderPipeline
        || cache.heapHandle != expected.heapHandle
        || mesh.runtimeMesh != runtimeMesh
        || mesh.runtimeGeometryContentRevision != expected.sourceRevision
        || mesh.positionBuffer != sourceBuffers.positionBuffer
        || mesh.normalBuffer != sourceBuffers.normalBuffer
        || mesh.tangentBuffer != sourceBuffers.tangentBuffer
        || mesh.uv0Buffer != sourceBuffers.uv0Buffer
        || mesh.colorBuffer != sourceBuffers.colorBuffer
        || mesh.meshletDescBuffer != sourceBuffers.meshletDescBuffer
        || mesh.meshletBoundsBuffer != sourceBuffers.meshletBoundsBuffer
        || mesh.meshletPositionRefDeltaBuffer != sourceBuffers.meshletPositionRefDeltaBuffer
        || mesh.meshletAttributeRefDeltaBuffer != sourceBuffers.meshletAttributeRefDeltaBuffer
        || mesh.meshletLocalVertexRefBuffer != sourceBuffers.meshletLocalVertexRefBuffer
        || mesh.meshletPrimitiveIndexBuffer != sourceBuffers.meshletPrimitiveIndexBuffer
    )
        return false;
    cache.acceptedContent = !runtimeMesh || expected.sourceRevision != 0u;
    cache.acceptedContentRevision = expected.sourceRevision;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMeshSystem::prepareObjectGeometryCache(MeshResources& mesh, const Core::ComputePipelineHandle& decoderPipeline){
    if(!decoderPipeline)
        return false;
    ECSRenderDetail::ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    if(cache.decoderPipeline != decoderPipeline){
        cache.decoderPipeline = decoderPipeline;
        cache.acceptedContent = false;
    }
    if(cache.buffer && cache.heapHandle.valid())
        return true;
    if(!cache.buffer){
        u64 byteSize = 0u;
        if(!mesh.meshletLocalVertexRefBuffer)
            return false;
        if(!ECSRenderDetail::ResolveObjectGeometryCacheByteSize(
            mesh.meshletLocalVertexRefBuffer->getDescription().byteSize,
            byteSize
        ))
            return false;
        const Name bufferName = DeriveName(mesh.meshName, AStringView(":object_geometry"));
        if(!bufferName)
            return false;
        Core::BufferDesc desc;
        desc
            .setByteSize(byteSize)
            .setStructStride(NWB_MESH_OBJECT_VERTEX_BYTE_SIZE)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(bufferName)
        ;
        cache.buffer = m_graphics.createBuffer(desc);
        if(!cache.buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create object geometry cache for mesh '{}'"), StringConvert(mesh.meshName.c_str()));
            return false;
        }
        cache.acceptedContent = false;
        cache.initialized = false;
    }
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle.valid() || !heap.write(handle, Core::DescriptorWriteItem::StructuredBuffer_UAV(0u, cache.buffer.get()))){
        if(handle.valid())
            heap.free(handle);
        return false;
    }
    cache.heapHandle = handle;
    return true;
}

ECSRenderDetail::ObjectGeometryCacheSnapshot RendererMeshSystem::objectGeometryCacheSnapshot(const MeshResources& mesh){
    const ECSRenderDetail::ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    return {
        .buffer = cache.buffer,
        .decoderPipeline = cache.decoderPipeline,
        .heapHandle = cache.heapHandle,
        .sourceRevision = mesh.runtimeGeometryContentRevision,
        .initialized = cache.initialized,
        .requiresDecode = !cache.acceptedContent || (mesh.runtimeMesh
            && (mesh.runtimeGeometryContentRevision == 0u || cache.acceptedContentRevision != mesh.runtimeGeometryContentRevision)),
    };
}

bool RendererMeshSystem::confirmObjectGeometryCache(
    const Name& meshKey,
    const RuntimeMeshBuffers& sourceBuffers,
    const ECSRenderDetail::ObjectGeometryCacheSnapshot& expected,
    const bool runtimeMesh
){
    MeshResources* mesh = nullptr;
    if(!findMeshResources(meshKey, mesh))
        return false;
    return ECSRenderDetail::AcceptObjectGeometryCacheWrite(*mesh, sourceBuffers, expected, runtimeMesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

