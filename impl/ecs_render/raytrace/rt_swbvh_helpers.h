// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/mesh_acceleration_update.h>
#include <impl/ecs_csg/components.h>

#include <global/algorithm.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_swbvh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshBufferSlotLookup = HashMap<
    const Core::Buffer*,
    u32,
    Hasher<const Core::Buffer*>,
    EqualTo<const Core::Buffer*>,
    Core::Alloc::ScratchArena
>;


// Hash semantic scene inputs, not padded object representations.
inline constexpr u32 s_SceneStaticCacheHashVersion = 2u;

inline void AppendTlasInstanceStaticCacheInput(u64& inOutHash, const Core::RayTracingInstanceDesc& instance){
    Fnv64AppendValue(inOutHash, instance.transform);
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceID));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceMask));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceContributionToHitGroupIndex));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.flags));
    const usize bottomLevelAsIdentity = reinterpret_cast<usize>(instance.bottomLevelAS);
    Fnv64AppendValue(inOutHash, bottomLevelAsIdentity);
}

[[nodiscard]] inline u64 ComputeTlasStaticSceneHash(
    const Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena>& instances
){
    u64 hash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(hash, s_SceneStaticCacheHashVersion);
    Fnv64AppendValue(hash, instances.size());
    for(const Core::RayTracingInstanceDesc& instance : instances)
        AppendTlasInstanceStaticCacheInput(hash, instance);
    return hash;
}

[[nodiscard]] inline u64 ComputeSceneSwBvhStaticSceneHash(
    const Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena>& instances,
    const Vector<SceneBvhPrimitiveCalculation, Core::Alloc::ScratchArena>& primitives
){
    NWB_ASSERT(instances.size() == primitives.size());

    u64 hash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(hash, s_SceneStaticCacheHashVersion);
    Fnv64AppendValue(hash, instances.size());
    for(usize index = 0u; index < instances.size(); ++index){
        const SceneSwBvhInstanceGpu& instance = instances[index];
        Float4 aabbMin{};
        Float4 aabbMax{};
        StoreFloat(VectorSetW(primitives[index].aabbMin, 0.0f), aabbMin);
        StoreFloat(VectorSetW(primitives[index].aabbMax, 0.0f), aabbMax);

        Fnv64AppendValue(hash, instance.worldToObject);
        Fnv64AppendValue(hash, instance.primitiveCount);
        Fnv64AppendValue(hash, aabbMin);
        Fnv64AppendValue(hash, aabbMax);
        Fnv64AppendValue(hash, primitives[index].transparentOccluder);
    }
    return hash;
}

[[nodiscard]] inline u64 ComputeShadowMaterialContextHash(
    const Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena>& instanceMaterials,
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    NWB_ASSERT(instanceMaterials.size() == instanceData.size());

    u64 hash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(hash, s_SceneStaticCacheHashVersion);
    Fnv64AppendBuffer(
        hash,
        reinterpret_cast<const u8*>(instanceMaterials.data()),
        instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu)
    );
    Fnv64AppendBuffer(
        hash,
        reinterpret_cast<const u8*>(instanceData.data()),
        instanceData.size() * sizeof(InstanceGpuData)
    );
    Fnv64AppendBuffer(hash, materialTypedBytes.data(), materialTypedBytes.size());
    return hash;
}


// Cross-frame cache pins raw keys; failed registrations retry next gather.
[[nodiscard]] inline bool AcquireMeshHeapHandle(
    Core::GpuDescriptorHeap& heap,
    RtMeshHeapHandleCache& cache,
    const Core::BufferHandle& bufferHandle,
    Core::GpuDescriptorHandle& outHandle
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    if(!bufferHandle)
        return false;

    Core::Buffer& buffer = *bufferHandle;
    const Core::Buffer* const bufferKey = &buffer;
    auto found = cache.find(bufferKey);
    if(found != cache.end()){
        NWB_ASSERT(found.value().handle.valid());
        found.value().seenThisFrame = true;
        outHandle = found.value().handle;
        return true;
    }

    if(!RayTracingDetail::RegisterHeapBuffer(
        heap,
        buffer,
        Core::GpuDescriptorClass::StorageBuffer,
        false,
        outHandle
    ))
        return false;

    RtMeshHeapHandleCacheEntry entry;
    entry.keepAlive = bufferHandle;
    entry.handle = outHandle;
    entry.seenThisFrame = true;
    cache.insert({bufferKey, Move(entry)});
    return true;
}

// Evict unseen cache entries; heap quarantine protects in-flight work.
inline void SweepUnseenMeshHeapHandles(
    Core::GpuDescriptorHeap& heap,
    RtMeshHeapHandleCache& cache
){
    for(auto it = cache.begin(); it != cache.end(); ){
        if(it.value().seenThisFrame){
            it.value().seenThisFrame = false;
            ++it;
        }
        else{
            heap.free(it.value().handle);
            it = cache.erase(it);
        }
    }
}

// Mark entries unseen before gathering.
inline void BeginMeshHeapHandleGather(
    RtMeshHeapHandleCache& cache
){
    for(auto it = cache.begin(); it != cache.end(); ++it)
        it.value().seenThisFrame = false;
}

[[nodiscard]] inline bool IsStorageBufferHeapHandle(const Core::GpuDescriptorHandle handle){
    return RayTracingDetail::IsHeapHandle(handle, Core::GpuDescriptorClass::StorageBuffer);
}

// Recording is not allowed to register a new descriptor after the shared graph has frozen resource identities.
[[nodiscard]] inline bool FindPreparedMeshHeapHandle(
    RtMeshHeapHandleCache& cache,
    const Core::BufferHandle& bufferHandle,
    Core::GpuDescriptorHandle& outHandle
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    if(!bufferHandle)
        return false;

    auto found = cache.find(bufferHandle.get());
    if(found == cache.end() || !IsStorageBufferHeapHandle(found.value().handle))
        return false;

    found.value().seenThisFrame = true;
    outHandle = found.value().handle;
    return true;
}

[[nodiscard]] inline bool IsAccelStructHeapHandle(const Core::GpuDescriptorHandle handle){
    return RayTracingDetail::IsHeapHandle(handle, Core::GpuDescriptorClass::AccelStruct);
}

template<typename RayTracingState>
[[nodiscard]] inline bool HasPreparedShadowMaterialContextBuffers(
    const RayTracingState& state,
    const usize materialCount,
    const usize instanceCount,
    const usize materialTypedUploadBytes
){
    const usize requiredMaterialTypedBytes = AlignUp(
        Max<usize>(materialTypedUploadBytes, sizeof(u32)),
        sizeof(u32)
    );
    return
        state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceMaterialCapacity >= materialCount
        && IsStorageBufferHeapHandle(state.m_shadowInstanceMaterialHeapHandle)
        && state.m_shadowInstanceBuffer
        && state.m_shadowInstanceCapacity >= instanceCount
        && IsStorageBufferHeapHandle(state.m_shadowInstanceHeapHandle)
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowMaterialTypedCapacity >= requiredMaterialTypedBytes
        && IsStorageBufferHeapHandle(state.m_shadowMaterialTypedHeapHandle)
    ;
}

template<typename RayTracingState>
[[nodiscard]] inline bool HasPreparedSceneBvhBuffers(
    const RayTracingState& state,
    const u32 instanceCount
){
    NWB_ASSERT(instanceCount > 0u);
    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    return
        state.m_sceneBvhNodeBuffer
        && state.m_sceneInstanceBuffer
        && state.m_sceneBvhNodeCapacity >= requiredNodeCount
        && state.m_sceneInstanceCapacity >= instanceCount
        && IsStorageBufferHeapHandle(state.m_sceneBvhNodeHeapHandle)
        && IsStorageBufferHeapHandle(state.m_sceneInstanceHeapHandle)
    ;
}

// Register writable scratch with its explicit owner.
[[nodiscard]] inline bool RegisterWritableBvhBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::Buffer& buffer,
    Core::GpuDescriptorHandle& outHandle
){
    return RayTracingDetail::RegisterHeapBuffer(
        heap,
        buffer,
        Core::GpuDescriptorClass::StorageBuffer,
        true,
        outHandle
    );
}

[[nodiscard]] inline bool ResolvePreparedMeshBlasBuild(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources,
    PreparedMeshBlasBuild& outBuild
){
    outBuild = {};
    if(
        !meshResources.meshName
        || !meshResources.positionBuffer
        || !meshResources.triangleIndexBuffer
        || !meshResources.blas
        || !meshResources.blas->getBackingBufferHandle()
    )
        return false;

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getCreationDescription();
    if(positionDesc.structStride == 0u || meshResources.meshletPrimitiveIndexCount == 0u)
        return false;

    const bool firstBuild = meshResources.blasBuildPending || !meshResources.blasBuildAccepted;
    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.blasRefitsSinceRebuild < adaptiveRefitsBeforeRebuild(meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount)
    ;
    outBuild.meshName = meshResources.meshName;
    outBuild.positionBuffer = meshResources.positionBuffer;
    outBuild.triangleIndexBuffer = meshResources.triangleIndexBuffer;
    outBuild.blas = meshResources.blas;
    outBuild.blasBackingBuffer = meshResources.blas->getBackingBufferHandle();
    outBuild.runtimeMeshVersion = meshResources.runtimeMeshVersion;
    outBuild.geometryContentRevision = meshResources.runtimeGeometryContentRevision;
    outBuild.acceptedGeometryContentRevision = meshResources.blasGeometryContentRevision;
    outBuild.positionByteSize = positionDesc.byteSize;
    outBuild.vertexStride = static_cast<u32>(positionDesc.structStride);
    outBuild.vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);
    outBuild.indexCount = meshResources.meshletPrimitiveIndexCount;
    outBuild.refitsBeforeBuild = meshResources.blasRefitsSinceRebuild;
    outBuild.refitsAfterBuild = performRefit ? (meshResources.blasRefitsSinceRebuild + 1u) : 0u;
    outBuild.runtimeMesh = meshResources.runtimeMesh;
    outBuild.buildPending = meshResources.blasBuildPending;
    outBuild.firstBuild = firstBuild;
    outBuild.backingFresh = meshResources.blasBackingFresh;
    outBuild.performRefit = performRefit;
    return true;
}

[[nodiscard]] inline bool MatchesPreparedMeshBlasBuild(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources,
    const PreparedMeshBlasBuild& build
){
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
    return
        positionDesc.structStride == build.vertexStride
        && positionDesc.byteSize == build.positionByteSize
        && build.vertexStride != 0u
        && build.vertexCount == positionDesc.byteSize / build.vertexStride
    ;
}

[[nodiscard]] inline bool RecordPreparedMeshBlasBuild(
    Core::CommandList& commandList,
    const PreparedMeshBlasBuild& build,
    const bool meshBlasAccelStructStatesGraphOwned,
    const bool meshBlasGeometryBuildInputStatesGraphOwned
){
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
    geometry
        .setTriangles(triangles)
        .setFlags(Core::RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
    ;
    Core::RayTracingAccelStructBuildFlags::Mask buildFlags = Core::RayTracingAccelStructBuildFlags::PreferFastTrace;
    if(build.runtimeMesh)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::AllowUpdate;
    if(build.performRefit)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::PerformUpdate;

    // Direct and retry callbacks retain the native input-state bridge. Frozen graph routes establish this state in their packet prologue.
    if(!meshBlasGeometryBuildInputStatesGraphOwned){
        commandList.setBufferState(build.positionBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
        commandList.setBufferState(build.triangleIndexBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    }
    if(!meshBlasAccelStructStatesGraphOwned)
        commandList.setAccelStructState(build.blas.get(), Core::ResourceStates::AccelStructWrite);
    commandList.commitBarriers();
    commandList.buildBottomLevelAccelStruct(build.blas.get(), &geometry, 1u, buildFlags);
    if(!meshBlasAccelStructStatesGraphOwned){
        commandList.setAccelStructState(build.blas.get(), Core::ResourceStates::AccelStructRead);
        commandList.commitBarriers();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

