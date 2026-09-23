// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/mesh_acceleration_update.h>
#include <impl/ecs_csg/components.h>
#include <global/algorithm.h>
#include <global/hash_utils.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareMeshBlasResources(
    ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources
){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getCreationDescription();
    if(positionDesc.structStride == 0u || meshResources.meshletPrimitiveIndexCount == 0u)
        return false;

    if(meshResources.blas)
        return true;

    const u32 vertexStride = static_cast<u32>(positionDesc.structStride);
    const u32 vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);
    Core::RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(meshResources.positionBuffer.get())
        .setVertexFormat(Core::Format::RGB32_FLOAT)
        .setVertexStride(vertexStride)
        .setVertexCount(vertexCount)
        .setIndexBuffer(meshResources.triangleIndexBuffer.get())
        .setIndexFormat(Core::Format::R32_UINT)
        .setIndexCount(meshResources.meshletPrimitiveIndexCount)
    ;
    Core::RayTracingGeometryDesc geometry;
    geometry
        .setTriangles(triangles)
        .setFlags(Core::RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
    ;
    Core::RayTracingAccelStructBuildFlags::Mask buildFlags = Core::RayTracingAccelStructBuildFlags::PreferFastTrace;
    if(meshResources.runtimeMesh)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::AllowUpdate;
    Core::RayTracingAccelStructDesc accelStructDesc(m_arena);
    accelStructDesc.addBottomLevelGeometry(geometry);
    accelStructDesc.setBuildFlags(buildFlags);
    accelStructDesc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    accelStructDesc.setDebugName(DeriveName(meshResources.meshName, AStringView(":blas")));
    Core::RayTracingAccelStructHandle blas = m_graphics.getDevice().createAccelStruct(accelStructDesc);
    if(!blas){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BLAS for mesh '{}'"), StringConvert(meshResources.meshName.c_str()));
        return false;
    }
    meshResources.blas = Move(blas);
    meshResources.blasBuildPending = true;
    meshResources.blasBuildAccepted = false;
    meshResources.blasGeometryContentRevision = 0u;
    meshResources.blasBackingFresh = true;
    meshResources.blasBackingStateHandoffPending = false;
    meshResources.blasRefitsSinceRebuild = 0u;
    return true;
}

bool RendererRayTracingSystem::buildMeshBlas(
    Core::CommandList& commandList,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources
){
    PreparedMeshBlasBuild build;
    if(
        !__hidden_rt_swbvh::ResolvePreparedMeshBlasBuild(meshResources, build)
        || !__hidden_rt_swbvh::RecordPreparedMeshBlasBuild(commandList, build, false, false)
    )
        return false;

    if(meshResources.blasBackingFresh)
        meshResources.blasBackingStateHandoffPending = true;
    meshResources.blasRefitsSinceRebuild = build.refitsAfterBuild;
    if(build.firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built BLAS for mesh '{}' (runtime {}, {} vertices, {} indices)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(build.vertexCount)
            , static_cast<u64>(build.indexCount)
        );
    }
    return true;
}

void RendererRayTracingSystem::releaseSwBvhScratchHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortKeysHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortPayloadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhVisitCounterHeapHandle);
        return;
    }
    m_rayTracingState.m_bvhSortKeysHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_bvhSortPayloadHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_bvhVisitCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

