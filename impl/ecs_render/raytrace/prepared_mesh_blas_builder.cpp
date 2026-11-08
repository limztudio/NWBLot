// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_mesh_blas_builder.h"

#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


Expected<PreparedMeshBlasBuild> PreparedMeshBlasBuilder::Resolve(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources)noexcept{
    PreparedMeshBlasBuild build;
    if(!meshResources.meshName || !meshResources.positionBuffer || !meshResources.triangleIndexBuffer || !meshResources.blas || !meshResources.blas->getBackingBufferHandle())
        return MakeUnexpected(Failure{});

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getCreationDescription();
    if(positionDesc.structStride == 0u || meshResources.meshletPrimitiveIndexCount == 0u)
        return MakeUnexpected(Failure{});

    const b8 firstBuild = meshResources.blasBuildPending || !meshResources.blasBuildAccepted;
    const b8 performRefit = meshResources.runtimeMesh && !firstBuild && meshResources.blasRefitsSinceRebuild < AdaptiveRefitsBeforeRebuild(meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount);
    build.meshName = meshResources.meshName;
    build.positionBuffer = meshResources.positionBuffer;
    build.triangleIndexBuffer = meshResources.triangleIndexBuffer;
    build.blas = meshResources.blas;
    build.blasBackingBuffer = meshResources.blas->getBackingBufferHandle();
    build.runtimeMeshVersion = meshResources.runtimeMeshVersion;
    build.geometryContentRevision = meshResources.runtimeGeometryContentRevision;
    build.acceptedGeometryContentRevision = meshResources.blasGeometryContentRevision;
    build.positionByteSize = positionDesc.byteSize;
    build.vertexStride = static_cast<u32>(positionDesc.structStride);
    build.vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);
    build.indexCount = meshResources.meshletPrimitiveIndexCount;
    build.refitsBeforeBuild = meshResources.blasRefitsSinceRebuild;
    build.refitsAfterBuild = performRefit ? (meshResources.blasRefitsSinceRebuild + 1u) : 0u;
    build.runtimeMesh = meshResources.runtimeMesh;
    build.buildPending = meshResources.blasBuildPending;
    build.firstBuild = firstBuild;
    build.backingFresh = meshResources.blasBackingFresh;
    build.performRefit = performRefit;
    return build;
}

b8 PreparedMeshBlasBuilder::Matches(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources, const PreparedMeshBlasBuild& build)noexcept{
    if(
        meshResources.meshName != build.meshName
        || meshResources.runtimeMesh != build.runtimeMesh
        || meshResources.runtimeMeshVersion != build.runtimeMeshVersion
        || meshResources.runtimeGeometryContentRevision != build.geometryContentRevision
        || meshResources.blasGeometryContentRevision != build.acceptedGeometryContentRevision
        || meshResources.positionBuffer.get() != build.positionBuffer.get()
        || meshResources.triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
        || meshResources.blas.get() != build.blas.get()
        || !meshResources.blas
        || meshResources.blas->getBackingBufferHandle().get() != build.blasBackingBuffer.get()
        || meshResources.meshletPrimitiveIndexCount != build.indexCount
        || meshResources.blasBuildPending != build.buildPending
        || (meshResources.blasBuildPending || !meshResources.blasBuildAccepted) != build.firstBuild
        || meshResources.blasBackingFresh != build.backingFresh
        || meshResources.blasRefitsSinceRebuild != build.refitsBeforeBuild
    )
        return false;

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getCreationDescription();
    return positionDesc.structStride == build.vertexStride && positionDesc.byteSize == build.positionByteSize && build.vertexStride != 0u && build.vertexCount == positionDesc.byteSize / build.vertexStride;
}

b8 PreparedMeshBlasBuilder::Record(const PreparedMeshBlasBuild& build, const b8 meshBlasGeometryBuildInputStatesGraphOwned, Core::CommandList& commandList){
    if(
        !build.positionBuffer
        || !build.triangleIndexBuffer
        || !build.blas
        || !build.blasBackingBuffer
        || build.vertexStride == 0u
        || build.vertexCount == 0u
        || build.indexCount == 0u
        || build.blas->getBackingBufferHandle().get() != build.blasBackingBuffer.get()
    )
        return false;

    Core::RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(build.positionBuffer.get())
        .setVertexFormat(Core::Format::RGB32_FLOAT)
        .setVertexStride(build.vertexStride)
        .setVertexCount(build.vertexCount)
        .setIndexBuffer(build.triangleIndexBuffer.get())
        .setIndexFormat(Core::Format::R32_UINT)
        .setIndexCount(build.indexCount)
    ;
    Core::RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles).setFlags(Core::RayTracingGeometryFlags::NoDuplicateAnyHitInvocation);
    Core::RayTracingAccelStructBuildFlags::Mask buildFlags = Core::RayTracingAccelStructBuildFlags::PreferFastTrace;
    if(build.runtimeMesh)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::AllowUpdate;
    if(build.performRefit)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::PerformUpdate;

    // Mixed geometry input ownership establishes these states locally; fully declared inputs use the packet prologue.
    if(!meshBlasGeometryBuildInputStatesGraphOwned){
        commandList.setBufferState(build.positionBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
        commandList.setBufferState(build.triangleIndexBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    }
    commandList.commitBarriers();
    commandList.buildBottomLevelAccelStruct(build.blas.get(), &geometry, 1u, buildFlags);
    return true;
}



NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
