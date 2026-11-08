// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/software_bvh_builder.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>
#include <impl/ecs_render/raytrace/types.adhd.h>

#include <impl/ecs_render/raytrace/raytracing_build_policy.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SoftwareBvhBuilder::createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(nodeBuffer && parentBuffer){
        if(RayTracingDetail::IsHeapHandle(nodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer) && RayTracingDetail::IsHeapHandle(parentHeapHandle, Core::GpuDescriptorClass::StorageBuffer))
            return true;

        Core::GpuDescriptorHandle acquiredNode = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle acquiredParent = Core::GpuDescriptorHandle::Invalid();
        const auto acquireDescriptors = [&]() -> Expected<void>{
            if(!nodeHeapHandle.valid()){
                const auto acquiredNodeResult = RayTracingDetail::RegisterHeapBuffer(heap, *nodeBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
                if(!acquiredNodeResult)
                    return MakeUnexpected(Failure{});
                acquiredNode = *acquiredNodeResult;
            }
            if(!parentHeapHandle.valid()){
                const auto acquiredParentResult = RayTracingDetail::RegisterHeapBuffer(heap, *parentBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
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
        return RayTracingDetail::IsHeapHandle(nodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer) && RayTracingDetail::IsHeapHandle(parentHeapHandle, Core::GpuDescriptorClass::StorageBuffer);
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
        .setDebugName(Name(s_RenderNameBvhMeshNodes))
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
        .setDebugName(Name(s_RenderNameBvhMeshParent))
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
        const auto newNodeHeapHandleResult = RayTracingDetail::RegisterHeapBuffer(heap, *newNodeBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
        if(!newNodeHeapHandleResult)
            return MakeUnexpected(Failure{});
        newNodeHeapHandle = *newNodeHeapHandleResult;
        const auto newParentHeapHandleResult = RayTracingDetail::RegisterHeapBuffer(heap, *newParentBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
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

b8 SoftwareBvhBuilder::ensureMeshSwBvhResources(u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle){
    if(!RayTracingBuildPolicy().validateMeshSwBvhPrimitiveCount(primitiveCount))
        return false;

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

b8 SoftwareBvhBuilder::meshSwBvhResourcesReady(const Core::BufferHandle& nodeBuffer, const Core::BufferHandle& parentBuffer, const Core::GpuDescriptorHandle nodeHeapHandle, const Core::GpuDescriptorHandle parentHeapHandle)noexcept{
    return
        nodeBuffer
        && parentBuffer
        && RayTracingDetail::IsHeapHandle(nodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(parentHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && m_state.m_bvhSortPipeline
        && m_state.m_bvhSortKeysBuffer
        && m_state.m_bvhSortPayloadBuffer
        && RayTracingDetail::IsHeapHandle(m_state.m_bvhSortKeysHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(m_state.m_bvhSortPayloadHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && m_state.m_bvhMortonPipeline
        && m_state.m_bvhTopologyPipeline
        && m_state.m_bvhFitPipeline
        && m_state.m_bvhVisitCounterBuffer
        && RayTracingDetail::IsHeapHandle(m_state.m_bvhVisitCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ;
}

b8 SoftwareBvhBuilder::buildMeshSwBvhPrepared(
    const u32 positionHeapSlot,
    const u32 triangleIndexHeapSlot,
    u32 primitiveCount,
    const Float3Int& aabbMin,
    const Float3Int& aabbMax,
    const Core::GpuDescriptorHandle nodeHeapHandle,
    const Core::GpuDescriptorHandle parentHeapHandle,
    Core::CommandList& commandList,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer
){
    if(primitiveCount == 0u || positionHeapSlot == Limit<u32>::s_Max || triangleIndexHeapSlot == Limit<u32>::s_Max || !meshSwBvhResourcesReady(nodeBuffer, parentBuffer, nodeHeapHandle, parentHeapHandle))
        return false;

    const u32 paddedCount = ResolveBvhPaddedSortCount(primitiveCount);

    const NotNull<Core::Buffer*> keysBuffer(m_state.m_bvhSortKeysBuffer.get());
    const NotNull<Core::Buffer*> payloadBuffer(m_state.m_bvhSortPayloadBuffer.get());
    const NotNull<Core::Buffer*> visitCounterBuffer(m_state.m_bvhVisitCounterBuffer.get());
    const NotNull<Core::Buffer*> meshNodeBuffer(nodeBuffer.get());
    const NotNull<Core::Buffer*> meshParentBuffer(parentBuffer.get());

    const BvhBuildPushConstants pushConstants = MakeBvhBuildPushConstants(
        primitiveCount,
        positionHeapSlot,
        triangleIndexHeapSlot,
        m_state.m_bvhSortKeysHeapHandle.slot(),
        m_state.m_bvhSortPayloadHeapHandle.slot(),
        nodeHeapHandle.slot(),
        parentHeapHandle.slot(),
        m_state.m_bvhVisitCounterHeapHandle.slot(),
        LoadFloatInt(aabbMin),
        LoadFloatInt(aabbMax)
    );

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

    dispatchBuildKernel(*m_state.m_bvhMortonPipeline, DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();

    // Attribute sorting to the mesh rebuild at its call site.
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SwBvhSort, m_graphics.getDevice(), commandList);

        if(!bvhBitonicSort(commandList, primitiveCount, paddedCount))
            return false;
    }
    bvhBuildBarrier();

    if(primitiveCount > 1u){
        dispatchBuildKernel(*m_state.m_bvhTopologyPipeline, DivideUp(primitiveCount - 1u, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
        bvhBuildBarrier();
    }

    dispatchBuildKernel(*m_state.m_bvhFitPipeline, DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    return true;
}

b8 SoftwareBvhBuilder::refitMeshSwBvhPrepared(
    const u32 positionHeapSlot,
    const u32 triangleIndexHeapSlot,
    u32 primitiveCount,
    const Core::GpuDescriptorHandle nodeHeapHandle,
    const Core::GpuDescriptorHandle parentHeapHandle,
    Core::CommandList& commandList,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer
){
    if(primitiveCount == 0u || positionHeapSlot == Limit<u32>::s_Max || triangleIndexHeapSlot == Limit<u32>::s_Max || !meshSwBvhResourcesReady(nodeBuffer, parentBuffer, nodeHeapHandle, parentHeapHandle))
        return false;

    const NotNull<Core::Buffer*> keysBuffer(m_state.m_bvhSortKeysBuffer.get());
    const NotNull<Core::Buffer*> payloadBuffer(m_state.m_bvhSortPayloadBuffer.get());
    const NotNull<Core::Buffer*> meshNodeBuffer(nodeBuffer.get());
    const NotNull<Core::Buffer*> meshParentBuffer(parentBuffer.get());
    const NotNull<Core::Buffer*> visitCounterBuffer(m_state.m_bvhVisitCounterBuffer.get());

    const BvhBuildPushConstants pushConstants = MakeBvhRefitPushConstants(
        primitiveCount,
        positionHeapSlot,
        triangleIndexHeapSlot,
        m_state.m_bvhSortKeysHeapHandle.slot(),
        m_state.m_bvhSortPayloadHeapHandle.slot(),
        nodeHeapHandle.slot(),
        parentHeapHandle.slot(),
        m_state.m_bvhVisitCounterHeapHandle.slot()
    );

    commandList.setEnableUavBarriersForBuffer(keysBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer.get(), true);
    Core::ComputeState computeState;
    computeState.setPipeline(m_state.m_bvhFitPipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_state.m_bvhFitPipeline.get());
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)), 1u, 1u);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

