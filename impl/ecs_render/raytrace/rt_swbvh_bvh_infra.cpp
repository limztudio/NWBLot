// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingSoftwareBvhDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendTlasInstanceStaticCacheInput(const Core::RayTracingInstanceDesc& instance, u64& inOutHash)noexcept{
    Fnv64AppendValue(inOutHash, instance.transform);
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceID));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceMask));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.instanceContributionToHitGroupIndex));
    Fnv64AppendValue(inOutHash, static_cast<u32>(instance.flags));
    Fnv64AppendValue(inOutHash, instance.bottomLevelAS);
}

u64 ComputeTlasStaticSceneHash(const Span<const Core::RayTracingInstanceDesc> instances)noexcept{
    u64 hash = s_Fnv64OffsetBasis;
    Fnv64AppendValue(hash, s_SceneStaticCacheHashVersion);
    Fnv64AppendValue(hash, instances.size());
    for(const Core::RayTracingInstanceDesc& instance : instances)
        AppendTlasInstanceStaticCacheInput(instance, hash);
    return hash;
}

u64 ComputeSceneSwBvhStaticSceneHash(const Span<const SceneSwBvhInstanceGpu> instances, const Span<const SceneBvhPrimitiveCalculation> primitives)noexcept{
    NWB_ASSERT(instances.size() == primitives.size());

    u64 hash = s_Fnv64OffsetBasis;
    Fnv64AppendValue(hash, s_SceneStaticCacheHashVersion);
    Fnv64AppendValue(hash, instances.size());
    for(usize index = 0u; index < instances.size(); ++index){
        const SceneSwBvhInstanceGpu& instance = instances[index];
        const SceneBvhPrimitiveCalculation& primitive = primitives[index];
        Fnv64AppendValue(hash, instance.worldToObject);
        Fnv64AppendValue(hash, instance.primitiveCount);
        Fnv64AppendValue(hash, primitive.aabbMin.x);
        Fnv64AppendValue(hash, primitive.aabbMin.y);
        Fnv64AppendValue(hash, primitive.aabbMin.z);
        Fnv64AppendValue(hash, 0.0f);
        Fnv64AppendValue(hash, primitive.aabbMax.x);
        Fnv64AppendValue(hash, primitive.aabbMax.y);
        Fnv64AppendValue(hash, primitive.aabbMax.z);
        Fnv64AppendValue(hash, 0.0f);
        Fnv64AppendBool(hash, primitive.transparentOccluder);
    }
    return hash;
}

Expected<Core::GpuDescriptorHandle> AcquireMeshHeapHandle(const Core::BufferHandle& bufferHandle, Core::GpuDescriptorHeap& heap, RtMeshHeapHandleCache& cache){
    if(!bufferHandle)
        return MakeUnexpected(Failure{});

    Core::Buffer& buffer = *bufferHandle;
    const NotNull<const Core::Buffer*> bufferKey(&buffer);
    auto found = cache.find(bufferKey);
    if(found != cache.end()){
        NWB_ASSERT(found.value().handle.valid());
        found.value().seenThisFrame = true;
        return found.value().handle;
    }

    const auto handle = RayTracingDetail::RegisterHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false);
    if(!handle)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorRetirementGuard descriptorRetirement(heap, *handle);
    RtMeshHeapHandleCacheEntry entry;
    entry.keepAlive = bufferHandle;
    entry.handle = *handle;
    entry.seenThisFrame = true;
    if(!cache.insert({bufferKey, Move(entry)}).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    descriptorRetirement.release();
    return *handle;
}

void SweepUnseenMeshHeapHandles(Core::GpuDescriptorHeap& heap, RtMeshHeapHandleCache& cache){
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

void BeginMeshHeapHandleGather(RtMeshHeapHandleCache& cache)noexcept{
    for(auto it = cache.begin(); it != cache.end(); ++it)
        it.value().seenThisFrame = false;
}

b8 ResolveShadowOccluderMaterial(
    const Core::ECS::EntityID entity,
    const Core::Assets::AssetRef<Material>& material,
    const Scene::TransformComponent* const transform,
    const u32 meshInstanceIndex,
    RendererMaterialSystem& materialSystem,
    ShadowMaterialSampledTextureCollector& sampledTextureCollector,
    MaterialTypedByteDataVector& shadowMaterialTypedBytes,
    ECSRenderDetail::MaterialTypedConstantRangeMap& shadowConstantTypedRanges,
    ECSRenderDetail::MaterialTypedByteContentRangeMap& shadowMutableTypedRanges,
    RayTracingOpticalSceneGather& opticalScene,
    b8& inOutContentComplete,
    NwbRtInstanceMaterialGpu& outInstanceMaterial,
    InstanceGpuData& outShadowInstance,
    b8* const outSceneHasTransparentOccluder
){
    outInstanceMaterial = {};
    outShadowInstance = {};
    MaterialSurfaceInfo* materialInfo = nullptr;
    if(const auto materialInfoResult = materialSystem.findMaterialSurfaceInfo(material); materialInfoResult){
        materialInfo = materialInfoResult->get();
        if(materialInfo->transparent && outSceneHasTransparentOccluder)
            *outSceneHasTransparentOccluder = true;
        // Trace dispatcher reads Texture2D fields via non-uniform slots. Retain resolved handles in preflight.
        const b8 sampledTexturesReady = materialInfo->surfaceDispatchId == Limit<u32>::s_Max || sampledTextureCollector.collect(
            *materialInfo,
            [&materialSystem](const MaterialSurfaceInfo& surface, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& pending){
                return materialSystem.appendPreparedMaterialSurfaceSampledTextures(surface, pending);
            }
        );
        if(!sampledTexturesReady)
            return false;
        const auto materialContext = materialSystem.appendShadowOccluderMaterialContext(entity, *materialInfo, transform, shadowMaterialTypedBytes, shadowConstantTypedRanges, shadowMutableTypedRanges);
        if(!materialContext)
            return false;
        outShadowInstance = materialContext->instance;
        outInstanceMaterial = ResolveInstanceShadowMaterialData(
            materialInfo->surfaceDispatchId,
            materialInfo->shadingModelId,
            materialInfo->transparent,
            materialInfo->refractive,
            materialContext->constantByteOffset,
            meshInstanceIndex
        );
    }
    if(!materialInfo || materialInfo->surfaceDispatchId == Limit<u32>::s_Max)
        inOutContentComplete = false;
    // An unavailable hook need not hide a boundary. Unknown/untransparent surfaces cannot use the outside shortcut.
    if(!materialInfo || (materialInfo->transparent && materialInfo->surfaceDispatchId == Limit<u32>::s_Max))
        opticalScene.markIncomplete();
    return true;
}

Expected<PreparedMeshBlasBuild> ResolvePreparedMeshBlasBuild(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources)noexcept{
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

b8 MatchesPreparedMeshBlasBuild(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources, const PreparedMeshBlasBuild& build)noexcept{
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

b8 RecordPreparedMeshBlasBuild(const PreparedMeshBlasBuild& build, const b8 meshBlasGeometryBuildInputStatesGraphOwned, Core::CommandList& commandList){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::releaseSwBvhScratchHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortKeysHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortPayloadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhVisitCounterHeapHandle);
        return;
    }
    m_rayTracingState.m_bvhSortKeysHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_bvhSortPayloadHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_bvhVisitCounterHeapHandle = Core::GpuDescriptorHandle::Invalid();
}

b8 RendererRayTracingSystem::ensureBvhSortPipeline(){
    if(m_rayTracingState.m_bvhSortPipeline)
        return true;
    if(m_rayTracingState.m_bvhSortPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software BVH sort requires the initialized global descriptor heap"));
        m_rayTracingState.m_bvhSortPipelineFailed = true;
        return false;
    }

    // Push-only layout; sort resources use the global heap.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(BvhSortPushConstants), Core::ShaderType::Compute, m_rayTracingState.m_bvhSortBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
        m_rayTracingState.m_bvhSortPipelineFailed = true;
        return false;
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(&BvhSubsystem::bitonicSortCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_BvhBitonicSort", m_rayTracingState.m_bvhSortShader)){
        m_rayTracingState.m_bvhSortPipelineFailed = true;
        return false;
    }

    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_rayTracingState.m_bvhSortShader, m_rayTracingState.m_bvhSortBindingLayout, false, m_rayTracingState.m_bvhSortPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort compute pipeline"));
        m_rayTracingState.m_bvhSortPipelineFailed = true;
        return false;
    }
    return true;
}

b8 RendererRayTracingSystem::ensureBvhSortBuffers(usize paddedCount){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    const auto sortHandlesReady = [this]()noexcept{
        return RayTracingDetail::IsHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle, Core::GpuDescriptorClass::StorageBuffer) && RayTracingDetail::IsHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle, Core::GpuDescriptorClass::StorageBuffer);
    };

    if(m_rayTracingState.m_bvhSortKeysBuffer && m_rayTracingState.m_bvhSortPayloadBuffer && m_rayTracingState.m_bvhSortCapacity >= paddedCount){
        if(sortHandlesReady())
            return true;

        // Register only missing handles to preserve live generations.
        Core::GpuDescriptorHandle acquiredKeys = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle acquiredPayload = Core::GpuDescriptorHandle::Invalid();
        const auto acquireDescriptors = [&]() -> Expected<void>{
            if(!m_rayTracingState.m_bvhSortKeysHeapHandle.valid()){
                const auto acquiredKeysResult = RayTracingDetail::RegisterHeapBuffer(heap, *m_rayTracingState.m_bvhSortKeysBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
                if(!acquiredKeysResult)
                    return MakeUnexpected(Failure{});
                acquiredKeys = *acquiredKeysResult;
            }
            if(!m_rayTracingState.m_bvhSortPayloadHeapHandle.valid()){
                const auto acquiredPayloadResult = RayTracingDetail::RegisterHeapBuffer(heap, *m_rayTracingState.m_bvhSortPayloadBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
                if(!acquiredPayloadResult)
                    return MakeUnexpected(Failure{});
                acquiredPayload = *acquiredPayloadResult;
            }
            return {};
        };
        if(!acquireDescriptors()){
            RayTracingDetail::RetireHeapHandle(heap, acquiredKeys);
            RayTracingDetail::RetireHeapHandle(heap, acquiredPayload);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing BVH sort scratch in the descriptor heap"));
            return false;
        }
        if(acquiredKeys.valid())
            m_rayTracingState.m_bvhSortKeysHeapHandle = acquiredKeys;
        if(acquiredPayload.valid())
            m_rayTracingState.m_bvhSortPayloadHeapHandle = acquiredPayload;
        return sortHandlesReady();
    }

    const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_bvhSortCapacity, paddedCount, s_BvhSortInitialCapacity);

    Core::BufferDesc keysBufferDesc;
    keysBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_sort_keys"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto keysBufferResult = m_graphics.createBuffer(keysBufferDesc);
    if(!keysBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort keys buffer"));
        return false;
    }
    Core::BufferHandle keysBuffer = Move(*keysBufferResult);

    Core::BufferDesc payloadBufferDesc;
    payloadBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_sort_payload"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto payloadBufferResult = m_graphics.createBuffer(payloadBufferDesc);
    if(!payloadBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort payload buffer"));
        return false;
    }
    Core::BufferHandle payloadBuffer = Move(*payloadBufferResult);

    Core::GpuDescriptorHandle keysHeapHandle;
    Core::GpuDescriptorHandle payloadHeapHandle;
    const auto acquireDescriptors = [&]() -> Expected<void>{
        const auto keysHeapHandleResult = RayTracingDetail::RegisterHeapBuffer(heap, *keysBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
        if(!keysHeapHandleResult)
            return MakeUnexpected(Failure{});
        keysHeapHandle = *keysHeapHandleResult;
        const auto payloadHeapHandleResult = RayTracingDetail::RegisterHeapBuffer(heap, *payloadBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
        if(!payloadHeapHandleResult)
            return MakeUnexpected(Failure{});
        payloadHeapHandle = *payloadHeapHandleResult;
        return {};
    };
    if(!acquireDescriptors()){
        RayTracingDetail::RetireHeapHandle(heap, keysHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, payloadHeapHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register BVH sort scratch in the descriptor heap"));
        return false;
    }

    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortKeysHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortPayloadHeapHandle);
    m_rayTracingState.m_bvhSortKeysBuffer = Move(keysBuffer);
    m_rayTracingState.m_bvhSortPayloadBuffer = Move(payloadBuffer);
    m_rayTracingState.m_bvhSortKeysHeapHandle = keysHeapHandle;
    m_rayTracingState.m_bvhSortPayloadHeapHandle = payloadHeapHandle;
    m_rayTracingState.m_bvhSortCapacity = capacity;
    return true;
}

b8 RendererRayTracingSystem::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(m_rayTracingState.m_bvhSortPipeline);
    NWB_ASSERT(m_rayTracingState.m_bvhSortKeysBuffer);
    NWB_ASSERT(m_rayTracingState.m_bvhSortPayloadBuffer);
    NWB_ASSERT(RayTracingDetail::IsHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle, Core::GpuDescriptorClass::StorageBuffer));
    NWB_ASSERT(RayTracingDetail::IsHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle, Core::GpuDescriptorClass::StorageBuffer));

    // Padded count is power-of-two and group-aligned.
    if(paddedCount < static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE))
        return false;

    const NotNull<Core::Buffer*> keysBuffer(m_rayTracingState.m_bvhSortKeysBuffer.get());
    const NotNull<Core::Buffer*> payloadBuffer(m_rayTracingState.m_bvhSortPayloadBuffer.get());

    // Sort scratch fits the max mesh. Fence only this dispatch's padded lanes, sentinels included.
    const Core::BufferRange sortRange(0u, static_cast<u64>(paddedCount) * sizeof(u32));
    commandList.setEnableUavBarriersForBuffer(keysBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer.get(), true);
    commandList.setBufferState(keysBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
    commandList.setBufferState(payloadBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
    commandList.commitBarriers();

    const u32 groupCount = paddedCount / static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);

    const auto dispatchSort = [this, &commandList](BvhSortPushConstants pushConstants, const u32 groups){
        pushConstants.keysHeapSlot = m_rayTracingState.m_bvhSortKeysHeapHandle.slot();
        pushConstants.payloadHeapSlot = m_rayTracingState.m_bvhSortPayloadHeapHandle.slot();
        Core::ComputeState computeState;
        computeState.setPipeline(m_rayTracingState.m_bvhSortPipeline.get());
        commandList.setComputeState(computeState);
        m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_rayTracingState.m_bvhSortPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groups, 1u, 1u);
    };
    const auto bvhSortBarrier = [&commandList, keysBuffer, payloadBuffer, sortRange](){
        commandList.setBufferState(keysBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
        commandList.setBufferState(payloadBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
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

b8 RendererRayTracingSystem::ensureBvhBuildPipeline(){
    if(m_rayTracingState.m_bvhMortonPipeline && m_rayTracingState.m_bvhTopologyPipeline && m_rayTracingState.m_bvhFitPipeline)
        return true;
    if(m_rayTracingState.m_bvhBuildPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software BVH build requires the initialized global descriptor heap"));
        m_rayTracingState.m_bvhBuildPipelineFailed = true;
        return false;
    }

    // Push-only layout; build resources use the global heap.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(BvhBuildPushConstants), Core::ShaderType::Compute, m_rayTracingState.m_bvhBuildBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
        m_rayTracingState.m_bvhBuildPipelineFailed = true;
        return false;
    }

    const auto createBuildPipeline = [this, &device](Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Core::Assets::AssetRef<ComputeShader> BvhSubsystem::* member, const NameText<char> debugLabel)->b8{
        return RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
            m_arena,
            device,
            m_shaderSystem,
            member,
            Core::ShaderArchive::s_DefaultVariant,
            Name(debugLabel),
            sizeof(BvhBuildPushConstants),
            false,
            m_rayTracingState.m_bvhBuildBindingLayout,
            shader,
            pipeline
        );
    };

    if(
        !createBuildPipeline(m_rayTracingState.m_bvhMortonShader, m_rayTracingState.m_bvhMortonPipeline, &BvhSubsystem::mortonCs, "ECSRender_BvhMorton")
        || !createBuildPipeline(m_rayTracingState.m_bvhTopologyShader, m_rayTracingState.m_bvhTopologyPipeline, &BvhSubsystem::topologyCs, "ECSRender_BvhTopology")
        || !createBuildPipeline(m_rayTracingState.m_bvhFitShader, m_rayTracingState.m_bvhFitPipeline, &BvhSubsystem::fitCs, "ECSRender_BvhFit")
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build compute pipeline"));
        m_rayTracingState.m_bvhBuildPipelineFailed = true;
        return false;
    }
    return true;
}

b8 RendererRayTracingSystem::ensureBvhVisitCounterBuffer(usize primitiveCount){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(m_rayTracingState.m_bvhVisitCounterBuffer && m_rayTracingState.m_bvhBuildCapacity >= primitiveCount){
        if(RayTracingDetail::IsHeapHandle(m_rayTracingState.m_bvhVisitCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer))
            return true;

        if(!m_rayTracingState.m_bvhVisitCounterHeapHandle.valid()){
            const auto acquired = RayTracingDetail::RegisterHeapBuffer(heap, *m_rayTracingState.m_bvhVisitCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
            if(acquired){
                m_rayTracingState.m_bvhVisitCounterHeapHandle = *acquired;
                return true;
            }
        }
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing BVH visit counter in the descriptor heap"));
        return false;
    }

    const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_bvhBuildCapacity, primitiveCount, s_BvhBuildInitialCapacity);

    // Shared visit-counter scratch is safe because mesh builds are serialized.
    Core::BufferDesc counterBufferDesc;
    counterBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_visit_counter"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto counterBufferResult = m_graphics.createBuffer(counterBufferDesc);
    if(!counterBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH visit counter buffer"));
        return false;
    }
    Core::BufferHandle counterBuffer = Move(*counterBufferResult);

    const auto counterHeapHandle = RayTracingDetail::RegisterHeapBuffer(heap, *counterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
    if(!counterHeapHandle){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register BVH visit counter in the descriptor heap"));
        return false;
    }

    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhVisitCounterHeapHandle);
    m_rayTracingState.m_bvhVisitCounterBuffer = Move(counterBuffer);
    m_rayTracingState.m_bvhVisitCounterHeapHandle = *counterHeapHandle;
    m_rayTracingState.m_bvhBuildCapacity = capacity;
    return true;
}

b8 RendererRayTracingSystem::ensureSceneBvhBuffers(u32 instanceCount){
    // Binary scene BVH keeps CPU topology; runtime scenes refit its bounds from live mesh roots.
    const usize requiredNodes = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!m_rayTracingState.m_sceneBvhNodeBuffer || m_rayTracingState.m_sceneBvhNodeCapacity < requiredNodes){
        const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_sceneBvhNodeCapacity, requiredNodes, s_SceneBvhInitialInstanceCapacity * 2u - 1u);

        Core::BufferDesc nodeBufferDesc;
        nodeBufferDesc
            .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * capacity))
            .setStructStride(sizeof(NwbBvhNodeGpu))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("scene_bvh_nodes"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto nodeBufferResult = m_graphics.createBuffer(nodeBufferDesc);
        if(!nodeBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH node buffer"));
            return false;
        }
        Core::BufferHandle nodeBuffer = Move(*nodeBufferResult);
        // Register the new view before replacing ownership to keep the old pair retryable.
        if(!replaceRayTraceMaterialContextHeapHandle(*nodeBuffer.get(), m_rayTracingState.m_sceneBvhNodeHeapHandle))
            return false;
        m_rayTracingState.m_sceneBvhNodeBuffer = Move(nodeBuffer);
        m_rayTracingState.m_sceneBvhNodeCapacity = capacity;
    }

    if(!m_rayTracingState.m_sceneInstanceBuffer || m_rayTracingState.m_sceneInstanceCapacity < instanceCount){
        const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_sceneInstanceCapacity, instanceCount, s_SceneBvhInitialInstanceCapacity);

        Core::BufferDesc instanceBufferDesc;
        instanceBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SceneSwBvhInstanceGpu) * capacity))
            .setStructStride(sizeof(SceneSwBvhInstanceGpu))
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("scene_bvh_instances"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto instanceBufferResult = m_graphics.createBuffer(instanceBufferDesc);
        if(!instanceBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH instance buffer"));
            return false;
        }
        Core::BufferHandle instanceBuffer = Move(*instanceBufferResult);
        if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), m_rayTracingState.m_sceneInstanceHeapHandle))
            return false;
        m_rayTracingState.m_sceneInstanceBuffer = Move(instanceBuffer);
        m_rayTracingState.m_sceneInstanceCapacity = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software scene BVH buffers (capacity {} instances)"), static_cast<u64>(capacity));
    }
    return
        ensureRayTraceMaterialContextHeapHandle(*m_rayTracingState.m_sceneBvhNodeBuffer.get(), m_rayTracingState.m_sceneBvhNodeHeapHandle)
        && ensureRayTraceMaterialContextHeapHandle(*m_rayTracingState.m_sceneInstanceBuffer.get(), m_rayTracingState.m_sceneInstanceHeapHandle)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

