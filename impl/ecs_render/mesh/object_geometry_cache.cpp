// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "object_geometry_cache.h"

#include "mesh_system.h"

#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
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


b8 AcceptObjectGeometryCacheWrite(MeshResources& mesh, const RuntimeMeshBuffers& sourceBuffers, const ObjectGeometryCacheSnapshot& expected, const b8 runtimeMesh)noexcept{
    ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    if(!expected.valid() || cache.buffer != expected.buffer)
        return false;
    // Accepted writes establish the retained buffer state even if current source content already moved on.
    return TryAcceptObjectGeometryCacheContent(
        cache.decoderPipeline == expected.decoderPipeline,
        cache.heapHandle == expected.heapHandle,
        cache.indexByteOffset,
        expected.indexByteOffset,
        cache.indexCount,
        expected.indexCount,
        mesh.runtimeMesh,
        runtimeMesh,
        mesh.runtimeGeometryContentRevision,
        expected.sourceRevision,
        mesh.sameBuffers(sourceBuffers),
        cache.initialized,
        cache.acceptedContent,
        cache.acceptedContentRevision
    );
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
        .requiresDecode = RequiresObjectGeometryCacheDecode(cache.acceptedContent, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, cache.acceptedContentRevision),
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
        const Expected<ObjectGeometryCacheLayout> layoutResult = TryResolveObjectGeometryCacheLayout(mesh.meshletLocalVertexRefBuffer->getDescription().byteSize, mesh.meshletPrimitiveIndexCount);
        if(!layoutResult)
            return false;
        const ObjectGeometryCacheLayout& layout = *layoutResult;
        const auto bufferName = DeriveName(mesh.meshName, AStringView(":object_geometry"));
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
        cache.indexByteOffset = layout.indexByteOffset;
        cache.indexCount = layout.indexCount;
        cache.acceptedContent = false;
        cache.initialized = false;
    }
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    return RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferUav(0u, cache.buffer.get()), cache.heapHandle);
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

