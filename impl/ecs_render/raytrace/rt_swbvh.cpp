// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

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

void AppendTlasInstanceStaticCacheInput(u64& inOutHash, const Core::RayTracingInstanceDesc& instance){
    Fnv64AppendValue(inOutHash, instance.transform);
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceID));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceMask));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceContributionToHitGroupIndex));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.flags));
    const usize bottomLevelAsIdentity = reinterpret_cast<usize>(instance.bottomLevelAS);
    Fnv64AppendValue(inOutHash, bottomLevelAsIdentity);
}

[[nodiscard]] u64 ComputeTlasStaticSceneHash(
    const Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena>& instances
){
    u64 hash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(hash, s_SceneStaticCacheHashVersion);
    Fnv64AppendValue(hash, instances.size());
    for(const Core::RayTracingInstanceDesc& instance : instances)
        AppendTlasInstanceStaticCacheInput(hash, instance);
    return hash;
}

[[nodiscard]] u64 ComputeSceneSwBvhStaticSceneHash(
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
        StoreFloat(VectorSetW(primitives[index].aabbMin, 0.0f), &aabbMin);
        StoreFloat(VectorSetW(primitives[index].aabbMax, 0.0f), &aabbMax);

        Fnv64AppendValue(hash, instance.worldToObject);
        Fnv64AppendValue(hash, instance.primitiveCount);
        Fnv64AppendValue(hash, aabbMin);
        Fnv64AppendValue(hash, aabbMax);
        Fnv64AppendValue(hash, primitives[index].transparentOccluder);
    }
    return hash;
}

[[nodiscard]] u64 ComputeShadowMaterialContextHash(
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
[[nodiscard]] bool AcquireMeshHeapHandle(
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
void SweepUnseenMeshHeapHandles(
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
void BeginMeshHeapHandleGather(
    RtMeshHeapHandleCache& cache
){
    for(auto it = cache.begin(); it != cache.end(); ++it)
        it.value().seenThisFrame = false;
}

[[nodiscard]] bool IsStorageBufferHeapHandle(const Core::GpuDescriptorHandle handle){
    return RayTracingDetail::IsHeapHandle(handle, Core::GpuDescriptorClass::StorageBuffer);
}

// Recording is not allowed to register a new descriptor after the shared graph has frozen resource identities.
[[nodiscard]] bool FindPreparedMeshHeapHandle(
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

[[nodiscard]] bool IsAccelStructHeapHandle(const Core::GpuDescriptorHandle handle){
    return RayTracingDetail::IsHeapHandle(handle, Core::GpuDescriptorClass::AccelStruct);
}

template<typename RayTracingState>
[[nodiscard]] bool HasPreparedShadowMaterialContextBuffers(
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
[[nodiscard]] bool HasPreparedSceneBvhBuffers(
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

[[nodiscard]] bool UploadPreparedShadowMaterialContextBuffers(
    Core::CommandList& commandList,
    Core::Buffer& instanceBuffer,
    Core::Buffer& materialTypedBuffer,
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes,
    const usize materialTypedUploadBytes
){
    if(materialTypedUploadBytes == 0u || materialTypedUploadBytes > materialTypedBytes.size())
        return false;

    if(!instanceData.empty()){
        commandList.setBufferState(&instanceBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.writeBuffer(&instanceBuffer, instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
        commandList.setBufferState(&instanceBuffer, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    commandList.setBufferState(&materialTypedBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(&materialTypedBuffer, materialTypedBytes.data(), materialTypedUploadBytes);
    commandList.setBufferState(&materialTypedBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

// Register writable scratch with its explicit owner.
[[nodiscard]] bool RegisterWritableBvhBuffer(
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

[[nodiscard]] bool ResolvePreparedMeshBlasBuild(
    const MeshResources& meshResources,
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

    const bool firstBuild = meshResources.blasBuildPending;
    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.blasRefitsSinceRebuild
            < adaptiveRefitsBeforeRebuild(meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount)
    ;
    outBuild.meshName = meshResources.meshName;
    outBuild.positionBuffer = meshResources.positionBuffer;
    outBuild.triangleIndexBuffer = meshResources.triangleIndexBuffer;
    outBuild.blas = meshResources.blas;
    outBuild.blasBackingBuffer = meshResources.blas->getBackingBufferHandle();
    outBuild.runtimeMeshVersion = meshResources.runtimeMeshVersion;
    outBuild.positionByteSize = positionDesc.byteSize;
    outBuild.vertexStride = static_cast<u32>(positionDesc.structStride);
    outBuild.vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);
    outBuild.indexCount = meshResources.meshletPrimitiveIndexCount;
    outBuild.refitsBeforeBuild = meshResources.blasRefitsSinceRebuild;
    outBuild.refitsAfterBuild = performRefit ? (meshResources.blasRefitsSinceRebuild + 1u) : 0u;
    outBuild.runtimeMesh = meshResources.runtimeMesh;
    outBuild.firstBuild = firstBuild;
    outBuild.backingFresh = meshResources.blasBackingFresh;
    outBuild.performRefit = performRefit;
    return true;
}

[[nodiscard]] bool MatchesPreparedMeshBlasBuild(
    const MeshResources& meshResources,
    const PreparedMeshBlasBuild& build
){
    if(
        meshResources.meshName != build.meshName
        || meshResources.runtimeMesh != build.runtimeMesh
        || meshResources.runtimeMeshVersion != build.runtimeMeshVersion
        || meshResources.positionBuffer.get() != build.positionBuffer.get()
        || meshResources.triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
        || meshResources.blas.get() != build.blas.get()
        || !meshResources.blas
        || meshResources.blas->getBackingBufferHandle().get() != build.blasBackingBuffer.get()
        || meshResources.meshletPrimitiveIndexCount != build.indexCount
        || meshResources.blasBuildPending != build.firstBuild
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

[[nodiscard]] bool RecordPreparedMeshBlasBuild(
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

    // Direct/retry and incomplete hybrid callbacks immediately hand these shared streams to the software-BVH
    // builder, so they retain the native bridge. Verified frozen graph routes establish this input state in their
    // packet prologue instead.
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


bool RendererRayTracingSystem::buildPendingMeshBlas(Core::CommandList& commandList){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        if(meshResources.runtimeMesh){
            if(!buildMeshBlas(commandList, meshResources)){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh BLAS build failed"));
                return false;
            }
            else
                meshResources.blasBuildPending = false;
            continue;
        }

        if(!meshResources.blasBuildPending)
            continue;
        if(buildMeshBlas(commandList, meshResources))
            meshResources.blasBuildPending = false;
        else
            return false;
    }
    return true;
}

bool RendererRayTracingSystem::preparePendingMeshBlasResources(){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    bool allResourcesReady = true;
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();
        if(!meshResources.runtimeMesh && !meshResources.blasBuildPending)
            continue;
        if(!prepareMeshBlasResources(meshResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: BLAS resource preflight failed for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            allResourcesReady = false;
        }
    }
    return allResourcesReady;
}

void RendererRayTracingSystem::clearPreparedMeshBlasBuilds()noexcept{
    m_preparedMeshBlasBuilds.clear();
    m_preparedMeshBlasBuildsReady = false;
}

bool RendererRayTracingSystem::capturePreparedMeshBlasBuilds(){
    clearPreparedMeshBlasBuilds();
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        const MeshResources& meshResources = it.value();
        // Runtime geometry refits every hardware frame; static geometry enters only when the preflight marked it
        // dirty. This deliberately includes off-screen meshes, matching the established native traversal.
        if(!meshResources.runtimeMesh && !meshResources.blasBuildPending)
            continue;

        PreparedMeshBlasBuild build;
        if(!__hidden_rt_swbvh::ResolvePreparedMeshBlasBuild(meshResources, build)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze BLAS build for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            clearPreparedMeshBlasBuilds();
            return false;
        }
        m_preparedMeshBlasBuilds.push_back(Move(build));
    }
    m_preparedMeshBlasBuildsReady = !m_preparedMeshBlasBuilds.empty();
    return true;
}

bool RendererRayTracingSystem::recordPreparedMeshBlasBuilds(
    Core::CommandList& commandList,
    const bool meshBlasAccelStructStatesGraphOwned,
    const bool meshBlasGeometryBuildInputStatesGraphOwned
){
    if(!m_preparedMeshBlasBuildsReady || m_preparedMeshBlasBuilds.empty())
        return false;

    auto& meshes = meshState().m_meshes;
    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        const auto found = meshes.find(build.meshName);
        if(
            found == meshes.end()
            || !__hidden_rt_swbvh::MatchesPreparedMeshBlasBuild(found.value(), build)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build no longer matches mesh '{}'")
                , StringConvert(build.meshName.c_str())
            );
            return false;
        }
    }
    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        if(!__hidden_rt_swbvh::RecordPreparedMeshBlasBuild(
            commandList,
            build,
            meshBlasAccelStructStatesGraphOwned,
            meshBlasGeometryBuildInputStatesGraphOwned
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record frozen BLAS build for mesh '{}'")
                , StringConvert(build.meshName.c_str())
            );
            return false;
        }
    }
    return true;
}

bool RendererRayTracingSystem::preparedMeshBlasBuildsReady()const noexcept{
    return m_preparedMeshBlasBuildsReady;
}

const PreparedMeshBlasBuildVector& RendererRayTracingSystem::preparedMeshBlasBuilds()const noexcept{
    return m_preparedMeshBlasBuilds;
}

void RendererRayTracingSystem::confirmPreparedMeshBlasBuilds()noexcept{
    if(!m_preparedMeshBlasBuildsReady)
        return;

    bool allPlansCurrent = true;
    auto& meshes = meshState().m_meshes;
    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        const auto found = meshes.find(build.meshName);
        if(
            found == meshes.end()
            || !__hidden_rt_swbvh::MatchesPreparedMeshBlasBuild(found.value(), build)
        ){
            allPlansCurrent = false;
            continue;
        }

        MeshResources& meshResources = found.value();
        meshResources.blasBuildPending = false;
        // The accepted Shadow Preparation state handoff now owns this generation's native final state.
        meshResources.blasBackingFresh = false;
        meshResources.blasBackingStateHandoffPending = false;
        meshResources.blasRefitsSinceRebuild = build.refitsAfterBuild;
        if(build.firstBuild){
            NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built BLAS for mesh '{}' (runtime {}, {} vertices, {} indices)")
                , StringConvert(build.meshName.c_str())
                , build.runtimeMesh
                , static_cast<u64>(build.vertexCount)
                , static_cast<u64>(build.indexCount)
            );
        }
    }
    // An unexpected replacement after recording is not rolled into the accepted mesh cache. Force a future TLAS
    // rebuild rather than retaining a static-scene hash that may describe the retired generation.
    if(!allPlansCurrent)
        rayTracingState().m_tlasStaticSceneHashValid = false;
    clearPreparedMeshBlasBuilds();
}

bool RendererRayTracingSystem::buildPendingMeshSwBvh(Core::CommandList& commandList){
    // Record only prepared SW-BVH work; callers decide when software tracing is needed.
    bool allBuildsReady = true;
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        if(meshResources.runtimeMesh){
            if(!updateMeshSwBvh(commandList, meshResources)){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh '{}' software BVH update failed")
                    , StringConvert(meshResources.meshName.c_str())
                );
                allBuildsReady = false;
            }
            continue;
        }

        if(!meshResources.swBvhBuildPending)
            continue;
        if(updateMeshSwBvh(commandList, meshResources))
            meshResources.swBvhBuildPending = false;
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: static mesh '{}' software BVH build failed")
                , StringConvert(meshResources.meshName.c_str())
            );
            allBuildsReady = false;
        }
    }
    return allBuildsReady;
}

bool RendererRayTracingSystem::preparePendingMeshSwBvhResources(){
    // Prepare resources before recording; runtime meshes prepare every frame.
    bool allResourcesReady = true;
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();
        if(!meshResources.runtimeMesh && !meshResources.swBvhBuildPending)
            continue;

        if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer){
            allResourcesReady = false;
            continue;
        }
        if(meshResources.meshletPrimitiveIndexCount == 0u || (meshResources.meshletPrimitiveIndexCount % s_RayTracingTriangleIndexCount) != 0u){
            allResourcesReady = false;
            continue;
        }

        const u32 primitiveCount = meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;
        if(!m_renderer.meshSystem().ensureMeshSwBvhInputHeapHandles(meshResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH input heap registration failed for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            allResourcesReady = false;
            continue;
        }
        if(!ensureMeshSwBvhResources(
            primitiveCount,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhNodeHeapHandle,
            meshResources.swBvhParentHeapHandle
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH resource preparation failed for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            allResourcesReady = false;
        }
    }
    return allResourcesReady;
}

void RendererRayTracingSystem::clearPreparedMeshSwBvhBuilds()noexcept{
    m_preparedMeshSwBvhBuilds.clear();
    m_preparedMeshSwBvhBuildsReady = false;
}

bool RendererRayTracingSystem::capturePreparedMeshSwBvhBuilds(){
    clearPreparedMeshSwBvhBuilds();
    auto& state = rayTracingState();
    bool hasCandidate = false;
    for(auto it = meshState().m_meshes.begin(); it != meshState().m_meshes.end(); ++it){
        const MeshResources& mesh = it.value();
        if(mesh.runtimeMesh || mesh.swBvhBuildPending){
            hasCandidate = true;
            break;
        }
    }
    // A software-only frame can have no dirty/static or runtime mesh work. In that case the shared scratch has not
    // necessarily been allocated, and an authoritative empty plan must retain the established no-op path.
    if(!hasCandidate)
        return true;
    const auto sharedResourcesReady = [&]{
        return
            state.m_bvhSortKeysBuffer
            && state.m_bvhSortPayloadBuffer
            && state.m_bvhVisitCounterBuffer
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(state.m_bvhSortKeysHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(state.m_bvhSortPayloadHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(state.m_bvhVisitCounterHeapHandle)
        ;
    };
    if(!sharedResourcesReady())
        return false;

    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        const MeshResources& mesh = it.value();
        // Runtime meshes update every software-only frame; static meshes enter exactly while their topology is
        // pending. This mirrors the direct loop, including off-screen mesh resources.
        if(!mesh.runtimeMesh && !mesh.swBvhBuildPending)
            continue;
        if(
            !mesh.meshName
            || !mesh.positionBuffer
            || !mesh.triangleIndexBuffer
            || mesh.meshletPrimitiveIndexCount == 0u
            || (mesh.meshletPrimitiveIndexCount % s_RayTracingTriangleIndexCount) != 0u
            || !meshSwBvhResourcesReady(
                mesh.swBvhNodeBuffer,
                mesh.swBvhParentBuffer,
                mesh.swBvhNodeHeapHandle,
                mesh.swBvhParentHeapHandle
            )
            || !__hidden_rt_swbvh::IsStorageBufferHeapHandle(mesh.swBvhPositionHeapHandle)
            || !__hidden_rt_swbvh::IsStorageBufferHeapHandle(mesh.swBvhTriangleIndexHeapHandle)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software BVH build for mesh '{}'")
                , StringConvert(mesh.meshName.c_str())
            );
            clearPreparedMeshSwBvhBuilds();
            return false;
        }

        const u32 primitiveCount = mesh.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;
        const bool firstBuild = !mesh.swBvhTopologyBuilt;
        const bool performRefit =
            mesh.runtimeMesh
            && !firstBuild
            && mesh.swBvhRefitsSinceRebuild < adaptiveRefitsBeforeRebuild(primitiveCount)
        ;
        const Core::BufferDesc& positionDesc = mesh.positionBuffer->getCreationDescription();
        const Core::BufferDesc& indexDesc = mesh.triangleIndexBuffer->getCreationDescription();
        const Core::BufferDesc& nodeDesc = mesh.swBvhNodeBuffer->getCreationDescription();
        const Core::BufferDesc& parentDesc = mesh.swBvhParentBuffer->getCreationDescription();
        const Core::BufferDesc& keysDesc = state.m_bvhSortKeysBuffer->getCreationDescription();
        const Core::BufferDesc& payloadDesc = state.m_bvhSortPayloadBuffer->getCreationDescription();
        const Core::BufferDesc& counterDesc = state.m_bvhVisitCounterBuffer->getCreationDescription();
        m_preparedMeshSwBvhBuilds.push_back(PreparedMeshSwBvhBuild{
            .meshName = mesh.meshName,
            .positionBuffer = mesh.positionBuffer,
            .triangleIndexBuffer = mesh.triangleIndexBuffer,
            .nodeBuffer = mesh.swBvhNodeBuffer,
            .parentBuffer = mesh.swBvhParentBuffer,
            .sortKeysBuffer = state.m_bvhSortKeysBuffer,
            .sortPayloadBuffer = state.m_bvhSortPayloadBuffer,
            .visitCounterBuffer = state.m_bvhVisitCounterBuffer,
            .positionHeapHandle = mesh.swBvhPositionHeapHandle,
            .triangleIndexHeapHandle = mesh.swBvhTriangleIndexHeapHandle,
            .nodeHeapHandle = mesh.swBvhNodeHeapHandle,
            .parentHeapHandle = mesh.swBvhParentHeapHandle,
            .sortKeysHeapHandle = state.m_bvhSortKeysHeapHandle,
            .sortPayloadHeapHandle = state.m_bvhSortPayloadHeapHandle,
            .visitCounterHeapHandle = state.m_bvhVisitCounterHeapHandle,
            .aabbMin = mesh.csgLocalBounds.minBounds,
            .aabbMax = mesh.csgLocalBounds.maxBounds,
            .runtimeMeshVersion = mesh.runtimeMeshVersion,
            .positionByteSize = positionDesc.byteSize,
            .indexByteSize = indexDesc.byteSize,
            .nodeByteSize = nodeDesc.byteSize,
            .parentByteSize = parentDesc.byteSize,
            .sortKeysByteSize = keysDesc.byteSize,
            .sortPayloadByteSize = payloadDesc.byteSize,
            .visitCounterByteSize = counterDesc.byteSize,
            .primitiveCount = primitiveCount,
            .refitsBeforeBuild = mesh.swBvhRefitsSinceRebuild,
            .refitsAfterBuild = performRefit ? (mesh.swBvhRefitsSinceRebuild + 1u) : 0u,
            .runtimeMesh = mesh.runtimeMesh,
            .buildPending = mesh.swBvhBuildPending,
            .firstBuild = firstBuild,
            .performRefit = performRefit,
        });
    }
    m_preparedMeshSwBvhBuildsReady = !m_preparedMeshSwBvhBuilds.empty();
    return true;
}

bool RendererRayTracingSystem::preparedMeshSwBvhBuildMatchesCurrent(const PreparedMeshSwBvhBuild& build){
    const auto found = meshState().m_meshes.find(build.meshName);
    if(found == meshState().m_meshes.end())
        return false;

    const MeshResources& mesh = found.value();
    const auto& state = rayTracingState();
    if(
        mesh.meshName != build.meshName
        || mesh.runtimeMesh != build.runtimeMesh
        || mesh.runtimeMeshVersion != build.runtimeMeshVersion
        || mesh.positionBuffer.get() != build.positionBuffer.get()
        || mesh.triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
        || mesh.swBvhNodeBuffer.get() != build.nodeBuffer.get()
        || mesh.swBvhParentBuffer.get() != build.parentBuffer.get()
        || mesh.swBvhPositionHeapHandle != build.positionHeapHandle
        || mesh.swBvhTriangleIndexHeapHandle != build.triangleIndexHeapHandle
        || mesh.swBvhNodeHeapHandle != build.nodeHeapHandle
        || mesh.swBvhParentHeapHandle != build.parentHeapHandle
        || state.m_bvhSortKeysBuffer.get() != build.sortKeysBuffer.get()
        || state.m_bvhSortPayloadBuffer.get() != build.sortPayloadBuffer.get()
        || state.m_bvhVisitCounterBuffer.get() != build.visitCounterBuffer.get()
        || state.m_bvhSortKeysHeapHandle != build.sortKeysHeapHandle
        || state.m_bvhSortPayloadHeapHandle != build.sortPayloadHeapHandle
        || state.m_bvhVisitCounterHeapHandle != build.visitCounterHeapHandle
        || mesh.meshletPrimitiveIndexCount != build.primitiveCount * s_RayTracingTriangleIndexCount
        || mesh.swBvhRefitsSinceRebuild != build.refitsBeforeBuild
        || mesh.swBvhBuildPending != build.buildPending
        || (!mesh.swBvhTopologyBuilt) != build.firstBuild
        || mesh.csgLocalBounds.minBounds != build.aabbMin
        || mesh.csgLocalBounds.maxBounds != build.aabbMax
        || !meshSwBvhResourcesReady(
            mesh.swBvhNodeBuffer,
            mesh.swBvhParentBuffer,
            mesh.swBvhNodeHeapHandle,
            mesh.swBvhParentHeapHandle
        )
    )
        return false;
    return
        mesh.positionBuffer->getCreationDescription().byteSize == build.positionByteSize
        && mesh.triangleIndexBuffer->getCreationDescription().byteSize == build.indexByteSize
        && mesh.swBvhNodeBuffer->getCreationDescription().byteSize == build.nodeByteSize
        && mesh.swBvhParentBuffer->getCreationDescription().byteSize == build.parentByteSize
        && state.m_bvhSortKeysBuffer->getCreationDescription().byteSize == build.sortKeysByteSize
        && state.m_bvhSortPayloadBuffer->getCreationDescription().byteSize == build.sortPayloadByteSize
        && state.m_bvhVisitCounterBuffer->getCreationDescription().byteSize == build.visitCounterByteSize
    ;
}

bool RendererRayTracingSystem::recordPreparedMeshSwBvhBuild(
    Core::CommandList& commandList,
    const PreparedMeshSwBvhBuild& build,
    const bool meshSwBvhInputStatesGraphOwned,
    const bool sentinelClearsGraphOwned,
    const bool graphBoundaryStatesOwned
){
    if(!preparedMeshSwBvhBuildMatchesCurrent(build)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build no longer matches mesh '{}'")
            , StringConvert(build.meshName.c_str())
        );
        return false;
    }

    // Direct/retry and incomplete-plan callers retain the native AccelStructBuildInput -> ShaderResource bridge.
    if(!meshSwBvhInputStatesGraphOwned){
        commandList.setBufferState(build.positionBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(build.triangleIndexBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    Core::BufferHandle nodeBuffer = build.nodeBuffer;
    Core::BufferHandle parentBuffer = build.parentBuffer;
    const bool recorded = build.performRefit
        ? refitMeshSwBvhPrepared(
            commandList,
            build.positionHeapHandle.slot(),
            build.triangleIndexHeapHandle.slot(),
            build.primitiveCount,
            nodeBuffer,
            parentBuffer,
            build.nodeHeapHandle,
            build.parentHeapHandle,
            sentinelClearsGraphOwned,
            graphBoundaryStatesOwned
        )
        : buildMeshSwBvhPrepared(
            commandList,
            build.positionHeapHandle.slot(),
            build.triangleIndexHeapHandle.slot(),
            build.primitiveCount,
            LoadFloatInt(build.aabbMin),
            LoadFloatInt(build.aabbMax),
            nodeBuffer,
            parentBuffer,
            build.nodeHeapHandle,
            build.parentHeapHandle,
            sentinelClearsGraphOwned,
            graphBoundaryStatesOwned
        )
    ;
    if(!recorded){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record frozen software BVH build for mesh '{}'")
            , StringConvert(build.meshName.c_str())
        );
    }
    return recorded;
}

bool RendererRayTracingSystem::recordPreparedMeshSwBvhBuildAfterGraphClears(
    Core::CommandList& commandList,
    const PreparedMeshSwBvhBuild& build
){
    // The graph callback has exact SRV/UAV uses and a typed clear predecessor, so it owns both the entry and
    // successor state boundaries. The native recorder keeps only dispatch-internal UAV fences.
    return recordPreparedMeshSwBvhBuild(commandList, build, true, true, true);
}

bool RendererRayTracingSystem::recordPreparedMeshSwBvhBuilds(
    Core::CommandList& commandList,
    const bool meshSwBvhInputStatesGraphOwned
){
    if(!m_preparedMeshSwBvhBuildsReady || m_preparedMeshSwBvhBuilds.empty())
        return false;

    // Preserve the aggregate recorder's all-plan validation before it emits any direct commands. The graph-split
    // pure-software route instead rejects its one shared packet if a later individual snapshot no longer matches.
    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        if(!preparedMeshSwBvhBuildMatchesCurrent(build)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build no longer matches mesh '{}'")
                , StringConvert(build.meshName.c_str())
            );
            return false;
        }
    }

    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        if(!recordPreparedMeshSwBvhBuild(commandList, build, meshSwBvhInputStatesGraphOwned, false, false))
            return false;
    }
    return true;
}

bool RendererRayTracingSystem::preparedMeshSwBvhBuildsReady()const noexcept{
    return m_preparedMeshSwBvhBuildsReady;
}

const PreparedMeshSwBvhBuildVector& RendererRayTracingSystem::preparedMeshSwBvhBuilds()const noexcept{
    return m_preparedMeshSwBvhBuilds;
}

void RendererRayTracingSystem::confirmPreparedMeshSwBvhBuilds()noexcept{
    if(!m_preparedMeshSwBvhBuildsReady)
        return;

    bool allPlansCurrent = true;
    auto& meshes = meshState().m_meshes;
    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        const auto found = meshes.find(build.meshName);
        if(
            found == meshes.end()
            || found.value().runtimeMesh != build.runtimeMesh
            || found.value().runtimeMeshVersion != build.runtimeMeshVersion
            || found.value().positionBuffer.get() != build.positionBuffer.get()
            || found.value().triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
            || found.value().swBvhNodeBuffer.get() != build.nodeBuffer.get()
            || found.value().swBvhParentBuffer.get() != build.parentBuffer.get()
            || found.value().swBvhRefitsSinceRebuild != build.refitsBeforeBuild
            || found.value().swBvhBuildPending != build.buildPending
            || (!found.value().swBvhTopologyBuilt) != build.firstBuild
        ){
            allPlansCurrent = false;
            continue;
        }

        MeshResources& mesh = found.value();
        if(!mesh.runtimeMesh)
            mesh.swBvhBuildPending = false;
        if(!build.performRefit)
            mesh.swBvhTopologyBuilt = true;
        mesh.swBvhRefitsSinceRebuild = build.refitsAfterBuild;
        if(build.firstBuild){
            NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built software BVH for mesh '{}' (runtime {}, {} triangles)")
                , StringConvert(build.meshName.c_str())
                , build.runtimeMesh
                , static_cast<u64>(build.primitiveCount)
            );
        }
    }
    if(!allPlansCurrent)
        rayTracingState().m_sceneSwBvhStaticSceneHashValid = false;
    clearPreparedMeshSwBvhBuilds();
}

bool RendererRayTracingSystem::preparedMeshSwBvhBuildProducesTopology(const MeshResources& mesh)const noexcept{
    if(!m_preparedMeshSwBvhBuildsReady)
        return false;
    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        if(
            build.meshName == mesh.meshName
            && build.runtimeMesh == mesh.runtimeMesh
            && build.runtimeMeshVersion == mesh.runtimeMeshVersion
            && build.positionBuffer.get() == mesh.positionBuffer.get()
            && build.triangleIndexBuffer.get() == mesh.triangleIndexBuffer.get()
            && build.nodeBuffer.get() == mesh.swBvhNodeBuffer.get()
            && build.parentBuffer.get() == mesh.swBvhParentBuffer.get()
            && build.buildPending == mesh.swBvhBuildPending
            && build.refitsBeforeBuild == mesh.swBvhRefitsSinceRebuild
        )
            return !build.performRefit;
    }
    return false;
}

bool RendererRayTracingSystem::prepareSceneTlasResources(Core::Alloc::ScratchArena& scratchArena){
    return buildSceneTlasImpl(nullptr, scratchArena);
}

bool RendererRayTracingSystem::buildSceneTlas(
    Core::CommandList& commandList,
    Core::Alloc::ScratchArena& scratchArena,
    const bool shadowMaterialContextBatchGraphOwned
){
    return buildSceneTlasImpl(&commandList, scratchArena, shadowMaterialContextBatchGraphOwned);
}

bool RendererRayTracingSystem::buildSceneTlasImpl(
    Core::CommandList* const commandList,
    Core::Alloc::ScratchArena& scratchArena,
    const bool shadowMaterialContextBatchGraphOwned
){
    using namespace __hidden_rt_swbvh;

    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    // RayTracingInstanceDesc contains only a raw BLAS pointer. The opaque graph-owned TLAS build retains this
    // parallel handle stream until the accepting Shadow Preparation packet has submitted.
    Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena> instanceBlases{ scratchArena };
    // Kept parallel to instances for hardware InstanceID lookup.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // All-occluder trace context; draw buffers hold one transparency class.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(candidateCount);
    instanceBlases.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);
    MeshBufferSlotLookup meshSlotLookup(
        0,
        Hasher<const Core::Buffer*>(),
        EqualTo<const Core::Buffer*>(),
        scratchArena
    );
    meshSlotLookup.reserve(candidateCount);

    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware TLAS build requires the descriptor-buffer TLAS heap layout"));
        return false;
    }
    BeginMeshHeapHandleGather(rayTracingState().m_hwMeshHeapHandleCache);
    const auto resolveMeshHeapHandle = [&](const Core::BufferHandle& buffer, Core::GpuDescriptorHandle& outHandle){
        return commandList
            ? FindPreparedMeshHeapHandle(rayTracingState().m_hwMeshHeapHandleCache, buffer, outHandle)
            : AcquireMeshHeapHandle(heap, rayTracingState().m_hwMeshHeapHandleCache, buffer, outHandle)
        ;
    };
    rayTracingState().m_shadowMeshIndexBuffers.clear();
    rayTracingState().m_shadowMeshAttributeBuffers.clear();
    rayTracingState().m_shadowMeshPositionBuffers.clear();
    rayTracingState().m_shadowMeshIndexHandles.clear();
    rayTracingState().m_shadowMeshAttributeHandles.clear();
    rayTracingState().m_shadowMeshPositionHandles.clear();
    rayTracingState().m_shadowMeshCount = 0u;
    // Gates hybrid software transparent-shadow work.
    rayTracingState().m_sceneHasTransparentOccluder = false;
    bool staticScene = true;

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        MeshResources* mesh = nullptr;
        RenderableMeshDesc resolvedMesh;
        const bool meshReady = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystem,
            m_renderer.meshSystem(),
            entity,
            resolvedMesh,
            mesh
        );
        if(!meshReady || !mesh || !mesh->blas || !mesh->triangleIndexBuffer || !mesh->attributeBuffer || !mesh->positionBuffer)
            continue;
        // Runtime mesh updates disable static TLAS reuse.
        if(resolvedMesh.runtime || mesh->runtimeMesh)
            staticScene = false;

        // Reuse one table slot for instances sharing geometry.
        Core::Buffer* meshIndexBuffer = mesh->triangleIndexBuffer.get();
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshIndexBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            Core::GpuDescriptorHandle indexHandle;
            Core::GpuDescriptorHandle attributeHandle;
            Core::GpuDescriptorHandle positionHandle;
            if(
                !resolveMeshHeapHandle(mesh->triangleIndexBuffer, indexHandle)
                || !resolveMeshHeapHandle(mesh->attributeBuffer, attributeHandle)
                || !resolveMeshHeapHandle(mesh->positionBuffer, positionHandle)
            ){
                if(!commandList){
                    SweepUnseenMeshHeapHandles(heap, rayTracingState().m_hwMeshHeapHandleCache);
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register HW scene mesh buffers in the global descriptor heap"));
                }
                else
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: HW scene mesh descriptor was not prepared before recording"));
                return false;
            }

            meshSlot = rayTracingState().m_shadowMeshCount;
            rayTracingState().m_shadowMeshIndexBuffers.push_back(meshIndexBuffer);
            rayTracingState().m_shadowMeshAttributeBuffers.push_back(mesh->attributeBuffer.get());
            rayTracingState().m_shadowMeshPositionBuffers.push_back(mesh->positionBuffer.get());
            rayTracingState().m_shadowMeshIndexHandles.push_back(indexHandle);
            rayTracingState().m_shadowMeshAttributeHandles.push_back(attributeHandle);
            rayTracingState().m_shadowMeshPositionHandles.push_back(positionHandle);
            meshSlotLookup.emplace(meshIndexBuffer, meshSlot);
            ++rayTracingState().m_shadowMeshCount;
        }

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBLAS(mesh->blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(s_RayTracingAllInstanceMask);

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        if(transform){
            const SIMDMatrix instanceWorld = MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            );
            StoreFloat(instanceWorld, &instanceDesc.transform);
        }

        // Build material context in InstanceID order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo)){
            if(materialInfo->transparent)
                rayTracingState().m_sceneHasTransparentOccluder = true;
            // The trace surface dispatcher reads this material's Texture2D fields through non-uniform bindless
            // slots. Retain the exact resolved handles during preflight; recording must never discover a texture
            // after the shared graph fixed its immutable resource set.
            if(
                !commandList
                && materialInfo->shadowTransmittanceModelId != Limit<u32>::s_Max
                && !appendPreparedShadowTraceMaterialSampledTextures(*materialInfo, scratchArena)
            )
                return false;
            u32 materialConstantByteOffset = 0u;
            if(!m_renderer.materialSystem().appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transform,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = RayTracingDetail::ResolveInstanceShadowMaterial(*materialInfo, materialConstantByteOffset, meshInstanceIndex);
        }
        instanceMaterial.indexSlot = rayTracingState().m_shadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = rayTracingState().m_shadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = rayTracingState().m_shadowMeshPositionHandles[meshSlot].slot();

        // Opaque candidates terminate RayQuery; software handles transparent transmittance.
        if(!(materialInfo && materialInfo->transparent))
            instanceDesc.setFlags(Core::RayTracingInstanceFlags::ForceOpaque);

        instances.push_back(instanceDesc);
        instanceBlases.push_back(mesh->blas);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    if(rayTracingState().m_shadowMeshCount > rayTracingState().m_shadowMeshHeapHighWater){
        rayTracingState().m_shadowMeshHeapHighWater = rayTracingState().m_shadowMeshCount;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: HW-shadow heap registration high-water: {} distinct meshes -> {} handles")
            , static_cast<u64>(rayTracingState().m_shadowMeshCount)
            , static_cast<u64>(rayTracingState().m_shadowMeshCount) * s_HardwareRayTracingMeshBufferCount
        );
    }
    if(!commandList)
        SweepUnseenMeshHeapHandles(heap, rayTracingState().m_hwMeshHeapHandleCache);

    rayTracingState().m_tlasInstanceCount = static_cast<u32>(instances.size());
    if(instances.empty()){
        rayTracingState().m_tlasDeviceAddress = 0u;
        rayTracingState().m_tlasStaticSceneHashValid = false;
        rayTracingState().m_hwShadowMaterialContextHashValid = false;
        return false;
    }

    // TLAS and material context use separate static keys.
    const u64 tlasStaticSceneHash = staticScene ? ComputeTlasStaticSceneHash(instances) : 0u;
    const bool canReuseTlas =
        staticScene
        && rayTracingState().m_tlasStaticSceneHashValid
        && rayTracingState().m_tlasStaticSceneHash == tlasStaticSceneHash
        && rayTracingState().m_tlas
        && rayTracingState().m_tlasMaxInstances >= instances.size()
        && IsAccelStructHeapHandle(rayTracingState().m_tlasHeapHandle)
    ;
    if(!staticScene || !canReuseTlas)
        rayTracingState().m_tlasStaticSceneHashValid = false;

    if(
        commandList
        && (
            !rayTracingState().m_tlas
            || rayTracingState().m_tlasMaxInstances < instances.size()
            || !IsAccelStructHeapHandle(rayTracingState().m_tlasHeapHandle)
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: scene TLAS changed after preflight; skipping recording-time replacement"));
        return false;
    }

    if(!canReuseTlas && (!rayTracingState().m_tlas || rayTracingState().m_tlasMaxInstances < instances.size())){
        const usize capacity = ::NextGrowingCapacity(
            rayTracingState().m_tlasMaxInstances,
            instances.size(),
            s_TlasInitialInstanceCapacity
        );

        Core::RayTracingAccelStructDesc accelStructDesc(arena());
        accelStructDesc.setTopLevelMaxInstances(capacity);
        accelStructDesc.setBuildFlags(Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
        accelStructDesc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        accelStructDesc.setDebugName(Name("scene_tlas"));

        auto& device = graphics().getDevice();
        Core::RayTracingAccelStructHandle tlas = device.createAccelStruct(accelStructDesc);
        if(!tlas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene TLAS (capacity {})")
                , static_cast<u64>(capacity)
            );
            return false;
        }
        // Retire the old heap block before replacing the TLAS generation.
        if(rayTracingState().m_tlasHeapHandle.valid()){
            heap.free(rayTracingState().m_tlasHeapHandle);
            rayTracingState().m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        rayTracingState().m_tlas = Move(tlas);
        rayTracingState().m_tlasBackingFresh = true;
        rayTracingState().m_tlasBackingStateHandoffPending = false;
        rayTracingState().m_tlasMaxInstances = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created scene TLAS (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }

    if(!canReuseTlas && commandList){
        // The backend records the acceleration-structure build directly. Keep the task graph's declared
        // AccelStructRead boundary truthful by explicitly publishing the native build write and its final read.
        commandList->setAccelStructState(rayTracingState().m_tlas.get(), Core::ResourceStates::AccelStructWrite);
        commandList->commitBarriers();
        commandList->buildTopLevelAccelStruct(
            rayTracingState().m_tlas.get(),
            instances.data(),
            instances.size(),
            Core::RayTracingAccelStructBuildFlags::PreferFastTrace
        );
        commandList->setAccelStructState(rayTracingState().m_tlas.get(), Core::ResourceStates::AccelStructRead);
        commandList->commitBarriers();
        if(rayTracingState().m_tlasBackingFresh)
            rayTracingState().m_tlasBackingStateHandoffPending = true;
    }
    rayTracingState().m_tlasDeviceAddress = rayTracingState().m_tlas->getDeviceAddress();

    // Allocate a heap block only for a new TLAS generation.
    if(!IsAccelStructHeapHandle(rayTracingState().m_tlasHeapHandle)){
        if(commandList){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: scene TLAS heap view was absent after preflight"));
            return false;
        }
        if(rayTracingState().m_tlasHeapHandle.valid()){
            heap.free(rayTracingState().m_tlasHeapHandle);
            rayTracingState().m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        const Core::GpuDescriptorHandle tlasHeapHandle = heap.allocate(Core::GpuDescriptorClass::AccelStruct);
        if(
            !tlasHeapHandle.valid()
            || !heap.write(tlasHeapHandle, Core::DescriptorWriteItem::RayTracingAccelStruct(0u, rayTracingState().m_tlas.get()))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register scene TLAS in the descriptor-buffer heap"));
            if(tlasHeapHandle.valid())
                heap.free(tlasHeapHandle);
            return false;
        }
        rayTracingState().m_tlasHeapHandle = tlasHeapHandle;
    }

    // A healthy hybrid packet publishes the software-compatible descriptor slots as the final shared material
    // context. The HW TLAS itself never reads that buffer; its caustic/surfel consumers run after the retained
    // software traversal table confirms the frozen graph context. Do not overwrite or validate it as a HW-only
    // snapshot here. If that optional tail misses, recordPreflightShadowVisibilityResources clears the plan and
    // calls this path again directly.
    const bool hybridSoftwareMaterialContextGraphOwned =
        commandList
        && shadowMaterialContextBatchGraphOwned
        && m_shadowVisibilityHybridPipelinePreflighted
        && m_preparedShadowMaterialContextReady
        && m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Software
    ;
    if(!hybridSoftwareMaterialContextGraphOwned){
        // Preserve a valid typed buffer and hash the descriptor-slot representation.
        if(shadowMaterialTypedBytes.empty())
            shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
        usize materialTypedUploadBytes = 0u;
        if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes, materialTypedUploadBytes))
            return false;
        if(
            commandList
            && !HasPreparedShadowMaterialContextBuffers(
                rayTracingState(),
                instanceMaterials.size(),
                shadowInstanceData.size(),
                materialTypedUploadBytes
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW shadow material context changed after preflight; skipping recording-time replacement"));
            return false;
        }
        const u64 hwMaterialContextHash = ComputeShadowMaterialContextHash(
            instanceMaterials,
            shadowInstanceData,
            shadowMaterialTypedBytes
        );
        const bool canReuseHwMaterialContext =
            staticScene
            && !rayTracingState().m_sceneHasTransparentOccluder
            && rayTracingState().m_hwShadowMaterialContextHashValid
            && rayTracingState().m_hwShadowMaterialContextHash == hwMaterialContextHash
            && HasPreparedShadowMaterialContextBuffers(
                rayTracingState(),
                instanceMaterials.size(),
                shadowInstanceData.size(),
                materialTypedUploadBytes
            )
        ;
        if(!canReuseHwMaterialContext){
            if(!commandList){
                if(
                    !ensureShadowInstanceMaterialBuffer(instances.size())
                    || !ensureShadowInstanceContextBuffer(shadowInstanceData.size())
                    || !ensureShadowMaterialTypedBuffer(materialTypedUploadBytes)
                    || !HasPreparedShadowMaterialContextBuffers(
                        rayTracingState(),
                        instanceMaterials.size(),
                        shadowInstanceData.size(),
                        materialTypedUploadBytes
                    )
                )
                    return false;
                if(!capturePreparedShadowMaterialContext(
                    PreparedShadowMaterialContextRoute::Hardware,
                    staticScene,
                    hwMaterialContextHash,
                    instanceMaterials.data(),
                    instanceMaterials.size(),
                    instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu),
                    shadowInstanceData.data(),
                    shadowInstanceData.size(),
                    shadowInstanceData.size() * sizeof(InstanceGpuData),
                    shadowMaterialTypedBytes.data(),
                    materialTypedUploadBytes
                ))
                    return false;
            }
            if(commandList){
                if(shadowMaterialContextBatchGraphOwned){
                    if(!matchesPreparedShadowMaterialContext(
                        PreparedShadowMaterialContextRoute::Hardware,
                        staticScene,
                        hwMaterialContextHash,
                        instanceMaterials.data(),
                        instanceMaterials.size(),
                        instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu),
                        shadowInstanceData.data(),
                        shadowInstanceData.size(),
                        shadowInstanceData.size() * sizeof(InstanceGpuData),
                        shadowMaterialTypedBytes.data(),
                        materialTypedUploadBytes
                    )){
                        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW shadow material context changed after graph preflight; rejecting frozen upload batch"));
                        return false;
                    }
                }
                else{
                    Core::Buffer* materialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
                    commandList->setBufferState(materialBuffer, Core::ResourceStates::CopyDest);
                    commandList->commitBarriers();
                    commandList->writeBuffer(materialBuffer, instanceMaterials.data(), instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu));
                    commandList->setBufferState(materialBuffer, Core::ResourceStates::ShaderResource);
                    commandList->commitBarriers();
                    if(!UploadPreparedShadowMaterialContextBuffers(
                        *commandList,
                        *rayTracingState().m_shadowInstanceBuffer.get(),
                        *rayTracingState().m_shadowMaterialTypedBuffer.get(),
                        shadowInstanceData,
                        shadowMaterialTypedBytes,
                        materialTypedUploadBytes
                    ))
                        return false;

                    if(staticScene){
                        rayTracingState().m_hwShadowMaterialContextHash = hwMaterialContextHash;
                        rayTracingState().m_hwShadowMaterialContextHashValid = true;
                    }
                    else
                        rayTracingState().m_hwShadowMaterialContextHashValid = false;
                    // HW context cannot represent SW node slots.
                    rayTracingState().m_swShadowMaterialContextHashValid = false;
                }
            }
        }
        else if(commandList && shadowMaterialContextBatchGraphOwned){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned HW shadow material context unexpectedly reused a native cache"));
            return false;
        }
    }

    // Freeze the selected hardware instance stream for opaque and hybrid frames. Hybrid recording keeps a direct
    // retry boundary: if the frozen plan loses its generation or BLAS identity, it rebuilds the current TLAS and
    // retains the valid opaque result before the optional software tail decides whether transparent tracing runs.
    if(!commandList && !canReuseTlas){
        if(!capturePreparedSceneTlasBuild(
            staticScene,
            tlasStaticSceneHash,
            instances,
            instanceBlases
        )){
            if(!rayTracingState().m_sceneHasTransparentOccluder){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: could not freeze opaque scene TLAS build after preflight"));
                return false;
            }
            // A hybrid capture miss remains a direct compatibility fallback. The software material/scene snapshots
            // may still be graph-owned independently, so do not discard the whole preflight transaction here.
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze hybrid scene TLAS build; retaining direct retry fallback"));
        }
    }

    if(staticScene && commandList){
        rayTracingState().m_tlasStaticSceneHash = tlasStaticSceneHash;
        rayTracingState().m_tlasStaticSceneHashValid = true;
    }
    return true;
}

bool RendererRayTracingSystem::prepareSceneSwBvhResources(Core::Alloc::ScratchArena& scratchArena){
    return buildSceneSwBvhImpl(nullptr, scratchArena);
}

bool RendererRayTracingSystem::buildSceneSwBvh(
    Core::CommandList& commandList,
    Core::Alloc::ScratchArena& scratchArena,
    const bool shadowMaterialContextBatchGraphOwned,
    const bool sceneBvhBatchGraphOwned,
    const bool meshSwBvhBuildsGraphOwned
){
    return buildSceneSwBvhImpl(
        &commandList,
        scratchArena,
        shadowMaterialContextBatchGraphOwned,
        sceneBvhBatchGraphOwned,
        meshSwBvhBuildsGraphOwned
    );
}

bool RendererRayTracingSystem::recordPreparedHybridHardwareMaterialContextFallback(Core::CommandList& commandList){
    if(m_preparedHybridHardwareFallbackBytes.empty()){
        m_preparedHybridHardwareFallbackRecorded = false;
        return false;
    }
    const u8* const bytes = m_preparedHybridHardwareFallbackBytes.data();
    return recordPreparedHybridHardwareMaterialContextFallback(
        commandList,
        bytes,
        m_preparedHybridHardwareFallbackInstanceMaterialByteCount,
        bytes + m_preparedHybridHardwareFallbackInstanceMaterialByteCount,
        m_preparedHybridHardwareFallbackInstanceByteCount,
        bytes + m_preparedHybridHardwareFallbackInstanceMaterialByteCount
            + m_preparedHybridHardwareFallbackInstanceByteCount,
        m_preparedHybridHardwareFallbackMaterialTypedByteCount
    );
}

bool RendererRayTracingSystem::recordPreparedHybridHardwareMaterialContextFallback(
    Core::CommandList& commandList,
    const void* const instanceMaterialData,
    const usize sourceInstanceMaterialByteCount,
    const void* const instanceData,
    const usize sourceInstanceByteCount,
    const void* const materialTypedData,
    const usize sourceMaterialTypedByteCount
){
    m_preparedHybridHardwareFallbackRecorded = false;
#if !defined(NWB_FINAL)
    if(m_forceHybridHardwareFallbackSnapshotStaleForTesting){
        m_forceHybridHardwareFallbackSnapshotStaleForTesting = false;
        m_expectHybridHardwareFallbackDirectRetryForTesting = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: test forced frozen hybrid hardware material context stale"));
        return false;
    }
#endif
    const auto isStorageHandle = [](const Core::GpuDescriptorHandle handle){
        return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
    };
    const auto& state = rayTracingState();
    const usize instanceMaterialByteCount = m_preparedHybridHardwareFallbackInstanceMaterialByteCount;
    const usize instanceByteCount = m_preparedHybridHardwareFallbackInstanceByteCount;
    const usize materialTypedByteCount = m_preparedHybridHardwareFallbackMaterialTypedByteCount;
    if(
        !m_preparedHybridHardwareFallbackReady
        || !instanceMaterialData
        || !instanceData
        || !materialTypedData
        || instanceMaterialByteCount == 0u
        || instanceByteCount == 0u
        || materialTypedByteCount == 0u
        || sourceInstanceMaterialByteCount != instanceMaterialByteCount
        || sourceInstanceByteCount != instanceByteCount
        || sourceMaterialTypedByteCount != materialTypedByteCount
        || instanceMaterialByteCount % sizeof(NwbRtInstanceMaterialGpu) != 0u
        || instanceByteCount % sizeof(InstanceGpuData) != 0u
        || instanceMaterialByteCount > Limit<usize>::s_Max - instanceByteCount
        || instanceMaterialByteCount + instanceByteCount > Limit<usize>::s_Max - materialTypedByteCount
        || m_preparedHybridHardwareFallbackBytes.size() != instanceMaterialByteCount + instanceByteCount + materialTypedByteCount
        || state.m_shadowInstanceMaterialBuffer.get() != m_preparedHybridHardwareFallbackInstanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_preparedHybridHardwareFallbackInstanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_preparedHybridHardwareFallbackMaterialTypedBuffer.get()
        || state.m_shadowInstanceMaterialCapacity != m_preparedHybridHardwareFallbackInstanceMaterialCapacity
        || state.m_shadowInstanceCapacity != m_preparedHybridHardwareFallbackInstanceCapacity
        || state.m_shadowMaterialTypedCapacity != m_preparedHybridHardwareFallbackMaterialTypedCapacity
        || state.m_shadowInstanceMaterialHeapHandle != m_preparedHybridHardwareFallbackInstanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_preparedHybridHardwareFallbackInstanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_preparedHybridHardwareFallbackMaterialTypedHeapHandle
        || !isStorageHandle(state.m_shadowInstanceMaterialHeapHandle)
        || !isStorageHandle(state.m_shadowInstanceHeapHandle)
        || !isStorageHandle(state.m_shadowMaterialTypedHeapHandle)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen hybrid hardware material context no longer matches preflight storage"));
        return false;
    }
    if(
        world().componentMutationVersion<RendererComponent>() != m_preparedHybridHardwareFallbackRendererMutationVersion
        || world().componentMutationVersion<NWB::Impl::Scene::TransformComponent>() != m_preparedHybridHardwareFallbackTransformMutationVersion
        || world().componentMutationVersion<MaterialInstanceComponent>() != m_preparedHybridHardwareFallbackMaterialMutationVersion
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid hardware fallback inputs changed after graph preflight"));
        return false;
    }

    // A graph-owned caller provides immutable declaration-time blobs. Verify they still equal the retained
    // preflight snapshot before recording, so a caller-side replacement cannot restore a context the compiled task
    // did not declare. The compatibility overload above passes the same retained ranges directly.
    if(
        NWB_MEMCMP(
            m_preparedHybridHardwareFallbackBytes.data(),
            instanceMaterialData,
            instanceMaterialByteCount
        ) != 0
        || NWB_MEMCMP(
            m_preparedHybridHardwareFallbackBytes.data() + instanceMaterialByteCount,
            instanceData,
            instanceByteCount
        ) != 0
        || NWB_MEMCMP(
            m_preparedHybridHardwareFallbackBytes.data() + instanceMaterialByteCount + instanceByteCount,
            materialTypedData,
            materialTypedByteCount
        ) != 0
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned hybrid hardware fallback bytes differ from preflight"));
        return false;
    }

    Core::Buffer* const instanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* const instanceBuffer = state.m_shadowInstanceBuffer.get();
    Core::Buffer* const materialTypedBuffer = state.m_shadowMaterialTypedBuffer.get();
    commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    if(
        !commandList.tryWriteBuffer(instanceMaterialBuffer, instanceMaterialData, instanceMaterialByteCount)
        || !commandList.tryWriteBuffer(instanceBuffer, instanceData, instanceByteCount)
        || !commandList.tryWriteBuffer(materialTypedBuffer, materialTypedData, materialTypedByteCount)
    )
        return false;
    commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    m_preparedHybridHardwareFallbackRecorded = true;
    bool reportHybridHardwareFallbackRestore = true;
#if !defined(NWB_FINAL)
    if(m_forceHybridSceneTraversalFallbackEveryFrameForTesting){
        reportHybridHardwareFallbackRestore = !m_reportedHybridHardwareFallbackRestoreLoopForTesting;
        m_reportedHybridHardwareFallbackRestoreLoopForTesting = true;
    }
#endif
    if(reportHybridHardwareFallbackRestore)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: restored frozen hybrid hardware material context"));
    return true;
}

bool RendererRayTracingSystem::buildSceneSwBvhImpl(
    Core::CommandList* const commandList,
    Core::Alloc::ScratchArena& scratchArena,
    const bool shadowMaterialContextBatchGraphOwned,
    const bool sceneBvhBatchGraphOwned,
    const bool meshSwBvhBuildsGraphOwned
){
    using namespace __hidden_rt_swbvh;

    // Software scene BVH and material context share hardware instance ordering.
    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    // Parallel instance records and CPU BVH build values.
    Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    Vector<SceneBvhPrimitiveCalculation, Core::Alloc::ScratchArena> instanceBvhPrimitives{ scratchArena };
    // Parallel material records index scene-BVH leaves.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // All-occluder trace context; draw buffers hold one transparency class.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    Vector<PreparedSceneSwBvhMesh, Core::Alloc::ScratchArena> preparedMeshes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(candidateCount);
    instanceBvhPrimitives.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    preparedMeshes.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);
    MeshBufferSlotLookup meshSlotLookup(
        0,
        Hasher<const Core::Buffer*>(),
        EqualTo<const Core::Buffer*>(),
        scratchArena
    );
    meshSlotLookup.reserve(candidateCount);

    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software scene BVH requires the initialized global descriptor heap"));
        return false;
    }
    BeginMeshHeapHandleGather(rayTracingState().m_swMeshHeapHandleCache);
    const auto resolveMeshAttributeHeapHandle = [&](const Core::BufferHandle& buffer, Core::GpuDescriptorHandle& outHandle){
        return commandList
            ? FindPreparedMeshHeapHandle(rayTracingState().m_swMeshHeapHandleCache, buffer, outHandle)
            : AcquireMeshHeapHandle(heap, rayTracingState().m_swMeshHeapHandleCache, buffer, outHandle)
        ;
    };
    rayTracingState().m_swShadowMeshNodeBuffers.clear();
    rayTracingState().m_swShadowMeshPositionBuffers.clear();
    rayTracingState().m_swShadowMeshIndexBuffers.clear();
    rayTracingState().m_swShadowMeshAttributeBuffers.clear();
    rayTracingState().m_swShadowMeshNodeHandles.clear();
    rayTracingState().m_swShadowMeshPositionHandles.clear();
    rayTracingState().m_swShadowMeshIndexHandles.clear();
    rayTracingState().m_swShadowMeshAttributeHandles.clear();
    rayTracingState().m_swShadowMeshCount = 0u;
    bool staticScene = true;

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        MeshResources* mesh = nullptr;
        RenderableMeshDesc resolvedMesh;
        const bool meshReady = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystem,
            m_renderer.meshSystem(),
            entity,
            resolvedMesh,
            mesh
        );
        // Preflight allocates storage before the first GPU topology build.  It may therefore gather a pending mesh
        // using the selected storage, while the recording path still requires the topology to have completed.
        const bool topologyReady = mesh && (
            mesh->swBvhTopologyBuilt
            || (!commandList && (mesh->runtimeMesh || mesh->swBvhBuildPending))
            // A frozen software-only plan records before this scene gather but commits MeshResources only after the
            // packet accepts. Its exact full-build operation therefore authoritatively supplies topology for this
            // one recording pass without an optimistic CPU-side state mutation.
            || (commandList && meshSwBvhBuildsGraphOwned && preparedMeshSwBvhBuildProducesTopology(*mesh))
        );
        if(
            !meshReady
            || !mesh
            || !topologyReady
            || !mesh->swBvhNodeBuffer
            || !__hidden_rt_swbvh::IsStorageBufferHeapHandle(mesh->swBvhNodeHeapHandle)
            || !mesh->positionBuffer
            || !mesh->triangleIndexBuffer
            || !mesh->attributeBuffer
            || !mesh->csgLocalBounds.valid()
        )
            continue;
        // Runtime mesh updates disable static scene-BVH reuse.
        if(resolvedMesh.runtime || mesh->runtimeMesh)
            staticScene = false;

        // Reuse one table slot for instances sharing geometry.
        Core::Buffer* meshNodeBuffer = mesh->swBvhNodeBuffer.get();
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshNodeBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            const Core::GpuDescriptorHandle nodeHandle = mesh->swBvhNodeHeapHandle;
            Core::GpuDescriptorHandle attributeHandle;
            const Core::GpuDescriptorHandle positionHandle = mesh->swBvhPositionHeapHandle;
            const Core::GpuDescriptorHandle indexHandle = mesh->swBvhTriangleIndexHeapHandle;
            if(
                !positionHandle.valid()
                || positionHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
                || !indexHandle.valid()
                || indexHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
                || !resolveMeshAttributeHeapHandle(mesh->attributeBuffer, attributeHandle)
            ){
                if(!commandList){
                    SweepUnseenMeshHeapHandles(heap, rayTracingState().m_swMeshHeapHandleCache);
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW scene mesh buffers in the global descriptor heap"));
                }
                else
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: SW scene mesh descriptor was not prepared before recording"));
                return false;
            }

            if(!commandList){
                const Core::BufferDesc& nodeDesc = mesh->swBvhNodeBuffer->getCreationDescription();
                const Core::BufferDesc& positionDesc = mesh->positionBuffer->getCreationDescription();
                const Core::BufferDesc& indexDesc = mesh->triangleIndexBuffer->getCreationDescription();
                const Core::BufferDesc& attributeDesc = mesh->attributeBuffer->getCreationDescription();
                preparedMeshes.push_back(PreparedSceneSwBvhMesh{
                    .meshName = mesh->meshName,
                    .nodeBuffer = mesh->swBvhNodeBuffer,
                    .positionBuffer = mesh->positionBuffer,
                    .triangleIndexBuffer = mesh->triangleIndexBuffer,
                    .attributeBuffer = mesh->attributeBuffer,
                    .nodeHeapHandle = nodeHandle,
                    .positionHeapHandle = positionHandle,
                    .triangleIndexHeapHandle = indexHandle,
                    .attributeHeapHandle = attributeHandle,
                    .runtimeMeshVersion = mesh->runtimeMeshVersion,
                    .nodeByteSize = nodeDesc.byteSize,
                    .positionByteSize = positionDesc.byteSize,
                    .triangleIndexByteSize = indexDesc.byteSize,
                    .attributeByteSize = attributeDesc.byteSize,
                    .primitiveCount = mesh->meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount,
                    .runtimeMesh = mesh->runtimeMesh,
                });
            }

            meshSlot = rayTracingState().m_swShadowMeshCount;
            rayTracingState().m_swShadowMeshNodeBuffers.push_back(meshNodeBuffer);
            rayTracingState().m_swShadowMeshPositionBuffers.push_back(mesh->positionBuffer.get());
            rayTracingState().m_swShadowMeshIndexBuffers.push_back(mesh->triangleIndexBuffer.get());
            rayTracingState().m_swShadowMeshAttributeBuffers.push_back(mesh->attributeBuffer.get());
            rayTracingState().m_swShadowMeshNodeHandles.push_back(nodeHandle);
            rayTracingState().m_swShadowMeshPositionHandles.push_back(positionHandle);
            rayTracingState().m_swShadowMeshIndexHandles.push_back(indexHandle);
            rayTracingState().m_swShadowMeshAttributeHandles.push_back(attributeHandle);
            meshSlotLookup.emplace(meshNodeBuffer, meshSlot);
            ++rayTracingState().m_swShadowMeshCount;
        }

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transform
            ? MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            )
            : MatrixIdentity()
        ;
        SIMDVector determinant;
        const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);

        const SIMDVector localMin = LoadFloatInt(mesh->csgLocalBounds.minBounds);
        const SIMDVector localMax = LoadFloatInt(mesh->csgLocalBounds.maxBounds);
        SIMDVector worldMin{};
        SIMDVector worldMax{};
        if(!AabbTests::Transform(objectToWorld, localMin, localMax, worldMin, worldMax))
            continue;
        RayTracingDetail::InflateSwShadowSceneBounds(worldMin, worldMax);

        SceneSwBvhInstanceGpu instance;
        StoreFloat(worldToObject, &instance.worldToObject);
        instance.primitiveCount = mesh->meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;

        // Build material context in scene-BVH leaf order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo)){
            // Software shadow, caustic, and surfel traversal evaluate the same material surface dispatcher as the
            // hardware path. Freeze its sampled textures alongside the scene-BVH material context.
            if(
                !commandList
                && materialInfo->shadowTransmittanceModelId != Limit<u32>::s_Max
                && !appendPreparedShadowTraceMaterialSampledTextures(*materialInfo, scratchArena)
            )
                return false;
            u32 materialConstantByteOffset = 0u;
            if(!m_renderer.materialSystem().appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transform,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = RayTracingDetail::ResolveInstanceShadowMaterial(*materialInfo, materialConstantByteOffset, meshInstanceIndex);
        }
        instanceMaterial.indexSlot = rayTracingState().m_swShadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = rayTracingState().m_swShadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = rayTracingState().m_swShadowMeshPositionHandles[meshSlot].slot();
        instanceMaterial.nodeSlot = rayTracingState().m_swShadowMeshNodeHandles[meshSlot].slot();
        SceneBvhPrimitiveCalculation bvhPrimitive;
        bvhPrimitive.aabbMin = worldMin;
        bvhPrimitive.aabbMax = worldMax;
        bvhPrimitive.centroid = VectorScale(VectorAdd(worldMin, worldMax), 0.5f);
        bvhPrimitive.transparentOccluder = (instanceMaterial.flags & RtInstanceMaterialFlag::Transparent) != 0u;

        instances.push_back(instance);
        instanceBvhPrimitives.push_back(bvhPrimitive);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    if(rayTracingState().m_swShadowMeshCount > rayTracingState().m_swShadowMeshHeapHighWater){
        rayTracingState().m_swShadowMeshHeapHighWater = rayTracingState().m_swShadowMeshCount;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: SW-shadow heap registration high-water: {} distinct meshes -> {} handles")
            , static_cast<u64>(rayTracingState().m_swShadowMeshCount)
            , static_cast<u64>(rayTracingState().m_swShadowMeshCount) * s_SoftwareRayTracingMeshBufferCount
        );
    }
    if(!commandList)
        SweepUnseenMeshHeapHandles(heap, rayTracingState().m_swMeshHeapHandleCache);

    const u32 instanceCount = static_cast<u32>(instances.size());
    if(instanceCount == 0u){
        if(commandList && (shadowMaterialContextBatchGraphOwned || sceneBvhBatchGraphOwned)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software scene became empty after graph preflight; rejecting frozen trace uploads"));
            return false;
        }
        rayTracingState().m_sceneBvhInstanceCount = 0u;
        rayTracingState().m_sceneSwBvhStaticSceneHashValid = false;
        rayTracingState().m_swShadowMaterialContextHashValid = false;
        return true;
    }

    // Topology hash includes bounds, transforms, and transparent-subtree class.
    const u64 sceneStaticHash = staticScene
        ? ComputeSceneSwBvhStaticSceneHash(instances, instanceBvhPrimitives)
        : 0u
    ;
    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    if(requiredNodeCount > static_cast<usize>(BvhNodeIndex::ChildIndexMask) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software scene BVH requires {} nodes, exceeding the tagged child-index limit")
            , static_cast<u64>(requiredNodeCount)
        );
        return false;
    }
    const bool canReuseSceneBvh =
        staticScene
        && rayTracingState().m_sceneSwBvhStaticSceneHashValid
        && rayTracingState().m_sceneSwBvhStaticSceneHash == sceneStaticHash
        && rayTracingState().m_sceneBvhInstanceCount == instanceCount
        && HasPreparedSceneBvhBuffers(rayTracingState(), instanceCount)
    ;
    if(!staticScene || !canReuseSceneBvh)
        rayTracingState().m_sceneSwBvhStaticSceneHashValid = false;

    if(commandList && !HasPreparedSceneBvhBuffers(rayTracingState(), instanceCount)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software scene BVH changed after preflight; skipping recording-time replacement"));
        return false;
    }

    if(!canReuseSceneBvh){
        if(
            !commandList
            && (
                !ensureSceneBvhBuffers(instanceCount)
                || !HasPreparedSceneBvhBuffers(rayTracingState(), instanceCount)
            )
        )
            return false;
        // CPU-build the small scene BVH and upload the shared node layout.
        Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
        indices.reserve(instanceCount);
        for(u32 i = 0u; i < instanceCount; ++i)
            indices.push_back(i);

        Vector<SceneBvhNodeCalculation, Core::Alloc::ScratchArena> buildNodes{ scratchArena };
        buildNodes.reserve(requiredNodeCount);
        Vector<u32, Core::Alloc::ScratchArena> instanceLeafCost{ scratchArena };
        instanceLeafCost.reserve(instanceCount);
        for(u32 i = 0u; i < instanceCount; ++i)
            instanceLeafCost.push_back(instances[i].primitiveCount);
        RayTracingDetail::BuildSceneBvhNode(
            indices.data(),
            0u,
            instanceCount,
            instanceBvhPrimitives.data(),
            buildNodes,
            instanceLeafCost.data()
        );
        NWB_ASSERT(buildNodes.size() == requiredNodeCount);

        Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena> nodes{ scratchArena };
        nodes.reserve(buildNodes.size());
        for(const SceneBvhNodeCalculation& buildNode : buildNodes){
            NwbBvhNodeGpu node;
            StoreFloatInt(buildNode.aabbMin, buildNode.leftChild, &node.aabbMinLeftChild);
            const u32 taggedRightChild = buildNode.rightChild
                | (buildNode.containsTransparentOccluder ? BvhNodeIndex::TransparentSubtreeFlag : 0u)
            ;
            StoreFloatInt(buildNode.aabbMax, taggedRightChild, &node.aabbMaxRightChild);
            nodes.push_back(node);
        }

        if(!commandList){
            if(!capturePreparedSceneBvh(
                staticScene,
                sceneStaticHash,
                nodes.data(),
                nodes.size(),
                nodes.size() * sizeof(NwbBvhNodeGpu),
                instances.data(),
                instances.size(),
                instances.size() * sizeof(SceneSwBvhInstanceGpu)
            ))
                return false;
        }
        else if(sceneBvhBatchGraphOwned){
            if(!matchesPreparedSceneBvh(
                staticScene,
                sceneStaticHash,
                nodes.data(),
                nodes.size(),
                nodes.size() * sizeof(NwbBvhNodeGpu),
                instances.data(),
                instances.size(),
                instances.size() * sizeof(SceneSwBvhInstanceGpu)
            )){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software scene BVH changed after graph preflight; rejecting frozen upload pair"));
                return false;
            }
        }
        else{
            Core::Buffer* nodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
            Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
            commandList->setBufferState(nodeBuffer, Core::ResourceStates::CopyDest);
            commandList->setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
            commandList->commitBarriers();
            commandList->writeBuffer(nodeBuffer, nodes.data(), nodes.size() * sizeof(NwbBvhNodeGpu));
            commandList->writeBuffer(instanceBuffer, instances.data(), instances.size() * sizeof(SceneSwBvhInstanceGpu));
            commandList->setBufferState(nodeBuffer, Core::ResourceStates::ShaderResource);
            commandList->setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
            commandList->commitBarriers();
        }
    }
    else if(commandList && sceneBvhBatchGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned software scene BVH unexpectedly reused a native cache"));
        return false;
    }

    // Preserve a valid typed buffer and refresh SW node-slot context independently.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    usize materialTypedUploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes, materialTypedUploadBytes))
        return false;
    if(
        commandList
        && !HasPreparedShadowMaterialContextBuffers(
            rayTracingState(),
            instanceMaterials.size(),
            shadowInstanceData.size(),
            materialTypedUploadBytes
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: SW shadow material context changed after preflight; skipping recording-time replacement"));
        return false;
    }
    const u64 swMaterialContextHash = ComputeShadowMaterialContextHash(
        instanceMaterials,
        shadowInstanceData,
        shadowMaterialTypedBytes
    );
    const bool canReuseSwMaterialContext =
        staticScene
        && rayTracingState().m_swShadowMaterialContextHashValid
        && rayTracingState().m_swShadowMaterialContextHash == swMaterialContextHash
        && HasPreparedShadowMaterialContextBuffers(
            rayTracingState(),
            instanceMaterials.size(),
            shadowInstanceData.size(),
            materialTypedUploadBytes
        )
    ;
    if(!canReuseSwMaterialContext){
        if(!commandList){
            if(
                !ensureShadowInstanceMaterialBuffer(instances.size())
                || !ensureShadowInstanceContextBuffer(shadowInstanceData.size())
                || !ensureShadowMaterialTypedBuffer(materialTypedUploadBytes)
                || !HasPreparedShadowMaterialContextBuffers(
                    rayTracingState(),
                    instanceMaterials.size(),
                    shadowInstanceData.size(),
                    materialTypedUploadBytes
                )
            )
                return false;
            // This replaces the hardware snapshot gathered before the hybrid software path. Retain that exact
            // immutable context first, so an optional SW-tail miss can restore opaque consumers without regathering
            // renderer/material data while Shadow Preparation is recording.
            if(
                m_preparedShadowMaterialContextReady
                && m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Hardware
                && !capturePreparedHybridHardwareMaterialContextFallback()
            )
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain frozen hybrid hardware material fallback"));
            if(!capturePreparedShadowMaterialContext(
                PreparedShadowMaterialContextRoute::Software,
                staticScene,
                swMaterialContextHash,
                instanceMaterials.data(),
                instanceMaterials.size(),
                instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu),
                shadowInstanceData.data(),
                shadowInstanceData.size(),
                shadowInstanceData.size() * sizeof(InstanceGpuData),
                shadowMaterialTypedBytes.data(),
                materialTypedUploadBytes
            ))
                return false;
        }
        if(commandList){
            if(shadowMaterialContextBatchGraphOwned){
                if(!matchesPreparedShadowMaterialContext(
                    PreparedShadowMaterialContextRoute::Software,
                    staticScene,
                    swMaterialContextHash,
                    instanceMaterials.data(),
                    instanceMaterials.size(),
                    instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu),
                    shadowInstanceData.data(),
                    shadowInstanceData.size(),
                    shadowInstanceData.size() * sizeof(InstanceGpuData),
                    shadowMaterialTypedBytes.data(),
                    materialTypedUploadBytes
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: SW shadow material context changed after graph preflight; rejecting frozen upload batch"));
                    return false;
                }
            }
            else{
                Core::Buffer* materialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
                commandList->setBufferState(materialBuffer, Core::ResourceStates::CopyDest);
                commandList->commitBarriers();
                commandList->writeBuffer(materialBuffer, instanceMaterials.data(), instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu));
                commandList->setBufferState(materialBuffer, Core::ResourceStates::ShaderResource);
                commandList->commitBarriers();
                if(!UploadPreparedShadowMaterialContextBuffers(
                    *commandList,
                    *rayTracingState().m_shadowInstanceBuffer.get(),
                    *rayTracingState().m_shadowMaterialTypedBuffer.get(),
                    shadowInstanceData,
                    shadowMaterialTypedBytes,
                    materialTypedUploadBytes
                ))
                    return false;

                if(staticScene){
                    rayTracingState().m_swShadowMaterialContextHash = swMaterialContextHash;
                    rayTracingState().m_swShadowMaterialContextHashValid = true;
                }
                else
                    rayTracingState().m_swShadowMaterialContextHashValid = false;
                // HW context cannot represent SW node slots.
                rayTracingState().m_hwShadowMaterialContextHashValid = false;
            }
        }
    }
    else if(commandList && shadowMaterialContextBatchGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned SW shadow material context unexpectedly reused a native cache"));
        return false;
    }

    rayTracingState().m_sceneBvhInstanceCount = instanceCount;
    if(staticScene && commandList && !sceneBvhBatchGraphOwned){
        rayTracingState().m_sceneSwBvhStaticSceneHash = sceneStaticHash;
        rayTracingState().m_sceneSwBvhStaticSceneHashValid = true;
    }
    if(!commandList){
        const bool frozenGraphScene =
            m_preparedSceneBvhReady
            && m_preparedShadowMaterialContextReady
            && m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Software
        ;
        if(frozenGraphScene){
            if(!capturePreparedSceneSwBvhTraversal(
                preparedMeshes.data(),
                preparedMeshes.size(),
                instanceCount
            )){
                // The paired immutable uploads remain valid even when their traversal-table snapshot cannot be
                // retained. The pure graph route keeps its existing direct recorder for this frame; the healthy
                // hybrid route retains its later optional direct-retry boundary.
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software scene traversal; retaining direct compatibility recorder"));
            }
        }
        else
            clearPreparedSceneSwBvhTraversal();
    }
    return true;
}

bool RendererRayTracingSystem::prepareMeshBlasResources(MeshResources& meshResources){
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
    Core::RayTracingAccelStructDesc accelStructDesc(arena());
    accelStructDesc.addBottomLevelGeometry(geometry);
    accelStructDesc.setBuildFlags(buildFlags);
    accelStructDesc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    accelStructDesc.setDebugName(DeriveName(meshResources.meshName, AStringView(":blas")));
    Core::RayTracingAccelStructHandle blas = graphics().getDevice().createAccelStruct(accelStructDesc);
    if(!blas){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BLAS for mesh '{}'")
            , StringConvert(meshResources.meshName.c_str())
        );
        return false;
    }
    meshResources.blas = Move(blas);
    meshResources.blasBackingFresh = true;
    meshResources.blasBackingStateHandoffPending = false;
    meshResources.blasRefitsSinceRebuild = 0u;
    return true;
}

bool RendererRayTracingSystem::buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources){
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
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, rayTracingState().m_bvhSortKeysHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, rayTracingState().m_bvhSortPayloadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, rayTracingState().m_bvhVisitCounterHeapHandle);
        return;
    }
    rayTracingState().m_bvhSortKeysHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_bvhSortPayloadHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_bvhVisitCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::ensureBvhSortPipeline(){
    if(rayTracingState().m_bvhSortPipeline)
        return true;
    if(rayTracingState().m_bvhSortPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software BVH sort requires the initialized global descriptor heap"));
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_bvhSortBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Push-only layout; sort resources use the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhSortPushConstants)));

        rayTracingState().m_bvhSortBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhSortBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
            rayTracingState().m_bvhSortPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_bvhSortShader,
        AssetsGraphicsBvh::s_BitonicSortShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_BvhBitonicSort"
    )){
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_bvhSortShader)
        .addBindingLayout(rayTracingState().m_bvhSortBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_bvhSortPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_bvhSortPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort compute pipeline"));
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhSortBuffers(usize paddedCount){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    const auto sortHandlesReady = [this](){
        return
            __hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhSortKeysHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhSortPayloadHeapHandle)
        ;
    };

    if(
        rayTracingState().m_bvhSortKeysBuffer
        && rayTracingState().m_bvhSortPayloadBuffer
        && rayTracingState().m_bvhSortCapacity >= paddedCount
    ){
        if(sortHandlesReady())
            return true;

        // Register only missing handles to preserve live generations.
        Core::GpuDescriptorHandle acquiredKeys = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle acquiredPayload = Core::GpuDescriptorHandle::invalid();
        if(
            (!rayTracingState().m_bvhSortKeysHeapHandle.valid()
                && !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *rayTracingState().m_bvhSortKeysBuffer.get(), acquiredKeys))
            || (!rayTracingState().m_bvhSortPayloadHeapHandle.valid()
                && !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *rayTracingState().m_bvhSortPayloadBuffer.get(), acquiredPayload))
        ){
            RayTracingDetail::RetireHeapHandle(heap, acquiredKeys);
            RayTracingDetail::RetireHeapHandle(heap, acquiredPayload);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing BVH sort scratch in the descriptor heap"));
            return false;
        }
        if(acquiredKeys.valid())
            rayTracingState().m_bvhSortKeysHeapHandle = acquiredKeys;
        if(acquiredPayload.valid())
            rayTracingState().m_bvhSortPayloadHeapHandle = acquiredPayload;
        return sortHandlesReady();
    }

    const usize capacity = ::NextGrowingCapacity(
        rayTracingState().m_bvhSortCapacity,
        paddedCount,
        s_BvhSortInitialCapacity
    );

    Core::BufferDesc keysBufferDesc;
    keysBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_sort_keys"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle keysBuffer = graphics().createBuffer(keysBufferDesc);
    if(!keysBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort keys buffer"));
        return false;
    }

    Core::BufferDesc payloadBufferDesc;
    payloadBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_sort_payload"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle payloadBuffer = graphics().createBuffer(payloadBufferDesc);
    if(!payloadBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort payload buffer"));
        return false;
    }

    Core::GpuDescriptorHandle keysHeapHandle;
    Core::GpuDescriptorHandle payloadHeapHandle;
    if(
        !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *keysBuffer.get(), keysHeapHandle)
        || !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *payloadBuffer.get(), payloadHeapHandle)
    ){
        RayTracingDetail::RetireHeapHandle(heap, keysHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, payloadHeapHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register BVH sort scratch in the descriptor heap"));
        return false;
    }

    RayTracingDetail::RetireHeapHandle(heap, rayTracingState().m_bvhSortKeysHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, rayTracingState().m_bvhSortPayloadHeapHandle);
    rayTracingState().m_bvhSortKeysBuffer = Move(keysBuffer);
    rayTracingState().m_bvhSortPayloadBuffer = Move(payloadBuffer);
    rayTracingState().m_bvhSortKeysHeapHandle = keysHeapHandle;
    rayTracingState().m_bvhSortPayloadHeapHandle = payloadHeapHandle;
    rayTracingState().m_bvhSortCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(rayTracingState().m_bvhSortPipeline);
    NWB_ASSERT(rayTracingState().m_bvhSortKeysBuffer);
    NWB_ASSERT(rayTracingState().m_bvhSortPayloadBuffer);
    NWB_ASSERT(__hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhSortKeysHeapHandle));
    NWB_ASSERT(__hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhSortPayloadHeapHandle));

    // Padded count is power-of-two and group-aligned.
    if(paddedCount < static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE))
        return false;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();

    // Consecutive sort steps require UAV barriers.
    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    const u32 groupCount = paddedCount / static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);

    const auto dispatchSort = [this, &commandList](BvhSortPushConstants pushConstants, const u32 groups){
        pushConstants.keysHeapSlot = rayTracingState().m_bvhSortKeysHeapHandle.slot();
        pushConstants.payloadHeapSlot = rayTracingState().m_bvhSortPayloadHeapHandle.slot();
        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_bvhSortPipeline.get());
        commandList.setComputeState(computeState);
        graphics().getDevice().getDescriptorHeap().bindCompute(commandList, *rayTracingState().m_bvhSortPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groups, 1u, 1u);
    };
    const auto bvhSortBarrier = [&commandList, keysBuffer, payloadBuffer](){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    };

    {
        BvhSortPushConstants pushConstants;
        pushConstants.elementCount = elementCount;
        pushConstants.mode = NWB_BVH_SORT_MODE_LOCAL_TILE;
        dispatchSort(pushConstants, groupCount);
        bvhSortBarrier();
    }

    // Merge inter-tile globally and intra-tile in groupshared tails.
    for(u32 sequenceSize = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE) << 1u; sequenceSize <= paddedCount; sequenceSize <<= 1u){
        for(u32 compareDistance = sequenceSize >> 1u; compareDistance >= static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE); compareDistance >>= 1u){
            BvhSortPushConstants pushConstants;
            pushConstants.elementCount = elementCount;
            pushConstants.compareDistance = compareDistance;
            pushConstants.sequenceSize = sequenceSize;
            pushConstants.mode = NWB_BVH_SORT_MODE_GLOBAL;
            dispatchSort(pushConstants, groupCount);
            bvhSortBarrier();
        }

        BvhSortPushConstants tailPushConstants;
        tailPushConstants.elementCount = elementCount;
        tailPushConstants.sequenceSize = sequenceSize;
        tailPushConstants.mode = NWB_BVH_SORT_MODE_GLOBAL_TAIL;
        dispatchSort(tailPushConstants, groupCount);
        bvhSortBarrier();
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhBuildPipeline(){
    if(
        rayTracingState().m_bvhMortonPipeline
        && rayTracingState().m_bvhTopologyPipeline
        && rayTracingState().m_bvhFitPipeline
    )
        return true;
    if(rayTracingState().m_bvhBuildPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software BVH build requires the initialized global descriptor heap"));
        rayTracingState().m_bvhBuildPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_bvhBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Push-only layout; build resources use the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhBuildPushConstants)));

        rayTracingState().m_bvhBuildBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhBuildBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
            rayTracingState().m_bvhBuildPipelineFailed = true;
            return false;
        }
    }

    const auto createBuildPipeline = [this, &device, &heap](
        Core::ShaderHandle& shader,
        Core::ComputePipelineHandle& pipeline,
        const Name& shaderName,
        const char* debugLabel
    )->bool{
        if(pipeline)
            return true;
        if(!m_renderer.shaderSystem().loadShader(shader, shaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Compute, debugLabel))
            return false;

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(rayTracingState().m_bvhBuildBindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        pipeline = device.createComputePipeline(pipelineDesc);
        return pipeline != nullptr;
    };

    if(
        !createBuildPipeline(rayTracingState().m_bvhMortonShader, rayTracingState().m_bvhMortonPipeline, AssetsGraphicsBvh::s_BvhMortonShaderName, "ECSRender_BvhMorton")
        || !createBuildPipeline(rayTracingState().m_bvhTopologyShader, rayTracingState().m_bvhTopologyPipeline, AssetsGraphicsBvh::s_BvhTopologyShaderName, "ECSRender_BvhTopology")
        || !createBuildPipeline(rayTracingState().m_bvhFitShader, rayTracingState().m_bvhFitPipeline, AssetsGraphicsBvh::s_BvhFitShaderName, "ECSRender_BvhFit")
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build compute pipeline"));
        rayTracingState().m_bvhBuildPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhVisitCounterBuffer(usize primitiveCount){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(rayTracingState().m_bvhVisitCounterBuffer && rayTracingState().m_bvhBuildCapacity >= primitiveCount){
        if(__hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhVisitCounterHeapHandle))
            return true;

        Core::GpuDescriptorHandle acquired = Core::GpuDescriptorHandle::invalid();
        if(!rayTracingState().m_bvhVisitCounterHeapHandle.valid()
            && __hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *rayTracingState().m_bvhVisitCounterBuffer.get(), acquired)){
            rayTracingState().m_bvhVisitCounterHeapHandle = acquired;
            return true;
        }
        RayTracingDetail::RetireHeapHandle(heap, acquired);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing BVH visit counter in the descriptor heap"));
        return false;
    }

    const usize capacity = ::NextGrowingCapacity(
        rayTracingState().m_bvhBuildCapacity,
        primitiveCount,
        s_BvhBuildInitialCapacity
    );

    // Shared visit-counter scratch is safe because mesh builds are serialized.
    Core::BufferDesc counterBufferDesc;
    counterBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_visit_counter"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle counterBuffer = graphics().createBuffer(counterBufferDesc);
    if(!counterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH visit counter buffer"));
        return false;
    }

    Core::GpuDescriptorHandle counterHeapHandle = Core::GpuDescriptorHandle::invalid();
    if(!__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *counterBuffer.get(), counterHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register BVH visit counter in the descriptor heap"));
        return false;
    }

    RayTracingDetail::RetireHeapHandle(heap, rayTracingState().m_bvhVisitCounterHeapHandle);
    rayTracingState().m_bvhVisitCounterBuffer = Move(counterBuffer);
    rayTracingState().m_bvhVisitCounterHeapHandle = counterHeapHandle;
    rayTracingState().m_bvhBuildCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::createMeshBvhStorage(
    usize primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::GpuDescriptorHandle& nodeHeapHandle,
    Core::GpuDescriptorHandle& parentHeapHandle
){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(nodeBuffer && parentBuffer){
        if(
            __hidden_rt_swbvh::IsStorageBufferHeapHandle(nodeHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(parentHeapHandle)
        )
            return true;

        Core::GpuDescriptorHandle acquiredNode = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle acquiredParent = Core::GpuDescriptorHandle::invalid();
        if(
            (!nodeHeapHandle.valid() && !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *nodeBuffer.get(), acquiredNode))
            || (!parentHeapHandle.valid() && !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *parentBuffer.get(), acquiredParent))
        ){
            RayTracingDetail::RetireHeapHandle(heap, acquiredNode);
            RayTracingDetail::RetireHeapHandle(heap, acquiredParent);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing per-mesh BVH storage in the descriptor heap"));
            return false;
        }
        if(acquiredNode.valid())
            nodeHeapHandle = acquiredNode;
        if(acquiredParent.valid())
            parentHeapHandle = acquiredParent;
        return
            __hidden_rt_swbvh::IsStorageBufferHeapHandle(nodeHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(parentHeapHandle)
        ;
    }
    if(nodeBuffer || parentBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: per-mesh BVH storage is partially allocated"));
        return false;
    }

    // Per-mesh binary LBVH needs 2N-1 persistent nodes.
    const usize nodeCount = primitiveCount * 2u - 1u;

    Core::BufferDesc nodeBufferDesc;
    nodeBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount))
        .setStructStride(sizeof(NwbBvhNodeGpu))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_mesh_nodes"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle newNodeBuffer = graphics().createBuffer(nodeBufferDesc);
    if(!newNodeBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH node buffer"));
        return false;
    }

    Core::BufferDesc parentBufferDesc;
    parentBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * nodeCount))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_mesh_parent"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle newParentBuffer = graphics().createBuffer(parentBufferDesc);
    if(!newParentBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH parent buffer"));
        return false;
    }

    Core::GpuDescriptorHandle newNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle newParentHeapHandle = Core::GpuDescriptorHandle::invalid();
    if(
        !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *newNodeBuffer.get(), newNodeHeapHandle)
        || !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *newParentBuffer.get(), newParentHeapHandle)
    ){
        RayTracingDetail::RetireHeapHandle(heap, newNodeHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, newParentHeapHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register per-mesh BVH storage in the descriptor heap"));
        return false;
    }

    nodeBuffer = Move(newNodeBuffer);
    parentBuffer = Move(newParentBuffer);
    nodeHeapHandle = newNodeHeapHandle;
    parentHeapHandle = newParentHeapHandle;
    return true;
}

bool RendererRayTracingSystem::ensureMeshSwBvhResources(
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::GpuDescriptorHandle& nodeHeapHandle,
    Core::GpuDescriptorHandle& parentHeapHandle
){
    if(primitiveCount > s_BvhMaxPrimitivesPerMesh){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh exceeds software BVH primitive cap ({} > {}), shadows skipped")
            , static_cast<u64>(primitiveCount)
            , static_cast<u64>(s_BvhMaxPrimitivesPerMesh)
        );
        return false;
    }

    if(!ensureBvhSortPipeline())
        return false;
    if(!ensureBvhBuildPipeline())
        return false;
    // Allocate shared sort/counter scratch at the maximum supported mesh size.
    if(!ensureBvhSortBuffers(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!ensureBvhVisitCounterBuffer(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!createMeshBvhStorage(primitiveCount, nodeBuffer, parentBuffer, nodeHeapHandle, parentHeapHandle))
        return false;
    return true;
}

bool RendererRayTracingSystem::meshSwBvhResourcesReady(
    const Core::BufferHandle& nodeBuffer,
    const Core::BufferHandle& parentBuffer,
    const Core::GpuDescriptorHandle nodeHeapHandle,
    const Core::GpuDescriptorHandle parentHeapHandle
){
    return
        nodeBuffer
        && parentBuffer
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(nodeHeapHandle)
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(parentHeapHandle)
        && rayTracingState().m_bvhSortPipeline
        && rayTracingState().m_bvhSortKeysBuffer
        && rayTracingState().m_bvhSortPayloadBuffer
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhSortKeysHeapHandle)
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhSortPayloadHeapHandle)
        && rayTracingState().m_bvhMortonPipeline
        && rayTracingState().m_bvhTopologyPipeline
        && rayTracingState().m_bvhFitPipeline
        && rayTracingState().m_bvhVisitCounterBuffer
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(rayTracingState().m_bvhVisitCounterHeapHandle)
    ;
}

bool RendererRayTracingSystem::buildMeshSwBvhPrepared(
    Core::CommandList& commandList,
    const u32 positionHeapSlot,
    const u32 triangleIndexHeapSlot,
    u32 primitiveCount,
    const SIMDVector aabbMin,
    const SIMDVector aabbMax,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    const Core::GpuDescriptorHandle nodeHeapHandle,
    const Core::GpuDescriptorHandle parentHeapHandle,
    const bool sentinelClearsGraphOwned,
    const bool graphBoundaryStatesOwned
){
    if(
        primitiveCount == 0u
        || positionHeapSlot == Limit<u32>::s_Max
        || triangleIndexHeapSlot == Limit<u32>::s_Max
        || !meshSwBvhResourcesReady(nodeBuffer, parentBuffer, nodeHeapHandle, parentHeapHandle)
    )
        return false;

    u32 paddedCount = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    while(paddedCount < primitiveCount)
        paddedCount <<= 1u;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();
    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.positionHeapSlot = positionHeapSlot;
    pushConstants.triangleIndexHeapSlot = triangleIndexHeapSlot;
    pushConstants.keysHeapSlot = rayTracingState().m_bvhSortKeysHeapHandle.slot();
    pushConstants.payloadHeapSlot = rayTracingState().m_bvhSortPayloadHeapHandle.slot();
    pushConstants.nodeHeapSlot = nodeHeapHandle.slot();
    pushConstants.parentHeapSlot = parentHeapHandle.slot();
    pushConstants.visitCounterHeapSlot = rayTracingState().m_bvhVisitCounterHeapHandle.slot();
    StoreFloat(VectorSetW(aabbMin, 0.0f), &pushConstants.aabbMin);
    StoreFloat(VectorSetW(aabbMax, 0.0f), &pushConstants.aabbMax);

    // The graph-split pure-software route lowers these typed CopyDest clears as adjacent built-in tasks. Direct
    // and hybrid compatibility routes preserve their established native sentinel setup here.
    if(!sentinelClearsGraphOwned){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::CopyDest);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::CopyDest);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(keysBuffer, BvhNodeIndex::Invalid);
        commandList.clearBufferUInt(meshParentBuffer, BvhNodeIndex::Invalid);
        commandList.clearBufferUInt(visitCounterBuffer, 0u);
    }

    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);

    const auto bvhBuildBarrier = [&commandList, keysBuffer, payloadBuffer, meshNodeBuffer, meshParentBuffer, visitCounterBuffer](){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    };

    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    const auto dispatchBuildKernel = [&commandList, &pushConstants, &heap](Core::ComputePipeline* pipeline, const u32 groupCount){
        Core::ComputeState computeState;
        computeState.setPipeline(pipeline);
        commandList.setComputeState(computeState);
        heap.bindCompute(commandList, *pipeline);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groupCount, 1u, 1u);
    };

    // The pure-software graph callback declares every input/output state. It owns the first boundary after its
    // typed clears; direct and hybrid callers retain the standalone native transition/UAV fence.
    if(!graphBoundaryStatesOwned)
        bvhBuildBarrier();

    dispatchBuildKernel(rayTracingState().m_bvhMortonPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();

    // Separates rebuild-sort timing from the one-shot self-test.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SwBvhSort, graphics().getDevice(), commandList);
        if(!bvhBitonicSort(commandList, primitiveCount, paddedCount))
            return false;
    }
    bvhBuildBarrier();

    if(primitiveCount > 1u){
        dispatchBuildKernel(rayTracingState().m_bvhTopologyPipeline.get(), DivideUp(primitiveCount - 1u, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
        bvhBuildBarrier();
    }

    dispatchBuildKernel(rayTracingState().m_bvhFitPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    // Shadow Preparation's declared successor uses lower the final node UAV -> SRV and retained scratch UAV
    // handoffs for graph callers. Keep the direct/hybrid close fence for compatibility recorders.
    if(!graphBoundaryStatesOwned)
        bvhBuildBarrier();
    return true;
}

bool RendererRayTracingSystem::refitMeshSwBvhPrepared(
    Core::CommandList& commandList,
    const u32 positionHeapSlot,
    const u32 triangleIndexHeapSlot,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    const Core::GpuDescriptorHandle nodeHeapHandle,
    const Core::GpuDescriptorHandle parentHeapHandle,
    const bool sentinelClearsGraphOwned,
    const bool graphBoundaryStatesOwned
){
    if(
        primitiveCount == 0u
        || positionHeapSlot == Limit<u32>::s_Max
        || triangleIndexHeapSlot == Limit<u32>::s_Max
        || !meshSwBvhResourcesReady(nodeBuffer, parentBuffer, nodeHeapHandle, parentHeapHandle)
    )
        return false;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();
    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.refitMode = NWB_BVH_BUILD_MODE_REFIT;
    pushConstants.positionHeapSlot = positionHeapSlot;
    pushConstants.triangleIndexHeapSlot = triangleIndexHeapSlot;
    pushConstants.keysHeapSlot = rayTracingState().m_bvhSortKeysHeapHandle.slot();
    pushConstants.payloadHeapSlot = rayTracingState().m_bvhSortPayloadHeapHandle.slot();
    pushConstants.nodeHeapSlot = nodeHeapHandle.slot();
    pushConstants.parentHeapSlot = parentHeapHandle.slot();
    pushConstants.visitCounterHeapSlot = rayTracingState().m_bvhVisitCounterHeapHandle.slot();

    // Refit retains topology and recomputes boxes. The pure-software graph route supplies this typed counter clear
    // immediately before the callback; hybrid and direct routes retain the native compatibility primitive.
    if(!sentinelClearsGraphOwned){
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(visitCounterBuffer, 0u);
    }

    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);
    // Fit declares all scratch views, so direct/hybrid callers retain its native entry UAV fence. The graph-split
    // pure-software callback declares these exact states and lowers the CopyDest/UAV handoff in its prologue.
    if(!graphBoundaryStatesOwned){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }

    Core::ComputeState computeState;
    computeState.setPipeline(rayTracingState().m_bvhFitPipeline.get());
    commandList.setComputeState(computeState);
    graphics().getDevice().getDescriptorHeap().bindCompute(commandList, *rayTracingState().m_bvhFitPipeline.get());
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)), 1u, 1u);

    // The graph successor owns this final node state for pure software; direct and hybrid callers retain it.
    if(!graphBoundaryStatesOwned){
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }
    return true;
}

bool RendererRayTracingSystem::updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;
    if(
        !meshResources.swBvhPositionHeapHandle.valid()
        || meshResources.swBvhPositionHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !meshResources.swBvhTriangleIndexHeapHandle.valid()
        || meshResources.swBvhTriangleIndexHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    )
        return false;
    if(meshResources.meshletPrimitiveIndexCount == 0u || (meshResources.meshletPrimitiveIndexCount % s_RayTracingTriangleIndexCount) != 0u)
        return false;
    const u32 primitiveCount = meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;
    if(!meshSwBvhResourcesReady(
        meshResources.swBvhNodeBuffer,
        meshResources.swBvhParentBuffer,
        meshResources.swBvhNodeHeapHandle,
        meshResources.swBvhParentHeapHandle
    ))
        return false;

    // Build kernels read input buffers as raw SRVs.
    commandList.setBufferState(meshResources.positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(meshResources.triangleIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Runtime meshes refit until the adaptive budget; first build initializes topology.
    const bool firstBuild = !meshResources.swBvhTopologyBuilt;
    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.swBvhRefitsSinceRebuild < adaptiveRefitsBeforeRebuild(primitiveCount)
    ;
    const u32 positionHeapSlot = meshResources.swBvhPositionHeapHandle.slot();
    const u32 triangleIndexHeapSlot = meshResources.swBvhTriangleIndexHeapHandle.slot();

    bool built = false;
    if(performRefit){
        built = refitMeshSwBvhPrepared(
            commandList,
            positionHeapSlot,
            triangleIndexHeapSlot,
            primitiveCount,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhNodeHeapHandle,
            meshResources.swBvhParentHeapHandle
        );
    }
    else{
        const SIMDVector aabbMin = LoadFloatInt(meshResources.csgLocalBounds.minBounds);
        const SIMDVector aabbMax = LoadFloatInt(meshResources.csgLocalBounds.maxBounds);
        built = buildMeshSwBvhPrepared(
            commandList,
            positionHeapSlot,
            triangleIndexHeapSlot,
            primitiveCount,
            aabbMin,
            aabbMax,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhNodeHeapHandle,
            meshResources.swBvhParentHeapHandle
        );
    }
    if(!built)
        return false;

    if(!performRefit)
        meshResources.swBvhTopologyBuilt = true;

    if(firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built software BVH for mesh '{}' (runtime {}, {} triangles)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(primitiveCount)
        );
    }

    meshResources.swBvhRefitsSinceRebuild = performRefit ? (meshResources.swBvhRefitsSinceRebuild + 1u) : 0u;
    return true;
}

bool RendererRayTracingSystem::ensureSceneBvhBuffers(u32 instanceCount){
    // Binary scene BVH needs 2N-1 CPU-uploaded SRV nodes.
    const usize requiredNodes = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhNodeCapacity < requiredNodes){
        const usize capacity = ::NextGrowingCapacity(
            rayTracingState().m_sceneBvhNodeCapacity,
            requiredNodes,
            s_SceneBvhInitialInstanceCapacity * 2u - 1u
        );

        Core::BufferDesc nodeBufferDesc;
        nodeBufferDesc
            .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * capacity))
            .setStructStride(sizeof(NwbBvhNodeGpu))
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("scene_bvh_nodes"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle nodeBuffer = graphics().createBuffer(nodeBufferDesc);
        if(!nodeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH node buffer"));
            return false;
        }
        // Register the new view before replacing ownership to keep the old pair retryable.
        if(!replaceRayTraceMaterialContextHeapHandle(*nodeBuffer.get(), rayTracingState().m_sceneBvhNodeHeapHandle))
            return false;
        rayTracingState().m_sceneBvhNodeBuffer = Move(nodeBuffer);
        rayTracingState().m_sceneBvhNodeCapacity = capacity;
    }

    if(!rayTracingState().m_sceneInstanceBuffer || rayTracingState().m_sceneInstanceCapacity < instanceCount){
        const usize capacity = ::NextGrowingCapacity(
            rayTracingState().m_sceneInstanceCapacity,
            instanceCount,
            s_SceneBvhInitialInstanceCapacity
        );

        Core::BufferDesc instanceBufferDesc;
        instanceBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SceneSwBvhInstanceGpu) * capacity))
            .setStructStride(sizeof(SceneSwBvhInstanceGpu))
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("scene_bvh_instances"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle instanceBuffer = graphics().createBuffer(instanceBufferDesc);
        if(!instanceBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH instance buffer"));
            return false;
        }
        if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), rayTracingState().m_sceneInstanceHeapHandle))
            return false;
        rayTracingState().m_sceneInstanceBuffer = Move(instanceBuffer);
        rayTracingState().m_sceneInstanceCapacity = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software scene BVH buffers (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }
    return ensureRayTraceMaterialContextHeapHandle(
        *rayTracingState().m_sceneBvhNodeBuffer.get(),
        rayTracingState().m_sceneBvhNodeHeapHandle
    ) && ensureRayTraceMaterialContextHeapHandle(
        *rayTracingState().m_sceneInstanceBuffer.get(),
        rayTracingState().m_sceneInstanceHeapHandle
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

