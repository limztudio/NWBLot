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

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ObjectGeometryCacheSnapshot::valid()const noexcept{
    return
        buffer && decoderPipeline && heapHandle.valid()
        && heapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && indexByteOffset != 0u && indexCount != 0u
    ;
}

bool ResolveObjectGeometryCacheLayout(
    const u64 localVertexRefByteSize,
    const u32 primitiveIndexCount,
    ObjectGeometryCacheLayout& outLayout)noexcept{
    outLayout = {};
    if(localVertexRefByteSize == 0u || localVertexRefByteSize % sizeof(MeshletLocalVertexRef) != 0u || primitiveIndexCount == 0u)
        return false;
    constexpr u64 s_Stride = NWB_MESH_OBJECT_VERTEX_BYTE_SIZE;
    constexpr u64 s_AddressableBytes = static_cast<u64>(Limit<u32>::s_Max) + 1u;
    const u64 vertexCount = localVertexRefByteSize / sizeof(MeshletLocalVertexRef) + NWB_MESH_OBJECT_FIRST_VERTEX_INDEX;
    if(vertexCount > static_cast<u64>(Limit<u32>::s_Max) / s_Stride)
        return false;
    const u64 indexByteOffset = vertexCount * s_Stride;
    const u64 indexedByteSize = indexByteOffset + static_cast<u64>(primitiveIndexCount) * sizeof(u32);
    if(indexedByteSize > s_AddressableBytes)
        return false;
    // The structured vertex view includes the allocation; raw index stores address the disjoint trailing region.
    const u64 bufferByteSize = AlignUp(indexedByteSize, s_Stride);
    if(bufferByteSize > s_AddressableBytes)
        return false;
    outLayout = { bufferByteSize, static_cast<u32>(indexByteOffset), primitiveIndexCount };
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
        || cache.indexByteOffset != expected.indexByteOffset
        || cache.indexCount != expected.indexCount
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
        ECSRenderDetail::ObjectGeometryCacheLayout layout;
        if(!mesh.meshletLocalVertexRefBuffer)
            return false;
        if(!ECSRenderDetail::ResolveObjectGeometryCacheLayout(
            mesh.meshletLocalVertexRefBuffer->getDescription().byteSize,
            mesh.meshletPrimitiveIndexCount,
            layout
        ))
            return false;
        const Name bufferName = DeriveName(mesh.meshName, AStringView(":object_geometry"));
        if(!bufferName)
            return false;
        Core::BufferDesc desc;
        desc
            .setByteSize(layout.bufferByteSize)
            .setStructStride(NWB_MESH_OBJECT_VERTEX_BYTE_SIZE)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(bufferName)
        ;
        cache.buffer = m_graphics.createBuffer(desc);
        if(!cache.buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create object geometry cache for mesh '{}'"), StringConvert(mesh.meshName.c_str()));
            return false;
        }
        cache.indexByteOffset = layout.indexByteOffset;
        cache.indexCount = layout.indexCount;
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
        .indexByteOffset = cache.indexByteOffset,
        .indexCount = cache.indexCount,
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

