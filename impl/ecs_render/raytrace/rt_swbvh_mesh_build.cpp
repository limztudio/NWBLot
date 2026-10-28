// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(nodeBuffer && parentBuffer){
        if(RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(nodeHeapHandle) && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(parentHeapHandle))
            return true;

        Core::GpuDescriptorHandle acquiredNode = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle acquiredParent = Core::GpuDescriptorHandle::Invalid();
        const auto acquireDescriptors = [&]() -> Expected<void>{
            if(!nodeHeapHandle.valid()){
                const auto acquiredNodeResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *nodeBuffer.get());
                if(!acquiredNodeResult)
                    return MakeUnexpected(Failure{});
                acquiredNode = *acquiredNodeResult;
            }
            if(!parentHeapHandle.valid()){
                const auto acquiredParentResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *parentBuffer.get());
                if(!acquiredParentResult)
                    return MakeUnexpected(Failure{});
                acquiredParent = *acquiredParentResult;
            }
            return {};
        };
        if(!acquireDescriptors()){
            RayTracingDetail::RetireHeapHandle(heap, acquiredNode);
            RayTracingDetail::RetireHeapHandle(heap, acquiredParent);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing per-mesh BVH storage in the descriptor heap"));
            return false;
        }
        if(acquiredNode.valid())
            nodeHeapHandle = acquiredNode;
        if(acquiredParent.valid())
            parentHeapHandle = acquiredParent;
        return RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(nodeHeapHandle) && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(parentHeapHandle);
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
    auto newNodeBufferResult = m_graphics.createBuffer(nodeBufferDesc);
    if(!newNodeBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH node buffer"));
        return false;
    }
    Core::BufferHandle newNodeBuffer = Move(*newNodeBufferResult);

    Core::BufferDesc parentBufferDesc;
    parentBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * nodeCount))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_mesh_parent"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto newParentBufferResult = m_graphics.createBuffer(parentBufferDesc);
    if(!newParentBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH parent buffer"));
        return false;
    }
    Core::BufferHandle newParentBuffer = Move(*newParentBufferResult);

    Core::GpuDescriptorHandle newNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle newParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
    const auto acquireDescriptors = [&]() -> Expected<void>{
        const auto newNodeHeapHandleResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *newNodeBuffer.get());
        if(!newNodeHeapHandleResult)
            return MakeUnexpected(Failure{});
        newNodeHeapHandle = *newNodeHeapHandleResult;
        const auto newParentHeapHandleResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *newParentBuffer.get());
        if(!newParentHeapHandleResult)
            return MakeUnexpected(Failure{});
        newParentHeapHandle = *newParentHeapHandleResult;
        return {};
    };
    if(!acquireDescriptors()){
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

bool RendererRayTracingSystem::ensureMeshSwBvhResources(u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle){
    if(primitiveCount > s_BvhMaxPrimitivesPerMesh){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh exceeds software BVH primitive cap ({} > {}), shadows skipped"), static_cast<u64>(primitiveCount), static_cast<u64>(s_BvhMaxPrimitivesPerMesh));
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

bool RendererRayTracingSystem::meshSwBvhResourcesReady(const Core::BufferHandle& nodeBuffer, const Core::BufferHandle& parentBuffer, const Core::GpuDescriptorHandle nodeHeapHandle, const Core::GpuDescriptorHandle parentHeapHandle)noexcept{
    return
        nodeBuffer
        && parentBuffer
        && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(nodeHeapHandle)
        && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(parentHeapHandle)
        && m_rayTracingState.m_bvhSortPipeline
        && m_rayTracingState.m_bvhSortKeysBuffer
        && m_rayTracingState.m_bvhSortPayloadBuffer
        && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle)
        && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle)
        && m_rayTracingState.m_bvhMortonPipeline
        && m_rayTracingState.m_bvhTopologyPipeline
        && m_rayTracingState.m_bvhFitPipeline
        && m_rayTracingState.m_bvhVisitCounterBuffer
        && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhVisitCounterHeapHandle)
    ;
}

bool RendererRayTracingSystem::buildMeshSwBvhPrepared(
    Core::CommandList& commandList,
    const u32 positionHeapSlot,
    const u32 triangleIndexHeapSlot,
    u32 primitiveCount,
    const Float3Int& aabbMin,
    const Float3Int& aabbMax,
    const Core::GpuDescriptorHandle nodeHeapHandle,
    const Core::GpuDescriptorHandle parentHeapHandle,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer
){
    if(primitiveCount == 0u || positionHeapSlot == Limit<u32>::s_Max || triangleIndexHeapSlot == Limit<u32>::s_Max || !meshSwBvhResourcesReady(nodeBuffer, parentBuffer, nodeHeapHandle, parentHeapHandle))
        return false;

    u32 paddedCount = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    while(paddedCount < primitiveCount)
        paddedCount <<= 1u;

    const NotNull<Core::Buffer*> keysBuffer(m_rayTracingState.m_bvhSortKeysBuffer.get());
    const NotNull<Core::Buffer*> payloadBuffer(m_rayTracingState.m_bvhSortPayloadBuffer.get());
    const NotNull<Core::Buffer*> visitCounterBuffer(m_rayTracingState.m_bvhVisitCounterBuffer.get());
    const NotNull<Core::Buffer*> meshNodeBuffer(nodeBuffer.get());
    const NotNull<Core::Buffer*> meshParentBuffer(parentBuffer.get());

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.positionHeapSlot = positionHeapSlot;
    pushConstants.triangleIndexHeapSlot = triangleIndexHeapSlot;
    pushConstants.keysHeapSlot = m_rayTracingState.m_bvhSortKeysHeapHandle.slot();
    pushConstants.payloadHeapSlot = m_rayTracingState.m_bvhSortPayloadHeapHandle.slot();
    pushConstants.nodeHeapSlot = nodeHeapHandle.slot();
    pushConstants.parentHeapSlot = parentHeapHandle.slot();
    pushConstants.visitCounterHeapSlot = m_rayTracingState.m_bvhVisitCounterHeapHandle.slot();
    pushConstants.aabbMin = Float3U(aabbMin.x, aabbMin.y, aabbMin.z);
    pushConstants.aabbMax = Float3U(aabbMax.x, aabbMax.y, aabbMax.z);

    commandList.setEnableUavBarriersForBuffer(keysBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer.get(), true);

    const auto bvhBuildBarrier = [&commandList, keysBuffer, payloadBuffer, meshNodeBuffer, meshParentBuffer, visitCounterBuffer](){
        commandList.setBufferState(keysBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshNodeBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshParentBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(visitCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    };

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    const auto dispatchBuildKernel = [&commandList, &pushConstants, &heap](Core::ComputePipeline& pipeline, const u32 groupCount){
        Core::ComputeState computeState;
        computeState.setPipeline(&pipeline);
        commandList.setComputeState(computeState);
        heap.bindCompute(commandList, pipeline);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groupCount, 1u, 1u);
    };

    dispatchBuildKernel(*m_rayTracingState.m_bvhMortonPipeline, DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();

    // Attribute sorting to the mesh rebuild at its call site.
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SwBvhSort, m_graphics.getDevice(), commandList);

        if(!bvhBitonicSort(commandList, primitiveCount, paddedCount))
            return false;
    }
    bvhBuildBarrier();

    if(primitiveCount > 1u){
        dispatchBuildKernel(*m_rayTracingState.m_bvhTopologyPipeline, DivideUp(primitiveCount - 1u, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
        bvhBuildBarrier();
    }

    dispatchBuildKernel(*m_rayTracingState.m_bvhFitPipeline, DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    return true;
}

bool RendererRayTracingSystem::refitMeshSwBvhPrepared(
    Core::CommandList& commandList,
    const u32 positionHeapSlot,
    const u32 triangleIndexHeapSlot,
    u32 primitiveCount,
    const Core::GpuDescriptorHandle nodeHeapHandle,
    const Core::GpuDescriptorHandle parentHeapHandle,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer
){
    if(primitiveCount == 0u || positionHeapSlot == Limit<u32>::s_Max || triangleIndexHeapSlot == Limit<u32>::s_Max || !meshSwBvhResourcesReady(nodeBuffer, parentBuffer, nodeHeapHandle, parentHeapHandle))
        return false;

    const NotNull<Core::Buffer*> keysBuffer(m_rayTracingState.m_bvhSortKeysBuffer.get());
    const NotNull<Core::Buffer*> payloadBuffer(m_rayTracingState.m_bvhSortPayloadBuffer.get());
    const NotNull<Core::Buffer*> meshNodeBuffer(nodeBuffer.get());
    const NotNull<Core::Buffer*> meshParentBuffer(parentBuffer.get());
    const NotNull<Core::Buffer*> visitCounterBuffer(m_rayTracingState.m_bvhVisitCounterBuffer.get());

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.refitMode = NWB_BVH_BUILD_MODE_REFIT;
    pushConstants.positionHeapSlot = positionHeapSlot;
    pushConstants.triangleIndexHeapSlot = triangleIndexHeapSlot;
    pushConstants.keysHeapSlot = m_rayTracingState.m_bvhSortKeysHeapHandle.slot();
    pushConstants.payloadHeapSlot = m_rayTracingState.m_bvhSortPayloadHeapHandle.slot();
    pushConstants.nodeHeapSlot = nodeHeapHandle.slot();
    pushConstants.parentHeapSlot = parentHeapHandle.slot();
    pushConstants.visitCounterHeapSlot = m_rayTracingState.m_bvhVisitCounterHeapHandle.slot();

    commandList.setEnableUavBarriersForBuffer(keysBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer.get(), true);
    Core::ComputeState computeState;
    computeState.setPipeline(m_rayTracingState.m_bvhFitPipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_rayTracingState.m_bvhFitPipeline.get());
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)), 1u, 1u);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

