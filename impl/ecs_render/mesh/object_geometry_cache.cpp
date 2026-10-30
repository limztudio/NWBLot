// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "object_geometry_cache.h"

#include "mesh_system.h"

#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/assets_mesh/payload_types.adhd.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>

#include <core/common/log.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ObjectGeometryCacheSnapshot::valid()const noexcept{
    return buffer && decoderPipeline && heapHandle.valid() && heapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer && indexByteOffset != 0u && indexCount != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AcceptObjectGeometryCacheWrite(MeshResources& mesh, const RuntimeMeshBuffers& sourceBuffers, const ObjectGeometryCacheSnapshot& expected, const b8 runtimeMesh)noexcept{
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


ECSRenderDetail::ObjectGeometryCacheSnapshot RendererMeshSystem::ObjectGeometryCacheSnapshot(const MeshResources& mesh)noexcept{
    const ECSRenderDetail::ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    return {
        .buffer = cache.buffer,
        .decoderPipeline = cache.decoderPipeline,
        .sourceRevision = mesh.runtimeGeometryContentRevision,
        .heapHandle = cache.heapHandle,
        .indexByteOffset = cache.indexByteOffset,
        .indexCount = cache.indexCount,
        .initialized = cache.initialized,
        .requiresDecode = !cache.acceptedContent || (mesh.runtimeMesh && (mesh.runtimeGeometryContentRevision == 0u || cache.acceptedContentRevision != mesh.runtimeGeometryContentRevision)),
    };
}

b8 RendererMeshSystem::prepareObjectGeometryCache(MeshResources& mesh, const Core::ComputePipelineHandle& decoderPipeline){
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
        if(!mesh.meshletLocalVertexRefBuffer)
            return false;
        const auto layout = ECSRenderDetail::ResolveObjectGeometryCacheLayout(mesh.meshletLocalVertexRefBuffer->getDescription().byteSize, mesh.meshletPrimitiveIndexCount);
        if(!layout)
            return false;
        const auto bufferName = DeriveName(mesh.meshName, AStringView(":object_geometry"));
        if(!bufferName)
            return false;
        Core::BufferDesc desc;
        desc
            .setByteSize(layout->bufferByteSize)
            .setStructStride(NWB_MESH_OBJECT_VERTEX_BYTE_SIZE)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(*bufferName)
        ;
        auto bufferResult = m_graphics.createBuffer(desc);
        if(bufferResult)
            cache.buffer = Move(*bufferResult);
        else
            cache.buffer.reset();
        if(!bufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create object geometry cache for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        cache.indexByteOffset = layout->indexByteOffset;
        cache.indexCount = layout->indexCount;
        cache.acceptedContent = false;
        cache.initialized = false;
    }
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    const Expected<Core::GpuDescriptorHandle> handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle)
        return false;
    Core::GpuDescriptorRetirementGuard retireHandle(heap, *handle);
    if(!heap.write(*handle, Core::DescriptorWriteItem::StructuredBufferUav(0u, cache.buffer.get())))
        return false;
    cache.heapHandle = *handle;
    retireHandle.release();
    return true;
}

b8 RendererMeshSystem::confirmObjectGeometryCache(const Name& meshKey, const RuntimeMeshBuffers& sourceBuffers, const ECSRenderDetail::ObjectGeometryCacheSnapshot& expected, const b8 runtimeMesh)noexcept{
    const auto mesh = findMeshResources(meshKey);
    if(!mesh)
        return false;
    return ECSRenderDetail::AcceptObjectGeometryCacheWrite(**mesh, sourceBuffers, expected, runtimeMesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
